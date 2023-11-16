/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "FiniteAbstractDomain.h"

#include <gtest/gtest.h>
#include <sstream>

using namespace sparta;

enum Determinism {
  DT_BOTTOM,
  IS_DET,
  NOT_DET,
  DT_TOP // Nullable
};

using Lattice = sparta::BitVectorLattice<Determinism, 4, std::hash<int>>;

/*
 *              TOP
 *             /   \
 *            D     E
 *           / \   /
 *          B    C
 *           \  /
 *            A
 *            |
 *          BOTTOM
 */
Lattice lattice({DT_BOTTOM, IS_DET, NOT_DET, DT_TOP},
                    {{DT_BOTTOM, IS_DET},
                     {DT_BOTTOM, NOT_DET},
                     {IS_DET, DT_TOP},
                     {NOT_DET, DT_TOP}});

using Domain =
    FiniteAbstractDomain<Determinism, Lattice, Lattice::Encoding, &lattice>;

TEST(MyDomainTest, latticeOperations) {
  Domain bottom(DT_BOTTOM);
  Domain top(DT_TOP);

//   EXPECT_TRUE(a.equals(Domain(A)));
//   EXPECT_FALSE(a.equals(b));
  EXPECT_TRUE(bottom.equals(Domain::bottom()));
  EXPECT_TRUE(top.equals(Domain::top()));
  EXPECT_FALSE(Domain::top().equals(Domain::bottom()));

//   EXPECT_TRUE(a.leq(b));
//   EXPECT_TRUE(a.leq(e));
//   EXPECT_FALSE(b.leq(e));
  EXPECT_TRUE(bottom.is_bottom());
  EXPECT_TRUE(top.is_top());

  EXPECT_TRUE(bottom.leq(top));
  EXPECT_FALSE(top.leq(bottom));

//   EXPECT_EQ(A, b.meet(c).element());
//   EXPECT_EQ(D, b.join(c).element());
//   EXPECT_EQ(C, d.meet(e).element());
//   EXPECT_EQ(TOP, d.join(e).element());
//   EXPECT_TRUE(d.join(top).is_top());
//   EXPECT_TRUE(e.meet(bottom).is_bottom());

//   EXPECT_TRUE(b.join(c).equals(b.widening(c)));
//   EXPECT_TRUE(b.narrowing(c).equals(b.meet(c)));

//   std::ostringstream o1, o2;
//   o1 << A;
//   o2 << a;
//   EXPECT_EQ(o1.str(), o2.str());
}

