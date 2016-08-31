#![allow(non_snake_case)]

#[macro_use]
extern crate nsstring;

use std::fmt::Write;
use nsstring::*;

#[macro_use]
mod gtest {
    use std::ffi::CString;
    use std::os::raw::c_char;

    /// Define an extern "C" function, like in an extern "C" block, which is
    /// discovered dynamically when called.
    ///
    /// This macro is very useful for defining gtests which need to call into
    /// C++ code which is only present when the gtest C++ code is attached. As
    /// this rust code is always included, we cannot statically link to this
    /// code.
    #[macro_export]
    macro_rules! dynamic_extern {
        () => {};
        (fn $name:ident ($($aname:ident : $aty:ty),*); $($rest:tt)*) => {
            dynamic_extern!(fn $name ($($aname : $aty),*) -> (); $($rest)*);
        };
        (fn $name:ident ($($aname:ident : $aty:ty),*) -> $result:ty; $($rest:tt)*) => {
            unsafe fn $name ($($aname : $aty),*) -> Result<$result, ()> {
                let sym = $crate::gtest::get_dynamic(
                    ::std::ffi::CString::new(stringify!($name)).unwrap().as_ptr());
                if sym.is_null() {
                    return Err(());
                }
                Ok(::std::mem::transmute::<*mut u8, unsafe extern "C" fn ($($aty),*) -> $result>
                   (sym)($($aname),*))
            }
            dynamic_extern!($($rest)*);
        };
    }

    /// Internal abstraction over dlsym used by dynamic_extern!
    #[cfg(unix)]
    pub unsafe fn get_dynamic(symbol: *const c_char) -> *mut u8 {
        #[cfg(target_os = "macos")]
        const RTLD_DEFAULT: isize = -2;
        #[cfg(not(target_os = "macos"))]
        const RTLD_DEFAULT: isize = 0;

        extern {
            fn dlsym(handle: *mut u8, symbol: *const c_char) -> *mut u8;
        }
        dlsym(RTLD_DEFAULT as *mut u8, symbol)
    }

    /// Internal abstraction over GetModuleHandle used by dynamic_extern!
    #[cfg(windows)]
    pub unsafe fn get_dynamic(symbol: *const c_char) -> *mut u8 {
        extern "system" {
            fn GetModuleHandleA(lpModuleName: *const c_char) -> *mut u8;
            fn GetProcAddress(hModule: *mut u8, lpProcName: *const c_char) -> *mut u8;
        }
        let handle = GetModuleHandleA(b"xul.dll\0".as_ptr() as *const c_char);
        GetProcAddress(handle, symbol)
    }

    /// Internal failure function used by expect! and expect_eq!
    ///
    /// Triggers a non-fatal gtest failure when called, with the message passed in.
    pub fn nonfatal_fail(msg: String) {
        dynamic_extern! {
            fn GTest_ExpectFailure(message: *const c_char);
        }
        unsafe {
            GTest_ExpectFailure(CString::new(msg).unwrap().as_ptr()).unwrap();
        }
    }

    /// This macro checks if the two arguments are equal, and causes a non-fatal
    /// GTest test failure if they are not.
    #[macro_export]
    macro_rules! expect_eq {
        ($x:expr, $y:expr) => {
            match (&$x, &$y) {
                (x, y) => if *x != *y {
                    $crate::gtest::nonfatal_fail(
                        format!("check failed: (`{:?}` == `{:?}`) at {}:{}",
                                x, y, file!(), line!()))
                }
            }
        }
    }

    /// This macro checks if its first argument is true. If they are not, it triggers
    /// a non-fatal GTest test failure.
    #[macro_export]
    macro_rules! expect {
        ($cond:expr) => (
            if !$cond {
                $crate::gtest::nonfatal_fail(
                    format!(concat!("expectation failed: ", stringify!($cond), " at {}:{}"),
                            file!(), line!()));
            }
        );
        ($cond:expr, $($arg:tt)+) => (
            if !$cond {
                $crate::gtest::nonfatal_fail(
                    format!("{} at {}:{}", format!($($arg)+), file!(), line!()));
            }
        );
    }
}


#[no_mangle]
pub extern fn Rust_StringFromCpp(cs: *const nsACString, s: *const nsAString) {
    unsafe {
        expect_eq!(&*cs, "Hello, World!");
        expect_eq!(&*s, "Hello, World!");
    }
}

#[no_mangle]
pub extern fn Rust_AssignFromRust(cs: *mut nsACString, s: *mut nsAString) {
    unsafe {
        (*cs).assign(&nsCString::from("Hello, World!"));
        expect_eq!(&*cs, "Hello, World!");
        (*s).assign(&nsString::from("Hello, World!"));
        expect_eq!(&*s, "Hello, World!");
    }
}

dynamic_extern! {
    fn Cpp_AssignFromCpp(cs: *mut nsACString, s: *mut nsAString);
}

#[no_mangle]
pub extern fn Rust_AssignFromCpp() {
    let mut cs = nsCString::new();
    let mut s = nsString::new();
    unsafe {
        Cpp_AssignFromCpp(&mut *cs, &mut *s).unwrap();
    }
    expect_eq!(cs, "Hello, World!");
    expect_eq!(s, "Hello, World!");
}

#[no_mangle]
pub extern fn Rust_FixedAssignFromCpp() {
    let mut cs_buf: [u8; 64] = [0; 64];
    let cs_buf_ptr = &cs_buf as *const _ as usize;
    let mut s_buf: [u16; 64] = [0; 64];
    let s_buf_ptr = &s_buf as *const _ as usize;
    let mut cs = nsFixedCString::new(&mut cs_buf);
    let mut s = nsFixedString::new(&mut s_buf);
    unsafe {
        Cpp_AssignFromCpp(&mut *cs, &mut *s).unwrap();
    }
    expect_eq!(cs, "Hello, World!");
    expect_eq!(s, "Hello, World!");
    expect_eq!(cs.as_ptr() as usize, cs_buf_ptr);
    expect_eq!(s.as_ptr() as usize, s_buf_ptr);
}

#[no_mangle]
pub extern fn Rust_AutoAssignFromCpp() {
    ns_auto_cstring!(cs);
    ns_auto_string!(s);
    unsafe {
        Cpp_AssignFromCpp(&mut *cs, &mut *s).unwrap();
    }
    expect_eq!(cs, "Hello, World!");
    expect_eq!(s, "Hello, World!");
}

#[no_mangle]
pub extern fn Rust_StringWrite() {
    ns_auto_cstring!(cs);
    ns_auto_string!(s);

    write!(s, "a").unwrap();
    write!(cs, "a").unwrap();
    expect_eq!(s, "a");
    expect_eq!(cs, "a");
    write!(s, "bc").unwrap();
    write!(cs, "bc").unwrap();
    expect_eq!(s, "abc");
    expect_eq!(cs, "abc");
    write!(s, "{}", 123).unwrap();
    write!(cs, "{}", 123).unwrap();
    expect_eq!(s, "abc123");
    expect_eq!(cs, "abc123");
}

