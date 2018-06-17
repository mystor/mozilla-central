/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/ContentChild.h"
#include "mozilla/Unused.h"
#include "nsArrayUtils.h"
#include "nsClipboardProxy.h"
#include "nsISupportsPrimitives.h"
#include "nsCOMPtr.h"
#include "nsComponentManagerUtils.h"
#include "nsXULAppAPI.h"
#include "nsContentUtils.h"
#include "nsStringStream.h"

using namespace mozilla;
using namespace mozilla::dom;

NS_IMPL_ISUPPORTS(nsClipboardProxy, nsIClipboard, nsIClipboardProxy)

nsClipboardProxy::nsClipboardProxy()
  : mClipboardCaps(false, false)
{
}

NS_IMETHODIMP
nsClipboardProxy::SetData(nsITransferable *aTransferable,
                          nsIClipboardOwner *anOwner, int32_t aWhichClipboard)
{
  ContentChild* child = ContentChild::GetSingleton();

  IPCDataTransfer ipcDataTransfer;
  nsContentUtils::TransferableToIPCTransferable(aTransferable, &ipcDataTransfer,
                                                false, child, nullptr);

  bool isPrivateData = false;
  aTransferable->GetIsPrivateData(&isPrivateData);
  nsCOMPtr<nsIPrincipal> requestingPrincipal;
  aTransferable->GetRequestingPrincipal(getter_AddRefs(requestingPrincipal));
  nsContentPolicyType contentPolicyType = nsIContentPolicy::TYPE_OTHER;
  aTransferable->GetContentPolicyType(&contentPolicyType);
  child->SendSetClipboard(ipcDataTransfer, isPrivateData,
                          IPC::Principal(requestingPrincipal),
                          contentPolicyType, aWhichClipboard);

  return NS_OK;
}

NS_IMETHODIMP
nsClipboardProxy::GetData(nsITransferable *aTransferable, int32_t aWhichClipboard)
{
  nsTArray<nsCString> types;
  
  nsCOMPtr<nsIArray> flavorList;
  aTransferable->FlavorsTransferableCanImport(getter_AddRefs(flavorList));
  if (flavorList) {
    uint32_t flavorCount = 0;
    flavorList->GetLength(&flavorCount);
    for (uint32_t j = 0; j < flavorCount; ++j) {
      nsCOMPtr<nsISupportsCString> flavor = do_QueryElementAt(flavorList, j);
      if (flavor) {
        nsAutoCString flavorStr;
        flavor->GetData(flavorStr);
        if (flavorStr.Length()) {
          types.AppendElement(flavorStr);
        }
      }
    }
  }

  IPCDataTransfer dataTransfer;
  ContentChild::GetSingleton()->SendGetClipboard(types, aWhichClipboard, &dataTransfer);

  return nsContentUtils::IPCTransferableToTransferable(
    dataTransfer, aTransferable, ContentChild::GetSingleton());
}

NS_IMETHODIMP
nsClipboardProxy::EmptyClipboard(int32_t aWhichClipboard)
{
  ContentChild::GetSingleton()->SendEmptyClipboard(aWhichClipboard);
  return NS_OK;
}

NS_IMETHODIMP
nsClipboardProxy::HasDataMatchingFlavors(const char **aFlavorList,
                                         uint32_t aLength, int32_t aWhichClipboard,
                                         bool *aHasType)
{
  *aHasType = false;

  nsTArray<nsCString> types;
  nsCString* t = types.AppendElements(aLength);
  for (uint32_t j = 0; j < aLength; ++j) {
    t[j].Rebind(aFlavorList[j], nsCharTraits<char>::length(aFlavorList[j]));
  }

  ContentChild::GetSingleton()->SendClipboardHasType(types, aWhichClipboard, aHasType);

  return NS_OK;
}

NS_IMETHODIMP
nsClipboardProxy::SupportsSelectionClipboard(bool *aIsSupported)
{
  *aIsSupported = mClipboardCaps.supportsSelectionClipboard();
  return NS_OK;
}


NS_IMETHODIMP
nsClipboardProxy::SupportsFindClipboard(bool *aIsSupported)
{
  *aIsSupported = mClipboardCaps.supportsFindClipboard();
  return NS_OK;
}

void
nsClipboardProxy::SetCapabilities(const ClipboardCapabilities& aClipboardCaps)
{
  mClipboardCaps = aClipboardCaps;
}
