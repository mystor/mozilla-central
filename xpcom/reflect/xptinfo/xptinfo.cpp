/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "xptinfo.h"
#include "nsISupports.h"
#include "mozilla/dom/DOMJSClass.h"
#include "mozilla/ArrayUtils.h"

using namespace mozilla;
using namespace mozilla::dom;
using namespace xpt::detail;

///////////////////////////////////////
// C++ Perfect Hash Helper Functions //
///////////////////////////////////////

// WARNING: This must match phf.py's implementation of the hash functions etc.
static const uint32_t FNV_OFFSET_BASIS = 0x811C9DC5;
static const uint32_t FNV_PRIME = 16777619;
static const uint32_t U32_HIGH_BIT = 0x80000000;

static uint32_t
Phf_DoHash(const void* bytes, uint32_t len, uint32_t h=FNV_OFFSET_BASIS)
{
  for (uint32_t i = 0; i < len; ++i) {
    h ^= reinterpret_cast<const uint8_t*>(bytes)[i];
    h *= FNV_PRIME;
  }
  return h;
}

static uint16_t
Phf_DoLookup(const void* aBytes, uint32_t aLen, const uint32_t* aIntr)
{
  uint32_t mid = aIntr[Phf_DoHash(aBytes, aLen) % kPHFSize];
  if (mid & U32_HIGH_BIT) {
    return mid & ~U32_HIGH_BIT;
  }
  return Phf_DoHash(aBytes, aLen, mid) % sInterfacesSize;
}
static_assert(kPHFSize == 256, "wrong phf size?");


////////////////////////////////////////
// PHF-based interface lookup methods //
////////////////////////////////////////

/* static */ const nsXPTInterfaceInfo*
nsXPTInterfaceInfo::ByIID(const nsIID& aIID)
{
  uint16_t idx = Phf_DoLookup(&aIID, sizeof(nsIID), sPHF_IIDs);

  const nsXPTInterfaceInfo* found = &sInterfaces[idx];
  return found->IID() == aIID ? found : nullptr;
}
static_assert(sizeof(nsIID) == 16, "IIDs have the wrong size?");

/* static */ const nsXPTInterfaceInfo*
nsXPTInterfaceInfo::ByName(const char* aName)
{
  uint16_t idx = Phf_DoLookup(aName, strlen(aName), sPHF_Names);
  idx = sPHF_NamesIdxs[idx];

  const nsXPTInterfaceInfo* found = &sInterfaces[idx];
  return strcmp(found->Name(), aName) ? nullptr : found;
}


////////////////////////////////////
// Constant Lookup Helper Methods //
////////////////////////////////////

// XXX: Remove when shims are gone.
// Looks for the ConstantSpec at aIndex, and puts that pointer into aSpec.
// Returns either the index of the found constant, or the number of constants if
// it was not found.
static uint16_t
GetWebIDLConst(uint16_t aHookIdx, uint16_t aIndex, const ConstantSpec** aSpec)
{
  const NativePropertyHooks* propHooks = sPropHooks[aHookIdx];

  uint16_t idx = 0;
  do {
    const NativeProperties* props[] = {
      propHooks->mNativeProperties.regular,
      propHooks->mNativeProperties.chromeOnly
    };
    for (size_t i = 0; i < ArrayLength(props); ++i) {
      auto prop = props[i];
      if (prop && prop->HasConstants()) {
        for (auto cs = prop->Constants()->specs; cs->name; ++cs) {
          // We have found one constant here.  We explicitly do not bother
          // calling isEnabled() here because it's OK to define potentially
          // extra constants on these shim interfaces.
          if (aSpec && idx == aIndex) {
            *aSpec = cs;
            return idx;
          }
          ++idx;
        }
      }
    }
  } while ((propHooks = propHooks->mProtoHooks));

  return idx;
}

uint16_t
nsXPTInterfaceInfo::ConstantCount() const
{
  if (!mIsShim) {
    return mNumConsts;
  }

  // Get the number of WebIDL constants.
  return GetWebIDLConst(mConsts, UINT16_MAX, nullptr);
}

