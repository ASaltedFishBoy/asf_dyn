#include "asf_check.hpp"
#include "cast.hpp"
#include "mirror_bind.hpp"

#include <print>
#include <unordered_set>

/* NxN diamond MI grid (I1..I9 shape).
   4x4 diamond (= I1..I9): expand 1..4, contract 3..1.
   Root G0_0; Leaf (Trait<Person> final bind) : G5_0, G5_1.
   Upward-only From->To (geometry closure, not can_dyn_upcast NxN). Reports
   channel count + distinct Final-keyed physical vtables (layout closure).

   Inheritance (arrow = : base):

                        G0_0
                       /    \
                   G1_0      G1_1
                  /    \    /    \
              G2_0      G2_1      G2_2
             /    \    /    \    /    \
         G3_0      G3_1      G3_2      G3_3
             \    /    \    /    \    /
              G4_0      G4_1      G4_2
                  \    /    \    /
                   G5_0      G5_1
                       \    /
                        Leaf
*/
template <typename T = void>
struct G0_0;
template <>
struct [[=asf_ann::mirror]]
G0_0<void> {
  int m_0_0(auto& self);
};
template <typename T = void>
struct G1_0;
template <>
struct [[=asf_ann::mirror]]
G1_0<void> : G0_0<void> {
  int m_1_0(auto& self);
};
template <typename T = void>
struct G1_1;
template <>
struct [[=asf_ann::mirror]]
G1_1<void> : G0_0<void> {
  int m_1_1(auto& self);
};
template <typename T = void>
struct G2_0;
template <>
struct [[=asf_ann::mirror]]
G2_0<void> : G1_0<void> {
  int m_2_0(auto& self);
};
template <typename T = void>
struct G2_1;
template <>
struct [[=asf_ann::mirror]]
G2_1<void> : G1_0<void>, G1_1<void> {
  int m_2_1(auto& self);
};
template <typename T = void>
struct G2_2;
template <>
struct [[=asf_ann::mirror]]
G2_2<void> : G1_1<void> {
  int m_2_2(auto& self);
};
template <typename T = void>
struct G3_0;
template <>
struct [[=asf_ann::mirror]]
G3_0<void> : G2_0<void> {
  int m_3_0(auto& self);
};
template <typename T = void>
struct G3_1;
template <>
struct [[=asf_ann::mirror]]
G3_1<void> : G2_0<void>, G2_1<void> {
  int m_3_1(auto& self);
};
template <typename T = void>
struct G3_2;
template <>
struct [[=asf_ann::mirror]]
G3_2<void> : G2_1<void>, G2_2<void> {
  int m_3_2(auto& self);
};
template <typename T = void>
struct G3_3;
template <>
struct [[=asf_ann::mirror]]
G3_3<void> : G2_2<void> {
  int m_3_3(auto& self);
};
template <typename T = void>
struct G4_0;
template <>
struct [[=asf_ann::mirror]]
G4_0<void> : G3_0<void>, G3_1<void> {
  int m_4_0(auto& self);
};
template <typename T = void>
struct G4_1;
template <>
struct [[=asf_ann::mirror]]
G4_1<void> : G3_1<void>, G3_2<void> {
  int m_4_1(auto& self);
};
template <typename T = void>
struct G4_2;
template <>
struct [[=asf_ann::mirror]]
G4_2<void> : G3_2<void>, G3_3<void> {
  int m_4_2(auto& self);
};
template <typename T = void>
struct G5_0;
template <>
struct [[=asf_ann::mirror]]
G5_0<void> : G4_0<void>, G4_1<void> {
  int m_5_0(auto& self);
};
template <typename T = void>
struct G5_1;
template <>
struct [[=asf_ann::mirror]]
G5_1<void> : G4_1<void>, G4_2<void> {
  int m_5_1(auto& self);
};
template <typename T = void>
struct Leaf;
template <>
struct [[=asf_ann::mirror]]
Leaf<void> : G5_0<void>, G5_1<void> {
  int m_leaf(auto& self);
};

struct Person {
  int id = 0;
  std::string payload{asf_test::no_sso("person")};
};

