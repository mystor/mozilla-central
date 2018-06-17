/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/ArrayUtils.h"
#include "mozilla/BasicEvents.h"
#include "mozilla/CheckedInt.h"

#include "DataTransfer.h"

#include "nsISupportsPrimitives.h"
#include "nsIScriptSecurityManager.h"
#include "mozilla/dom/DOMStringList.h"
#include "nsArray.h"
#include "nsError.h"
#include "nsIDragService.h"
#include "nsIClipboard.h"
#include "nsContentUtils.h"
#include "nsIContent.h"
#include "nsIObjectInputStream.h"
#include "nsIObjectOutputStream.h"
#include "nsIStorageStream.h"
#include "nsStringStream.h"
#include "nsCRT.h"
#include "nsIScriptObjectPrincipal.h"
#include "nsIScriptContext.h"
#include "nsIDocument.h"
#include "nsIScriptGlobalObject.h"
#include "nsVariant.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/DataTransferBinding.h"
#include "mozilla/dom/DataTransferItemList.h"
#include "mozilla/dom/Directory.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/FileList.h"
#include "mozilla/dom/BindingUtils.h"
#include "mozilla/dom/OSFileSystem.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/URLSearchParams.h"
#include "nsNetUtil.h"
#include "nsTransferable.h"

namespace mozilla {
namespace dom {

NS_IMPL_CYCLE_COLLECTION_CLASS(DataTransfer)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(DataTransfer)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mParent)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mItems)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mDragTarget)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mDragImage)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
NS_IMPL_CYCLE_COLLECTION_UNLINK_END
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(DataTransfer)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mParent)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mItems)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mDragTarget)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mDragImage)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END
NS_IMPL_CYCLE_COLLECTION_TRACE_WRAPPERCACHE(DataTransfer)

NS_IMPL_CYCLE_COLLECTING_ADDREF(DataTransfer)
NS_IMPL_CYCLE_COLLECTING_RELEASE(DataTransfer)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(DataTransfer)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(mozilla::dom::DataTransfer)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

// the size of the array
const char DataTransfer::sEffects[8][9] = {
  "none", "copy", "move", "copyMove", "link", "copyLink", "linkMove", "all"
};

// Used for custom clipboard types.
enum CustomClipboardTypeId {
  eCustomClipboardTypeId_None,
  eCustomClipboardTypeId_String
};

// The dom.events.dataTransfer.protected.enabled preference controls whether or
// not the `protected` dataTransfer state is enabled. If the `protected`
// dataTransfer stae is disabled, then the DataTransfer will be read-only
// whenever it should be protected, and will not be disconnected after a drag
// event is completed.
static bool
PrefProtected()
{
  static bool sInitialized = false;
  static bool sValue = false;
  if (!sInitialized) {
    sInitialized = true;
    Preferences::AddBoolVarCache(&sValue, "dom.events.dataTransfer.protected.enabled");
  }
  return sValue;
}

static DataTransfer::Mode
ModeForEvent(EventMessage aEventMessage)
{
  switch (aEventMessage) {
  case eCut:
  case eCopy:
  case eDragStart:
    // For these events, we want to be able to add data to the data transfer,
    // Otherwise, the data is already present.
    return DataTransfer::Mode::ReadWrite;
  case eDrop:
  case ePaste:
  case ePasteNoFormatting:
    // For these events we want to be able to read the data which is stored in
    // the DataTransfer, rather than just the type information.
    return DataTransfer::Mode::ReadOnly;
  default:
    return PrefProtected()
      ? DataTransfer::Mode::Protected
      : DataTransfer::Mode::ReadOnly;
  }
}

DataTransfer::DataTransfer(nsISupports* aParent, EventMessage aEventMessage,
                           bool aIsExternal, int32_t aClipboardType)
  : mParent(aParent)
  , mDropEffect(nsIDragService::DRAGDROP_ACTION_NONE)
  , mEffectAllowed(nsIDragService::DRAGDROP_ACTION_UNINITIALIZED)
  , mEventMessage(aEventMessage)
  , mCursorState(false)
  , mMode(ModeForEvent(aEventMessage))
  , mIsExternal(aIsExternal)
  , mUserCancelled(false)
  , mIsCrossDomainSubFrameDrop(false)
  , mClipboardType(aClipboardType)
  , mDragImageX(0)
  , mDragImageY(0)
{
  mItems = new DataTransferItemList(this, aIsExternal);

  // For external usage, cache the data from the native clipboard or drag.
  if (mIsExternal && mMode != Mode::ReadWrite) {
    if (aEventMessage == ePasteNoFormatting) {
      mEventMessage = ePaste;
      CacheExternalClipboardFormats(true);
    } else if (aEventMessage == ePaste) {
      CacheExternalClipboardFormats(false);
    } else if (aEventMessage >= eDragDropEventFirst &&
               aEventMessage <= eDragDropEventLast) {
      CacheExternalDragFormats();
    }
  }
}

DataTransfer::DataTransfer(nsISupports* aParent,
                           EventMessage aEventMessage,
                           const uint32_t aEffectAllowed,
                           bool aCursorState,
                           bool aIsExternal,
                           bool aUserCancelled,
                           bool aIsCrossDomainSubFrameDrop,
                           int32_t aClipboardType,
                           DataTransferItemList* aItems,
                           DataSource* aDataSource,
                           Element* aDragImage,
                           uint32_t aDragImageX,
                           uint32_t aDragImageY)
  : mParent(aParent)
  , mDropEffect(nsIDragService::DRAGDROP_ACTION_NONE)
  , mEffectAllowed(aEffectAllowed)
  , mEventMessage(aEventMessage)
  , mCursorState(aCursorState)
  , mMode(ModeForEvent(aEventMessage))
  , mIsExternal(aIsExternal)
  , mUserCancelled(aUserCancelled)
  , mIsCrossDomainSubFrameDrop(aIsCrossDomainSubFrameDrop)
  , mClipboardType(aClipboardType)
  , mDataSource(aDataSource)
  , mDragImage(aDragImage)
  , mDragImageX(aDragImageX)
  , mDragImageY(aDragImageY)
{
  MOZ_ASSERT(mParent);
  MOZ_ASSERT(aItems);

  // We clone the items array after everything else, so that it has a valid
  // mParent value
  mItems = aItems->Clone(this);
  // The items are copied from aItems into mItems. There is no need to copy
  // the actual data in the items as the data transfer will be read only. The
  // dragstart event is the only time when items are
  // modifiable, but those events should have been using the first constructor
  // above.
  NS_ASSERTION(aEventMessage != eDragStart,
               "invalid event type for DataTransfer constructor");
}

