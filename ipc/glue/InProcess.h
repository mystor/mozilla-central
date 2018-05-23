/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_ipc_InProcess_h
#define mozilla_ipc_InProcess_h

#include "mozilla/ipc/PInProcessParent.h"
#include "mozilla/ipc/PInProcessChild.h"
#include "mozilla/StaticPtr.h"

namespace mozilla {
namespace ipc {

class InProcessParent : public nsIObserver
                      , public PInProcessParent
{
public:
  friend class InProcessChild;

  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER

  static InProcessParent* Singleton() {
    if (!sSingleton) {
      InProcessParent::Startup();
    }
    return sSingleton;
  }

private:
  static void Startup();
  static StaticRefPtr<InProcessParent> sSingleton;
  static bool sIsShutdown;
};

class InProcessChild : public nsISupports
                     , public PInProcessChild
{
public:
  friend class InProcessParent;

  NS_DECL_ISUPPORTS

  static InProcessChild* Singleton() {
    if (!sSingleton) {
      InProcessParent::Startup();
    }
    return sSingleton;
  }

private:
  // NOTE: Shared static state for PInProcess is stored in InProcessChild.
  static StaticRefPtr<InProcessChild> sSingleton;
};

} // namespace ipc
} // namespace mozilla

#endif // defined(mozilla_ipc_InProcess_h)
