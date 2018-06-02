/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef BrowsingContext_h
#define BrowsingContext_h

#include "mozilla/LinkedList.h"
#include "mozilla/RefPtr.h"
#include "mozilla/WeakPtr.h"
#include "nsCOMPtr.h"
#include "nsCycleCollectionParticipant.h"
#include "nsString.h"

class nsIDocShell;

namespace mozilla {

class BrowsingContext
  : public SupportsWeakPtr<BrowsingContext>
  , public LinkedListElement<RefPtr<BrowsingContext>>
{
public:
  static void Init();

  static already_AddRefed<BrowsingContext> Get(uint64_t aId);
  static already_AddRefed<BrowsingContext> GetOrCreate(uint64_t aId,
                                                       const nsAString& aName);

  explicit BrowsingContext(nsIDocShell* aDocShell);
  BrowsingContext(uint64_t aBrowsingContextId, const nsAString& aName);

  void Attach(BrowsingContext* aParent);
  void Detach();

  void SetName(const nsAString& aName);
  void GetName(nsAString& aName);
  bool NameEquals(const nsAString& aName);

  uint64_t Id() const { return mBrowsingContextId; }

  BrowsingContext* Parent() const;

  MOZ_DECLARE_WEAKREFERENCE_TYPENAME(BrowsingContext)
  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(BrowsingContext)
  NS_DECL_CYCLE_COLLECTION_NATIVE_CLASS(BrowsingContext)

  using Children = AutoCleanLinkedList<RefPtr<BrowsingContext>>;

private:
  ~BrowsingContext();

  const uint64_t mBrowsingContextId;
  WeakPtr<BrowsingContext> mParent;
  Children mChildren;
  nsCOMPtr<nsIDocShell> mDocShell;
  nsString mName;
};

} // namespace mozilla
#endif
