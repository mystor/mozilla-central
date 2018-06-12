/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_BrowsingContext_h
#define mozilla_dom_BrowsingContext_h

#include "nsWrapperCache.h"

namespace mozilla {
namespace dom {

class RelatedContextSet;

class BrowsingContext : public nsWrapperCache
{
  friend class RelatedContextSet;
public:
  enum class State {
    Active,
    Background,
    Dead
  };

  // We have to do some fancy stuff with our reference counts. See
  // BrowsingContext.cpp for the reasoning.
  nsrefcnt AddRef();
  nsrefcnt Release();

  // Kill this BrowsingContext - this disconnects the context from the tree, and
  // could potentially kill it if no other outstanding references exist.
  void Die();

private:
  ~BrowsingContext();

  void DieInternal();

  State mState;

  // These pointers are managed by the lifecycle of RelatedContextSet objects.
  // They are not strong due to using this custom logic.
  RelatedContextSet* mContextSet;
  BrowsingContext* mParent;

  // XXX(nika): This should probably be stored on the live SHEntry?
  nsTArray<BrowsingContext*> mLiveChildren;
  nsTArray<BrowsingContext*> mAllChildren;
};

} // namespace dom

namespace ipc {
// Support sending BrowsingContext over IPC.
//
// NOTE: We only support sending BrowsingContext over PContent-managed actors.
template<>
struct IPDLParamTraits<dom::BrowsingContext>
{
  void Write(IPC::Message* aMsg, IProtocol* aActor, dom::BrowsingContext* aContext);
  bool Read(const IPC::Message* aMsg, PickleIterator* aIter,
            IProtocol* aActor, RefPtr<dom::BrowsingContext>& aContext);
};
} // namespace ipc
} // namespace mozilla

#endif // defined(mozilla_dom_BrowsingContext_h)
