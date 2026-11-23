// pointer_ops destroy cannot be op_unsupported: every cache payload must be
// destructible. Copy/move may be stripped; destroy may not. The
// static_assert inside pointer_ops must fire.
#include "asf.hpp"

int main() {
  using BadOps = asf_dyn::pointer_ops<asf_dyn::op_unsupported, asf_dyn::op_trivial,
                                asf_dyn::op_trivial>;
  return static_cast<int>(sizeof(BadOps));
}
