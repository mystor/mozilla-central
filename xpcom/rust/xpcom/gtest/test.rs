#![allow(non_snake_case)]

#[macro_use]
extern crate xpcom;

extern crate nserror;

use std::ptr;
use xpcom::XpCom;
use nserror::{nsresult, NS_OK};

xpcom_service! {
    service IOSERVICE: xpcom::nsIIOService2 =
        xpcom::nsID(0x9ac9e770, 0x18bc, 0x11d3,
                    [0x93, 0x37, 0x00, 0x10, 0x4b, 0xa0, 0xfd, 0x40]);
}

#[no_mangle]
pub unsafe extern fn Rust_CallIURIFromRust() -> bool {
    let uri = IOSERVICE.newURI(b"https://google.com", ptr::null(), None).unwrap().unwrap();
    let host = uri.get_host().unwrap();
    assert_eq!(&*host, "google.com");

    assert!(IOSERVICE.QueryInterface::<xpcom::nsISupports>().unwrap().is_some());
    assert!(IOSERVICE.QueryInterface::<xpcom::nsIURI>().is_err());
    true
}

#[no_mangle]
pub unsafe extern fn Rust_ImplementRunnableInRust(it_worked: *mut bool,
                                                  runnable: *mut *const xpcom::nsIRunnable) {
    // Define a type which implements nsIRunnable in rust.
    #[derive(xpcom)]
    #[xpimplements(nsIRunnable)]
    struct InitMyRunnable {
        it_worked: *mut bool,
    }

    impl MyRunnable {
        unsafe fn run(&self) -> nsresult {
            *self.it_worked = true;
            NS_OK
        }
    }

    // Create my runnable type, and forget it into the outparameter!
    let my_runnable = MyRunnable::allocate(InitMyRunnable {
        it_worked: it_worked
    });
    my_runnable.QueryInterface(&xpcom::nsIRunnable::iid(), runnable as *mut _);
}
