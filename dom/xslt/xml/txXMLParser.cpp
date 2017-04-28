/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "txXMLParser.h"
#include "txURIUtils.h"
#include "txXPathTreeWalker.h"

#include "nsIDocument.h"
#include "nsIDOMDocument.h"
#include "nsSyncLoadService.h"
#include "nsNetUtil.h"
#include "nsIURI.h"
#include "nsIPrincipal.h"

nsresult
txParseDocumentFromURI(const nsAString& aHref,
                       const txXPathNode& aLoader,
                       nsAString& aErrMsg,
                       txXPathNode** aResult)
{
    NS_ENSURE_ARG_POINTER(aResult);
    *aResult = nullptr;
    nsCOMPtr<nsIURI> documentURI;
    nsresult rv = NS_NewURI(getter_AddRefs(documentURI), aHref);
    NS_ENSURE_SUCCESS(rv, rv);

    nsIDocument* loaderDocument = txXPathNativeNode::getDocument(aLoader);

    nsCOMPtr<nsILoadGroup> loadGroup = loaderDocument->GetDocumentLoadGroup();

    // For the system principal loaderUri will be null here, which is good
    // since that means that chrome documents can load any uri.
    RefPtr<nsIDOMDocument> theDocument;
    nsAutoSyncOperation sync(loaderDocument);
    rv = nsSyncLoadService::LoadDocument(documentURI,
                                         nsIContentPolicy::TYPE_INTERNAL_XMLHTTPREQUEST,
                                         loaderDocument->NodePrincipal(),
                                         nsILoadInfo::SEC_REQUIRE_CORS_DATA_INHERITS,
                                         loadGroup, true,
                                         loaderDocument->GetReferrerPolicy(),
                                         getter_AddRefs(theDocument));

    if (NS_FAILED(rv)) {
        aErrMsg.AppendLiteral("Document load of ");
        aErrMsg.Append(aHref);
        aErrMsg.AppendLiteral(" failed.");
        return NS_FAILED(rv) ? rv : NS_ERROR_FAILURE;
    }

    *aResult = txXPathNativeNode::createXPathNode(theDocument);
    if (!*aResult) {
        return NS_ERROR_FAILURE;
    }

    // NOTE: We leak a reference here. the node we created above will have its
    // mNode field NS_RELEASE()-ed by txXPathNodeUtils::release called from
    // txLoadedDocumentEntry (the mDocument field of that type is what aResult
    // points to here) when it goes away, so we need to do the addref here for
    // it.
    mozilla::Unused << do_AddRef(theDocument);

    return NS_OK;
}
