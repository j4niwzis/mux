// SPDX-License-Identifier: AGPL-3.0-only
// mux.variant -- One of several types: std::variant's interface, built in
// time linear in how many there are. libc++ keeps a variant as a union
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

// How a known alternative is read at runtime, by the compiler: clang drops
// a check of what was just written only when the object is read from the
// buffer (std::launder is free there; through the pointer it reloads the
// pointer, its tag lost when inlining); gcc only when it is read through the
// pointer (its std::launder is a barrier nothing is carried across). C++
// cannot see which compiler it is; only the compiler's own macro says.
#if defined(__clang__)
inline constexpr bool kReadsBuffer = true;
#else
inline constexpr bool kReadsBuffer = false;
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
// The same, halved each step: the alternatives [Lo, Hi) split at their
// middle, the index compared once per step -- log2(n) compares, not n, a
// tree the optimiser lays out as a jump or a few branches, each node one
// instantiation (2n in all).
template <std::size_t Lo, std::size_t Hi, class F, class... Ts>
constexpr decltype(auto) halve_to(std::size_t index, F& f) {
  if constexpr (Hi - Lo == 1) {
    return f(std::type_identity<std::tuple_element_t<Lo, std::tuple<Ts...>>>{});
  } else {
    constexpr std::size_t mid = Lo + (Hi - Lo) / 2;
    if (index < mid)
      return halve_to<Lo, mid, F, Ts...>(index, f);
    return halve_to<mid, Hi, F, Ts...>(index, f);
  }
}

// The place of T among Ts; sizeof...(Ts) where it is not one.
template <class T, class... Ts>
consteval std::size_t index_in() {
  constexpr std::array<bool, sizeof...(Ts)> same{std::same_as<T, Ts>...};
  for (std::size_t i = 0; i < same.size(); ++i)
    if (same[i])
      return i;
  return sizeof...(Ts);
}
template <class T, class... Ts>
concept one_of = (std::same_as<T, Ts> || ...);
}  // namespace mux::detail

export namespace mux {

template <class... Ts>
class variant;

template <class V>
struct variant_size;
template <class... Ts>
struct variant_size<variant<Ts...>> : std::integral_constant<std::size_t, sizeof...(Ts)> {};
template <class V>
struct variant_size<const V> : variant_size<V> {};
template <class V>
inline constexpr std::size_t variant_size_v = variant_size<V>::value;

template <std::size_t I, class V>
struct variant_alternative;
template <std::size_t I, class... Ts>
struct variant_alternative<I, variant<Ts...>> {
  using type = std::tuple_element_t<I, std::tuple<Ts...>>;
};
template <std::size_t I, class V>
struct variant_alternative<I, const V> {
  using type = const typename variant_alternative<I, V>::type;
};
template <std::size_t I, class V>
using variant_alternative_t = typename variant_alternative<I, V>::type;

template <class... Ts>
class variant {
  using first = std::tuple_element_t<0, std::tuple<Ts...>>;

 public:
  static constexpr std::size_t kSize = sizeof...(Ts);

