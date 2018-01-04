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

// Helper functions and data structures are exported in the root of the crate.
mod base;
pub use base::*;

mod refptr;
pub use refptr::*;

// XPCOM interface definitions.
pub mod interfaces;

// XPCOM service getters.
pub mod services;

// Implementation details of the xpcom_macros crate.
#[doc(hidden)]
pub mod reexports;