DataTransfer::~DataTransfer()
{}

// static
already_AddRefed<DataTransfer>
DataTransfer::Constructor(const GlobalObject& aGlobal, ErrorResult& aRv)
{

  RefPtr<DataTransfer> transfer = new DataTransfer(aGlobal.GetAsSupports(),
                                                   eCopy, /* is external */ false, /* clipboard type */ -1);
  transfer->mEffectAllowed = nsIDragService::DRAGDROP_ACTION_NONE;
  return transfer.forget();
}

JSObject*
DataTransfer::WrapObject(JSContext* aCx, JS::Handle<JSObject*> aGivenProto)
{
  return DataTransferBinding::Wrap(aCx, this, aGivenProto);
}

void
DataTransfer::SetDataSource(DataTransfer::DataSource* aSource)
{
  mDataSource = aSource;
  mIsExternal = true;

  // Cache which flavors the data source provides us.
  uint32_t itemCount = mDataSource->MozItemCount();
  for (uint32_t i = 0; i < itemCount; ++i) {
    mDataSource->CacheFlavors(this, i);
  }
}

void
DataTransfer::SetDropEffect(const nsAString& aDropEffect)
{
  // the drop effect can only be 'none', 'copy', 'move' or 'link'.
  for (uint32_t e = 0; e <= nsIDragService::DRAGDROP_ACTION_LINK; e++) {
    if (aDropEffect.EqualsASCII(sEffects[e])) {
      // don't allow copyMove
      if (e != (nsIDragService::DRAGDROP_ACTION_COPY |
                nsIDragService::DRAGDROP_ACTION_MOVE)) {
        mDropEffect = e;
      }
      break;
    }
  }
}

void
DataTransfer::SetEffectAllowed(const nsAString& aEffectAllowed)
{
  if (aEffectAllowed.EqualsLiteral("uninitialized")) {
    mEffectAllowed = nsIDragService::DRAGDROP_ACTION_UNINITIALIZED;
    return;
  }

  static_assert(nsIDragService::DRAGDROP_ACTION_NONE == 0,
                "DRAGDROP_ACTION_NONE constant is wrong");
  static_assert(nsIDragService::DRAGDROP_ACTION_COPY == 1,
                "DRAGDROP_ACTION_COPY constant is wrong");
  static_assert(nsIDragService::DRAGDROP_ACTION_MOVE == 2,
                "DRAGDROP_ACTION_MOVE constant is wrong");
  static_assert(nsIDragService::DRAGDROP_ACTION_LINK == 4,
                "DRAGDROP_ACTION_LINK constant is wrong");

  for (uint32_t e = 0; e < ArrayLength(sEffects); e++) {
    if (aEffectAllowed.EqualsASCII(sEffects[e])) {
      mEffectAllowed = e;
      break;
    }
  }
}

void
DataTransfer::GetMozTriggeringPrincipalURISpec(nsAString& aPrincipalURISpec)
{
  nsCOMPtr<nsIDragSession> dragSession = nsContentUtils::GetDragSession();
  if (!dragSession) {
    aPrincipalURISpec.Truncate(0);
    return;
  }

  nsCString principalURISpec;
  dragSession->GetTriggeringPrincipalURISpec(principalURISpec);
  CopyUTF8toUTF16(principalURISpec, aPrincipalURISpec);
}

already_AddRefed<FileList>
DataTransfer::GetFiles(nsIPrincipal& aSubjectPrincipal)
{
  return mItems->Files(&aSubjectPrincipal);
}

void
DataTransfer::GetTypes(nsTArray<nsString>& aTypes, CallerType aCallerType) const
{
  // When called from bindings, aTypes will be empty, but since we might have
  // Gecko-internal callers too, clear it to be safe.
  aTypes.Clear();

  const nsTArray<RefPtr<DataTransferItem>>* items = mItems->MozItemsAt(0);
  if (NS_WARN_IF(!items)) {
    return;
  }

  for (uint32_t i = 0; i < items->Length(); i++) {
    DataTransferItem* item = items->ElementAt(i);
    MOZ_ASSERT(item);

    if (item->ChromeOnly() && aCallerType != CallerType::System) {
      continue;
    }

    // NOTE: The reason why we get the internal type here is because we want
    // kFileMime to appear in the types list for backwards compatibility
    // reasons.
    nsAutoString type;
    item->GetInternalType(type);
    if (item->Kind() != DataTransferItem::KIND_FILE || type.EqualsASCII(kFileMime)) {
      // If the entry has kind KIND_STRING or KIND_OTHER we want to add it to the list.
      aTypes.AppendElement(type);
    }
  }

  for (uint32_t i = 0; i < mItems->Length(); ++i) {
    bool found = false;
    DataTransferItem* item = mItems->IndexedGetter(i, found);
    MOZ_ASSERT(found);
    if (item->Kind() != DataTransferItem::KIND_FILE) {
      continue;
    }
    aTypes.AppendElement(NS_LITERAL_STRING("Files"));
    break;
  }
}

