// cache_ops destroy cannot be op_unsupported: every cache payload must be
// destructible. Copy/move may be stripped; destroy may not. The
// static_assert inside cache_ops must fire.
#include "dyn.hpp"

int main() {
  using BadOps = asf::cache_ops<asf::op_unsupported, asf::op_trivial,
                                asf::op_trivial>;
  return static_cast<int>(sizeof(BadOps));
}
