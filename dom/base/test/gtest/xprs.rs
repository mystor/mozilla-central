// XXX: We don't need this feature if we can use the libc crate from crates.io,
// but right now we don't have the ability to do that with the build system, so
// this will work in the short term (but requires a nightly rustc).
#![feature(libc)]

#![allow(non_camel_case_types)]

extern crate libc;

use std::ptr;
use std::slice;
use std::ops::Deref;
use std::str;

pub type NsResult = libc::uint32_t;

#[macro_export]
macro_rules! ns_try {
    ($t:expr) => {
        {
            let res = $t;
            if res != 0 {
                return Err(res)
            }
        }
    }
}

#[cfg(windows)]
type RefCountType = libc::ulong;

#[cfg(not(windows))]
type RefCountType = libc::uint32_t;

#[repr(C)]
#[derive(Copy, Clone)]
pub struct IID(libc::uint32_t, libc::uint16_t, libc::uint16_t,
               [libc::uint8_t; 8]);

pub enum ISupports {}

#[repr(C)]
pub struct ISupportsVTable {
    query_interface: unsafe extern "C" fn(*const ISupports, *const IID, *mut *const u8) -> NsResult,
    addref: unsafe extern "C" fn(*const ISupports) -> RefCountType,
    release: unsafe extern "C" fn(*const ISupports) -> RefCountType,
}

unsafe impl XpCom for ISupports {
    type VTable = ISupportsVTable;
    fn iid() -> IID {
        IID(0x00000000, 0x0000, 0x0000,
            [0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46])
    }
}

pub unsafe trait XpCom {
    type VTable;
    fn iid() -> IID;

    fn get_vtable(&self) -> &Self::VTable {
        struct HasVTable {
            vtable: *const u8,
        }
        unsafe {
            &*((*(self as *const Self as *const HasVTable)).vtable as *const Self::VTable)
        }
    }

    fn get_isupports_vtable(&self) -> &ISupportsVTable {
        unsafe {
            &*(self.get_vtable() as *const Self::VTable as *const ISupportsVTable)
        }
    }

    fn get_isupports(&self) -> &ISupports {
        unsafe {
            &*(self as *const Self as *const ISupports)
        }
    }

    // XXX: Should query_interface produce a ComPtr<U> or a &U?
    fn query_interface<U: XpCom>(&self) -> Option<ComPtr<U>> {
        let mut p = ptr::null();
        let iid = U::iid();
        let result = unsafe {
            ((*self.get_isupports_vtable()).query_interface)(self.get_isupports(),
                                                             &iid as *const _,
                                                             &mut p
                                                               as *mut *const U
                                                               as *mut *const u8)
        };
        if result == 0 && p != ptr::null() {
            Some(ComPtr {ptr: p})
        } else {
            None
        }
    }
}

// XXX: NonZero?
#[derive(Debug)]
pub struct ComPtr<T: XpCom> {
    ptr: *const T,
}

impl <T: XpCom> Deref for ComPtr<T> {
    type Target = T;
    fn deref(&self) -> &T {
        unsafe { &*self.ptr }
    }
}

impl <T: XpCom> Drop for ComPtr<T> {
    fn drop(&mut self) {
        unsafe {
            ((*self.get_isupports_vtable()).release)(self.get_isupports());
        }
    }
}

impl <T: XpCom> Clone for ComPtr<T> {
    fn clone(&self) -> ComPtr<T> {
        let rc = unsafe {
            ((*self.get_isupports_vtable()).addref)(self.get_isupports())
        };
        assert!(rc > 0);
        ComPtr {
            ptr: self.ptr as *const T
        }
    }
}

impl <T: XpCom> ComPtr<T> {
    pub fn new(t: &T) -> ComPtr<T> {
        let rc = unsafe {
            ((*t.get_isupports_vtable()).addref)(t.get_isupports())
        };
        assert!(rc > 0);
        ComPtr {
            ptr: t as *const T
        }
    }

