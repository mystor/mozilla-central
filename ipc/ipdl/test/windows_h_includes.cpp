// This file acts as a cross-platform test to ensure that the files imported by
// default in IPDL generated headers don't include <windows.h>.

// This define attempts to catch places where we include windows.h on all
// platforms. Various IPC related files which include windows.h conditionally
// will check TEST_NO_WINDOWS_H and error the build if it is set.
#define TEST_NO_WINDOWS_H 1

// These headers are included by default in generated IPDL headers.
#include "mozilla/Attributes.h"
#include "IPCMessageStart.h"
#include "ipc/IPCMessageUtils.h"
#include "mozilla/RefPtr.h"
#include "nsString.h"
#include "nsTArray.h"
#include "mozilla/ipc/ProtocolUtils.h"
#include "nsTHashtable.h"
#include "mozilla/OperatorNewExtensions.h"
#include "mozilla/dom/ClientIPCTypes.h"
#include "prenv.h"
#include "base/id_map.h"
#include "mozilla/ipc/MessageChannel.h"

#ifdef _WINDOWS_
#error "Never include windows.h in this file!"
#endif