const char*
nsXPTInterfaceInfo::Constant(uint16_t aIndex, JS::MutableHandleValue aValue) const
{
  if (!mIsShim) {
    MOZ_ASSERT(aIndex < mNumConsts);

    if (const nsXPTInterfaceInfo* pi = GetParent()) {
      MOZ_ASSERT(!pi->mIsShim);
      if (aIndex < pi->mNumConsts) {
        return pi->Constant(aIndex, aValue);
      }
      aIndex -= pi->mNumConsts;
    }

    // Extract the value and name from the Constant Info.
    const ConstInfo& info = sConsts[mConsts + aIndex];
    if (info.mSigned || info.mValue <= (uint32_t)INT32_MAX) {
      aValue.set(JS::Int32Value((int32_t)info.mValue));
    } else {
      aValue.set(JS::DoubleValue(info.mValue));
    }
    return GetString(info.mName);
  }

  // Get a single WebIDL constant.
  const ConstantSpec* spec;
  GetWebIDLConst(mConsts, aIndex, &spec);
  aValue.set(spec->value);
  return spec->name;
}

const nsXPTMethodInfo*
nsXPTInterfaceInfo::MethodByName(const char *aMethodName,
                                 uint16_t* aIndex) const
{
  const nsXPTInterfaceInfo* pi = GetParent();
  if (aIndex) {
    *aIndex = 0;
  }

  // Check if we can find the method in this interface.
  uint16_t localCount = MethodCount() - (pi ? pi->MethodCount() : 0);
  for (uint16_t idx = 0; idx < localCount; ++idx) {
    const nsXPTMethodInfo& method = xpt::detail::GetMethod(mMethods + idx);

    if (!strcmp(aMethodName, method.Name())) {
      if (aIndex) {
        *aIndex = idx;
      }
      return &method;
    }
  }

  // Check if our parent interface has this method.
  return pi ? pi->MethodByName(aMethodName, aIndex) : nullptr;
}


////////////////////////////////////////////////
// nsIInterfaceInfo backcompat implementation //
////////////////////////////////////////////////

nsresult
nsXPTInterfaceInfo::GetName(char** aName) const
{
  *aName = moz_xstrdup(Name());
  return NS_OK;
}

nsresult
nsXPTInterfaceInfo::GetInterfaceIID(nsIID** aIID) const
{
  *aIID = mIID.Clone();
  return NS_OK;
}

nsresult
nsXPTInterfaceInfo::IsScriptable(bool* aRes) const
{
  *aRes = IsScriptable();
  return NS_OK;
}

nsresult
nsXPTInterfaceInfo::IsBuiltinClass(bool* aRes) const
{
  *aRes = IsBuiltinClass();
  return NS_OK;
}

nsresult
nsXPTInterfaceInfo::GetParent(const nsXPTInterfaceInfo** aParent) const
{
  *aParent = GetParent();
  return NS_OK;
}

nsresult
nsXPTInterfaceInfo::GetMethodCount(uint16_t* aMethodCount) const
{
  *aMethodCount = MethodCount();
  return NS_OK;
}

nsresult
nsXPTInterfaceInfo::GetConstantCount(uint16_t* aConstantCount) const
{
  *aConstantCount = ConstantCount();
  return NS_OK;
}

nsresult
nsXPTInterfaceInfo::GetMethodInfo(uint16_t aIndex, const nsXPTMethodInfo** aInfo) const
{
  *aInfo = aIndex < MethodCount() ? &Method(aIndex) : nullptr;
  return *aInfo ? NS_OK : NS_ERROR_FAILURE;
}

nsresult
nsXPTInterfaceInfo::GetMethodInfoForName(const char* aMethodName, uint16_t* aIndex,
                                         const nsXPTMethodInfo** aInfo) const
{
  *aInfo = MethodByName(aMethodName, aIndex);
  return *aInfo ? NS_OK : NS_ERROR_FAILURE;
}

nsresult
nsXPTInterfaceInfo::GetConstant(uint16_t aIndex,
                                JS::MutableHandleValue aConstant,
                                char** aName) const
{
  *aName = aIndex < ConstantCount()
    ? moz_xstrdup(Constant(aIndex, aConstant))
    : nullptr;
  return *aName ? NS_OK : NS_ERROR_FAILURE;
}

nsresult
nsXPTInterfaceInfo::GetInfoForParam(uint16_t aMethodIndex,
                                    const nsXPTParamInfo* aParam,
                                    const nsXPTInterfaceInfo** aRetval) const
{
  const nsXPTType* type = &aParam->Type();
  while (type->Tag() == TD_ARRAY) {
    type = &type->ArrayElementType();
  }

  *aRetval = type->Tag() == TD_INTERFACE_TYPE ? type->GetInterface() : nullptr;
  return *aRetval ? NS_OK : NS_ERROR_FAILURE;
}