void
DataTransfer::GetData(const nsAString& aFormat, nsAString& aData,
                      nsIPrincipal& aSubjectPrincipal,
                      ErrorResult& aRv)
{
  // return an empty string if data for the format was not found
  aData.Truncate();

  nsCOMPtr<nsIVariant> data;
  nsresult rv =
    GetDataAtInternal(aFormat, 0, &aSubjectPrincipal,
                      getter_AddRefs(data));
  if (NS_FAILED(rv)) {
    if (rv != NS_ERROR_DOM_INDEX_SIZE_ERR) {
      aRv.Throw(rv);
    }
    return;
  }

  if (data) {
    nsAutoString stringdata;
    data->GetAsAString(stringdata);

    // for the URL type, parse out the first URI from the list. The URIs are
    // separated by newlines
    nsAutoString lowercaseFormat;
    nsContentUtils::ASCIIToLower(aFormat, lowercaseFormat);

    if (lowercaseFormat.EqualsLiteral("url")) {
      int32_t lastidx = 0, idx;
      int32_t length = stringdata.Length();
      while (lastidx < length) {
        idx = stringdata.FindChar('\n', lastidx);
        // lines beginning with # are comments
        if (stringdata[lastidx] == '#') {
          if (idx == -1) {
            break;
          }
        }
        else {
          if (idx == -1) {
            aData.Assign(Substring(stringdata, lastidx));
          } else {
            aData.Assign(Substring(stringdata, lastidx, idx - lastidx));
          }
          aData = nsContentUtils::TrimWhitespace<nsCRT::IsAsciiSpace>(aData,
                                                                      true);
          return;
        }
        lastidx = idx + 1;
      }
    }
    else {
      aData = stringdata;
    }
  }
}

void
DataTransfer::SetData(const nsAString& aFormat, const nsAString& aData,
                      nsIPrincipal& aSubjectPrincipal,
                      ErrorResult& aRv)
{
  RefPtr<nsVariantCC> variant = new nsVariantCC();
  variant->SetAsAString(aData);

  aRv = SetDataAtInternal(aFormat, variant, 0, &aSubjectPrincipal);
}

void
DataTransfer::ClearData(const Optional<nsAString>& aFormat,
                        nsIPrincipal& aSubjectPrincipal,
                        ErrorResult& aRv)
{
  if (IsReadOnly()) {
    aRv.Throw(NS_ERROR_DOM_NO_MODIFICATION_ALLOWED_ERR);
    return;
  }

  if (MozItemCount() == 0) {
    return;
  }

  if (aFormat.WasPassed()) {
    MozClearDataAtHelper(aFormat.Value(), 0, aSubjectPrincipal, aRv);
  } else {
    MozClearDataAtHelper(EmptyString(), 0, aSubjectPrincipal, aRv);
  }
}

void
DataTransfer::SetMozCursor(const nsAString& aCursorState)
{
  // Lock the cursor to an arrow during the drag.
  mCursorState = aCursorState.EqualsLiteral("default");
}

already_AddRefed<nsINode>
DataTransfer::GetMozSourceNode()
{
  nsCOMPtr<nsIDragSession> dragSession = nsContentUtils::GetDragSession();
  if (!dragSession) {
    return nullptr;
  }

  nsCOMPtr<nsINode> sourceNode;
  dragSession->GetSourceNode(getter_AddRefs(sourceNode));
  if (sourceNode && !nsContentUtils::LegacyIsCallerNativeCode()
      && !nsContentUtils::CanCallerAccess(sourceNode)) {
    return nullptr;
  }

  return sourceNode.forget();
}

already_AddRefed<DOMStringList>
DataTransfer::MozTypesAt(uint32_t aIndex, CallerType aCallerType,
                         ErrorResult& aRv) const
{
  // Only the first item is valid for clipboard events
  if (aIndex > 0 &&
      (mEventMessage == eCut || mEventMessage == eCopy ||
       mEventMessage == ePaste)) {
    aRv.Throw(NS_ERROR_DOM_INDEX_SIZE_ERR);
    return nullptr;
  }

  RefPtr<DOMStringList> types = new DOMStringList();
  if (aIndex < MozItemCount()) {
    // note that you can retrieve the types regardless of their principal
    const nsTArray<RefPtr<DataTransferItem>>& items = *mItems->MozItemsAt(aIndex);

    bool addFile = false;
    for (uint32_t i = 0; i < items.Length(); i++) {
      if (items[i]->ChromeOnly() && aCallerType != CallerType::System) {
        continue;
      }

      // NOTE: The reason why we get the internal type here is because we want
      // kFileMime to appear in the types list for backwards compatibility
      // reasons.
      nsAutoString type;
      items[i]->GetInternalType(type);
      if (NS_WARN_IF(!types->Add(type))) {
        aRv.Throw(NS_ERROR_FAILURE);
        return nullptr;
      }

      if (items[i]->Kind() == DataTransferItem::KIND_FILE) {
        addFile = true;
      }
    }

    if (addFile) {
      types->Add(NS_LITERAL_STRING("Files"));
    }
  }

  return types.forget();
}

nsresult
DataTransfer::GetDataAtNoSecurityCheck(const nsAString& aFormat,
                                       uint32_t aIndex,
                                       nsIVariant** aData)
{
  return GetDataAtInternal(aFormat, aIndex,
                           nsContentUtils::GetSystemPrincipal(), aData);
}

nsresult
DataTransfer::GetDataAtInternal(const nsAString& aFormat, uint32_t aIndex,
                                nsIPrincipal* aSubjectPrincipal,
                                nsIVariant** aData)
{
  *aData = nullptr;

  if (aFormat.IsEmpty()) {
    return NS_OK;
  }

  if (aIndex >= MozItemCount()) {
    return NS_ERROR_DOM_INDEX_SIZE_ERR;
  }

  // Only the first item is valid for clipboard events
  if (aIndex > 0 &&
      (mEventMessage == eCut || mEventMessage == eCopy ||
       mEventMessage == ePaste)) {
    return NS_ERROR_DOM_INDEX_SIZE_ERR;
  }

  nsAutoString format;
  GetRealFormat(aFormat, format);

  MOZ_ASSERT(aSubjectPrincipal);

  RefPtr<DataTransferItem> item = mItems->MozItemByTypeAt(format, aIndex);
  if (!item) {
    // The index exists but there's no data for the specified format, in this
    // case we just return undefined
    return NS_OK;
  }

  // If we have chrome only content, and we aren't chrome, don't allow access
  if (!nsContentUtils::IsSystemPrincipal(aSubjectPrincipal) && item->ChromeOnly()) {
    return NS_OK;
  }

  // DataTransferItem::Data() handles the principal checks
  ErrorResult result;
  nsCOMPtr<nsIVariant> data = item->Data(aSubjectPrincipal, result);
  if (NS_WARN_IF(!data || result.Failed())) {
    return result.StealNSResult();
  }

  data.forget(aData);
  return NS_OK;
}

