/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/ipc/InProcessParent.h"
#include "mozilla/ipc/InProcessChild.h"

namespace mozilla {
namespace ipc {

StaticRefPtr<InProcessParent> InProcessParent::sSingleton;
StaticRefPtr<InProcessChild> InProcessChild::sSingleton;
bool InProcessParent::sShutdown = false;

NS_IMPL_ISUPPORTS(InProcessParent, nsIObserver)
NS_IMPL_ISUPPORTS0(InProcessChild)

/* static */ void
InProcessParent::Startup()
{
  MOZ_ASSERT(NS_IsMainThread());

  if (sShutdown) {
    NS_WARNING("Could not get in-process actor while shutting down!");
    return;
  }

  nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
  if (!obs) {
    sShutdown = true;
    NS_WARNING("Failed to get nsIObserverService for in-process actor");
    return;
  }

  RefPtr<InProcessParent> parent = new InProcessParent();
  RefPtr<InProcessChild> child = new InProcessChild();

  // Observe the shutdown event to close & clean up after ourselves.
  nsresult rv = obs->AddObserver(parent, NS_XPCOM_SHUTDOWN_THREADS_OBSERVER_ID, false);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  // Link the two actors
  if (!child->OpenOnSameThread(parent->GetIPCChannel(), ChildSide)) {
    MOZ_CRASH("Failed to open InProcessChild!");
  }

  child->SetActorAlive();
  parent->SetOtherProcessId(base::GetCurrentProcId());

  // Expose the actor singletons.
  InProcessParent::sSingleton = parent.forget();
  InProcessChild::sSingleton = child.forget();
}

NS_IMETHODIMP
InProcessParent::Observe(nsISupports* aSubject, const char* aTopic, const wchar_t* aData)
{
  MOZ_ASSERT(!strcmp(aTopic, NS_XPCOM_SHUTDOWN_THREADS_OBSERVER_ID));

  sShutdown = true;
  Close();

  InProcessParent::sSingleton = nullptr;
  InProcessChild::sSingleton = nullptr;
}

} // namespace ipc
} // namespace mozilla