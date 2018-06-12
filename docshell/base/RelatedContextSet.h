/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_RelatedContextSet_h
#define mozilla_dom_RelatedContextSet_h

#include "mozilla/BrowsingContext.h"

namespace mozilla {
namespace dom {


//////////////////////////////////////////////////////////////////////////////
//                         Browsing Context Lifetime                        //
//////////////////////////////////////////////////////////////////////////////
//
// In order to handle messages being sent between processes cleanly, the
// lifetime semantics of Browsing Context objects are somewhat complex.
//
// ~ Content Processes ~
//
// Browsing Contexts are handled in sets. Each RelatedContextSet represents a
// set of BrowsingContexts which may reference one-another. A content process
// becomes 'subscribed' to a RelatedContextSet when any context from that set is
// transmitted to it.
//
// Every time a BrowsingContext is sent to a content process, it is sent with an
// ContextSet epoch. This is used for coordinating unsubscribing.
//
// A Browsing Context is not free'd when its reference count reaches 0. Instead,
// the RelatedContextSet keeps track of how many browsing contexts it contains
// with a non-0 reference count.
//
// When that number reaches 0, a message is sent to the parent process asking to
// unsubscribe, passing down the current epoch. The RelatedContextSet is also
// flagged such that it will appear to be dead to most code.
//
// If the epoch matches in the parent, the set is removed from the
// ContentParent's subscription list, and the parent replies with whether or not
// the unsubscription succeeded. If it did, the child process may free
// everything.
//
// Browsing Contexts are marked as 'dead' when the containing nsFrameLoader is
// destroyed (~approximately~). Dead browsing contexts are sent over IPC as
// null, and may be deleted as soon as their reference count reaches 0.
//
// ~ Parent Process ~
//
// When a browsing context 'dies', a message is sent to each subscribed content
// process. Once all processes acknowledge the death, the context object is
// flagged as deletable, and will be destroyed when the reference count hits 0.
//
// The RelatedContextSet is destroyed when all Browsing Contexts in it are
// destroyed.
//
// The parent process always maintains a copy of live browsing contexts.


// A "Unit of Related Browsing Contexts" according to the web standard. This
// struct is similar to the TabGroup object, however it is created at a
// different time, can be shared between processes, and manages BrowsingContext
// objects.
//
// As Browsing Contexts are created, they are added to either an existing or new
// RelatedContextSet. This set represents all Browsing Contexts which are aware
// of each-other's existence in the scripting sense. This involves tracking
// across opener.
//
// When a BrowsingContext is sent over IPC to a process which is unaware of it,
// the complete RelatedContextSet is sent alongside it, such that all
// BrowsingContext references are preserved.
//
// The Chrome RelatedContextSet represents the set of all BrowsingContext
// objects in chrome docshells.
class RelatedContextSet final
{
  friend class BrowsingContext;
public:
  // Lifecycle notes:
  //  - The RelatedContextSet is created when the current process becomes aware
  //    of it, due to a BrowsingContext in this set being sent over IPC or being
  //    created in-process.
  //  - Each BrowsingContext holds a strong reference to its RelatedContextSet,
  //    keeping it alive.
  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(RelatedContextSet)
  NS_DECL_CYCLE_COLLECTION_NATIVE_CLASS(RelatedContextSet)

  RelatedContextSet();

  static already_AddRefed<RelatedContextSet> ChromeContextSet() {
    MOZ_RELEASE_ASSERT(XRE_IsParentProcess(), "Chrome BC in content?");
    if (!sChromeSet) {
      sChromeSet = new RelatedContextSet();
    }
    return do_AddRef(sChromeSet);
  }

private:
  explicit RelatedContextSet(uint64_t aUniqueID);
  ~RelatedContextSet();

  uint64_t mUniqueID;

  // NOTE: mContexts is maintained by BrowsingContext, which will remove itself
  // from its RelatedContextSet when it dies.
  nsTHashtable<nsPtrHashKey<BrowsingContext>> mContexts;

  // What ContextSets does this process know about?
  typedef nsDataHashtable<nsInt64HashKey, RelatedContextSet*> KnownSetTable;
  static StaticAutoPtr<KnownSetTable> sKnownSets;

  static RelatedContextSet* sChromeSet;  // weak
};

} // namespace dom
} // namespace mozilla

#endif // defined mozilla_dom_RelatedContextSet_h
