use std::ffi::CStr;
use {
    RefPtr,
    XpCom,
    nsIComponentManager,
    nsIServiceManager,
    nsIComponentRegistrar,
    nsCID,
};

macro_rules! try_opt {
    ($e: expr) => {
        match $e {
            Some(x) => x,
            None => return None,
        }
    }
}

#[inline]
pub fn component_manager() -> Option<RefPtr<nsIComponentManager>> {
    unsafe {
        RefPtr::from_raw(Gecko_GetComponentManager())
    }
}

#[inline]
pub fn service_manager() -> Option<RefPtr<nsIServiceManager>> {
    unsafe {
        RefPtr::from_raw(Gecko_GetServiceManager())
    }
}

#[inline]
pub fn component_registrar() -> Option<RefPtr<nsIComponentRegistrar>> {
    unsafe {
        RefPtr::from_raw(Gecko_GetComponentRegistrar())
    }
}

#[inline]
pub fn create_instance<T: XpCom>(cid: &nsCID) -> Option<RefPtr<T>> {
    unsafe {
        try_opt!(component_manager()).createInstance(cid, None).unwrap_or(None)
    }
}

#[inline]
pub fn create_instance_by_contract_id<T: XpCom>(id: &CStr) -> Option<RefPtr<T>> {
    unsafe {
        try_opt!(component_manager()).createInstanceByContractID(id.as_ptr(), None).unwrap_or(None)
    }
}

#[inline]
pub fn get_service<T: XpCom>(cid: &nsCID) -> Option<RefPtr<T>> {
    unsafe {
        try_opt!(service_manager()).getService(cid).unwrap_or(None)
    }
}

#[inline]
pub fn get_service_by_contract_id<T: XpCom>(id: &CStr) -> Option<RefPtr<T>> {
    unsafe {
        try_opt!(service_manager()).getServiceByContractID(id.as_ptr()).unwrap_or(None)
    }
}

extern "C" {
    fn Gecko_GetComponentManager() -> *const nsIComponentManager;
    fn Gecko_GetServiceManager() -> *const nsIServiceManager;
    fn Gecko_GetComponentRegistrar() -> *const nsIComponentRegistrar;
}
