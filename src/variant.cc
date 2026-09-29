// SPDX-License-Identifier: AGPL-3.0-only
// mux.variant -- One of several types, as std::variant holds it, but built
// in time linear in how many there are. libc++ keeps a variant as a union
// nested as deep as it has alternatives, and reaching or making the k-th
// goes k levels down, each level a template of its own: a variant of 148
// requests cost 148 * 149 / 2 instantiations in every unit that made them
// all.
//
// Here: which one it is, a buffer as large as the largest, and a pointer to
// the object in it -- what the compiler sees the object through, not the
// buffer's bytes. The object is held in a holder<T>, derived from one empty
// base the pointer is kept as: a base pointer cast down to its holder is a
// constant expression everywhere, where a void* cast back is not. In
// constant evaluation the holder is allocated instead: placement into a
// buffer is not a constant expression.
//
// Which alternative an operation is for -- making, moving, copying,
// destroying, visiting -- is found two ways, by the build:
// - optimised (a release build): the alternatives folded into a nested type,
//   layer<T0, layer<T1, ... core>>, by `|` over the pack, and walked a layer
//   at a time with an `if` on the index -- a chain the optimiser makes a
//   jump, each layer one instantiation;
// - not (outside a release build): a table of function pointers per
//   operation, read through a volatile pointer, so nothing is inlined across
//   it -- only the table is made.
export module mux.variant;

import std;

namespace mux::detail {
// Whether alternatives are found through tables (outside a release build):
// CMake says which build this is; C++ cannot see it.
#ifdef MUX_ERASED
inline constexpr bool kVariantTables = true;
#else
inline constexpr bool kVariantTables = false;
#endif

// What every alternative is held in: one base for the pointer, empty.
struct held {};
template <class T>
struct holder : held {
  T value;
  template <class... Args>
  constexpr explicit holder(std::in_place_t, Args&&... args) : value(std::forward<Args>(args)...) {}
};
template <class T>
[[nodiscard]] constexpr T& value_of(held* p) noexcept {
  return static_cast<holder<T>*>(p)->value;
}
template <class T>
[[nodiscard]] constexpr const T& value_of(const held* p) noexcept {
  return static_cast<const holder<T>*>(p)->value;
}

// The onion: the alternatives folded into one nested type by `|`, walked a
// layer at a time.
struct core {};
template <class T, class Rest>
struct layer {};
template <class T>
struct peel {};
template <class T, class Rest>
constexpr layer<T, Rest> operator|(peel<T>, Rest) noexcept {
  return {};
}
// `f` given the type of the one at `index`: the last layer, as it is; any
// other, as it is where the index is 0, else the rest a layer further in.
template <class F, class T>
constexpr decltype(auto) peel_to(layer<T, core>, std::size_t, F& f) {
  return f(std::type_identity<T>{});
}
template <class F, class T, class Rest>
constexpr decltype(auto) peel_to(layer<T, Rest>, std::size_t index, F& f) {
  if (index == 0)
    return f(std::type_identity<T>{});
  return peel_to(Rest{}, index - 1, f);
}

// The place of T among Ts.
template <class T, class... Ts>
consteval std::size_t index_in() {
  constexpr std::array<bool, sizeof...(Ts)> same{std::same_as<T, Ts>...};
  for (std::size_t i = 0; i < same.size(); ++i)
    if (same[i])
      return i;
  return sizeof...(Ts);
}
}  // namespace mux::detail

export namespace mux {

template <class... Ts>
class variant {
 public:
  static constexpr std::size_t kSize = sizeof...(Ts);

  template <class T>
    requires(std::same_as<std::remove_cvref_t<T>, Ts> || ...)
  constexpr variant(T&& value)  // NOLINT: converting, as std::variant's is
      : fIndex(detail::index_in<std::remove_cvref_t<T>, Ts...>()) {
    this->make<std::remove_cvref_t<T>>(std::forward<T>(value));
  }
  constexpr variant(variant&& other) noexcept : fIndex(other.fIndex) { this->move_from(other); }
  constexpr variant(const variant& other) : fIndex(other.fIndex) { this->copy_from(other); }
  constexpr variant& operator=(variant&& other) noexcept {
    if (this != &other) {
      this->destroy();
      fIndex = other.fIndex;
      this->move_from(other);
    }
    return *this;
  }
  constexpr variant& operator=(const variant& other) {
    if (this != &other) {
      this->destroy();
      fIndex = other.fIndex;
      this->copy_from(other);
    }
    return *this;
  }
  constexpr ~variant() { this->destroy(); }

  [[nodiscard]] constexpr std::size_t index() const noexcept { return fIndex; }

