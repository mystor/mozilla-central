/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsSupportsPrimitives_h__
#define nsSupportsPrimitives_h__

#include "mozilla/Attributes.h"

#include "nsISupportsPrimitives.h"
#include "nsCOMPtr.h"
#include "nsString.h"

class nsSupportsID final : public nsISupportsID
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSISUPPORTSPRIMITIVE
  NS_DECL_NSISUPPORTSID

  nsSupportsID();

private:
  friend class nsISupportsID;
  ~nsSupportsID() {}

  nsID mData;
};

/***************************************************************************/

class nsSupportsCString final : public nsISupportsCString
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSISUPPORTSPRIMITIVE
  NS_DECL_NSISUPPORTSCSTRING

  nsSupportsCString() {}

private:
  friend class nsISupportsCString;
  ~nsSupportsCString() {}

  nsCString mData;
};

/***************************************************************************/

class nsSupportsString final : public nsISupportsString
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSISUPPORTSPRIMITIVE
  NS_DECL_NSISUPPORTSSTRING

  nsSupportsString() {}

private:
  friend class nsISupportsString;
  ~nsSupportsString() {}

  nsString mData;
};

/***************************************************************************/

class nsSupportsPRBool final : public nsISupportsPRBool
{
public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSISUPPORTSPRIMITIVE
  NS_DECL_NSISUPPORTSPRBOOL

  nsSupportsPRBool();

private:
  friend class nsISupportsPRBool;
  ~nsSupportsPRBool() {}

  bool mData;
};

/***************************************************************************/

class nsSupportsPRUint8 final : public nsISupportsPRUint8
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSISUPPORTSPRIMITIVE
  NS_DECL_NSISUPPORTSPRUINT8

  nsSupportsPRUint8();

private:
  friend class nsISupportsPRUint8;
  ~nsSupportsPRUint8() {}

  uint8_t mData;
};

/***************************************************************************/

class nsSupportsPRUint16 final : public nsISupportsPRUint16
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSISUPPORTSPRIMITIVE
  NS_DECL_NSISUPPORTSPRUINT16

  nsSupportsPRUint16();

private:
  friend class nsISupportsPRUint16;
  ~nsSupportsPRUint16() {}

  uint16_t mData;
};

/***************************************************************************/

class nsSupportsPRUint32 final : public nsISupportsPRUint32
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSISUPPORTSPRIMITIVE
  NS_DECL_NSISUPPORTSPRUINT32

  nsSupportsPRUint32();

private:
  friend class nsISupportsPRUint32;
  ~nsSupportsPRUint32() {}

  uint32_t mData;
};

/***************************************************************************/

class nsSupportsPRUint64 final : public nsISupportsPRUint64
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSISUPPORTSPRIMITIVE
  NS_DECL_NSISUPPORTSPRUINT64

  nsSupportsPRUint64();

private:
  friend class nsISupportsPRUint64;
  ~nsSupportsPRUint64() {}

  uint64_t mData;
};

/***************************************************************************/

class nsSupportsPRTime final : public nsISupportsPRTime
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSISUPPORTSPRIMITIVE
  NS_DECL_NSISUPPORTSPRTIME

  nsSupportsPRTime();

private:
  friend class nsISupportsPRTime;
  ~nsSupportsPRTime() {}

  PRTime mData;
};

/***************************************************************************/

class nsSupportsChar final : public nsISupportsChar
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSISUPPORTSPRIMITIVE
  NS_DECL_NSISUPPORTSCHAR

  nsSupportsChar();

private:
  friend class nsISupportsChar;
  ~nsSupportsChar() {}

  char mData;
};

/***************************************************************************/

class nsSupportsPRInt16 final : public nsISupportsPRInt16
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSISUPPORTSPRIMITIVE
  NS_DECL_NSISUPPORTSPRINT16

  nsSupportsPRInt16();

private:
  friend class nsISupportsPRInt16;
  ~nsSupportsPRInt16() {}

  int16_t mData;
};

/***************************************************************************/

class nsSupportsPRInt32 final : public nsISupportsPRInt32
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSISUPPORTSPRIMITIVE
  NS_DECL_NSISUPPORTSPRINT32

  nsSupportsPRInt32();

private:
  friend class nsISupportsPRInt32;
  ~nsSupportsPRInt32() {}

  int32_t mData;
};

/***************************************************************************/

class nsSupportsPRInt64 final : public nsISupportsPRInt64
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSISUPPORTSPRIMITIVE
  NS_DECL_NSISUPPORTSPRINT64

  nsSupportsPRInt64();

private:
  friend class nsISupportsPRInt64;
  ~nsSupportsPRInt64() {}

  int64_t mData;
};

/***************************************************************************/

class nsSupportsFloat final : public nsISupportsFloat
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSISUPPORTSPRIMITIVE
  NS_DECL_NSISUPPORTSFLOAT

  nsSupportsFloat();

private:
  friend class nsISupportsFloat;
  ~nsSupportsFloat() {}

  float mData;
};

/***************************************************************************/

class nsSupportsDouble final : public nsISupportsDouble
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSISUPPORTSPRIMITIVE
  NS_DECL_NSISUPPORTSDOUBLE

  nsSupportsDouble();

private:
  friend class nsISupportsDouble;
  ~nsSupportsDouble() {}

  double mData;
};

/***************************************************************************/

class nsSupportsInterfacePointer final : public nsISupportsInterfacePointer
{
public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSISUPPORTSPRIMITIVE
  NS_DECL_NSISUPPORTSINTERFACEPOINTER

  nsSupportsInterfacePointer();

  template<typename T>
  explicit nsSupportsInterfacePointer(T* aSupports)
    : mData(aSupports), mIID(NS_GET_TEMPLATE_IID(T))
  { }

private:
  friend class nsISupportsInterfacePointer;
  ~nsSupportsInterfacePointer() {}

  nsCOMPtr<nsISupports> mData;
  nsID mIID;
};

#endif /* nsSupportsPrimitives_h__ */
