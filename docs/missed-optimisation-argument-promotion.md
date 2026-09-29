# Missed optimisation: ArgumentPromotion drops `!tbaa` from a conditionally-executed load it promotes, which blocks store-to-load forwarding through the promoted pointer

Found with clang 23.1.2 (Debian `23.1.2~++20260920033443+85ac56026243`) at `-O3` on x86_64. Neither a gcc bug nor a TBAA bug: the TBAA tag is present in the frontend's IR and survives every pass until `ArgumentPromotionPass`, which re-creates the load in the caller without it.

## Reproducer (no templates, no headers)

```cpp
struct held {};
struct s0 { int state; };
struct s1 { long x; };
struct h0 : held { s0 value; };
struct h1 : held { s1 value; };
struct V { unsigned char index; alignas(8) unsigned char buffer[8]; held* object; };

static void set(s0& o) { o.state = 7; }
static void set(s1&) {}
static bool check(s0& o) { return o.state == 7; }
static bool check(s1&) { return true; }

static void visit_set(V& v) {
  if (v.index == 0) return set(static_cast<h0*>(v.object)->value);
  return set(static_cast<h1*>(v.object)->value);
}
static bool visit_check(V& v) {
  if (v.index == 0) return check(static_cast<h0*>(v.object)->value);
  return check(static_cast<h1*>(v.object)->value);
}

bool probe(V& v) { visit_set(v); return visit_check(v); }
```

`clang++ -std=c++23 -O3 -S -emit-llvm`

### Expected

`probe` stores `7` through `v.object` when `index == 0` and returns `true`: the second check reads the `int` just written through the same pointer, and nothing in between can change either `v.object` (a pointer; the only store is an `int`) or `v.index` (the only store is an `int`, not a character type).

### Actual

```llvm
define ... i1 @_Z5probeR1V(ptr ... %0) {
  %2 = load i8, ptr %0, align 8, !tbaa !9
  %3 = icmp eq i8 %2, 0
  br i1 %3, label %4, label %10
4:
  %5 = getelementptr inbounds nuw i8, ptr %0, i64 16
  %6 = load ptr, ptr %5, align 8                ; <- v.object, no !tbaa
  store i32 7, ptr %6, align 4, !tbaa !13
  %7 = load ptr, ptr %5, align 8                ; <- v.object reloaded, no !tbaa
  %8 = load i32, ptr %7, align 4, !tbaa !13
  %9 = icmp eq i32 %8, 7                        ; <- the check is kept
  br label %10
10:
  %11 = phi i1 [ %9, %4 ], [ true, %1 ]
  ret i1 %11
}
```

The two loads of `v.object` carry no TBAA tag, so alias analysis cannot tell that `store i32 7` through `%6` does not overwrite `v.object` itself: `v.object` is reloaded, `state` is reloaded, and the comparison stays.

## Where the tag is lost

`-mllvm -print-after-all -mllvm -filter-print-funcs=_ZL9visit_setR1V`:

After the inliner, `visit_set` still loads `v.object` with its tag, **in one arm only** (the `s1` arm's load died with the empty `set(s1&)`):

```llvm
define internal fastcc void @_ZL9visit_setR1V(ptr noundef nonnull align 8 dereferenceable(24) %0) {
  %2 = load i8, ptr %0, align 8, !tbaa !9
  %3 = icmp eq i8 %2, 0
  br i1 %3, label %4, label %7
4:
  %5 = getelementptr inbounds nuw i8, ptr %0, i64 16
  %6 = load ptr, ptr %5, align 8, !tbaa !13     ; tagged
  store i32 7, ptr %6, align 4, !tbaa !14
  ...
```

`ArgumentPromotionPass` then promotes the `V&` argument (the function is internal, the argument `dereferenceable(24)`), passing `index` and `object` by value:

```llvm
define internal fastcc void @_ZL9visit_setR1V(i8 %0, ptr %1) {
  %3 = icmp eq i8 %0, 0
  br i1 %3, label %4, label %5
4:
  store i32 7, ptr %1, align 4, !tbaa !9
  ...
```

and the loads it creates at the call sites in `probe` have no `!tbaa`. Since the load of `object` was conditional in the callee, the promotion *speculates* it into the caller (legal thanks to `dereferenceable(24)`), and the metadata is not carried over to the speculated load. The same happens for `visit_check`, so `probe` ends up with two untagged loads of the same field around an `int` store.

## Why it matters

This is the shape every `std::variant`-like type takes when it keeps a pointer to its held object (or when a visitor dispatches on an index and reaches the object through a pointer member): a `visit` that writes, followed by a `visit` that reads, never folds, however small the variant (two alternatives suffice). Written inline (no helper functions to promote), the same code folds.

## Workarounds that make it fold (for reference)

1. Load the pointer unconditionally, before the branch, in the callee:
   ```cpp
   static void visit_set(V& v) {
     held* o = v.object;
     if (v.index == 0) return set(static_cast<h0*>(o)->value);
     return set(static_cast<h1*>(o)->value);
   }
   ```
   The promoted load is then not speculative and keeps its tag.
2. `__builtin_assume_separate_storage(v.object, &v.object);` in each callee.

## Possible fix

When `ArgumentPromotion` materialises a load in the caller for a load that was not guaranteed to execute in the callee, it could still attach the TBAA (and other AA) metadata common to the callee's loads of that offset: TBAA describes the type of the memory location (here the `V::object` field of a `V` the argument is `dereferenceable(24)` for), which does not depend on the path the load was on. Only metadata whose meaning depends on execution (`!nonnull`, `!range`, `!noundef`, `!align` on the loaded value…) needs dropping for a speculated load.

## Same code in gcc 14

gcc 14.2 (`-O3`) folds this reproducer completely (`return 1`) when reading through the pointer. (With `std::launder` on the buffer instead of the pointer, gcc keeps the check: its `.LAUNDER` internal call is an optimisation barrier — a separate, gcc-side matter.)
