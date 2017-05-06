#include "gtest/gtest.h"
#include "nsCOMPtr.h"
#include "nsIRunnable.h"

extern "C" bool Rust_CallIURIFromRust();

TEST(RustXpcom, CallIURIFromRust)
{
  // XXX: Improve
  EXPECT_TRUE(Rust_CallIURIFromRust());
}

extern "C" void Rust_ImplementRunnableInRust(bool* aItWorked,
                                             nsIRunnable** aRunnable);

TEST(RustXpcom, ImplementRunnableInRust)
{
  bool itWorked = false;
  nsCOMPtr<nsIRunnable> runnable;
  Rust_ImplementRunnableInRust(&itWorked, getter_AddRefs(runnable));

  EXPECT_TRUE(runnable);
  EXPECT_FALSE(itWorked);
  runnable->Run();
  EXPECT_TRUE(itWorked);
}
