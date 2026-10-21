// Lifetime: destroy via vt[-1] (~Src), construct, *dyn shallow copy vs deep
// clone, owning Ptr.
#include "cast.hpp"

#include <memory>
#include <print>
#include <string>

template <typename T = void>
struct Droppable;

template <>
struct Droppable<void> {
  auto who(auto const& self) -> std::string const& { return self.name; }
  [[=asf_ann::imple]] auto op_construct(auto const& self) -> std::remove_cvref_t<decltype(self)>  { return self; }
};

template <typename T = void>
struct OwnBase;

template <>
struct OwnBase<void> {
  auto who(auto const& self) -> std::string const& { return self.name; }
};

template <typename T = void>
struct OwnLeaf;

template <>
struct OwnLeaf<void> : OwnBase<void> {
  [[=asf_ann::imple]] auto op_construct(auto const& self) -> std::remove_cvref_t<decltype(self)>  { return self; }
};

struct Person {
  std::string name;
  ~Person() { std::println("~Person {}", name); }
};

template <>
struct Droppable<Person> final {};

template <>
struct OwnLeaf<Person> final {};

int main() {
  // --- destroy on dyn: always vt[-1] (~Src) ---
  {
    auto* heap = new Person{"Yan"};
    asf::dyn<Droppable> hd{heap};
    std::println("before destroy who = {}", hd->who());
    hd.destroy();  // ~Person via vt[-1]; handle clears
    std::println("after destroy engaged = {}", static_cast<bool>(hd));
    ::operator delete(heap);  // destroy() does not free the allocation
  }

  // --- *dyn proxy: shallow copy vs construct() deep clone ---
  {
    Person src{"Ada"};
    asf::dyn<Droppable> d{&src};
    auto view = *d;
    auto alias = view;                 // shallow: same observation
    auto clone = asf::construct(view); // deep: owned copy (default Ptr)

    src.name = "Zed";
    std::println("alias sees = {}", alias->who());  // Zed
    std::println("clone keeps = {}", clone->who());                 // Ada

    // rproxy selects move-construct overload (non-const observation)
    Person mv{"MoveMe"};
    asf::dyn<Droppable> md{&mv};
    auto moved = asf::construct(md.rproxy());
    std::println("construct(rproxy) who = {}", moved->who());
  }

  // --- unique_ptr ownership: last owner runs ~Src then frees storage ---
  {
    Person drop_src{"Uptr"};
    asf::dyn<Droppable> obs{&drop_src};
    auto owned = asf::construct<std::unique_ptr>(*obs);
    std::println("unique_dyn who = {}", owned->who());
    // scope end: ~Person via vt[-1], then ::operator delete
  }

  // --- shared_ptr / weak_ptr ---
  {
    Person sp_src{"Sptr"};
    asf::dyn<Droppable> obs{&sp_src};
    auto a = asf::construct<std::shared_ptr>(*obs);
    auto b = a;
    a.reset();
    std::println("shared remaining = {}", b->who());

    asf::weak_dyn<Droppable> weak{b};
    if (auto locked = weak.lock())
      std::println("weak lock = {}", locked->who());
    b.reset();
    std::println("weak expired = {}", weak.expired());
  }

  // --- sub_dyn: own-section methods only; no table-origin prefix ---
  {
    Person own_src{"Sub"};
    asf::dyn<OwnLeaf> leaf{&own_src};
    asf::sub_dyn<OwnBase> sub{leaf};
    std::println("sub_dyn who = {}", sub->who());
  }

#if 0  // --- wrong / ill-formed ---
  // Store destroy in a deleter, then hand off: see examples/*/07_pointer_trait.cpp.
  // *c_dyn construct is always a deep copy, even asf::construct(cd.rproxy()).
  // weak_dyn has no method surface; lock() first.
  asf::weak_dyn<Droppable> w;
  w->who();
#endif
}
