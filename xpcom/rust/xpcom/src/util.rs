use nserror::{nsresult, NS_OK};
use {get_service, nsCID, nsISupports, RefPtr, XpCom};

#[derive(xpcom)]
#[xpimplements(nsIRunnable)]
pub struct InitRunnableFunction {
    // NOTE: For very unfortunate reasons, we cannot use a generic type under
    // derive(xpcom), as we cannot generate vtables in static memory based on
    // generic instantiation. If this is changed, we can change this.
    f: Box<Fn()>
}

impl RunnableFunction {
    pub fn new<T: Fn() + 'static>(f: T) -> RefPtr<RunnableFunction> {
        Self::from_box(Box::new(f))
    }

    pub fn from_box(f: Box<Fn()>) -> RefPtr<Self> {
        Self::allocate(InitRunnableFunction {
            f: f,
        })
    }

    fn run(&self) -> nsresult {
        (self.f)();
        NS_OK
    }
}

extern "C" {
    fn NS_IsMainThread() -> bool;
}

#[inline]
pub fn is_main_thread() -> bool {
    unsafe {
        NS_IsMainThread()
    }
}

static mut XPCOM_SHUTTING_DOWN: bool = false;
static mut XPCOM_SERVICES: *mut Vec<*const nsISupports> = 0 as *mut Vec<*const nsISupports>;
#[no_mangle]
pub unsafe extern "C" fn Rust_ShutdownXpcomServices() {
    XPCOM_SHUTTING_DOWN = true;

    // Free any references held by XPCOM_SERVICES
    if !XPCOM_SERVICES.is_null() {
        for service in (*XPCOM_SERVICES).drain(..) {
            (*service).Release();
        }
        Box::from_raw(XPCOM_SERVICES);
        XPCOM_SERVICES = 0 as *mut Vec<*const nsISupports>;
    }
}

/// This is a static initializer struct which is used in the implementation of
/// xpcom_service!. It is marked as Sync so it can be made global, but only
/// contains single-threaded members. Access to the fields of this struct is
/// made safe through checking `is_main_thread` before dereferencing it.
///
/// The two fields of this struct are public for the xpcom_service! macro.
/// Please do not use them.
///
/// The deref method gets the XPCOM service stored in this &'static mut. Panics
/// if is_main_thread() returns false, or if the service could not be found.
pub struct XpcomService<T: XpCom>(#[doc(hidden)] pub *const T, #[doc(hidden)] pub nsCID);
impl<T: XpCom> XpcomService<T> {
    #[doc(hidden)]
    pub unsafe fn get(&'static mut self) -> &T {
        // If we're on the main thread or XPCOM is shutting down, crash, as we
        // cannot run the service getter.
        assert!(is_main_thread());
        assert!(!XPCOM_SHUTTING_DOWN);

        // If we haven't initialized the service yet, initialize our cache.
        if self.0.is_null() {
            get_service::<T>(&self.1)
                .expect("Failed to get service in XpcomService lazy service getter")
                .forget(&mut self.0);

            // Make sure we have the XPCOM_SERVICES array, and add our service
            // pointer into it, after casting it to nsISupports.
            if XPCOM_SERVICES.is_null() {
                XPCOM_SERVICES = Box::into_raw(Box::new(Vec::new()));
            }
            (*XPCOM_SERVICES).push(self.0 as *const nsISupports);
        }

        // Return the cached service reference.
        &*self.0
    }
}

unsafe impl<T: XpCom> Sync for XpcomService<T> {}

#[macro_export]
#[doc(hidden)]
macro_rules! __xpcom_service_internal {
    ($(#[$attr:meta])* service $N:ident : $T:ty = $cid:expr; $($t:tt)*) => {
        __xpcom_service_internal!(@PRIV, $(#[$attr])* service $N : $T = $cid; $($t)*);
    };
    ($(#[$attr:meta])* pub service $N:ident : $T:ty = $cid:expr; $($t:tt)*) => {
        __xpcom_service_internal!(@PUB, $(#[$attr])* service $N : $T = $cid; $($t)*);
    };
    (@$VIS:ident, $(#[$attr:meta])* service $N:ident : $T:ty = $cid:expr; $($t:tt)*) => {
        __xpcom_service_internal!(@MAKE TY, $VIS, $(#[$attr])*, $N);
        impl $crate::reexports::Deref for $N {
            type Target = $T;
            #[allow(unsafe_code)]
            fn deref(&self) -> &$T {
                static mut LAZY: $crate::XpcomService<$T> =
                    $crate::XpcomService(0 as *const $T, $cid);
                unsafe {
                    LAZY.get()
                }
            }
        }
        __xpcom_service_internal!($($t)*);
    };
    (@MAKE TY, PUB, $(#[$attr:meta])*, $N:ident) => {
        #[allow(missing_copy_implementations)]
        #[allow(non_camel_case_types)]
        #[allow(dead_code)]
        $(#[$attr])*
        pub struct $N {__private_field: ()}
        #[doc(hidden)]
        pub static $N: $N = $N {__private_field: ()};
    };
    (@MAKE TY, PRIV, $(#[$attr:meta])*, $N:ident) => {
        #[allow(missing_copy_implementations)]
        #[allow(non_camel_case_types)]
        #[allow(dead_code)]
        $(#[$attr])*
        struct $N {__private_field: ()}
        #[doc(hidden)]
        static $N: $N = $N {__private_field: ()};
    };
    () => ()
}

/// The implementation of this macro is very similar to lazy-static, however,
/// unlike lazy-static, this type is optimized to store XPCOM services.
///
/// It is used as follows:
///
/// ```rust
/// xpcom_service! {
///     /// This defines a static named `IOSERVICE` which can be dereferenced
///     /// on the main thread to get a reference to the IOSERVICE's
///     /// nsIIOService2 object.
///     service IOSERVICE: nsIIOService2 =
///         xpcom::nsID(0x9ac9e770, 0x18bc, 0x11d3,
///                     [0x93, 0x37, 0x00, 0x10, 0x4b, 0xa0, 0xfd, 0x40]);
/// }
/// ```
///
/// # Panics
///
/// This static variable will panic if it is not run on the xpcom main thread or
/// if the service could not be found.
#[macro_export]
macro_rules! xpcom_service {
    ($(#[$attr:meta])* service $N:ident : $T:ty = $cid:expr; $($t:tt)*) => {
        __xpcom_service_internal!(@PRIV, $(#[$attr])* service $N : $T = $cid; $($t)*);
    };
    ($(#[$attr:meta])* pub service $N:ident : $T:ty = $cid:expr; $($t:tt)*) => {
        __xpcom_service_internal!(@PUB, $(#[$attr])* service $N : $T = $cid; $($t)*);
    };
    () => ()
}

