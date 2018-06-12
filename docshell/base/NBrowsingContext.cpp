
#include "mozilla/dom/BrowsingContext.h"

namespace mozilla {
namespace dom {

nsrefcnt
BrowsingContext::AddRef()
{
  MOZ_ASSERT(NS_IsMainThread(), "wrong thread");
  MOZ_ASSERT(int32_t(mRefCnt) >= 0, "illegal refcnt");

  nsrefcnt count = ++mRefCnt;
  NS_LOG_ADDREF(this, count, "BrowsingContext", sizeof(*this));

  // We have our first live reference to this context. Let the ContextSet know!
  if (count == 1 && !IsDead()) {
    mContextSet->RegisterContextRef(this);
  }
  return count;
}

nsrefcnt
BrowsingContext::Release()
{
  MOZ_ASSERT(NS_IsMainThread(), "wrong thread");
  MOZ_ASSERT(int32_t(mRefCnt) > 0, "dup release");

  nsrefcnt count = --mRefCnt;
  NS_LOG_RELEASE(this, count, "BrowsingContext");

  // We lost our last live refreence to this context. If we're dead, we can go
  // away, otherwise we need to let the ContextSet know.
  if (count == 0) {
    if (IsDead()) {
      delete this;
      return 0;
    }
    mContextSet->UnregisterContextRef(this);
  }
  return count;
}

void
BrowsingContext::Die()
{
  MOZ_ASSERT(mState != State::Dead);

  mParent->mAllChildren.RemoveElement(this);
  mParent->mLiveChildren.RemoveElement(this);
  DieInternal();
}

void
BrowsingContext::DieInternal()
{
  MOZ_ASSERT(mState != State::Dead);

  // Keep ourselves alive while we die - when this reference is lost, if it's
  // the last reference, we may be destroyed.
  RefPtr<BrowsingContext> kungFuDeathGrip(this);

  // Disconnect children.
  nsTArray<BrowsingContext*> children = std::move(mAllChildren);
  for (BrowsingContext* child : children) {
    child->DieInternal();
  }

  // Clear our reference.
  mParent = nullptr;
  mState = State::Dead;
}

BrowsingContext::~BrowsingContext()
{
  MOZ_ASSERT(XRE_IsContentProcess() || mState == State::Dead);
}

} // namespace dom
} // namespace mozilla
