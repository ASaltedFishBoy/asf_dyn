#pragma once

// Rust-like trait with an intermediate mirror:
//   Trait<void>     method list (auto& self is actually the impl's self mirror)
//   (default)       trait methods are dyn slots
//   Annotations may sit on Trait<void> or on one method. Method overrides
//   Trait. Pairs: dyn/no_dyn, expl/no_expl, imple/no_imple, mirror/no_mirror.
//   Defaults: dyn, no_expl, no_imple, no_mirror. Both forms on one declaration
//   are ill-formed. A dyn slot requires self as the first parameter and
//   at most one deduced template parameter (self); a second cannot be a dyn
//   slot. Return type must be written on the declaration (no undetermined
//   auto / decltype(auto) return).
//   Layout-only (not methods): truncate / truncated (same root).
//   [[=asf_ann::truncate]]  active cut on Trait or base-specifier: dyn cannot
//                   upcast further / that MI branch gets no MULTI_VT cover.
//   [[=asf_ann::truncated]] passive on Trait: inheriting it is a truncate
//                   edge (dyn cannot upcast to that Interface).
//   static            on Trait<void>: iface-only (opt out of the dyn vtable)
//   [[=asf_ann::mirror]]  chooses presented Self at each call. Mirror Arg
//                   -> iface: bind ungated. Sourced: every method
//                   callable; mark -> pass mirror, else Src. Unsourced:
//                   only [[=mirror]] methods (iface only, no dyn).
//                   Sourced -> dyn may mix mirror / Src sections.
//   body            Trait<void> default impl
//   [[=asf_ann::imple]]   between Trait<T> and Trait<void>: insert an
//                   impl from T. Non-op: same structural match as Trait<T>
//                   (name, params incl. cvref; self cvref kind — mirror
//                   peels View/T vs the interface self; object members
//                   also match const / ref-qual — lvalue self matches
//                   unqualified or &). op_*: special lookup, not under that
//                   name (ctor / = / convert / matching op; construct:
//                   auto&& is an rvalue).
//   Trait<T>        impl; first param is mirror<T> or mirror<T, T>
//   mirror<T>         from implicit T& or user conversion; mirror-to-mirror cannot drop cv
//   mirror<T, TT>     from TT if TT converts to mirror<T>
//   h->m(...)       method surface on temporary from operator-> (handle stays thin)
//   (*h) op x       operator* -> same proxy; Trait uses op_add/... names (not
//                   C++ operator tokens -- explicit-object Self!=mirror is unsafe)
//   To(*h)          carrier: operator R() from op_convert* only (never
//                   op_construct). Concrete operator T() may have T==Self;
//                   templated operator To() must not convert to Self.
//                   [[=asf_ann::expl]] (default implicit, like operator T())
//   (*h) = x        op_assign* -> operator= for concrete RHS only (not Src/
//                   auto). Src/auto assign is never on the carrier (iface or
//                   dyn *); only construct is the Src-related dyn-dispatch op.
//   h2 = h1         handle/pointer assign (rebind); not (*h) value assign.
//   op_construct    return must be remove_cvref of the first parameter
//                   (e.g. `-> std::remove_cvref_t<decltype(self)>`); a
//                   concrete type only if that impl targets that type alone.
//                   Overloads are ordinary dyn slots (dispatch by args /
//                   self value category). *dyn copy/move -> *dyn; proxy does
//                   not own Storage. sub_dyn construct stays on the same
//                   slice (cannot recover Final table top). Default void*;
//                   OutPtr customizable.
//   unsourced iface (plain / unsourced-mirror Arg): carrier is convert +
//                   concrete assign only (construct is not a carrier op);
//                   reverse friends on *iface only. Unsourced-mirror Arg
//                   binds without a layout gate; only [[=mirror]] callable.
//   iface<Trait, T>   concrete handle. T is the object or mirror<View, Src>
//                         Src set => Self = mirror<T, Src>; else from impl
//   to_mirror        object only (no mirror). <T> => mirror<T, Src>;
//                    omitted => mirror<T, T>. Object converts to that T
//                    (T&& / const T& / T as T&) or user / identity_mirror
//   to_pure_mirror   bare mirror<T>. Object or mirror; obj may take <T>
//   src_of(mirror)   sourced identity. cv-ref from T, not from m.
//   destroy          always vt[-1] (~Src). Not a Trait method.
//                  handle API: destroy() calls that slot.
//                  size()/align(): sizeof/alignof Storage from the vt tail
//                  when Trait has dyn op_construct; empty -> 0.
//                  Negative prefix APIs (destroy, typeid_of):
//                  not on sub_dyn (prefix is the table origin; a section
//                  cannot know if it sits there).
//   operator->     return type proxy_t = fn_proxy<Host, Fns>
//   to_iface<Trait>(obj)  object, or sourced mirror if ann::mirror
//   iface.as<To>    upcast or downcast along Trait bases (same Self/Src)
//   iface implicit  Trait up/down (ctor/assign; keeps T/Src) when Self
//                   (mirror or src) can form the target Trait's Self.
//                   as<To>() rewraps Self along the Trait chain (up or down).
//                   get() returns Self by value (copy of T& / mirror).
//   c_iface<Trait, T>  const-self: const T, or mirror<const View, Src>
//   iface OP U      Trait op_* by self param index (0: h OP u; 1: u OP h, no_dyn).
//                   Operator cv follows that method's self; no special case
//                   for compound assign.
//   cast.hpp        trait_cast/c_trait_cast: iface / observer dyn / owning dyn
//                   to_dyn/to_c_dyn (ptr|handle; user mirror if ann::mirror)
//   dyn<Trait, Ptr=void*>  {complete erased Ptr, vt}; default void* is
//                   non-owning observation.
//                   Casts keep the same complete Ptr.
//                   Sourced mirror bind may use view!=Src: destroy is vt[-1]
//                   (~Src), not a Trait method.
//                   Ptr aliases: void* (observation), unique_dyn / shared_dyn
//                   (= shared_ptr<void>) / weak_dyn (= weak_ptr<void>),
//                   cache_buf<Size,Align> (raw buffer) /
//                   cache_ptr<Size,Align[,Ops]> (erased cache Ptr on cache_buf).
//                   cache_ptr_for<T[,Ops]> = cache_ptr<sizeof(T),alignof(T)[,Ops]>
//                   (concrete size-for-T; no unique_ptr deleter; not
//                   free-construct).
//                   Free construct: construct(*dyn) follows source Ptr;
//                   construct<Ptr>(*dyn); construct<cache_ptr...>(*dyn).
//                   to_dyn<Trait, cache_ptr>(value|mirror).
//                   to_dyn<Trait, std::unique_ptr>([alloc, ]value).
//                   Deep move: construct(d.rproxy()) (same Ptr as source).
//                   Complete Ptr embeds the unique_ptr deleter.
//                   unique_ptr: omitted Deleter == default_delete<void>; explicit non-default
//                   Alloc -> alloc_delete (sized). dyn_adopt_delete<Free>
//                   runs vt[-1] destroy then Free; dyn_default_delete is
//                   its default-Free alias (::operator delete). unique_ptr
//                   with either is deleter-kind.
//                   Shorthands: unique_dyn/cache_dyn/shared/weak.
//                   Lifecycle: unique_ptr vt destroy then Deleter; cache_dyn
//                   vt destroy then clear; shared_ptr one allocator block.
//                   to_dyn adopts Ptr (deleter kept). Value->unique_ptr/cache
//                   forwards.
//                   Alloc resolve on construct: optional first function arg
//                   (detected by *dyn / proxy position) > Out Ptr baked >
//                   source Ptr > default. Remaining args after the proxy
//                   dispatch to op_construct. Uses allocator_traits.
//                   weak_ptr: expired/bool/lock() only (like std::weak_ptr);
//                   call/-> via lock() -> shared_ptr dyn. shared/weak OK.
//                   Handle convert (ctor/assign): Trait upcast and SrcP -> Ptr
//                   together. explicitness follows the Ptr types.
//                   Same Ptr copies/moves (or cache via vt). Deleter dest
//                   from vt_destroy: P(raw, d). Else Ptr(src).
//                   trait_cast uses this constructor.
//   c_dyn<...>      const handle; dyn -> c_dyn implicit (shares vt);
//                   else const-masked vtable from ptr/mirror. Destroy is
//                   still vt[-1] (~Src), including for a vt_destroy Ptr.
//   *dyn/*c_dyn proxy  Trait-keyed (independent of Ptr): method/ops surface
//                   (proxy_t by value). Copy/move of proxy is shallow (same
//                   observation). Object clone: construct(proxy_t)=copy,
//                   construct(rproxy_t)=move. Explicit proxy <-> rproxy is
//                   also shallow; CTAD: dyn_rproxy(p) / dyn_proxy(rp).
//                   Implicit proxy -> c_proxy (shallow);
//                   rproxy has no such conversions.
//                   dyn / c_dyn: d.proxy()/rproxy()/c_proxy() (also *d).
//                   c_dyn: c_proxy() is the same type as proxy().
//                   std::move(*d) stays proxy_t&&.
//   Trait<void> : B0<void>, B1<void>, ...   interface MI (declaration order;
//                   B0 primary). Leaf: Trait<void> final => all Trait<T> final;
//                   or Trait<T> final for that As only.
//                   ASF_HAS_DYN_INTERMEDIATE (default 1): non-final ptr/mirror
//                   bind ADL-fetches; dyn final injects (same-TU STMP first
//                   wins; multi-TU ODR/COMDAT, no weak). 0: final-only
//                   ptr/mirror bind. iface: no final.

