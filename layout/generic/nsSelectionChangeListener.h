/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsSelectionChangeListener_h_
#define nsSelectionChangeListener_h_

#include "nsISelectionListener.h"
#include "nsISelectionPrivate.h"
#include "mozilla/Attributes.h"

class nsSelectionChangeListener final : public nsISelectionListener
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSISELECTIONLISTENER

    void Listen(nsISelectionPrivate *aSelection)
  {
    NS_ASSERTION(aSelection, "Null selection passed to Listen()");
    aSelection->AddSelectionListener(this);
  }

  static nsSelectionChangeListener* GetInstance()
  {
    if (!sInstance) {
      sInstance = new nsSelectionChangeListener();

      NS_ADDREF(sInstance);
    }

    return sInstance;
  }

  static void Shutdown()
  {
    NS_IF_RELEASE(sInstance);
  }

private:
  ~nsSelectionChangeListener() {}

  static nsSelectionChangeListener* sInstance;
};

#endif