  // As std::variant's: the first alternative, made by default.
  constexpr variant() noexcept(std::is_nothrow_default_constructible_v<first>)
    requires std::default_initializable<first>
      : fIndex(0) {
    this->make<first>();
  }
  // One of them, converted to: only its own type, exactly -- a request, a
  // tag, a change as it is named.
  template <class T>
    requires detail::one_of<std::remove_cvref_t<T>, Ts...>
  constexpr variant(T&& value)  // NOLINT: converting, as std::variant's is
      : fIndex(static_cast<index_type>(detail::index_in<std::remove_cvref_t<T>, Ts...>())) {
    this->make<std::remove_cvref_t<T>>(std::forward<T>(value));
  }
  template <class T, class... Args>
    requires detail::one_of<T, Ts...>
  constexpr explicit variant(std::in_place_type_t<T>, Args&&... args) : fIndex(static_cast<index_type>(detail::index_in<T, Ts...>())) {
    this->make<T>(std::forward<Args>(args)...);
  }
  template <std::size_t I, class... Args>
    requires(I < sizeof...(Ts))
  constexpr explicit variant(std::in_place_index_t<I>, Args&&... args) : fIndex(static_cast<index_type>(I)) {
    this->make<std::tuple_element_t<I, std::tuple<Ts...>>>(std::forward<Args>(args)...);
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
  template <class T>
    requires detail::one_of<std::remove_cvref_t<T>, Ts...>
  constexpr variant& operator=(T&& value) {
    this->emplace<std::remove_cvref_t<T>>(std::forward<T>(value));
    return *this;
  }
  constexpr ~variant() { this->destroy(); }

  template <class T, class... Args>
    requires detail::one_of<T, Ts...>
  constexpr T& emplace(Args&&... args) {
    this->destroy();
    fIndex = static_cast<index_type>(detail::index_in<T, Ts...>());
    this->make<T>(std::forward<Args>(args)...);
    return detail::value_of<T>(fObject);
  }
  template <std::size_t I, class... Args>
    requires(I < sizeof...(Ts))
  constexpr std::tuple_element_t<I, std::tuple<Ts...>>& emplace(Args&&... args) {
    return this->emplace<std::tuple_element_t<I, std::tuple<Ts...>>>(std::forward<Args>(args)...);
  }

  [[nodiscard]] constexpr std::size_t index() const noexcept { return fIndex; }
  [[nodiscard]] constexpr bool valueless_by_exception() const noexcept { return false; }
  constexpr void swap(variant& other) {
    variant kept(std::move(other));
    other = std::move(*this);
    *this = std::move(kept);
  }

  // C++26's member visit: the one it holds, given to `f`.
  template <class Self, class F>
  constexpr decltype(auto) visit(this Self&& self, F&& f) {
    return std::forward<Self>(self).template visit_as<void, true>(f);
  }
  template <class R, class Self, class F>
  constexpr R visit(this Self&& self, F&& f) {
    return std::forward<Self>(self).template visit_as<R, false>(f);
  }

  // The one held, as a T -- where it is one.
  template <class T>
  [[nodiscard]] constexpr T* get_if() noexcept {
    return fIndex == detail::index_in<T, Ts...>() ? &this->template value<T>() : nullptr;
  }
  template <class T>
  [[nodiscard]] constexpr const T* get_if() const noexcept {
    return fIndex == detail::index_in<T, Ts...>() ? &this->template value<T>() : nullptr;
  }

  friend constexpr bool operator==(const variant& a, const variant& b)
    requires(std::equality_comparable<Ts> && ...)
  {
    if (a.fIndex != b.fIndex)
      return false;
    return a.visit([&]<class T>(const T& one) { return one == *b.template get_if<T>(); });
  }
  friend constexpr auto operator<=>(const variant& a, const variant& b)
    requires(std::three_way_comparable<Ts> && ...)
  {
    using order = std::common_comparison_category_t<std::compare_three_way_result_t<Ts>...>;
    if (a.fIndex != b.fIndex)
      return order(a.fIndex <=> b.fIndex);
    return a.visit([&]<class T>(const T& one) -> order { return one <=> *b.template get_if<T>(); });
  }

 private:
  using onion = decltype((detail::peel<Ts>{} | ... | detail::core{}));

  // `f` given the one held, as it is referred to (a const variant's is const):
  // what it gives back, of the first's type, or R where one is asked for.
  template <class R, bool Deduced, class F>
  constexpr decltype(auto) visit_as(F& f) {
    using Out = std::conditional_t<Deduced, std::invoke_result_t<F&, first&>, R>;
    if constexpr (detail::kVariantTables) {
      static constexpr std::array<Out (*)(F&, detail::held*), kSize> table{
          +[](F& g, detail::held* p) -> Out { return std::invoke(g, detail::value_of<Ts>(p)); }...};
      if consteval {
        return table[fIndex](f, fObject);
      } else {
        static const auto* volatile opaque = table.data();
        return opaque[fIndex](f, this->object());
      }
    } else {
      auto at = [&]<class T>(std::type_identity<T>) -> Out { return std::invoke(f, this->template value<T>()); };
      return detail::halve_to<0, kSize, decltype(at), Ts...>(fIndex, at);
    }
  }
  template <class R, bool Deduced, class F>
  constexpr decltype(auto) visit_as(F& f) const {
    using Out = std::conditional_t<Deduced, std::invoke_result_t<F&, const first&>, R>;
    const detail::held* object = this->object();
    if constexpr (detail::kVariantTables) {
      static constexpr std::array<Out (*)(F&, const detail::held*), kSize> table{
          +[](F& g, const detail::held* p) -> Out { return std::invoke(g, detail::value_of<Ts>(p)); }...};
      if consteval {
        return table[fIndex](f, object);
      } else {
        static const auto* volatile opaque = table.data();
        return opaque[fIndex](f, object);
      }
    } else {
      auto at = [&]<class T>(std::type_identity<T>) -> Out { return std::invoke(f, this->template value<T>()); };
      return detail::halve_to<0, kSize, decltype(at), Ts...>(fIndex, at);
    }
  }

  // The object held. At runtime it is always the holder made in the buffer,
  // its empty base at the buffer's start: said so, for the optimiser to
  // take the buffer's address rather than load the pointer.
  [[nodiscard]] constexpr detail::held* object() noexcept {
    if consteval {
      return fObject;
    } else {
      [[assume(static_cast<void*>(fObject) == static_cast<void*>(fBuffer))]];
      return fObject;
    }
  }
  [[nodiscard]] constexpr const detail::held* object() const noexcept {
    if consteval {
      return fObject;
    } else {
      [[assume(static_cast<const void*>(fObject) == static_cast<const void*>(fBuffer))]];
      return fObject;
    }
  }

  // The one held, as a T, where its type is known: at runtime the holder in
  // the buffer itself (laundered, as placement new made it), so nothing is
  // loaded to reach it; in constant evaluation, through the pointer.
  template <class T>
  [[nodiscard]] constexpr T& value() noexcept {
    if consteval {
      return detail::value_of<T>(fObject);
    } else if constexpr (detail::kReadsBuffer) {
      return std::launder(reinterpret_cast<detail::holder<T>*>(fBuffer))->value;
    } else {
      return detail::value_of<T>(fObject);
    }
  }
  template <class T>
  [[nodiscard]] constexpr const T& value() const noexcept {
    if consteval {
      return detail::value_of<T>(static_cast<const detail::held*>(fObject));
    } else if constexpr (detail::kReadsBuffer) {
      return std::launder(reinterpret_cast<const detail::holder<T>*>(fBuffer))->value;
    } else {
      return detail::value_of<T>(static_cast<const detail::held*>(fObject));
    }
  }

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
    to.template make<T>(std::move(from.template value<T>()));
  }
  template <class T>
  static constexpr void copy_one(variant& to, const variant& from) {
    to.template make<T>(from.template value<T>());
  }
  template <class T>
  static constexpr void destroy_one(variant& self) {
    if consteval {
      delete static_cast<detail::holder<T>*>(self.fObject);
    } else {
      if constexpr (detail::kReadsBuffer) {
        std::launder(reinterpret_cast<detail::holder<T>*>(self.fBuffer))->~holder();
      } else {
        static_cast<detail::holder<T>*>(self.fObject)->~holder();
      }
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
      detail::halve_to<0, kSize, decltype(at), Ts...>(fIndex, at);
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
      detail::halve_to<0, kSize, decltype(at), Ts...>(fIndex, at);
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
      detail::halve_to<0, kSize, decltype(at), Ts...>(fIndex, at);
    }
  }

