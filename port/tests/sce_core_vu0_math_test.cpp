#include <gtest/gtest.h>

#include <cfloat>
#include <climits>
#include <cmath>

#include "sce_core/vu0_math.hpp"

namespace vu0 = sce_core::vu0;

TEST(SceCoreVu0, AZeroVectorNormalisesToZero) {
    vu0::Vector zero = {0.0f, 0.0f, 0.0f, 5.0f};
    vu0::Vector out;
    vu0::Normalize(out, zero);
    EXPECT_EQ(out[0], 0.0f);
    EXPECT_EQ(out[3], 0.0f);
    vu0::Vector v = {3.0f, 0.0f, 4.0f, 1.0f};
    vu0::Normalize(v, v);
    EXPECT_FLOAT_EQ(v[0], 0.6f);
    EXPECT_FLOAT_EQ(v[2], 0.8f);
}

TEST(SceCoreVu0, TheInverseUndoesARigidTransform) {
    vu0::Matrix m;
    vu0::UnitMatrix(m);
    vu0::RotMatrixY(m, m, 0.5f);
    vu0::Vector move = {1.0f, 2.0f, 3.0f, 0.0f};
    vu0::TransMatrix(m, m, move);
    vu0::Matrix inverse;
    vu0::InversMatrix(inverse, m);
    vu0::Vector p = {4.0f, -5.0f, 6.0f, 1.0f};
    vu0::Vector q;
    vu0::ApplyMatrix(q, m, p);
    vu0::ApplyMatrix(q, inverse, q);
    for (int i = 0; i < 4; i++) {
        EXPECT_NEAR(q[i], p[i], 1e-5f);
    }
}

TEST(SceCoreVu0, FixedPointTruncatesAndSaturates) {
    vu0::Vector  v = {1.99f, -1.99f, 3.0e9f, -3.0e9f};
    vu0::IVector out;
    vu0::FTOI0Vector(out, v);
    EXPECT_EQ(out[0], 1);
    EXPECT_EQ(out[1], -1);
    EXPECT_EQ(out[2], INT_MAX);
    EXPECT_EQ(out[3], INT_MIN);
    vu0::FTOI4Vector(out, v);
    EXPECT_EQ(out[0], 31);
}

TEST(SceCoreVu0, RotTransPersIsTwelvePointFourAfterTheDivide) {
    vu0::Matrix m;
    vu0::UnitMatrix(m);
    vu0::Vector  v = {2.0f, 4.0f, 6.0f, 2.0f};
    vu0::IVector out;
    vu0::RotTransPers(out, m, v, 0);
    EXPECT_EQ(out[0], 16);
    EXPECT_EQ(out[1], 32);
    EXPECT_EQ(out[2], 48);
    vu0::RotTransPers(out, m, v, 1);
    EXPECT_EQ(out[2], 3);
    EXPECT_EQ(out[3], 2);
}
