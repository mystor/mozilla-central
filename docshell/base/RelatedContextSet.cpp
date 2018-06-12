/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/RelatedContextSet.h"

namespace mozilla {
namespace dom {

StaticAutoPtr<RelatedContextSet::KnownSetTable> RelatedContextSet::sKnownSets;
RelatedContextSet* RelatedContextSet::sChromeSet = nullptr;

// XXX(nika): Check if we actually need Cycle Collection here?
NS_IMPL_CYCLE_COLLECTION0(RelatedContextSet)

RelatedContextSet::RelatedContextSet()
  : RelatedContextSet(nsContentUtils::GenerateRelatedContextSetId())
{ }

RelatedContextSet::RelatedContextSet(uint64_t aUniqueID)
  : mUniqueID(aUniqueID)
{
  if (!sKnownSets) {
    sKnownSets = new KnownSetTable();
    ClearOnShutdown(&sKnownSets);
  }

  auto e = sKnownSets->LookupForAdd(mUniqueID);
  MOZ_RELEASE_ASSERT(!e, "Duplicate RelatedContextSet ID");
  e.orInsert([&] { return this; });
}

RelatedContextSet::~RelatedContextSet()
{
  MOZ_DIAGNOSTIC_ASSERT(mContexts.IsEmpty());

  if (sKnownSets) {
    bool found = sKnownSets->Remove(mUniqueID);
    MOZ_RELEASE_ASSERT(found);
  }

  // Ensure sChromeSet doesn't dangle.
  if (sChromeSet == this) {
    sChromeSet = nullptr;
  }

  if (XRE_IsContentProcess()) {
    ContentChild::Unsubscribe(mUniqueID);
  }
}

} // namespace dom
} // namespace mozilla