  // Which one: in the smallest unsigned that holds every index.
  using index_type = std::conditional_t<(sizeof...(Ts) <= 0xff), std::uint8_t,
                                        std::conditional_t<(sizeof...(Ts) <= 0xffff), std::uint16_t, std::size_t>>;
  index_type fIndex = 0;
  alignas(detail::holder<Ts>...) unsigned char fBuffer[std::max({sizeof(detail::holder<Ts>)...})];
  detail::held* fObject = nullptr;
};

// As std::get and std::get_if.
template <class T, class... Ts>
[[nodiscard]] constexpr T& get(variant<Ts...>& v) {
  if (T* one = v.template get_if<T>())
    return *one;
  throw std::bad_variant_access();
}
template <class T, class... Ts>
[[nodiscard]] constexpr const T& get(const variant<Ts...>& v) {
  if (const T* one = v.template get_if<T>())
    return *one;
  throw std::bad_variant_access();
}
template <std::size_t I, class... Ts>
[[nodiscard]] constexpr auto& get(variant<Ts...>& v) {
  return mux::get<std::tuple_element_t<I, std::tuple<Ts...>>>(v);
}
template <std::size_t I, class... Ts>
[[nodiscard]] constexpr const auto& get(const variant<Ts...>& v) {
  return mux::get<std::tuple_element_t<I, std::tuple<Ts...>>>(v);
}
template <class T, class... Ts>
[[nodiscard]] constexpr T* get_if(variant<Ts...>* v) noexcept {
  return v ? v->template get_if<T>() : nullptr;
}
template <class T, class... Ts>
[[nodiscard]] constexpr const T* get_if(const variant<Ts...>* v) noexcept {
  return v ? v->template get_if<T>() : nullptr;
}
template <class T, class... Ts>
[[nodiscard]] constexpr bool holds_alternative(const variant<Ts...>& v) noexcept {
  return v.index() == detail::index_in<T, Ts...>();
}

// As std::visit, over one or more variants -- ours, or std's (a library's
// types, knot's config): each visited in turn, the visitor given them all.
template <class V, class F>
constexpr decltype(auto) visit_one(V&& v, F&& f) {
  return std::forward<V>(v).visit(std::forward<F>(f));
}
template <class... Ts, class F>
constexpr decltype(auto) visit_one(std::variant<Ts...>& v, F&& f) {
  return std::visit(std::forward<F>(f), v);
}
template <class... Ts, class F>
constexpr decltype(auto) visit_one(const std::variant<Ts...>& v, F&& f) {
  return std::visit(std::forward<F>(f), v);
}
template <class... Ts, class F>
constexpr decltype(auto) visit_one(std::variant<Ts...>&& v, F&& f) {
  return std::visit(std::forward<F>(f), std::move(v));
}
template <class F, class V>
constexpr decltype(auto) visit(F&& f, V&& v) {
  return mux::visit_one(std::forward<V>(v), std::forward<F>(f));
}
template <class F, class V, class W, class... More>
constexpr decltype(auto) visit(F&& f, V&& v, W&& w, More&&... more) {
  return mux::visit_one(std::forward<V>(v), [&](auto&& one) -> decltype(auto) {
    return mux::visit([&](auto&&... rest) -> decltype(auto) {
      return std::invoke(f, std::forward<decltype(one)>(one), std::forward<decltype(rest)>(rest)...);
    }, std::forward<W>(w), std::forward<More>(more)...);
  });
}

}  // namespace mux