template <>
struct G0_0<Person> {
  static int m_0_0(asf::mirror<Person> self) {
    ++self.id;
    return self.id * 100 + 0;
  }
};
template <>
struct G1_0<Person> {
  static int m_1_0(asf::mirror<Person> self) {
    ++self.id;
    return self.id * 100 + 10;
  }
};
template <>
struct G1_1<Person> {
  static int m_1_1(asf::mirror<Person> self) {
    ++self.id;
    return self.id * 100 + 11;
  }
};
template <>
struct G2_0<Person> {
  static int m_2_0(asf::mirror<Person> self) {
    ++self.id;
    return self.id * 100 + 20;
  }
};
template <>
struct G2_1<Person> {
  static int m_2_1(asf::mirror<Person> self) {
    ++self.id;
    return self.id * 100 + 21;
  }
};
template <>
struct G2_2<Person> {
  static int m_2_2(asf::mirror<Person> self) {
    ++self.id;
    return self.id * 100 + 22;
  }
};
template <>
struct G3_0<Person> {
  static int m_3_0(asf::mirror<Person> self) {
    ++self.id;
    return self.id * 100 + 30;
  }
};
template <>
struct G3_1<Person> {
  static int m_3_1(asf::mirror<Person> self) {
    ++self.id;
    return self.id * 100 + 31;
  }
};
template <>
struct G3_2<Person> {
  static int m_3_2(asf::mirror<Person> self) {
    ++self.id;
    return self.id * 100 + 32;
  }
};
template <>
struct G3_3<Person> {
  static int m_3_3(asf::mirror<Person> self) {
    ++self.id;
    return self.id * 100 + 33;
  }
};
template <>
struct G4_0<Person> {
  static int m_4_0(asf::mirror<Person> self) {
    ++self.id;
    return self.id * 100 + 40;
  }
};
template <>
struct G4_1<Person> {
  static int m_4_1(asf::mirror<Person> self) {
    ++self.id;
    return self.id * 100 + 41;
  }
};
template <>
struct G4_2<Person> {
  static int m_4_2(asf::mirror<Person> self) {
    ++self.id;
    return self.id * 100 + 42;
  }
};
template <>
struct G5_0<Person> {
  static int m_5_0(asf::mirror<Person> self) {
    ++self.id;
    return self.id * 100 + 50;
  }
};
template <>
struct G5_1<Person> {
  static int m_5_1(asf::mirror<Person> self) {
    ++self.id;
    return self.id * 100 + 51;
  }
};
template <>
struct Leaf<Person> final {
  static int m_leaf(asf::mirror<Person> self) {
    ++self.id;
    return self.id * 100 + 99;
  }
};


