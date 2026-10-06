# Code style

How mux and its libraries (tern, loom, knot, chevron, alef, splice, skiff,
skiff-widgets) are written. A change that does not follow this is not done.

## Language and build

- C++26, **modules only**: every unit is a module interface, partition or
  implementation unit. No headers of our own; a library's headers only in a
  global module fragment, where a library has no module.
- clang 23. Build with libc++ or libstdc++; both are supported.
- Where the toolchain has a bug, work around it in the code and say why in a
  comment, with the reduced case if there is one. Known ones:
  - libstdc++ range adaptor pipes across modules: write
    `std::ranges::to<T>(view)` and `std::views::transform(range, f)` (call
    form), not `range | std::views::transform(f) | std::ranges::to<T>()`.
  - A class template specialization declared `extern template` in a module
    must not be deleted in a unit that imports it: destroy it in the unit
    that instantiates it (see `destroy_account`).

## Types at the boundary

- Everything is typed. Whatever comes in -- JSON, XML, a protocol's
  fields, a config file, a name, a key -- is read **once**, where it comes
  in, into structs, enums and variants. Code past that boundary works on
  the types. No generic trees (`knot::value` and the like) and no raw
  strings to dig through.
- What the program does not understand is kept opaque (the raw JSON text,
  for "View source") and never looked into.
- Never go from one typed value to another through text: no typed ->
  JSON -> typed round trips, no re-parsing what was already read. One type
  becomes another with a plain function, or knot's typed conversion.
- JSON is written through knot (`knot::write`, `knot::to_json_string`) and
  read with `knot::try_read`, lazily from any input range.

## No branching on strings

- A name, a tag, a type, a kind -- anything that comes in as text and
  decides what the code does -- is read into a `std::variant` (or
  `spl::variant`) of types at the boundary, through a `*_of` function with
  a table. No `name == "..."` chains anywhere past it.

## Variants and dispatch

- Work on a variant with `visit` and an overload set (`spl::overloaded{...}`,
  one callable per alternative), or with members and overloads on the
  alternative types themselves, so that every alternative is handled and a
  new one cannot be forgotten.
- `std::holds_alternative`, `std::get_if` chains,
  `if constexpr (std::same_as<...>)` and `std::is_same_v` dispatch are not
  used. Where visiting is truly impossible, say why in a comment.
- Dispatch by overloading, not by comparing types in a generic lambda.

## Calls and erasure

- No `std::function`, no function pointers. What a node or a library calls
  into the program is a template parameter: a type with static members, or
  a callable type.
- A library tells the program things as **data** the program reads (a list
  it drains, a change it returns), not through a callback.
- Exceptions, allowed on purpose:
  - `src/app/workers.cc`: the worker pool's job queue
    (`std::move_only_function`).
  - skiff's `AnyNode` and the erased walks, outside a release build.
  - Outside a release build, any type erasure that cuts build time or
    memory (`std::function`, tables of function pointers, Asio's
    `any_completion_handler`). A release build is fully static.
  - `spl::variant`: an index, a buffer and a table per type, by design.
- No macros where the language can do it: templates, structured binding
  packs, tables of member pointers walked by templates, overloading. A
  `#define` only where a library's headers require one.

## Declarative code

- What builds or scans a sequence is a ranges pipeline or algorithm
  (`std::views::transform`, `filter`, `join`, `enumerate`/`iota`,
  `std::ranges::to`, `std::ranges::find_if`...), not a hand-written loop
  pushing into an output.
- Lazy where it can be: conversions and encodings are views that allocate
  nothing. A whole copy is made only where its consumer keeps a pointer to
  contiguous memory (a C API's buffer), with a comment saying so.
- Functions take any range (a concept on the element type), never
  `std::span`.
- What streams (a hash, a MAC, a cipher) is fed a piece at a time through a
  buffer on the stack.
- Prefer constexpr to code generators: data a program needs (a Unicode
  table, a list) is read from its source file as published, with `#embed`,
  by constexpr code at compile time.

## Bytes and casts

- Never `reinterpret_cast`, and no C-style cast doing its job: no viewing
  one type's bytes as another's (`char*` as `std::uint8_t*`), no
  `std::as_bytes`. Bytes are kept in the type their reader wants from the
  start, converted element by element (`std::ranges::to`, `std::bit_cast`
  for one value).
- Where a C API itself takes another pointer type (`sockaddr*`, a
  callback's `void*`), the narrowest cast it requires, behind one small
  typed wrapper, with a comment saying why.

## The UI (skiff)

- Reuse the nodes that exist: where there is already a node for something
  (a reply bar, a header, a button, a list row), use it -- made a template
  on what differs, or moved where both can import it -- never a copy.
- A node's children are a `parts` aggregate walked as a binding pack, not a
  hand-written child list.
- Drawing is declarative: fills, borders and corner radii in the node's
  spec; skiff and skiff-widgets nodes for pictures, avatars and icons --
  not `drawSelf` in the app.
- A widget library offers extension points (a template parameter with
  hooks, data a hook returns), not a feature of one program: the text area
  keeps spans of a format the program names, and mux makes Telegram's
  editor on it.

## Comments, names and commits

- Comments say what a thing is and why it is so, in plain English
  sentences. Where code works around something, the comment says what
  happened without it.
- English everywhere: code, comments, commits, documents.
- A commit message says what changed and why: a short first line, then the
  reason -- what was wrong, what it is now.