//   layout          grid-merge sections (all ancestors' methods on the table).
//                   Cover = layout as if that Trait were final (own DAG), not a
//                   slice of the real final. With ASF_HAS_DYN_MULTI_VT: Redirect
//                   slots interleaved after each non-prefix section (never only
//                   at the end; section-prefix upcasts still share a leading
//                   vt slice). Without: sections only (no cover tables).
//                   Alternate: Redirects at negative indices (weakest); unused.
//   ASF_HAS_DYN_MULTI_VT  1 (default): Redirect/cover multi-vtable; dyn->dyn
//     full-path upcast (any Trait base via Redirect when not section-prefix).
//     0: sections-only; dyn->dyn section-prefix / same physical vt only.
//     Mutually exclusive with ASF_HAS_DYN_SINGLE_INHERIT (CMake FATAL_ERROR).
//   ASF_HAS_DYN_SINGLE_INHERIT  0 (default): Trait MI allowed. 1: require SI
//     among live dyn bases at generation (truncate/truncated edges ok).
//     Mutex with ASF_HAS_DYN_MULTI_VT. Not an extra upcast switch.
//   ASF_HAS_DYN_RTTI    0: no meta / dyn_cast
//                      1: vt meta header; dyn_cast (non-prefix targets still
//                         need MULTI_VT Redirects)
//   ASF_HAS_DYN_INTERMEDIATE  1 (default): to_dyn from non-final via ADL fetch;
//     only dyn final injects (same-TU STMP first wins; multi-TU ODR/COMDAT, no
//     weak). RTTI on that dyn follows the winning leaf vt. 0: final-only bind.
//   RTTI prefix     meta
//   method slots    include op_construct overloads (by-args dispatch).
//                   Destroy is vt[-1] (~Src), not a method slot.
//   meta            find(trait_id)->one of tables[0..x) (distinct physical
//                   vts); type info; per physical-vt-key copy.
//   vtable key      (Mask, Final, Trait, Storage, As): cover tables belong to
//                   the final root (Redirect I9->I8 -> vtable<...,I9,I8,...>);
//                   not type-linked to a bare intermediate Trait table
//   dyn_cast        RTTI: static upcast (upcast_vt) else meta.find; empty miss
//   unsafe_downcast  retype Trait only; same ptr + same vt pointer (no adjust).
//                   Not static_cast. Safe only when the handle already sits on
//                   the leaf/final (or same physical) table you mean to name.
//                   MULTI_VT hazard: after a Redirect upcast the handle holds a
//                   cover vt (layout-as-if-To-final), not the leaf table; naming
//                   a more-derived Trait while keeping that cover vt is UB
//                   (wrong slots / wrong Final). Prefer dyn_cast when RTTI on;
//                   without MULTI_VT, mainly section-prefix same-table cases.
//   dyn/iface       own methods only; base methods via as<To>() or
//                   proxy<To>() / rproxy<To>()
//                   (get_proxy / get_rproxy / get_forward_proxy).
//   as<To>()        member only. dyn: same-vt view (dyn_view / c_dyn_view),
//                   never convert / never switch vt. iface: Self rewrap of a
//                   Trait on the inheritance chain (up or down). Storable cast: implicit ctor /
//                   to_dyn / trait_cast (iface: member trait_cast<To>() /
//                   c_trait_cast<To>(); also downcasts when Self converts).
//   get()           dyn: Ptr&; iface: Self by value (T& / mirror).
//   raw()           dyn: void* / const void* erased object address (identity;
//                   empty cache_dyn -> nullptr). Hot path uses storage_addr().
//   proxy<To>() / rproxy<To>() / c_proxy<To>()  call surface. To == this
//                   Trait uses proxy() / rproxy() / c_proxy(); a base To uses
//                   that view's proxy / rproxy / c_proxy. c_dyn / const iface:
//                   c_proxy() is the same type as proxy().
//   get_proxy<To>(h)            proxy<To>()  (const&)
//   get_rproxy<To>(h)           rproxy<To>() (const&)
//   get_forward_proxy<To>(h)    lvalue proxy / rvalue rproxy
//   sub_dyn / c_sub_dyn<Trait[,Ptr...]>  isolated section slice of a leaf/final
//                   dyn table (to_sub_dyn / ctor from dyn; not a dyn member).
//                   Same Ptr as dyn: lvalue copies Ptr, rvalue moves Ptr.
//                   Independent of MULTI_VT; Trait's own methods only; no
//                   secondary / implicit upcast / to_dyn from sub_dyn.
//                   No negative prefix (vt[-1] destroy, RTTI meta):
//                   a section cannot know if it sits at table
//                   origin (this Trait may be a sibling even with no bases).
//                   vt_destroy Ptr only if pointer_trait::release(*this)
//                   is valid.

// Layout (public vs detail/; include order):
//   mirror.hpp                 mirror (public)
//   ref.hpp                    ref<P> / cref<P> / as_ref / as_cref (public)
//   iface.hpp                  iface<> handles (public)
//   dyn.hpp                    dyn / c_dyn (+ ref.hpp) (public)
//   interface.hpp              this umbrella (pulls dyn.hpp)
//   cast.hpp                   as_* / cast helpers (public, separate)

#include "dyn.hpp"
