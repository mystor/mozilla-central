#![allow(non_snake_case)]
#![allow(non_camel_case_types)]

extern crate libc;
extern crate nsstring;
extern crate nserror;

// re-export the xpcom_macros macro
#[macro_use]
#[allow(unused_imports)]
extern crate xpcom_macros;
#[doc(hidden)]
pub use xpcom_macros::*;

// Re-export all of the symbols into the crate root
mod base;
pub use base::*;

mod idl;
pub use idl::*;

mod nonidl;
pub use nonidl::*;

mod refptr;
pub use refptr::*;

mod statics;
pub use statics::*;

// The reexports module is intentionally not re-exported into the root, as it is
// intended to only be used internally by the `xpcom_macros` crate.
#[doc(hidden)]
pub mod reexports;

// We want to use the procedural macros defined in `xpcom_macros` in tree, so we
// need a fake private xpcom module.
mod xpcom {
    pub use super::*;
}
