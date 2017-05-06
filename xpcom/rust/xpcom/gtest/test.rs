#![allow(non_snake_case)]

extern crate xpcom;

use std::ptr;
use xpcom::RefPtr;

const NS_IOSERVICE_CID: xpcom::nsCID =
    xpcom::nsID(0x9ac9e770, 0x18bc, 0x11d3,
                [0x93, 0x37, 0x00, 0x10, 0x4b, 0xa0, 0xfd, 0x40]);

#[no_mangle]
pub unsafe extern fn Rust_CallIURIFromRust() -> bool {
    let io_svc: RefPtr<xpcom::nsIIOService2> = xpcom::get_service(&NS_IOSERVICE_CID).unwrap();
    let uri = io_svc.newURI(b"https://google.com", ptr::null(), None).unwrap().unwrap();
    let host = uri.get_host().unwrap();
    assert_eq!(&*host, "google.com");

    assert!(io_svc.QueryInterface::<xpcom::nsISupports>().unwrap().is_some());
    assert!(io_svc.QueryInterface::<xpcom::nsIURI>().is_err());
    true
}