namespace {

template <template <class> class Final>
consteval std::size_t layout_table_count() {
  return asf::detail::vtable_instance_count<Final>();
}

using VtSet = std::unordered_set<asf::detail::vt_ptr>;

// Upcast geometry (no can_dyn_upcast matrix): direct parents are level�? with
// same or adjacent b (expand: (a,b)->(a-1,b)/(a-1,b-1); contract:
// (a,b)->(a-1,b)/(a-1,b+1)); channels = reflexive-transitive closure (=100).
// Each upcast inserts the resulting vt into `tables`.
template <template <class> class To, template <class> class From>
void do_upcast(asf::dyn<From>& from, std::size_t& channels, VtSet& tables) {
  auto h = asf::trait_cast<To>(from);
  ++channels;
  tables.insert(asf::detail::vt_of(h));
}

void upcasts_from_G0_0(asf::dyn<G0_0>& from, std::size_t& channels,
                       VtSet& tables) {
  do_upcast<G0_0>(from, channels, tables);
}

void upcasts_from_G1_0(asf::dyn<G1_0>& from, std::size_t& channels,
                       VtSet& tables) {
  do_upcast<G0_0>(from, channels, tables);
  do_upcast<G1_0>(from, channels, tables);
}

void upcasts_from_G1_1(asf::dyn<G1_1>& from, std::size_t& channels,
                       VtSet& tables) {
  do_upcast<G0_0>(from, channels, tables);
  do_upcast<G1_1>(from, channels, tables);
}

void upcasts_from_G2_0(asf::dyn<G2_0>& from, std::size_t& channels,
                       VtSet& tables) {
  do_upcast<G0_0>(from, channels, tables);
  do_upcast<G1_0>(from, channels, tables);
  do_upcast<G2_0>(from, channels, tables);
}

void upcasts_from_G2_1(asf::dyn<G2_1>& from, std::size_t& channels,
                       VtSet& tables) {
  do_upcast<G0_0>(from, channels, tables);
  do_upcast<G1_0>(from, channels, tables);
  do_upcast<G1_1>(from, channels, tables);
  do_upcast<G2_1>(from, channels, tables);
}

void upcasts_from_G2_2(asf::dyn<G2_2>& from, std::size_t& channels,
                       VtSet& tables) {
  do_upcast<G0_0>(from, channels, tables);
  do_upcast<G1_1>(from, channels, tables);
  do_upcast<G2_2>(from, channels, tables);
}

void upcasts_from_G3_0(asf::dyn<G3_0>& from, std::size_t& channels,
                       VtSet& tables) {
  do_upcast<G0_0>(from, channels, tables);
  do_upcast<G1_0>(from, channels, tables);
  do_upcast<G2_0>(from, channels, tables);
  do_upcast<G3_0>(from, channels, tables);
}

void upcasts_from_G3_1(asf::dyn<G3_1>& from, std::size_t& channels,
                       VtSet& tables) {
  do_upcast<G0_0>(from, channels, tables);
  do_upcast<G1_0>(from, channels, tables);
  do_upcast<G1_1>(from, channels, tables);
  do_upcast<G2_0>(from, channels, tables);
  do_upcast<G2_1>(from, channels, tables);
  do_upcast<G3_1>(from, channels, tables);
}

void upcasts_from_G3_2(asf::dyn<G3_2>& from, std::size_t& channels,
                       VtSet& tables) {
  do_upcast<G0_0>(from, channels, tables);
  do_upcast<G1_0>(from, channels, tables);
  do_upcast<G1_1>(from, channels, tables);
  do_upcast<G2_1>(from, channels, tables);
  do_upcast<G2_2>(from, channels, tables);
  do_upcast<G3_2>(from, channels, tables);
}

void upcasts_from_G3_3(asf::dyn<G3_3>& from, std::size_t& channels,
                       VtSet& tables) {
  do_upcast<G0_0>(from, channels, tables);
  do_upcast<G1_1>(from, channels, tables);
  do_upcast<G2_2>(from, channels, tables);
  do_upcast<G3_3>(from, channels, tables);
}

void upcasts_from_G4_0(asf::dyn<G4_0>& from, std::size_t& channels,
                       VtSet& tables) {
  do_upcast<G0_0>(from, channels, tables);
  do_upcast<G1_0>(from, channels, tables);
  do_upcast<G1_1>(from, channels, tables);
  do_upcast<G2_0>(from, channels, tables);
  do_upcast<G2_1>(from, channels, tables);
  do_upcast<G3_0>(from, channels, tables);
  do_upcast<G3_1>(from, channels, tables);
  do_upcast<G4_0>(from, channels, tables);
}

void upcasts_from_G4_1(asf::dyn<G4_1>& from, std::size_t& channels,
                       VtSet& tables) {
  do_upcast<G0_0>(from, channels, tables);
  do_upcast<G1_0>(from, channels, tables);
  do_upcast<G1_1>(from, channels, tables);
  do_upcast<G2_0>(from, channels, tables);
  do_upcast<G2_1>(from, channels, tables);
  do_upcast<G2_2>(from, channels, tables);
  do_upcast<G3_1>(from, channels, tables);
  do_upcast<G3_2>(from, channels, tables);
  do_upcast<G4_1>(from, channels, tables);
}

void upcasts_from_G4_2(asf::dyn<G4_2>& from, std::size_t& channels,
                       VtSet& tables) {
  do_upcast<G0_0>(from, channels, tables);
  do_upcast<G1_0>(from, channels, tables);
  do_upcast<G1_1>(from, channels, tables);
  do_upcast<G2_1>(from, channels, tables);
  do_upcast<G2_2>(from, channels, tables);
  do_upcast<G3_2>(from, channels, tables);
  do_upcast<G3_3>(from, channels, tables);
  do_upcast<G4_2>(from, channels, tables);
}

void upcasts_from_G5_0(asf::dyn<G5_0>& from, std::size_t& channels,
                       VtSet& tables) {
  do_upcast<G0_0>(from, channels, tables);
  do_upcast<G1_0>(from, channels, tables);
  do_upcast<G1_1>(from, channels, tables);
  do_upcast<G2_0>(from, channels, tables);
  do_upcast<G2_1>(from, channels, tables);
  do_upcast<G2_2>(from, channels, tables);
  do_upcast<G3_0>(from, channels, tables);
  do_upcast<G3_1>(from, channels, tables);
  do_upcast<G3_2>(from, channels, tables);
  do_upcast<G4_0>(from, channels, tables);
  do_upcast<G4_1>(from, channels, tables);
  do_upcast<G5_0>(from, channels, tables);
}

void upcasts_from_G5_1(asf::dyn<G5_1>& from, std::size_t& channels,
                       VtSet& tables) {
  do_upcast<G0_0>(from, channels, tables);
  do_upcast<G1_0>(from, channels, tables);
  do_upcast<G1_1>(from, channels, tables);
  do_upcast<G2_0>(from, channels, tables);
  do_upcast<G2_1>(from, channels, tables);
  do_upcast<G2_2>(from, channels, tables);
  do_upcast<G3_1>(from, channels, tables);
  do_upcast<G3_2>(from, channels, tables);
  do_upcast<G3_3>(from, channels, tables);
  do_upcast<G4_1>(from, channels, tables);
  do_upcast<G4_2>(from, channels, tables);
  do_upcast<G5_1>(from, channels, tables);
}

void upcasts_from_Leaf(asf::dyn<Leaf>& from, std::size_t& channels,
                       VtSet& tables) {
  do_upcast<G0_0>(from, channels, tables);
  do_upcast<G1_0>(from, channels, tables);
  do_upcast<G1_1>(from, channels, tables);
  do_upcast<G2_0>(from, channels, tables);
  do_upcast<G2_1>(from, channels, tables);
  do_upcast<G2_2>(from, channels, tables);
  do_upcast<G3_0>(from, channels, tables);
  do_upcast<G3_1>(from, channels, tables);
  do_upcast<G3_2>(from, channels, tables);
  do_upcast<G3_3>(from, channels, tables);
  do_upcast<G4_0>(from, channels, tables);
  do_upcast<G4_1>(from, channels, tables);
  do_upcast<G4_2>(from, channels, tables);
  do_upcast<G5_0>(from, channels, tables);
  do_upcast<G5_1>(from, channels, tables);
  do_upcast<Leaf>(from, channels, tables);
}

constexpr std::size_t expected_upcast_channels = 100;

int run() {
  Person person{.id = 7};
  auto root = asf_test::to_dyn_m<Leaf>(person);
  // Src-dependent: bump id and encode prior id into the return.
  if (root->m_leaf() != 8 * 100 + 99 || person.id != 8)
    return 3;

  std::size_t channels = 0;
  VtSet tables;
  {
    auto h = asf::trait_cast<G0_0>(root);
    upcasts_from_G0_0(h, channels, tables);
  }
  {
    auto h = asf::trait_cast<G1_0>(root);
    upcasts_from_G1_0(h, channels, tables);
  }
  {
    auto h = asf::trait_cast<G1_1>(root);
    upcasts_from_G1_1(h, channels, tables);
  }
  {
    auto h = asf::trait_cast<G2_0>(root);
    upcasts_from_G2_0(h, channels, tables);
  }
  {
    auto h = asf::trait_cast<G2_1>(root);
    upcasts_from_G2_1(h, channels, tables);
  }
  {
    auto h = asf::trait_cast<G2_2>(root);
    upcasts_from_G2_2(h, channels, tables);
  }
  {
    auto h = asf::trait_cast<G3_0>(root);
    upcasts_from_G3_0(h, channels, tables);
  }
  {
    auto h = asf::trait_cast<G3_1>(root);
    upcasts_from_G3_1(h, channels, tables);
  }
  {
    auto h = asf::trait_cast<G3_2>(root);
    upcasts_from_G3_2(h, channels, tables);
  }
  {
    auto h = asf::trait_cast<G3_3>(root);
    upcasts_from_G3_3(h, channels, tables);
  }
  {
    auto h = asf::trait_cast<G4_0>(root);
    upcasts_from_G4_0(h, channels, tables);
  }
  {
    auto h = asf::trait_cast<G4_1>(root);
    upcasts_from_G4_1(h, channels, tables);
  }
  {
    auto h = asf::trait_cast<G4_2>(root);
    upcasts_from_G4_2(h, channels, tables);
  }
  {
    auto h = asf::trait_cast<G5_0>(root);
    upcasts_from_G5_0(h, channels, tables);
  }
  {
    auto h = asf::trait_cast<G5_1>(root);
    upcasts_from_G5_1(h, channels, tables);
  }
  upcasts_from_Leaf(root, channels, tables);
  if (channels != expected_upcast_channels)
    return 2;

  constexpr auto layout_n = layout_table_count<Leaf>();
  constexpr auto section_n = asf::detail::sections_only<Leaf>().size();
  auto n_tables = tables.size();
  if (n_tables != layout_n)
    return 1;

  // Spot-check: direct parent (a-1, same b) and a downcast that must fail.
  static_assert(asf::detail::can_dyn_upcast<G2_1, G3_1>());
  static_assert(!asf::detail::can_dyn_upcast<G3_1, G2_1>());
  static_assert(!asf::detail::can_dyn_upcast<G1_0, G1_1>());
  {
    auto i8 = asf::trait_cast<G3_1>(root);
    auto i5 = asf::trait_cast<G2_1>(i8);
    int before = person.id;
    if (i5->m_2_1() != (before + 1) * 100 + 21 || person.id != before + 1)
      return 4;
    if (i8->m_3_1() != (before + 2) * 100 + 31 || person.id != before + 2)
      return 4;
    if (asf::trait_cast<G0_0>(root)->m_0_0() != (before + 3) * 100 + 0 ||
        person.id != before + 3)
      return 4;
  }

  std::println(
      "iface_grid_vt ok: diamond=4x4 nodes=16 "
      "sections={} layout_tables={} upcast_channels={} distinct_vtables={} "
      "src_id={}",
      section_n, layout_n, channels, n_tables, person.id);
  return 0;
}

}  // namespace

int main() {
#if !ASF_HAS_DYN_MULTI_VT
  std::println("iface_grid_vt needs ASF_HAS_DYN_MULTI_VT");
  return 0;
#else
  return run();
#endif
}