void
DataTransfer::MozGetDataAt(JSContext* aCx, const nsAString& aFormat,
                           uint32_t aIndex,
                           JS::MutableHandle<JS::Value> aRetval,
                           nsIPrincipal& aSubjectPrincipal,
                           mozilla::ErrorResult& aRv)
{
  nsCOMPtr<nsIVariant> data;
  aRv = GetDataAtInternal(aFormat, aIndex, &aSubjectPrincipal,
                          getter_AddRefs(data));
  if (aRv.Failed()) {
    return;
  }

  if (!data) {
    aRetval.setNull();
    return;
  }

  JS::Rooted<JS::Value> result(aCx);
  if (!VariantToJsval(aCx, data, aRetval)) {
    aRv = NS_ERROR_FAILURE;
    return;
  }
}

/* static */ bool
DataTransfer::PrincipalMaySetData(const nsAString& aType,
                                  nsIVariant* aData,
                                  nsIPrincipal* aPrincipal)
{
  if (!nsContentUtils::IsSystemPrincipal(aPrincipal)) {
    DataTransferItem::eKind kind = DataTransferItem::KindFromData(aData);
    if (kind == DataTransferItem::KIND_OTHER) {
      NS_WARNING("Disallowing adding non string/file types to DataTransfer");
      return false;
    }

    if (aType.EqualsASCII(kFileMime) ||
        aType.EqualsASCII(kFilePromiseMime)) {
      NS_WARNING("Disallowing adding x-moz-file or x-moz-file-promize types to DataTransfer");
      return false;
    }
  }
  return true;
}

void
DataTransfer::TypesListMayHaveChanged()
{
  DataTransferBinding::ClearCachedTypesValue(this);
}

already_AddRefed<DataTransfer>
DataTransfer::MozCloneForEvent(const nsAString& aEvent, ErrorResult& aRv)
{
  RefPtr<nsAtom> atomEvt = NS_Atomize(aEvent);
  if (!atomEvt) {
    aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
    return nullptr;
  }
  EventMessage eventMessage = nsContentUtils::GetEventMessage(atomEvt);

  RefPtr<DataTransfer> dt;
  nsresult rv = Clone(mParent, eventMessage, false, false, getter_AddRefs(dt));
  if (NS_FAILED(rv)) {
    aRv.Throw(rv);
    return nullptr;
  }
  return dt.forget();
}

nsresult
DataTransfer::SetDataAtInternal(const nsAString& aFormat, nsIVariant* aData,
                                uint32_t aIndex,
                                nsIPrincipal* aSubjectPrincipal)
{
  if (aFormat.IsEmpty()) {
    return NS_OK;
  }

  if (IsReadOnly()) {
    return NS_ERROR_DOM_NO_MODIFICATION_ALLOWED_ERR;
  }

  // Specifying an index less than the current length will replace an existing
  // item. Specifying an index equal to the current length will add a new item.
  if (aIndex > MozItemCount()) {
    return NS_ERROR_DOM_INDEX_SIZE_ERR;
  }

  // Only the first item is valid for clipboard events
  if (aIndex > 0 &&
      (mEventMessage == eCut || mEventMessage == eCopy ||
       mEventMessage == ePaste)) {
    return NS_ERROR_DOM_INDEX_SIZE_ERR;
  }

  // Don't allow the custom type to be assigned.
  if (aFormat.EqualsLiteral(kCustomTypesMime)) {
    return NS_ERROR_DOM_NOT_SUPPORTED_ERR;
  }

  if (!PrincipalMaySetData(aFormat, aData, aSubjectPrincipal)) {
    return NS_ERROR_DOM_SECURITY_ERR;
  }

  return SetDataWithPrincipal(aFormat, aData, aIndex, aSubjectPrincipal);
}

void
DataTransfer::MozSetDataAt(JSContext* aCx, const nsAString& aFormat,
                           JS::Handle<JS::Value> aData, uint32_t aIndex,
                           nsIPrincipal& aSubjectPrincipal,
                           ErrorResult& aRv)
{
  nsCOMPtr<nsIVariant> data;
  aRv = nsContentUtils::XPConnect()->JSValToVariant(aCx, aData,
                                                    getter_AddRefs(data));
  if (!aRv.Failed()) {
    aRv = SetDataAtInternal(aFormat, data, aIndex, &aSubjectPrincipal);
  }
}

void
DataTransfer::MozClearDataAt(const nsAString& aFormat, uint32_t aIndex,
                             nsIPrincipal& aSubjectPrincipal,
                             ErrorResult& aRv)
{
  if (IsReadOnly()) {
    aRv.Throw(NS_ERROR_DOM_NO_MODIFICATION_ALLOWED_ERR);
    return;
  }

  if (aIndex >= MozItemCount()) {
    aRv.Throw(NS_ERROR_DOM_INDEX_SIZE_ERR);
    return;
  }

  // Only the first item is valid for clipboard events
  if (aIndex > 0 &&
      (mEventMessage == eCut || mEventMessage == eCopy ||
       mEventMessage == ePaste)) {
    aRv.Throw(NS_ERROR_DOM_INDEX_SIZE_ERR);
    return;
  }

  MozClearDataAtHelper(aFormat, aIndex, aSubjectPrincipal, aRv);

  // If we just cleared the 0-th index, and there are still more than 1 indexes
  // remaining, MozClearDataAt should cause the 1st index to become the 0th
  // index. This should _only_ happen when the MozClearDataAt function is
  // explicitly called by script, as this behavior is inconsistent with spec.
  // (however, so is the MozClearDataAt API)

  if (aIndex == 0 && mItems->MozItemCount() > 1 &&
      mItems->MozItemsAt(0)->Length() == 0) {
    mItems->PopIndexZero();
  }
}

