#include "nsTArray.h"

void BlackBox(int32_t i);

void TakesTArray(const nsTArray<int32_t>& aArray) {
  for (uint32_t i : aArray) {
    BlackBox(i);
  }
}
void TakesTArray2(const nsTArray<int32_t>& aArray) {
  for (uint32_t idx = 0; idx < aArray.Length(); ++idx) {
    BlackBox(aArray[idx]);
  }
}
