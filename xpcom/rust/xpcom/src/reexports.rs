/// The automatically generated code from `xpcom_macros` depends on some types
/// which are defined in other libraries which `xpcom` depends on, but which may
/// not be `extern crate`-ed into the crate the macros are expanded into. This
/// module re-exports those types from `xpcom` so that they can be used from the
/// macro.

// re-export libc so it can be used by the procedural macro.
pub extern crate libc;

pub use nsstring::{nsACString, nsAString};

pub use nserror::{nsresult, NsresultExt, NS_ERROR_NO_INTERFACE, NS_OK};

pub use std::ops::Deref;