nsresult
nsXPTInterfaceInfo::GetIIDForParam(uint16_t aMethodIndex,
                                   const nsXPTParamInfo* aParam,
                                   nsIID** aRetval) const
{
  const nsXPTInterfaceInfo* info;
  GetInfoForParam(aMethodIndex, aParam, &info);
  *aRetval = nullptr;
  if (info) {
    *aRetval = info->IID().Clone();
    return NS_OK;
  }
  return NS_ERROR_FAILURE;
}

nsresult
nsXPTInterfaceInfo::GetTypeForParam(uint16_t aMethodIndex,
                                    const nsXPTParamInfo* aParam,
                                    uint16_t aDimension,
                                    nsXPTType* aRetval) const
{
  const nsXPTType* type = &aParam->Type();
  for (uint16_t i = 0; i < aDimension; ++i) {
    if (type->Tag() != TD_ARRAY) {
      NS_ERROR("bad dimension");
      return NS_ERROR_INVALID_ARG;
    }
    type = &type->ArrayElementType();
  }

  *aRetval = *type; // NOTE: This copies the type, which is fine I guess?
  return NS_OK;
}

nsresult
nsXPTInterfaceInfo::GetSizeIsArgNumberForParam(uint16_t aMethodIndex,
                                               const nsXPTParamInfo* aParam,
                                               uint16_t aDimension,
                                               uint8_t* aRetval) const
{
  const nsXPTType* type = &aParam->Type();
  for (uint16_t i = 0; i < aDimension; ++i) {
    if (type->Tag() != TD_ARRAY) {
      NS_ERROR("bad dimension");
      return NS_ERROR_INVALID_ARG;
    }
    type = &type->ArrayElementType();
  }

  if (type->Tag() != TD_ARRAY &&
      type->Tag() != TD_PSTRING_SIZE_IS &&
      type->Tag() != TD_PWSTRING_SIZE_IS) {
    NS_ERROR("not a size_is");
    return NS_ERROR_INVALID_ARG;
  }

  *aRetval = type->ArgNum();
  return NS_OK;
}

nsresult
nsXPTInterfaceInfo::GetInterfaceIsArgNumberForParam(uint16_t aMethodIndex,
                                                    const nsXPTParamInfo* aParam,
                                                    uint8_t* aRetval) const
{
  const nsXPTType* type = &aParam->Type();
  while (type->Tag() == TD_ARRAY) {
    type = &type->ArrayElementType();
  }

  if (type->Tag() != TD_INTERFACE_IS_TYPE) {
    NS_ERROR("not an iid_is");
    return NS_ERROR_INVALID_ARG;
  }

  *aRetval = type->ArgNum();
  return NS_OK;
}

nsresult
nsXPTInterfaceInfo::IsIID(const nsIID* aIID, bool* aIs) const
{
  *aIs = mIID == *aIID;
  return NS_OK;
}

nsresult
nsXPTInterfaceInfo::GetNameShared(const char** aName) const
{
  *aName = Name();
  return NS_OK;
}

nsresult
nsXPTInterfaceInfo::GetIIDShared(const nsIID** aIID) const
{
  *aIID = &IID();
  return NS_OK;
}

nsresult
nsXPTInterfaceInfo::IsFunction(bool* aRetval) const
{
  *aRetval = IsFunction();
  return NS_OK;
}

nsresult
nsXPTInterfaceInfo::HasAncestor(const nsIID* aIID, bool* aRetval) const
{
  *aRetval = HasAncestor(*aIID);
  return NS_OK;
}

nsresult
nsXPTInterfaceInfo::GetIIDForParamNoAlloc(uint16_t aMethodIndex,
                                          const nsXPTParamInfo* aParam,
                                          nsIID* aIID) const
{
  const nsXPTInterfaceInfo* info;
  nsresult rv = GetInfoForParam(aMethodIndex, aParam, &info);
  if (NS_FAILED(rv)) {
    return rv;
  }
  *aIID = info->IID();
  return NS_OK;
}

nsresult
nsXPTInterfaceInfo::IsMainProcessScriptableOnly(bool* aRetval) const
{
  *aRetval = IsMainProcessScriptableOnly();
  return NS_OK;
}
