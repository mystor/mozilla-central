//! This module contains the required types and functions for the implementation
//! of nsIURI for MozURL.

use super::*;

use std::os::raw::c_char;
use std::ffi::CStr;

/// This helper macro will cause NS_ERROR_NULL_POINTER to be returned if the
/// value passed into it is null, and will deref it.
macro_rules! der {
  ($p:expr) => {
    *match $p {
      p if p.is_null() => return NS_ERROR_NULL_POINTER,
      p => p,
    }
  }
}

impl MozURL {
  // Hacky mechanism for recovering a MozURL from a nsIURI if the vtable matches.
  unsafe fn from_iuri(&self, iuri: *const nsIURI) -> Option<&MozURL> {
    // XXX(hack): Theoretically we should be able to statically know this, but
    // xpcom_macros doesn't expose it :'-(
    let us: *const nsIURIVTable = self.__base_nsIURI;
    assert_eq!(
      &self.__base_nsIURI as *const *const nsIURIVTable,
      &self as *const Self as *const *const nsIURIVTable,
    );

    // If our vtables match, do the cast!
    if us == *(iuri as *const *const nsIURIVTable) {
      Some(&*(iuri as *const MozURL))
    } else {
      None
    }
  }

  // NOTE: This nsIURI implementation always produces punycode encoded output strings.
  pub unsafe fn GetSpec(&self, spec: *mut nsACString) -> nsresult {
    der!(spec).assign(mozurl_spec(self));
    NS_OK
  }

  pub unsafe fn GetPrePath(&self, prepath: *mut nsACString) -> nsresult {
    der!(prepath).assign(mozurl_prepath(self));
    NS_OK
  }

  pub unsafe fn GetScheme(&self, scheme: *mut nsACString) -> nsresult {
    der!(scheme).assign(mozurl_scheme(self));
    NS_OK
  }

  pub unsafe fn GetUserPass(&self, userpass: *mut nsACString) -> nsresult {
    der!(userpass).assign(mozurl_userpass(self));
    NS_OK
  }

  pub unsafe fn GetUsername(&self, user: *mut nsACString) -> nsresult {
    der!(user).assign(mozurl_username(self));
    NS_OK
  }

  pub unsafe fn GetPassword(&self, pass: *mut nsACString) -> nsresult {
    der!(pass).assign(mozurl_password(self));
    NS_OK
  }

  pub unsafe fn GetHostPort(&self, hostport: *mut nsACString) -> nsresult {
    der!(hostport).assign(mozurl_host_port(self));
    NS_OK
  }

  pub unsafe fn GetHost(&self, host: *mut nsACString) -> nsresult {
    der!(host).assign(mozurl_host(self));
    NS_OK
  }

  pub unsafe fn GetPort(&self, port: *mut i32) -> nsresult {
    der!(port) = mozurl_port();
    NS_OK
  }

  pub unsafe fn GetPathQueryRef(&self, out: *mut nsACString) -> nsresult {
    der!(out).assign(mozurl_path());
    NS_OK
  }

  pub unsafe fn Equals(&self, other: *const nsIURI, res: *mut bool) -> nsresult {
    der!(res) = false;
    if let Some(other) = self.from_iuri(&der!(other)) {
      *res = self.url == other.url;
    }
    NS_OK
  }

  pub unsafe fn SchemeIs(&self, scheme: *const c_char) -> bool {
    let other = unsafe { CStr::from_ptr(scheme) }.to_bytes();
    der!(res) = mozurl_scheme(self).to_bytes() == other;
    NS_OK
  }

  pub unsafe fn Clone(&self, out: *mut *const nsIURI) -> nsresult {
    Self::allocate(InitMozURL{ uri: self.uri }).forget(&mut der!(out));
    NS_OK
  }

  pub unsafe fn Resolve(&self, relative: *const nsACString, res: *mut nsACString) -> nsresult {
    let resolved = try_or_malformed!(self.join(&der!(relative)));
    // Try to transfer string buffer ownership.
    der!(res).take_from(&mut nsCString::from(resolved.to_string()));
    NS_OK
  }

  pub unsafe fn GetAsciiSpec(&self, res: *mut nsACString) -> nsresult {
    der!(res).assign(mozurl_spec(self));
    NS_OK
  }

  pub unsafe fn GetAsciiHostPort(&self, res: *mut nsACString) -> nsresult {
    der!(res).assign(mozurl_host_port(self));
    NS_OK
  }

  pub unsafe fn GetAsciiHost(&self, res: *mut nsACString) -> nsresult {
    der!(res).assign(mozurl_host(self));
    NS_OK
  }

  pub unsafe fn GetRef(&self, res: *mut nsACString) -> nsresult {
    der!(res).assign(mozurl_fragment(self));
    NS_OK
  }

  pub unsafe fn EqualsExceptRef(&self, other: *const nsIURI, res: *mut bool) -> nsresult {
    der!(res) = false;
    if let Some(other) = self.from_iuri(&der!(other)) {
      *res = self[..Location::FragmentBegin] == other[..Location::FragmentBegin];
    }
    NS_OK
  }

  pub unsafe fn CloneIgnoringRef(&self, res: *mut *const nsIURI) -> nsresult {
    self.CloneWithNewRef(&nsCStr::new(), res)
  }

  pub unsafe fn CloneWithNewRef(&self, new: *const nsACString, res: *mut *const nsIURI) -> nsresult {
    let mut url = self.url.clone();
    let rv = mozurl_set_fragment(&mut url, &der!(new));
    if rv.failed() {
      return rv;
    }
    Self::allocate(InitMozURL { url }).forget(&der!(res));
    NS_OK
  }

  pub unsafe fn GetSpecIgnoringRef(&self, res: *mut nsACString) -> nsresult {
    der!(res).assign(&self[..Location::FragmentBegin]);
    NS_OK
  }

  pub unsafe fn HasRef(&self, res: *mut bool) -> nsresult {
    der!(res) = mozurl_has_fragment(self);
    NS_OK
  }

  pub unsafe fn GetFilePath(&self, res: *mut nsACString) -> nsresult {
    der!(res).assign(mozurl_filepath(self));
    NS_OK
  }

  pub unsafe fn GetQuery(&self, res: *mut nsACString) -> nsresult {
    der!(res).assign(mozurl_query(self));
    NS_OK
  }

  pub unsafe fn GetDisplayHost(&self, res: *mut nsACString) -> nsresult {
    mozurl_display_host(self, &mut der!(res));
    NS_OK
  }

  pub unsafe fn GetDisplayHostPort(&self, res: *mut nsACString) -> nsresult {
    mozurl_display_host_port(self, &mut der!(res));
    NS_OK
  }

  pub unsafe fn GetDisplaySpec(&self, res: *mut nsACString) -> nsresult {
    mozurl_display_spec(self, &mut der!(res));
    NS_OK
  }

  pub unsafe fn GetDisplayPrePath(&self, res: *mut nsACString) -> nsresult {
    mozurl_display_prepath(self, &mut der!(res));
    NS_OK
  }

  pub unsafe fn Mutate(&self, res: *mut *const nsIURIMutator) -> nsresult {
    mozurl_get_imutator(self, res);
    NS_OK
  }
}