void
DataTransfer::MozClearDataAtHelper(const nsAString& aFormat, uint32_t aIndex,
                                   nsIPrincipal& aSubjectPrincipal,
                                   ErrorResult& aRv)
{
  MOZ_ASSERT(!IsReadOnly());
  MOZ_ASSERT(aIndex < MozItemCount());
  MOZ_ASSERT(aIndex == 0 ||
             (mEventMessage != eCut && mEventMessage != eCopy &&
              mEventMessage != ePaste));

  nsAutoString format;
  GetRealFormat(aFormat, format);

  mItems->MozRemoveByTypeAt(format, aIndex, aSubjectPrincipal, aRv);
}

void
DataTransfer::SetDragImage(Element& aImage, int32_t aX, int32_t aY)
{
  if (!IsReadOnly()) {
    mDragImage = &aImage;
    mDragImageX = aX;
    mDragImageY = aY;
  }
}

void
DataTransfer::UpdateDragImage(Element& aImage, int32_t aX, int32_t aY)
{
  if (mEventMessage < eDragDropEventFirst || mEventMessage > eDragDropEventLast) {
    return;
  }

  nsCOMPtr<nsIDragSession> dragSession = nsContentUtils::GetDragSession();
  if (dragSession) {
    dragSession->UpdateDragImage(&aImage, aX, aY);
  }
}

