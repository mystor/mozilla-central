use RefCounted;

#[repr(C)]
#[derive(Copy, Clone, Eq, PartialEq)]
pub struct nsID(pub u32, pub u16, pub u16, pub [u8; 8]);
pub type nsIID = nsID;
pub type nsCID = nsID;

/// A type which implements XpCom must follow the following rules:
///
/// * It must be a legal XPCOM interface.
/// * The result of a QueryInterface or similar call, passing the nsIID returned
///   from iid(), must return a valid reference to an object of the given type.
pub unsafe trait XpCom : RefCounted {
    // XXX: This should be an associated constant, but those are still unstable,
    // so a static method it is!
    // NOTE: When associated constants are stabilized, change this.
    fn iid() -> nsIID;
}
