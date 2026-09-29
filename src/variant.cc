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
// buffer's bytes. In constant evaluation the object is allocated instead
// (placement into a buffer is not a constant expression; a void* cast back
// to its type is, since C++26). Moving, copying, destroying and visiting go
// through one table each per variant type (and per visitor): an entry per
// alternative, made by one pack expansion.
export module mux.variant;

import std;

namespace mux::detail {
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
    static constexpr std::array<R (*)(F&, void*), kSize> table{
        +[](F& g, void* p) -> R { return std::invoke(g, *static_cast<Ts*>(p)); }...};
    return table[fIndex](f, fObject);
  }
  template <class F>
  constexpr decltype(auto) visit(F&& f) const {
    using R = std::invoke_result_t<F&, const std::tuple_element_t<0, std::tuple<Ts...>>&>;
    static constexpr std::array<R (*)(F&, const void*), kSize> table{
        +[](F& g, const void* p) -> R { return std::invoke(g, *static_cast<const Ts*>(p)); }...};
    return table[fIndex](f, fObject);
  }

 private:
  template <class T, class... Args>
  constexpr void make(Args&&... args) {
    if consteval {
      fObject = new T(std::forward<Args>(args)...);
    } else {
      fObject = ::new (static_cast<void*>(fBuffer)) T(std::forward<Args>(args)...);
    }
  }
  template <class T>
  static constexpr void move_one(variant& to, variant& from) {
    to.template make<T>(std::move(*static_cast<T*>(from.fObject)));
  }
  template <class T>
  static constexpr void copy_one(variant& to, const variant& from) {
    to.template make<T>(*static_cast<const T*>(from.fObject));
  }
  template <class T>
  static constexpr void destroy_one(variant& self) {
    if consteval {
      delete static_cast<T*>(self.fObject);
    } else {
      static_cast<T*>(self.fObject)->~T();
    }
  }
  static constexpr std::array<void (*)(variant&, variant&), kSize> kMove{&move_one<Ts>...};
  static constexpr std::array<void (*)(variant&, const variant&), kSize> kCopy{&copy_one<Ts>...};
  static constexpr std::array<void (*)(variant&), kSize> kDestroy{&destroy_one<Ts>...};

  std::size_t fIndex = 0;
  alignas(Ts...) unsigned char fBuffer[std::max({sizeof(Ts)...})];
  void* fObject = nullptr;
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
