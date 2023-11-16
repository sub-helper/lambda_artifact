/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "ConstantEnvironment.h"

int64_t SignedConstantDomain::max_element() const {
  if (constant_domain().is_value()) {
    return *constant_domain().get_constant();
  }
  switch (interval()) {
  case sign_domain::Interval::EMPTY:
    not_reached_log("Empty interval does not have a max element");
  case sign_domain::Interval::EQZ:
  case sign_domain::Interval::LEZ:
    return 0;
  case sign_domain::Interval::LTZ:
    return -1;
  case sign_domain::Interval::GEZ:
  case sign_domain::Interval::GTZ:
  case sign_domain::Interval::ALL:
  case sign_domain::Interval::NEZ:
    return std::numeric_limits<int64_t>::max();
  case sign_domain::Interval::SIZE:
    not_reached();
  }
}

int64_t SignedConstantDomain::min_element() const {
  if (constant_domain().is_value()) {
    return *constant_domain().get_constant();
  }
  switch (interval()) {
  case sign_domain::Interval::EMPTY:
    not_reached_log("Empty interval does not have a min element");
  case sign_domain::Interval::EQZ:
  case sign_domain::Interval::GEZ:
    return 0;
  case sign_domain::Interval::GTZ:
    return 1;
  case sign_domain::Interval::LEZ:
  case sign_domain::Interval::LTZ:
  case sign_domain::Interval::ALL:
  case sign_domain::Interval::NEZ:
    return std::numeric_limits<int64_t>::min();
  case sign_domain::Interval::SIZE:
    not_reached();
  }
}

// TODO: Instead of this custom meet function, the ConstantValue should get a
// custom meet AND JOIN that knows about the relationship of NEZ and certain
// non-null custom object domains.
ConstantValue meet(const ConstantValue& left, const ConstantValue& right) {
  // looks like a lambda function to me
  // A lambda with empty capture clause [ ] can access only those variable which are local to it.
  auto is_nez = [](const ConstantValue& value) {
    auto signed_value = value.maybe_get<SignedConstantDomain>();
    return signed_value &&
           signed_value->interval() == sign_domain::Interval::NEZ;
  };
  auto is_not_null = [](const ConstantValue& value) {
    return !value.is_top() && !value.is_bottom() &&
           !value.maybe_get<SignedConstantDomain>();
  };
  // Non-null objects of custom object domains are compatible with NEZ, and
  // more specific.
  if (is_nez(left) && is_not_null(right)) {
    return right;
  }
  if (is_nez(right) && is_not_null(left)) {
    return left;
  }
  // Non-null objects of different custom object domains can never alias, so
  // they meet at bottom, which is the default meet implementation for
  // disjoint domains.
  return left.meet(right);
}
