use std::ffi::CStr;
use std::ptr;
use nserror::NsresultExt;
use {
    RefPtr,
    GetterAddrefs,
    XpCom,
    nsCID,
};

use interfaces::{
    nsIComponentManager,
    nsIServiceManager,
    nsIComponentRegistrar,
};

macro_rules! try_opt {
    ($e: expr) => {
        match $e {
            Some(x) => x,
            None => return None,
        }
    }
}

/// Get a reference to the global `nsIComponentManager`.
///
/// Can return `None` during shutdown.
#[inline]
pub fn component_manager() -> Option<RefPtr<nsIComponentManager>> {
    unsafe {
        RefPtr::from_raw(Gecko_GetComponentManager())
    }
}

/// Get a reference to the global `nsIServiceManager`.
///
/// Can return `None` during shutdown.
#[inline]
pub fn service_manager() -> Option<RefPtr<nsIServiceManager>> {
    unsafe {
        RefPtr::from_raw(Gecko_GetServiceManager())
    }
}

/// Get a reference to the global `nsIComponentRegistrar`
///
/// Can return `None` during shutdown.
#[inline]
pub fn component_registrar() -> Option<RefPtr<nsIComponentRegistrar>> {
    unsafe {
        RefPtr::from_raw(Gecko_GetComponentRegistrar())
    }
}

/// Helper for calling `nsIComponentManager::CreateInstance` on the global
/// `nsIComponentRegistrar`.
#[inline]
pub fn create_instance<T: XpCom>(cid: &nsCID) -> Option<RefPtr<T>> {
    unsafe {
        let mut ga = GetterAddrefs::<T>::new();
        if try_opt!(component_manager()).CreateInstance(
            cid,
            ptr::null(),
            &T::IID,
            ga.void_ptr(),
        ).succeeded() {
            ga.refptr()
        } else {
            None
        }
    }
}

/// Helper for calling `nsIComponentManager::CreateInstanceByContractID` on the
/// global `nsIComponentRegistrar`.
#[inline]
pub fn create_instance_by_contract_id<T: XpCom>(id: &CStr) -> Option<RefPtr<T>> {
    unsafe {
        let mut ga = GetterAddrefs::<T>::new();
        if try_opt!(component_manager()).CreateInstanceByContractID(
            id.as_ptr(),
            ptr::null(),
            &T::IID,
            ga.void_ptr(),
        ).succeeded() {
            ga.refptr()
        } else {
            None
        }
    }
}

/// Helper for calling `nsIServiceManager::GetService` on the global
/// `nsIServiceManager`.
#[inline]
pub fn get_service<T: XpCom>(cid: &nsCID) -> Option<RefPtr<T>> {
    unsafe {
        let mut ga = GetterAddrefs::<T>::new();
        if try_opt!(service_manager()).GetService(
            cid,
            &T::IID,
            ga.void_ptr(),
        ).succeeded() {
            ga.refptr()
        } else {
            None
        }
    }
}

/// Helper for calling `nsIServiceManager::GetServiceByContractID` on the global
/// `nsIServiceManager`.
#[inline]
pub fn get_service_by_contract_id<T: XpCom>(id: &CStr) -> Option<RefPtr<T>> {
    unsafe {
        let mut ga = GetterAddrefs::<T>::new();
        if try_opt!(service_manager()).GetServiceByContractID(
            id.as_ptr(),
            &T::IID,
            ga.void_ptr()
        ).succeeded() {
            ga.refptr()
        } else {
            None
        }
    }
}

extern "C" {
    fn Gecko_GetComponentManager() -> *const nsIComponentManager;
    fn Gecko_GetServiceManager() -> *const nsIServiceManager;
    fn Gecko_GetComponentRegistrar() -> *const nsIComponentRegistrar;
}
