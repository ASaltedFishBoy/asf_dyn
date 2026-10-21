#pragma once

// Intermediate ptr/mirror-bind (ASF_HAS_DYN_INTERMEDIATE).
// Included from interface.hpp inside asf::detail after vtable<> is defined.
// See docs/en/spec/features.md (ASF_HAS_DYN_INTERMEDIATE).

// --- ADL object-vt: non-final calls; final injects (same-TU STMP first wins) ---
// Only dyn final walks the definition path (dyn_vt_inject). Non-final only
// ADL-calls adl_dyn_object_vt. Same-TU: friend-injection marker dyn_vt_gen
// (auto return -> requires false until defined) skips a second inject so the
// friend adl_dyn_object_vt is not redefined. Cross-TU: ODR/COMDAT pick (no weak).
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-template-friend"
#pragma GCC diagnostic ignored "-Wsfinae-incomplete"
#endif
template <bool MaskNonConst, template <class> class Trait, typename Storage,
          typename As>
struct dyn_vt_tag {
  friend vt_ptr adl_dyn_object_vt(dyn_vt_tag);
};

// Marker: friend declared with undeduced auto so requires{dyn_vt_gen} is false
// until dyn_vt_inject defines it (concrete int return would make it always true).
template <bool MaskNonConst, template <class> class Trait, typename Storage,
          typename As>
struct dyn_vt_state {
  friend consteval auto dyn_vt_gen(dyn_vt_state);
};

template <bool MaskNonConst, template <class> class Final,
          template <class> class Base, typename Storage, typename As>
struct dyn_vt_inject {
  friend consteval auto dyn_vt_gen(
      dyn_vt_state<MaskNonConst, Base, Storage, As>) {
    return 0;
  }

  // Force the injected friend into the object file. At -O1+ GCC would fold
  // the body to vtable<>::slots() and drop the symbol, leaving non-final
  // callers (main.o) undefined. Two TUs may each keep their own copy.
#if defined(__GNUC__)
  friend vt_ptr __attribute__((used))
      adl_dyn_object_vt(dyn_vt_tag<MaskNonConst, Base, Storage, As>) {
    return vtable<MaskNonConst, Final, Final, Storage, As>::slots();
  }
#else
  friend vt_ptr adl_dyn_object_vt(
      dyn_vt_tag<MaskNonConst, Base, Storage, As>) {
    return vtable<MaskNonConst, Final, Final, Storage, As>::slots();
  }
#endif
};
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

template <bool MaskNonConst, template <class> class Final,
          template <class> class Base, typename Storage, typename As>
struct dyn_vt_register {
  static consteval void inject_if_needed() {
    using State = dyn_vt_state<MaskNonConst, Base, Storage, As>;
    if constexpr (requires { dyn_vt_gen(State{}); }) {
      // Same-TU: already injected by an earlier final -- skip (no redefinition).
    } else {
      (void)sizeof(dyn_vt_inject<MaskNonConst, Final, Base, Storage, As>);
    }
  }

  // ODR-use: naming force runs STMP inject at compile time.
  static constexpr int force = (inject_if_needed(), 0);
};

template <bool MaskNonConst, template <class> class Final, typename Storage,
          typename As, std::size_t I>
struct dyn_vt_register_at {
  static constexpr auto sec = sections_only_of(^^Final<void>)[I];
  using Reg = [:std::meta::substitute(
      ^^dyn_vt_register,
      {std::meta::reflect_constant(MaskNonConst), ^^Final,
       std::meta::template_of(sec), ^^Storage, ^^As}):];
  static constexpr int force = Reg::force;
};

template <bool MaskNonConst, template <class> class Final, typename Storage,
          typename As>
struct dyn_vt_adl_registrar {
  static consteval std::size_t n() {
    return sections_only_of(^^Final<void>).size();
  }

  template <class...>
  struct type_sink {};

  template <typename>
  struct force_pack;

  template <std::size_t... I>
  struct force_pack<std::index_sequence<I...>>
      : type_sink<std::integral_constant<
            int, dyn_vt_register_at<MaskNonConst, Final, Storage, As,
                                    I>::force>...> {};

  using force = force_pack<std::make_index_sequence<n()>>;

  // ELF emits an in-class friend only on odr-use in the TU, and the
  // registering TU never calls it. dyn_object_slots calls emit_all() before
  // returning; the calls are dead but the __attribute__((used)) on the
  // injected friend keeps the symbol in the object file. (MinGW COMDAT
  // emitted it implicitly.)
  template <std::size_t I>
  static void emit_one() {
    constexpr auto sec = sections_only_of(^^Final<void>)[I];
    using Tag = [:std::meta::substitute(
        ^^dyn_vt_tag,
        {std::meta::reflect_constant(MaskNonConst),
         std::meta::template_of(sec), ^^Storage, ^^As}):];
    (void)adl_dyn_object_vt(Tag{});
  }

  template <std::size_t... I>
  static void emit_all(std::index_sequence<I...>) {
    (emit_one<I>(), ...);
  }

  static void emit_all() { emit_all(std::make_index_sequence<n()>{}); }
};

template <bool MaskNonConst, template <class> class Trait, typename Storage,
          typename As>
vt_ptr dyn_object_slots() {
  assert_no_final_trait_parent<Trait, As>();
  assert_single_inherit<Trait>();
  if constexpr (is_trait_final<Trait, As>()) {
    (void)sizeof(typename dyn_vt_adl_registrar<MaskNonConst, Trait, Storage,
                                               As>::force);
    dyn_vt_adl_registrar<MaskNonConst, Trait, Storage, As>::emit_all();
    return vtable<MaskNonConst, Trait, Trait, Storage, As>::slots();
  } else {
    return adl_dyn_object_vt(dyn_vt_tag<MaskNonConst, Trait, Storage, As>{});
  }
}