  // The one it holds, given to `f`; what `f` gives back.
  template <class F>
  constexpr decltype(auto) visit(F&& f) {
    using R = std::invoke_result_t<F&, std::tuple_element_t<0, std::tuple<Ts...>>&>;
    if constexpr (detail::kVariantTables) {
      static constexpr std::array<R (*)(F&, detail::held*), kSize> table{
          +[](F& g, detail::held* p) -> R { return std::invoke(g, detail::value_of<Ts>(p)); }...};
      if consteval {
        return table[fIndex](f, fObject);
      } else {
        static const auto* volatile opaque = table.data();
        return opaque[fIndex](f, fObject);
      }
    } else {
      auto at = [&]<class T>(std::type_identity<T>) -> R { return std::invoke(f, detail::value_of<T>(fObject)); };
      return detail::peel_to(onion{}, fIndex, at);
    }
  }
  template <class F>
  constexpr decltype(auto) visit(F&& f) const {
    using R = std::invoke_result_t<F&, const std::tuple_element_t<0, std::tuple<Ts...>>&>;
    const detail::held* object = fObject;
    if constexpr (detail::kVariantTables) {
      static constexpr std::array<R (*)(F&, const detail::held*), kSize> table{
          +[](F& g, const detail::held* p) -> R { return std::invoke(g, detail::value_of<Ts>(p)); }...};
      if consteval {
        return table[fIndex](f, object);
      } else {
        static const auto* volatile opaque = table.data();
        return opaque[fIndex](f, object);
      }
    } else {
      auto at = [&]<class T>(std::type_identity<T>) -> R { return std::invoke(f, detail::value_of<T>(object)); };
      return detail::peel_to(onion{}, fIndex, at);
    }
  }

 private:
  using onion = decltype((detail::peel<Ts>{} | ... | detail::core{}));

  template <class T, class... Args>
  constexpr void make(Args&&... args) {
    if consteval {
      fObject = new detail::holder<T>(std::in_place, std::forward<Args>(args)...);
    } else {
      fObject = ::new (static_cast<void*>(fBuffer)) detail::holder<T>(std::in_place, std::forward<Args>(args)...);
    }
  }
  template <class T>
  static constexpr void move_one(variant& to, variant& from) {
    to.template make<T>(std::move(detail::value_of<T>(from.fObject)));
  }
  template <class T>
  static constexpr void copy_one(variant& to, const variant& from) {
    to.template make<T>(detail::value_of<T>(static_cast<const detail::held*>(from.fObject)));
  }
  template <class T>
  static constexpr void destroy_one(variant& self) {
    if consteval {
      delete static_cast<detail::holder<T>*>(self.fObject);
    } else {
      static_cast<detail::holder<T>*>(self.fObject)->~holder();
    }
  }
  // Each operation for the one held: through its table, or its layer.
  constexpr void move_from(variant& other) {
    if constexpr (detail::kVariantTables) {
      static constexpr std::array<void (*)(variant&, variant&), kSize> table{&move_one<Ts>...};
      if consteval {
        table[fIndex](*this, other);
      } else {
        static const auto* volatile opaque = table.data();
        opaque[fIndex](*this, other);
      }
    } else {
      auto at = [&]<class T>(std::type_identity<T>) { move_one<T>(*this, other); };
      detail::peel_to(onion{}, fIndex, at);
    }
  }
  constexpr void copy_from(const variant& other) {
    if constexpr (detail::kVariantTables) {
      static constexpr std::array<void (*)(variant&, const variant&), kSize> table{&copy_one<Ts>...};
      if consteval {
        table[fIndex](*this, other);
      } else {
        static const auto* volatile opaque = table.data();
        opaque[fIndex](*this, other);
      }
    } else {
      auto at = [&]<class T>(std::type_identity<T>) { copy_one<T>(*this, other); };
      detail::peel_to(onion{}, fIndex, at);
    }
  }
  constexpr void destroy() {
    if constexpr (detail::kVariantTables) {
      static constexpr std::array<void (*)(variant&), kSize> table{&destroy_one<Ts>...};
      if consteval {
        table[fIndex](*this);
      } else {
        static const auto* volatile opaque = table.data();
        opaque[fIndex](*this);
      }
    } else {
      auto at = [&]<class T>(std::type_identity<T>) { destroy_one<T>(*this); };
      detail::peel_to(onion{}, fIndex, at);
    }
  }

  std::size_t fIndex = 0;
  alignas(detail::holder<Ts>...) unsigned char fBuffer[std::max({sizeof(detail::holder<Ts>)...})];
  detail::held* fObject = nullptr;
};

// As std::visit, for one variant.
template <class F, class... Ts>
constexpr decltype(auto) visit(F&& f, variant<Ts...>& v) {
  return v.visit(std::forward<F>(f));
}
template <class F, class... Ts>
constexpr decltype(auto) visit(F&& f, const variant<Ts...>& v) {
  return v.visit(std::forward<F>(f));
}

}  // namespace mux
