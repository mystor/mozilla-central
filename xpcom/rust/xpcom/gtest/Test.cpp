#include "gtest/gtest.h"

extern "C" bool Rust_CallIURIFromRust();

TEST(RustXpcom, CallIURIFromRust)
{
  // XXX: Improve
  EXPECT_TRUE(Rust_CallIURIFromRust());
}