    pub unsafe fn from_ptr(t: *mut T) -> Option<ComPtr<T>> {
        if t == ptr::null_mut() {
            return None;
        }
        let rc = ((*(*t).get_isupports_vtable()).addref)((*t).get_isupports());
        assert!(rc > 0);
        Some(ComPtr {
            ptr: t as *const T
        })
    }
}

pub mod ns_str_flags {
    pub const F_NONE: u32 = 0; // no flags

    // data flags are in the lower 16-bits
    // IsTerminated returns true
    pub const F_TERMINATED: u32 = 1 << 0;
    // IsVoid returns true
    pub const F_VOIDED: u32 = 1 << 1;
    // mData points to a heap-allocated, shared buffer
    pub const F_SHARED: u32 = 1 << 2;
    // mData points to a heap-allocated, raw buffer
    pub const F_OWNED: u32 = 1 << 3;
    // mData points to a fixed-size writable, dependent buffer
    pub const F_FIXED: u32 = 1 << 4;
    // mData points to a string literal; F_TERMINATED will also be set
    pub const F_LITERAL: u32 = 1 << 5;

    // class flags are in the upper 16-bits
    // indicates that |this| is of type nsTFixedString
    pub const F_CLASS_FIXED: u32 = 1 << 16;
}

#[repr(C)]
pub struct nsACString {
    data: *const u8,
    length: u32,
    flags: u32,
}

impl Deref for nsACString {
    type Target = [u8];
    fn deref(&self) -> &[u8] {
        let data = if self.data.is_null() {
            0x1 as *const u8 // XXX: arbitrary non-null value
        } else {
            self.data
        };
        unsafe {
            slice::from_raw_parts(data, self.length as usize)
        }
    }
}

pub struct nsCString(nsACString);

impl nsCString {
    pub fn new_empty() -> nsCString {
        nsCString(nsACString {
            data: ptr::null(),
            length: 0,
            flags: ns_str_flags::F_NONE,
        })
    }

    pub fn from_slice(s: &[u8]) -> nsCString {
        unsafe {
            let data = libc::malloc(s.len()) as *mut u8;
            ptr::copy_nonoverlapping(s.as_ptr(), data, s.len());
            nsCString(nsACString {
                data: data,
                length: s.len() as u32,
                flags: ns_str_flags::F_OWNED,
            })
        }
    }

    pub unsafe fn dependent_from_slice(s: &[u8]) -> nsCString {
        nsCString(nsACString {
            data: s.as_ptr(),
            length: s.len() as u32,
            flags: ns_str_flags::F_NONE,
        })
    }

    pub fn as_aptr(&self) -> *const nsACString {
        return &self.0
    }

    pub fn as_aptr_mut(&mut self) -> *mut nsACString {
        return &mut self.0
    }
}

impl Deref for nsCString {
    type Target = [u8];
    fn deref(&self) -> &[u8] {
        return &self.0;
    }
}

impl Drop for nsCString {
    fn drop(&mut self) {
        if self.0.flags & ns_str_flags::F_SHARED != 0 {
            unsafe {
                nsStringBuffer_Release(self.0.data as *mut libc::c_void);
            }
        }
        if self.0.flags & ns_str_flags::F_OWNED != 0 {
            unsafe {
                libc::free(self.0.data as *mut libc::c_void);
            }
        }
    }
}

#[repr(C)]
pub struct nsAString {
    data: *const u16,
    length: u32,
    flags: u32,
}

impl Deref for nsAString {
    type Target = [u16];
    fn deref(&self) -> &[u16] {
        let data = if self.data.is_null() {
            0x1 as *const u16 // arbitrary non-null value
        } else {
            self.data
        };
        unsafe {
            slice::from_raw_parts(data, self.length as usize)
        }
    }
}

pub struct nsString(nsAString);

impl nsString {
    pub fn new_empty() -> nsString {
        nsString(nsAString {
            data: ptr::null(),
            length: 0,
            flags: ns_str_flags::F_NONE,
        })
    }

