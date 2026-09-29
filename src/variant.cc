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
// constant expression everywhere, where a void* cast back is not (C++26
// only, and not in every compiler). In constant evaluation the holder is
// allocated instead: placement into a buffer is not a constant expression. Moving, copying, destroying and visiting go
// through one table each per variant type (and per visitor): an entry per
// alternative, made by one pack expansion.
export module mux.variant;

import std;

namespace mux::detail {
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
  constexpr variant(variant&& other) noexcept : fIndex(other.fIndex) { kMove[fIndex](*this, other); }
  constexpr variant(const variant& other) : fIndex(other.fIndex) { kCopy[fIndex](*this, other); }
  constexpr variant& operator=(variant&& other) noexcept {
    if (this != &other) {
      kDestroy[fIndex](*this);
      fIndex = other.fIndex;
      kMove[fIndex](*this, other);
    }
    return *this;
  }
  constexpr variant& operator=(const variant& other) {
    if (this != &other) {
      kDestroy[fIndex](*this);
      fIndex = other.fIndex;
      kCopy[fIndex](*this, other);
    }
    return *this;
  }
  constexpr ~variant() { kDestroy[fIndex](*this); }

  [[nodiscard]] constexpr std::size_t index() const noexcept { return fIndex; }

  // The one it holds, given to `f`; what `f` gives back.
  template <class F>
  constexpr decltype(auto) visit(F&& f) {
    using R = std::invoke_result_t<F&, std::tuple_element_t<0, std::tuple<Ts...>>&>;
    static constexpr std::array<R (*)(F&, detail::held*), kSize> table{
        +[](F& g, detail::held* p) -> R { return std::invoke(g, detail::value_of<Ts>(p)); }...};
    return table[fIndex](f, fObject);
  }
  template <class F>
  constexpr decltype(auto) visit(F&& f) const {
    using R = std::invoke_result_t<F&, const std::tuple_element_t<0, std::tuple<Ts...>>&>;
    static constexpr std::array<R (*)(F&, const detail::held*), kSize> table{
        +[](F& g, const detail::held* p) -> R { return std::invoke(g, detail::value_of<Ts>(p)); }...};
    return table[fIndex](f, fObject);
  }

 private:
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
  static constexpr std::array<void (*)(variant&, variant&), kSize> kMove{&move_one<Ts>...};
  static constexpr std::array<void (*)(variant&, const variant&), kSize> kCopy{&copy_one<Ts>...};
  static constexpr std::array<void (*)(variant&), kSize> kDestroy{&destroy_one<Ts>...};

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
