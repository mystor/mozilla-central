/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"
#include "nsISupports.h"
#include "nsIURI.h"
#include "nsCOMPtr.h"
#include "nsNetUtil.h"

extern "C" uint8_t xprs_test(nsISupports* p);

TEST(rust, DoesThis)
{
  nsCOMPtr<nsIURI> uri;
  NS_NewURI(getter_AddRefs(uri), "https://example.com/foo/bar");
  ASSERT_EQ(1, 1);
  uint8_t v = xprs_test(uri.get());
  ASSERT_EQ(v, 1);
  // ASSERT_EQ(xprs_test(uri), 1);
}