    pub fn from_slice(s: &[u16]) -> nsString {
        unsafe {
            let data = libc::malloc(s.len()) as *mut u16;
            ptr::copy_nonoverlapping(s.as_ptr(), data, s.len());
            nsString(nsAString {
                data: data,
                length: s.len() as u32,
                flags: ns_str_flags::F_OWNED,
            })
        }
    }

    pub unsafe fn dependent_from_slice(s: &[u16]) -> nsString {
        nsString(nsAString {
            data: s.as_ptr(),
            length: s.len() as u32,
            flags: ns_str_flags::F_NONE,
        })
    }

    pub fn as_aptr(&self) -> *const nsAString {
        return &self.0
    }

    pub fn as_aptr_mut(&mut self) -> *mut nsAString {
        return &mut self.0
    }
}

impl Deref for nsString {
    type Target = [u16];
    fn deref(&self) -> &[u16] {
        return &self.0;
    }
}

impl Drop for nsString {
    fn drop(&mut self) {
        unsafe {
            if self.0.flags & ns_str_flags::F_SHARED != 0 {
                nsStringBuffer_Release(self.0.data as *mut libc::c_void);
            }
            if self.0.flags & ns_str_flags::F_OWNED != 0 {
                libc::free(self.0.data as *mut libc::c_void);
            }
        }
    }
}

extern "C" {
    fn nsStringBuffer_Release(p: *mut libc::c_void);
}

pub enum IURI {}

#[repr(C)]
pub struct IURIVTable {
    isupports: ISupportsVTable,

    get_spec: unsafe extern "C" fn(this: *const IURI, spec: *mut nsACString) -> NsResult,
    set_spec: unsafe extern "C" fn(this: *const IURI, spec: *const nsACString) -> NsResult,

    get_prepath: unsafe extern "C" fn(this: *const IURI, prepath: *mut nsACString) -> NsResult,

    get_scheme: unsafe extern "C" fn(this: *const IURI, scheme: *mut nsACString) -> NsResult,
    set_scheme: unsafe extern "C" fn(this: *const IURI, scheme: *const nsACString) -> NsResult,

    get_user_pass: unsafe extern "C" fn(this: *const IURI, userpass: *mut nsACString) -> NsResult,
    set_user_pass: unsafe extern "C" fn(this: *const IURI, userpass: *const nsACString) -> NsResult,



  // /* attribute AUTF8String username; */
  // NS_IMETHOD GetUsername(nsACString & aUsername) = 0;
  // NS_IMETHOD SetUsername(const nsACString & aUsername) = 0;

  // /* attribute AUTF8String password; */
  // NS_IMETHOD GetPassword(nsACString & aPassword) = 0;
  // NS_IMETHOD SetPassword(const nsACString & aPassword) = 0;

  // /* attribute AUTF8String hostPort; */
  // NS_IMETHOD GetHostPort(nsACString & aHostPort) = 0;
  // NS_IMETHOD SetHostPort(const nsACString & aHostPort) = 0;

  // /* attribute AUTF8String host; */
  // NS_IMETHOD GetHost(nsACString & aHost) = 0;
  // NS_IMETHOD SetHost(const nsACString & aHost) = 0;

  // /* attribute long port; */
  // NS_IMETHOD GetPort(int32_t *aPort) = 0;
  // NS_IMETHOD SetPort(int32_t aPort) = 0;

  // /* attribute AUTF8String path; */
  // NS_IMETHOD GetPath(nsACString & aPath) = 0;
  // NS_IMETHOD SetPath(const nsACString & aPath) = 0;

  // /* boolean equals (in nsIURI other); */
  // NS_IMETHOD Equals(nsIURI *other, bool *_retval) = 0;

  // /* boolean schemeIs (in string scheme); */
  // NS_IMETHOD SchemeIs(const char * scheme, bool *_retval) = 0;

  // /* nsIURI clone (); */
  // NS_IMETHOD Clone(nsIURI * *_retval) = 0;

  // /* AUTF8String resolve (in AUTF8String relativePath); */
  // NS_IMETHOD Resolve(const nsACString & relativePath, nsACString & _retval) = 0;

