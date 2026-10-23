// to_dyn with an undersized cache_ptr Spec: sizeof/alignof fit is
// static_assert, not a soft-empty handle.
#include "dyn.hpp"
#include "cast.hpp"

#include <string>

namespace {

template <typename T = void>
struct Stringable;

template <>
struct Stringable<void> {
  std::string to_string(auto const& self);
};

struct Session {
  int n{};
  std::string label;
};

template <>
struct Stringable<Session> final {
  static std::string to_string(Session const& self) {
    return self.label;
  }
};

}  // namespace

int main() {
  Session s{1, "tiny"};
  auto d = asf::to_dyn<Stringable, asf::cache_ptr<1, 1>>(s);
  return static_cast<int>(static_cast<bool>(d));
}
