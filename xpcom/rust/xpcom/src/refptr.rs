use std::mem;
use std::ptr;
use std::ops::Deref;
use std::marker::PhantomData;

use nsrefcnt;

/// A trait representing a type which can be reference counted invasively.
/// The object is responsible for freeing its backing memory when its
/// reference count reaches 0.
pub unsafe trait RefCounted {
    unsafe fn addref(&self);
    unsafe fn release(&self);
}

/// A smart pointer holding a RefCounted object. The object itself manages its
/// own memory. RefPtr will invoke the addref and release methods at the
/// appropriate times to facilitate the bookkeeping.
pub struct RefPtr<T: RefCounted + 'static> {
    // We're going to cheat and store the internal reference as an &'static T
    // instead of an *const T or Shared<T>, because Shared and NonZero are
    // unstable, and we need to build on stable rust.
    // I believe that this is "safe enough", as this module is private and
    // no other module can read this reference.
    _ptr: &'static T,
    // As we aren't using Shared<T>, we need to add this phantomdata to
    // prevent unsoundness in dropck
    _marker: PhantomData<T>,
}

impl <T: RefCounted + 'static> RefPtr<T> {
    /// Construct a new RefPtr from a reference to the refcounted object
    #[inline]
    pub fn new(p: &T) -> RefPtr<T> {
        unsafe {
            p.addref();
            RefPtr {
                _ptr: mem::transmute(p),
                _marker: PhantomData,
            }
        }
    }

    /// Construct a RefPtr from a raw pointer, addrefing it.
    #[inline]
    pub unsafe fn from_raw(p: *const T) -> Option<RefPtr<T>> {
        if p.is_null() {
            return None;
        }
        (*p).addref();
        Some(RefPtr {
            _ptr: &*p,
            _marker: PhantomData,
        })
    }

    /// Construct a RefPtr from a raw pointer, without addrefing it.
    #[inline]
    pub unsafe fn from_raw_dont_addref(p: *const T) -> Option<RefPtr<T>> {
        if p.is_null() {
            return None;
        }
        Some(RefPtr {
            _ptr: &*p,
            _marker: PhantomData,
        })
    }
}

impl <T: RefCounted + 'static> Deref for RefPtr<T> {
    type Target = T;
    #[inline]
    fn deref(&self) -> &T {
        self._ptr
    }
}

impl <T: RefCounted + 'static> Drop for RefPtr<T> {
    #[inline]
    fn drop(&mut self) {
        unsafe {
            self._ptr.release();
        }
    }
}

impl <T: RefCounted + 'static> Clone for RefPtr<T> {
    #[inline]
    fn clone(&self) -> RefPtr<T> {
        RefPtr::new(self)
    }
}

/// A helper struct for constructing RefPtr<T> from raw pointer outparameters.
/// Holds a *const T internally which will be released if non null when
/// destructed, and can be easily transformed into an Option<RefPtr<T>>.
pub struct GetterAddrefs<T: RefCounted + 'static> {
    _ptr: *const T,
    _marker: PhantomData<T>,
}

impl <T: RefCounted + 'static> GetterAddrefs<T> {
    /// Create a GetterAddrefs, initializing it with the null pointer.
    #[inline]
    pub fn new() -> GetterAddrefs<T> {
        GetterAddrefs {
            _ptr: ptr::null(),
            _marker: PhantomData,
        }
    }

    /// Get a reference to the internal *const T. This method is unsafe,
    /// as the destructor of this class depends on the internal *const T
    /// being either a valid reference to a value of type T, or null.
    #[inline]
    pub unsafe fn ptr(&mut self) -> &mut *const T {
        &mut self._ptr
    }

    /// Transform this GetterAddrefs into an Option<RefPtr<T>>, without
    /// performing any addrefs or releases.
    #[inline]
    pub fn refptr(self) -> Option<RefPtr<T>> {
        let p = self._ptr;
        // Don't run the destructor because we don't want to release the stored
        // pointer.
        mem::forget(self);
        unsafe {
            RefPtr::from_raw_dont_addref(p)
        }
    }
}

impl <T: RefCounted + 'static> Drop for GetterAddrefs<T> {
    #[inline]
    fn drop(&mut self) {
        if !self._ptr.is_null() {
            unsafe {
                (*self._ptr).release();
            }
        }
    }
}
