/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BrowsingContext.h"

#include "mozilla/dom/ContentChild.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/StaticPtr.h"

#include "nsIDocShell.h"

#include "nsContentUtils.h"

namespace mozilla {

static StaticAutoPtr<BrowsingContext::Children> sRootBrowsingContexts;

static StaticAutoPtr<nsDataHashtable<nsUint64HashKey, BrowsingContext*>>
  sBrowsingContexts;

void
BrowsingContext::Init()
{
  if (!sRootBrowsingContexts) {
    sRootBrowsingContexts = new BrowsingContext::Children();
    ClearOnShutdown(&sRootBrowsingContexts);
  }

  if (!sBrowsingContexts) {
    sBrowsingContexts =
      new nsDataHashtable<nsUint64HashKey, BrowsingContext*>();
  }
}

/* static */ already_AddRefed<BrowsingContext>
BrowsingContext::Get(uint64_t aId)
{
  RefPtr<BrowsingContext> abc = sBrowsingContexts->Get(aId);
  return abc.forget();
}

/* static */ already_AddRefed<BrowsingContext>
BrowsingContext::GetOrCreate(uint64_t aId, const nsAString& aName)
{
  RefPtr<BrowsingContext> abc = sBrowsingContexts->Get(aId);
  if (!abc) {
    abc = new BrowsingContext(aId, aName);
  }

  return abc.forget();
}

BrowsingContext::BrowsingContext(nsIDocShell* aDocShell)
  : mBrowsingContextId(nsContentUtils::GenerateBrowsingContextId())
  , mDocShell(aDocShell)
{
  sBrowsingContexts->Put(mBrowsingContextId, this);
}

BrowsingContext::BrowsingContext(uint64_t aBrowsingContextId,
                                 const nsAString& aName)
  : mBrowsingContextId(aBrowsingContextId)
  , mName(aName)
{
  sBrowsingContexts->Put(mBrowsingContextId, this);
}

void
BrowsingContext::Attach(BrowsingContext* aParent)
{
  if (isInList()) {
    MOZ_DIAGNOSTIC_ASSERT(sBrowsingContexts->Contains(Id()));
    return;
  }

  auto* children = aParent ? &aParent->mChildren : sRootBrowsingContexts.get();
  children->insertBack(this);
  mParent = aParent;

  if (!XRE_IsContentProcess()) {
    return;
  }

  auto cc = dom::ContentChild::GetSingleton();
  MOZ_DIAGNOSTIC_ASSERT(cc);
  cc->SendAttachBrowsingContext(
    dom::BrowsingContextId(mParent ? mParent->Id() : 0),
    dom::BrowsingContextId(Id()),
    mName);
}

void
BrowsingContext::Detach()
{
  RefPtr<BrowsingContext> kungFuDeathGrip(this);

  if (!isInList()) {
    return;
  }

  remove();

  if (!XRE_IsContentProcess()) {
    return;
  }

  auto cc = dom::ContentChild::GetSingleton();
  MOZ_DIAGNOSTIC_ASSERT(cc);
  cc->SendDetachBrowsingContext(dom::BrowsingContextId(Id()));
}

void
BrowsingContext::SetName(const nsAString& aName)
{
  mName = aName;
}

void
BrowsingContext::GetName(nsAString& aName)
{
  aName = mName;
}

bool
BrowsingContext::NameEquals(const nsAString& aName)
{
  return mName.Equals(aName);
}

BrowsingContext*
BrowsingContext::Parent() const
{
  return mParent;
}

BrowsingContext::~BrowsingContext()
{
  MOZ_DIAGNOSTIC_ASSERT(!isInList());
  sBrowsingContexts->Remove(mBrowsingContextId);
}

static void
ImplCycleCollectionUnlink(BrowsingContext::Children& aField)
{
  aField.clear();
}

static void
ImplCycleCollectionTraverse(nsCycleCollectionTraversalCallback& aCallback,
                            BrowsingContext::Children& aField,
                            const char* aName,
                            uint32_t aFlags = 0)
{
  for (BrowsingContext* aContext : aField) {
    aCallback.NoteNativeChild(aContext,
                              NS_CYCLE_COLLECTION_PARTICIPANT(BrowsingContext));
  }
}

NS_IMPL_CYCLE_COLLECTION(BrowsingContext, mDocShell, mChildren)
NS_IMPL_CYCLE_COLLECTION_ROOT_NATIVE(BrowsingContext, AddRef)
NS_IMPL_CYCLE_COLLECTION_UNROOT_NATIVE(BrowsingContext, Release)

} // namespace mozilla