already_AddRefed<Promise>
DataTransfer::GetFilesAndDirectories(nsIPrincipal& aSubjectPrincipal,
                                     ErrorResult& aRv)
{
  nsCOMPtr<nsINode> parentNode = do_QueryInterface(mParent);
  if (!parentNode) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  nsCOMPtr<nsIGlobalObject> global = parentNode->OwnerDoc()->GetScopeObject();
  MOZ_ASSERT(global);
  if (!global) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  RefPtr<Promise> p = Promise::Create(global, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  RefPtr<FileList> files = mItems->Files(&aSubjectPrincipal);
  if (NS_WARN_IF(!files)) {
    return nullptr;
  }

  Sequence<RefPtr<File>> filesSeq;
  files->ToSequence(filesSeq, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  p->MaybeResolve(filesSeq);

  return p.forget();
}

already_AddRefed<Promise>
DataTransfer::GetFiles(bool aRecursiveFlag,
                       nsIPrincipal& aSubjectPrincipal,
                       ErrorResult& aRv)
{
  // Currently we don't support directories.
  return GetFilesAndDirectories(aSubjectPrincipal, aRv);
}

void
DataTransfer::AddElement(Element& aElement, ErrorResult& aRv)
{
  if (IsReadOnly()) {
    aRv.Throw(NS_ERROR_DOM_NO_MODIFICATION_ALLOWED_ERR);
    return;
  }

  mDragTarget = &aElement;
}

nsresult
DataTransfer::Clone(nsISupports* aParent, EventMessage aEventMessage,
                    bool aUserCancelled, bool aIsCrossDomainSubFrameDrop,
                    DataTransfer** aNewDataTransfer)
{
  RefPtr<DataTransfer> newDataTransfer =
    new DataTransfer(aParent, aEventMessage, mEffectAllowed, mCursorState,
                     mIsExternal, aUserCancelled, aIsCrossDomainSubFrameDrop,
                     mClipboardType, mItems, mDataSource, mDragImage,
                     mDragImageX, mDragImageY);

  newDataTransfer.forget(aNewDataTransfer);
  return NS_OK;
}

already_AddRefed<nsIArray>
DataTransfer::GetTransferables(nsINode* aDragTarget)
{
  MOZ_ASSERT(aDragTarget);

  nsIDocument* doc = aDragTarget->GetComposedDoc();
  if (!doc) {
    return nullptr;
  }

  return GetTransferables(doc->GetLoadContext());
}

already_AddRefed<nsIArray>
DataTransfer::GetTransferables(nsILoadContext* aLoadContext)
{
  nsCOMPtr<nsIMutableArray> transArray = nsArray::Create();
  if (!transArray) {
    return nullptr;
  }

  uint32_t count = MozItemCount();
  for (uint32_t i = 0; i < count; i++) {
    nsCOMPtr<nsITransferable> transferable = GetTransferable(i, aLoadContext);
    if (transferable) {
      transArray->AppendElement(transferable);
    }
  }

  return transArray.forget();
}

// What flavor should we store data with this type as in the nsITransferable?
// Returns either a null-terminated cstring with the type to use, or nullptr if
// the type is custom.
static const char*
ToTransFlavor(const nsAString& aFlavor)
{
  // BACKCOMPAT: Store "text/plain" as "text/unicode" for internal consumers.
  if (aFlavor.EqualsLiteral(kTextMime)) {
    return kUnicodeMime;
  }

  const char* knownFormats[] = {
    /* kTextMime, */ kHTMLMime, kNativeHTMLMime, kRTFMime,
    kURLMime, kURLDataMime, kURLDescriptionMime, kURLPrivateMime,
    kPNGImageMime, kJPEGImageMime, kGIFImageMime, kNativeImageMime,
    kFileMime, kFilePromiseMime, kFilePromiseURLMime,
    kFilePromiseDestFilename, kFilePromiseDirectoryMime,
    kMozTextInternal, kHTMLContext, kHTMLInfo, kImageRequestMime };

  for (uint32_t fi = 0; fi < ArrayLength(knownFormats); ++fi) {
    if (aFlavor.EqualsASCII(knownFormats[fi])) {
      return knownFormats[fi];
    }
  }
  return nullptr; // Custom type
}

// Either add a DataTransferItem directly to the nsITransferable, add it to the
// aCustomData URLParams object, or skip it.
//
// Returns true if data was added to aTrans.
bool
DataTransfer::AddToTrans(nsITransferable* aTrans, DataTransferItem* aItem,
                         URLParams& aCustomData)
{
  nsCOMPtr<nsISupports> transData;
  uint32_t transBytes;

  // Get the nsISupports data to put in the nsITransferable from the DataTransferItem.
  nsCOMPtr<nsIVariant> vdata = aItem->DataNoSecurityCheck();
  if (!vdata || !ConvertFromVariant(vdata, getter_AddRefs(transData),
                                    &transBytes) || !transData) {
    NS_WARNING("Extracting Transferable data from DataTransferItem failed");
    return false;
  }

  // Determine which 'flavor' to use to store the transferable.
  nsAutoString internalType;
  aItem->GetInternalType(internalType);
  const char* transFlavor = ToTransFlavor(internalType);

  // If we are looking at a custom type (one without a specific transfer
  // flavor), add it to aCustomData.
  //
  // NOTE: We currently only support encoding custom string data into the
  // transferable. Other data types will only be preserved during in-app drags
  // due to the DataTransfer being cached on the Drag Service.
  if (!transFlavor) {
    nsAutoString strData;
    nsCOMPtr<nsISupportsString> wrappedData = do_QueryInterface(transData);
    if (!wrappedData || NS_FAILED(wrappedData->GetData(strData))) {
      return false;
    }

    aCustomData.Append(internalType, strData);
    return false; // Data was not directly added to aTrans
  }

  // If a converter is set for a format, add the converter to the transferable.
  nsCOMPtr<nsIFormatConverter> converter = do_QueryInterface(transData);
  if (converter) {
    aTrans->AddDataFlavor(transFlavor);
    aTrans->SetConverter(converter);
    return false; // No data was added, only converters.
  }

  return NS_SUCCEEDED(aTrans->SetTransferData(transFlavor, transData, transBytes));
}

already_AddRefed<nsITransferable>
DataTransfer::GetTransferable(uint32_t aIndex, nsILoadContext* aLoadContext)
{
  if (aIndex >= MozItemCount()) {
    return nullptr;
  }

  RefPtr<nsTransferable> trans = new nsTransferable(aLoadContext);

  // Load the data into the nsITransferable, and extract a set of custom data.
  bool addedData = false;
  URLParams customData;
  for (auto& dti : *mItems->MozItemsAt(aIndex)) {
    addedData = AddToTrans(trans, dti, customData) || addedData;
  }

  // If we have any custom data, serialize & add it to our nsITransferable with
  // kCustomTypesMime.
  if (customData.Length() > 0) {
    nsAutoString custom;
    customData.Serialize(custom);

    // XXX(nika): There was some reason I needed to make this a cstring - not
    // sure what.
    nsCOMPtr<nsISupportsCString> customSupports =
      new nsSupportsCString(NS_ConvertUTF16toUTF8(custom));

    addedData = NS_SUCCEEDED(trans->SetTransferData(kCustomTypesMime, customSupports,
                                                    customSupports->Value().Length()))
      || addedData;
  }

  // If at least one of our attempts to add data to the transferable succeeded,
  // return it.
  if (addedData) {
    return trans.forget();
  }
  return nullptr;
}

bool
DataTransfer::ConvertFromVariant(nsIVariant* aVariant,
                                 nsISupports** aSupports,
                                 uint32_t* aLength) const
{
  *aSupports = nullptr;
  *aLength = 0;

  uint16_t type;
  aVariant->GetDataType(&type);
  if (type == nsIDataType::VTYPE_INTERFACE ||
      type == nsIDataType::VTYPE_INTERFACE_IS) {
    nsCOMPtr<nsISupports> data;
    if (NS_FAILED(aVariant->GetAsISupports(getter_AddRefs(data)))) {
      return false;
    }

    nsCOMPtr<nsIFlavorDataProvider> fdp = do_QueryInterface(data);
    if (fdp) {
      // for flavour data providers, use kFlavorHasDataProvider (which has the
      // value 0) as the length.
      fdp.forget(aSupports);
      *aLength = nsITransferable::kFlavorHasDataProvider;
    }
    else {
      // wrap the item in an nsISupportsInterfacePointer
      nsCOMPtr<nsISupportsInterfacePointer> ptrSupports =
        do_CreateInstance(NS_SUPPORTS_INTERFACE_POINTER_CONTRACTID);
      if (!ptrSupports) {
        return false;
      }

      ptrSupports->SetData(data);
      ptrSupports.forget(aSupports);

      *aLength = sizeof(nsISupportsInterfacePointer *);
    }

    return true;
  }

  char16_t* chrs;
  uint32_t len = 0;
  nsresult rv = aVariant->GetAsWStringWithSize(&len, &chrs);
  if (NS_FAILED(rv)) {
    return false;
  }

  nsAutoString str;
  str.Adopt(chrs, len);

  nsCOMPtr<nsISupportsString>
    strSupports(do_CreateInstance(NS_SUPPORTS_STRING_CONTRACTID));
  if (!strSupports) {
    return false;
  }

  strSupports->SetData(str);

  strSupports.forget(aSupports);

  // each character is two bytes
  *aLength = str.Length() << 1;

  return true;
}

void
DataTransfer::Disconnect()
{
  SetMode(Mode::Protected);
  if (PrefProtected()) {
    ClearAll();
  }
}

void
DataTransfer::ClearAll()
{
  mItems->ClearAllItems();
}

uint32_t
DataTransfer::MozItemCount() const
{
  return mItems->MozItemCount();
}

nsresult
DataTransfer::SetDataWithPrincipal(const nsAString& aFormat,
                                   nsIVariant* aData,
                                   uint32_t aIndex,
                                   nsIPrincipal* aPrincipal,
                                   bool aHidden)
{
  nsAutoString format;
  GetRealFormat(aFormat, format);

  ErrorResult rv;
  RefPtr<DataTransferItem> item =
    mItems->SetDataWithPrincipal(format, aData, aIndex, aPrincipal,
                                 /* aInsertOnly = */ false,
                                 aHidden,
                                 rv);
  return rv.StealNSResult();
}

void
DataTransfer::SetDataWithPrincipalFromOtherProcess(const nsAString& aFormat,
                                                   nsIVariant* aData,
                                                   uint32_t aIndex,
                                                   nsIPrincipal* aPrincipal,
                                                   bool aHidden)
{
  if (aFormat.EqualsLiteral(kCustomTypesMime)) {
    FillInExternalCustomTypes(aData, aIndex, aPrincipal);
  } else {
    nsAutoString format;
    GetRealFormat(aFormat, format);

    ErrorResult rv;
    RefPtr<DataTransferItem> item =
      mItems->SetDataWithPrincipal(format, aData, aIndex, aPrincipal,
                                   /* aInsertOnly = */ false, aHidden, rv);
    if (NS_WARN_IF(rv.Failed())) {
      rv.SuppressException();
    }
  }
}

void
DataTransfer::GetRealFormat(const nsAString& aInFormat,
                            nsAString& aOutFormat) const
{
  // treat text/unicode as equivalent to text/plain
  nsAutoString lowercaseFormat;
  nsContentUtils::ASCIIToLower(aInFormat, lowercaseFormat);
  if (lowercaseFormat.EqualsLiteral("text") ||
      lowercaseFormat.EqualsLiteral("text/unicode")) {
    aOutFormat.AssignLiteral("text/plain");
    return;
  }

  if (lowercaseFormat.EqualsLiteral("url")) {
    aOutFormat.AssignLiteral("text/uri-list");
    return;
  }

  aOutFormat.Assign(lowercaseFormat);
}

nsresult
DataTransfer::CacheExternalData(const char* aFormat, uint32_t aIndex,
                                nsIPrincipal* aPrincipal, bool aHidden)
{
  ErrorResult rv;
  RefPtr<DataTransferItem> item;

  // XXX(nika): Why do we do this check here and then re-do it in GetRealFormat
  // below? Can we get away with just using GetRealFormat? Also, this check is
  // case sensitive, unlike the one in GetRealFormat. Is that intentional?
  if (strcmp(aFormat, kUnicodeMime) == 0) {
    item = mItems->SetDataWithPrincipal(NS_LITERAL_STRING("text/plain"), nullptr,
                                        aIndex, aPrincipal, false, aHidden, rv);
    if (NS_WARN_IF(rv.Failed())) {
      return rv.StealNSResult();
    }
    return NS_OK;
  }

  // XXX(nika): Should we handle kURLDataMime in GetRealFormat?
  if (strcmp(aFormat, kURLDataMime) == 0) {
    item = mItems->SetDataWithPrincipal(NS_LITERAL_STRING("text/uri-list"), nullptr,
                                        aIndex, aPrincipal, false, aHidden, rv);
    if (NS_WARN_IF(rv.Failed())) {
      return rv.StealNSResult();
    }
    return NS_OK;
  }

  nsAutoString format;
  GetRealFormat(NS_ConvertUTF8toUTF16(aFormat), format);
  item = mItems->SetDataWithPrincipal(format, nullptr, aIndex,
                                      aPrincipal, false, aHidden, rv);
  if (NS_WARN_IF(rv.Failed())) {
    return rv.StealNSResult();
  }
  return NS_OK;
}

void
DataTransfer::CacheExternalDragFormats()
{
  // Called during the constructor to cache the formats available from an
  // external drag. The data associated with each format will be set to null.
  // This data will instead only be retrieved in FillInExternalDragData when
  // asked for, as it may be time consuming for the source application to
  // generate it.

  nsCOMPtr<nsIDragSession> dragSession = nsContentUtils::GetDragSession();
  if (!dragSession) {
    return;
  }

  // make sure that the system principal is used for external drags
  nsIScriptSecurityManager* ssm = nsContentUtils::GetSecurityManager();
  nsCOMPtr<nsIPrincipal> sysPrincipal;
  ssm->GetSystemPrincipal(getter_AddRefs(sysPrincipal));

  // there isn't a way to get a list of the formats that might be available on
  // all platforms, so just check for the types that can actually be imported
  // XXXndeakin there are some other formats but those are platform specific.
  // NOTE: kFileMime must have index 0
  const char* formats[] = { kFileMime, kHTMLMime, kURLMime, kURLDataMime,
                            kUnicodeMime, kPNGImageMime };

  uint32_t count;
  dragSession->GetNumDropItems(&count);
  for (uint32_t c = 0; c < count; c++) {
    bool hasFileData = false;
    dragSession->IsDataFlavorSupported(kFileMime, &hasFileData);

    // First, check for the special format that holds custom types.
    bool supported;
    dragSession->IsDataFlavorSupported(kCustomTypesMime, &supported);
    if (supported) {
      FillInExternalCustomTypes(c, sysPrincipal);
    }

    for (uint32_t f = 0; f < ArrayLength(formats); f++) {
      // IsDataFlavorSupported doesn't take an index as an argument and just
      // checks if any of the items support a particular flavor, even though
      // the GetData method does take an index. Here, we just assume that
      // every item being dragged has the same set of flavors.
      bool supported;
      dragSession->IsDataFlavorSupported(formats[f], &supported);
      // if the format is supported, add an item to the array with null as
      // the data. When retrieved, GetRealData will read the data.
      if (supported) {
        CacheExternalData(formats[f], c, sysPrincipal, /* hidden = */ f && hasFileData);
      }
    }
  }
}

// TODO: Every call to HasDataMatchingFlavors in this function performs sync IPC!
void
DataTransfer::CacheExternalClipboardFormats(bool aPlainTextOnly)
{
  NS_ASSERTION(mEventMessage == ePaste,
               "caching clipboard data for invalid event");

  // Called during the constructor for paste events to cache the formats
  // available on the clipboard. As with CacheExternalDragFormats, the
  // data will only be retrieved when needed.

  nsCOMPtr<nsIClipboard> clipboard =
    do_GetService("@mozilla.org/widget/clipboard;1");
  if (!clipboard || mClipboardType < 0) {
    return;
  }

  nsIScriptSecurityManager* ssm = nsContentUtils::GetSecurityManager();
  nsCOMPtr<nsIPrincipal> sysPrincipal;
  ssm->GetSystemPrincipal(getter_AddRefs(sysPrincipal));

  if (aPlainTextOnly) {
    bool supported;
    const char* unicodeMime[] = { kUnicodeMime };
    clipboard->HasDataMatchingFlavors(unicodeMime, 1, mClipboardType,
                                      &supported);
    if (supported) {
      CacheExternalData(kUnicodeMime, 0, sysPrincipal, false);
    }
    return;
  }

  // Check if the clipboard has any files
  bool hasFileData = false;
  const char *fileMime[] = { kFileMime };
  clipboard->HasDataMatchingFlavors(fileMime, 1, mClipboardType, &hasFileData);

  // We will be ignoring any application/x-moz-file files found in the paste
  // datatransfer within e10s, as they will fail to be sent over IPC. Because of
  // that, we will unset hasFileData, whether or not it would have been set.
  // (bug 1308007)
  if (XRE_IsContentProcess()) {
    hasFileData = false;
  }

  // there isn't a way to get a list of the formats that might be available on
  // all platforms, so just check for the types that can actually be imported.
  // NOTE: kCustomTypesMime must have index 0, kFileMime index 1
  const char* formats[] = { kCustomTypesMime, kFileMime, kHTMLMime, kRTFMime,
                            kURLMime, kURLDataMime, kUnicodeMime, kPNGImageMime };

  for (uint32_t f = 0; f < mozilla::ArrayLength(formats); ++f) {
    // check each format one at a time
    bool supported;
    clipboard->HasDataMatchingFlavors(&(formats[f]), 1, mClipboardType,
                                      &supported);
    // if the format is supported, add an item to the array with null as
    // the data. When retrieved, GetRealData will read the data.
    if (supported) {
      if (f == 0) {
        FillInExternalCustomTypes(0, sysPrincipal);
      } else {
        // In non-e10s we support pasting files from explorer.exe.
        // Unfortunately, we fail to send that data over IPC in e10s, so we
        // don't want to add the item to the DataTransfer and end up producing a
        // null `application/x-moz-file`. (bug 1308007)
        if (XRE_IsContentProcess() && f == 1) {
          continue;
        }

        // If we aren't the file data, and we have file data, we want to be hidden
        CacheExternalData(formats[f], 0, sysPrincipal, /* hidden = */ f != 1 && hasFileData);
      }
    }
  }
}

void
DataTransfer::FillAllExternalData()
{
  if (mIsExternal) {
    for (uint32_t i = 0; i < MozItemCount(); ++i) {
      const nsTArray<RefPtr<DataTransferItem>>& items = *mItems->MozItemsAt(i);
      for (uint32_t j = 0; j < items.Length(); ++j) {
        MOZ_ASSERT(items[j]->Index() == i);

        items[j]->FillInExternalData();
      }
    }
  }
}

void
DataTransfer::FillInExternalCustomTypes(uint32_t aIndex,
                                        nsIPrincipal* aPrincipal)
{
  // Force loading the custom type data from whatever data provider we have by
  // creating a DataTransferItem, and forcing the data to be filled-in. We never
  // actually add this item to our DataTransfer.
  //
  // XXX(nika): This seems super sketchy
  // XXX(nika): Should we assert we're external here?
  RefPtr<DataTransferItem> item = new DataTransferItem(this,
                                                       NS_LITERAL_STRING(kCustomTypesMime),
                                                       DataTransferItem::KIND_STRING);
  item->SetIndex(aIndex);

  nsCOMPtr<nsIVariant> variant = item->DataNoSecurityCheck();
  if (!variant) {
    return;
  }

  FillInExternalCustomTypes(variant, aIndex, aPrincipal);
}

void
DataTransfer::FillInExternalCustomTypes(nsIVariant* aData, uint32_t aIndex,
                                        nsIPrincipal* aPrincipal)
{
  nsAutoCString encoded;
  if (NS_FAILED(aData->GetAsACString(encoded))) {
    return;
  }

  struct CustomTypeAdder : public URLParams::ForEachIterator
  {
    CustomTypeAdder(uint32_t aIndex, nsIPrincipal* aPrincipal, DataTransfer* aDataTransfer)
      : mIndex(aIndex), mPrincipal(aPrincipal), mDataTransfer(aDataTransfer)
    {}

    bool URLParamsIterator(const nsAString& aFormat, const nsAString& aValue) override {
      RefPtr<nsVariantCC> variant = new nsVariantCC();
      MOZ_ALWAYS_SUCCEEDS(variant->SetAsAString(aValue));

      mDataTransfer->SetDataWithPrincipal(aFormat, variant, mIndex, mPrincipal);
      return true;
    }

    uint32_t mIndex;
    nsIPrincipal* mPrincipal;
    DataTransfer* mDataTransfer;
  };

  CustomTypeAdder iter(aIndex, aPrincipal, this);
  URLParams::Parse(encoded, iter);
}

void
DataTransfer::SetMode(DataTransfer::Mode aMode)
{
  if (!PrefProtected() && aMode == Mode::Protected) {
    mMode = Mode::ReadOnly;
  } else {
    mMode = aMode;
  }
}

void
DataTransfer::TransferableSource::CacheFlavors(DataTransfer* aDataTransfer,
                                               uint32_t aIndex)
{
  if (aIndex >= mTrans.Length()) {
    return;
  }

  // XXX(nika): This isn't exactly a fun API D: - perhaps we should change it?
  nsCOMPtr<nsIArray> flavors;
  uint32_t length = 0;
  mTrans[aIndex]->FlavorsTransferableCanExport(getter_AddRefs(flavors));
  if (flavors) {
    flavors->GetLength(&length);
  }

  // Add an empty entry for each type the transferable can export.
  for (uint32_t i = 0; i < length; ++i) {
    nsCOMPtr<nsISupportsCString> flavor = do_QueryElementAt(flavors, i);
    if (!flavor) {
      continue;
    }
    aDataTransfer->CacheExternalData(flavor->Value().get(), aIndex, mPrincipal,
                                     /* aHidden = */ mHideNonFiles &&
                                     !flavor->Value().EqualsLiteral(kFileMime));
  }
}

already_AddRefed<nsISupports>
DataTransfer::TransferableSource::GetData(const char* aFormat, uint32_t aIndex)
{
  nsresult rv = NS_ERROR_FAILURE;
  nsCOMPtr<nsISupports> data;
  if (aIndex < mTrans.Length()) {
    uint32_t dummy = 0;
    rv = mTrans[aIndex]->GetTransferData(aFormat, getter_AddRefs(data), &dummy);
  }
  return NS_SUCCEEDED(rv) ? data.forget() : nullptr;
}

void
DataTransfer::TransferableSource::Add(nsITransferable* aTrans)
{
  mTrans.AppendElement(aTrans);

  if (!mHideNonFiles) { // Hide files if we got one.
    nsCOMPtr<nsISupports> file = GetData(kFileMime, mTrans.Length() - 1);
    mHideNonFiles = file;
  }
}

} // namespace dom
} // namespace mozilla