  // /* readonly attribute ACString asciiSpec; */
  // NS_IMETHOD GetAsciiSpec(nsACString & aAsciiSpec) = 0;

  // /* readonly attribute ACString asciiHostPort; */
  // NS_IMETHOD GetAsciiHostPort(nsACString & aAsciiHostPort) = 0;

  // /* readonly attribute ACString asciiHost; */
  // NS_IMETHOD GetAsciiHost(nsACString & aAsciiHost) = 0;

  // /* readonly attribute ACString originCharset; */
  // NS_IMETHOD GetOriginCharset(nsACString & aOriginCharset) = 0;

  // /* attribute AUTF8String ref; */
  // NS_IMETHOD GetRef(nsACString & aRef) = 0;
  // NS_IMETHOD SetRef(const nsACString & aRef) = 0;

  // /* boolean equalsExceptRef (in nsIURI other); */
  // NS_IMETHOD EqualsExceptRef(nsIURI *other, bool *_retval) = 0;

  // /* nsIURI cloneIgnoringRef (); */
  // NS_IMETHOD CloneIgnoringRef(nsIURI * *_retval) = 0;

  // /* readonly attribute AUTF8String specIgnoringRef; */
  // NS_IMETHOD GetSpecIgnoringRef(nsACString & aSpecIgnoringRef) = 0;

  // /* readonly attribute boolean hasRef; */
  // NS_IMETHOD GetHasRef(bool *aHasRef) = 0;
}

unsafe impl XpCom for IURI {
    type VTable = IURIVTable;
    fn iid() -> IID {
        IID(0x92073a54, 0x6d78, 0x4f30,
            [0x91, 0x3a, 0xb8, 0x71, 0x81, 0x32, 0x08, 0xc6])
    }
}

impl IURI {
    pub unsafe fn get_spec(&self) -> Result<nsCString, NsResult> {
        let mut s = nsCString::new_empty();
        ns_try!((self.get_vtable().get_spec)(self as *const _, s.as_aptr_mut()));
        Ok(s)
    }

    pub unsafe fn set_spec(&self, s: &[u8]) -> Result<(), NsResult> {
        let s = nsCString::dependent_from_slice(s);
        ns_try!((self.get_vtable().set_spec)(self as *const _, s.as_aptr()));
        Ok(())
    }

    pub unsafe fn get_prepath(&self) -> Result<nsCString, NsResult> {
        let mut s = nsCString::new_empty();
        ns_try!((self.get_vtable().get_prepath)(self as *const _, s.as_aptr_mut()));
        Ok(s)
    }

    pub unsafe fn get_scheme(&self) -> Result<nsCString, NsResult> {
        let mut s = nsCString::new_empty();
        ns_try!((self.get_vtable().get_scheme)(self as *const _, s.as_aptr_mut()));
        Ok(s)
    }

    pub unsafe fn set_scheme(&self, s: &[u8]) -> Result<(), NsResult> {
        let s = nsCString::dependent_from_slice(s);
        ns_try!((self.get_vtable().set_scheme)(self as *const _, s.as_aptr()));
        Ok(())
    }

    pub unsafe fn get_user_pass(&self) -> Result<nsCString, NsResult> {
        let mut s = nsCString::new_empty();
        ns_try!((self.get_vtable().get_user_pass)(self as *const _, s.as_aptr_mut()));
        Ok(s)
    }

    pub unsafe fn set_user_pass(&self, s: &[u8]) -> Result<(), NsResult> {
        let s = nsCString::dependent_from_slice(s);
        ns_try!((self.get_vtable().set_user_pass)(self as *const _, s.as_aptr()));
        Ok(())
    }
}

#[no_mangle]
pub extern fn xprs_test(p: *const ISupports) -> u8 {
    if p.is_null() {
        return 0;
    }
    if let Some(uri) = unsafe { &*p }.query_interface::<IURI>() {
        if let Ok(s) = unsafe { uri.get_spec() } {
            println!("We got a spec! Its value is {:?}", str::from_utf8(&s));
            return 1;
        }
    }
    0
}

