// XXX: HACK
#![recursion_limit="128"]

#[macro_use]
extern crate quote;

extern crate syn;

extern crate proc_macro;

#[macro_use]
extern crate lazy_static;

use proc_macro::TokenStream;

use quote::Tokens;

use syn::*;

use std::collections::{HashMap, HashSet};
use std::default::Default;

type Result<T> = std::result::Result<T, Box<std::error::Error>>;

#[derive(Debug)]
struct Param {
    name: &'static str,
    ty: &'static str,
}

#[derive(Debug)]
struct Method {
    name: &'static str,
    abi: &'static str,
    params: &'static [Param],
    ret: &'static str,
}

#[derive(Debug)]
struct Interface {
    name: &'static str,
    base: Option<&'static str>,
    methods: Option<&'static [Method]>,
}

lazy_static! {
    static ref IFACES: HashMap<&'static str, &'static Interface> = {
        let lists: &[&[Interface]] =
            include!(concat!(env!("MOZ_TOPOBJDIR"), "/dist/xpcrs/bt/all.rs"));

        let mut hm = HashMap::new();
        for &list in lists {
            for iface in list {
                hm.insert(iface.name, iface);
            }
        }
        hm
    };
}

// Scan the attributes looking for an #[xpimplements] attribute. The identifier
// arguments passed to this attribute are the interfaces which the type wants to
// directly implement.
fn get_bases(attrs: &[Attribute]) -> Result<Vec<&str>> {
    let mut inherits = Vec::new();
    for attr in attrs {
        if let MetaItem::List(ref name, ref items) = attr.value {
            if name != "xpimplements" {
                continue;
            }

            for item in items {
                if let NestedMetaItem::MetaItem(MetaItem::Word(ref iface)) = *item {
                    inherits.push(iface.as_ref());
                } else {
                    return Err("Unexpected non-identifier in xpimplements \
                                attribute list".into());
                }
            }
        }
    }
    Ok(inherits)
}

fn get_fields(di: &DeriveInput) -> Result<&[Field]> {
    match di.body {
        Body::Struct(VariantData::Struct(ref fields)) => Ok(fields),
        _ => Err("The initializer struct must be a standard \
                  named value struct definition".into())
    }
}

fn mk_path_ty(segments: &[&str]) -> Ty {
    Ty::Path(None, Path {
        global: true,
        segments: segments.iter().map(|&seg| {
            PathSegment {
                ident: seg.into(),
                parameters: PathParameters::none(),
            }
        }).collect()
    })
}

fn gen_real_struct(init: &DeriveInput, bases: &[&str]) -> Result<DeriveInput> {
    // Determine the name for the real struct based on the name of the
    // initializer struct's name.
    if !init.ident.as_ref().starts_with("Init") {
        return Err("The target struct's name must begin with Init".into());
    }
    let name: Ident = init.ident.as_ref()[4..].into();

    // Add the vtable and refcnt fields to the struct declaration.
    let mut fields = vec![];
    for base in bases {
        fields.push(Field {
            ident: Some(format!("__base_{}", base).into()),
            vis: Visibility::Inherited,
            attrs: vec![],
            ty: Ty::Ptr(
                Box::new(MutTy {
                    ty: mk_path_ty(&["xpcom", &format!("{}VTable", base)]),
                    mutability: Mutability::Immutable,
                })
            ),
        });
    }
    fields.push(Field {
        ident: Some("__refcnt".into()),
        vis: Visibility::Inherited,
        attrs: vec![],
        ty: mk_path_ty(&["xpcom", "Refcnt"]),
    });

    // Add the data fields from the initializer to the struct declaration.
    fields.extend(get_fields(init)?.iter().cloned());

    // Create the real struct definition
    Ok(DeriveInput {
        ident: name,
        vis: init.vis.clone(),
        attrs: vec![
            // #[repr(C)]
            Attribute {
                style: AttrStyle::Outer,
                value: MetaItem::List(
                    "repr".into(),
                    vec![NestedMetaItem::MetaItem(
                        MetaItem::Word("C".into())
                    )],
                ),
                is_sugared_doc: false,
            }
        ],
        generics: Generics::default(),
        body: Body::Struct(VariantData::Struct(fields)),
    })
}

fn gen_vtable_methods(base: &str) -> Result<Tokens> {
    let base_ty = Ident::from(base);

    let iface = IFACES.get(base)
        .ok_or(format!("Interface {} does not exist", base))?;

    let base_methods = if let Some(base) = iface.base {
        gen_vtable_methods(base)?
    } else {
        quote!{}
    };

    let methods = iface.methods
        .ok_or(format!("Interface {} cannot be implemented in rust", base))?;

    let mut method_defs = Vec::new();
    for method in methods {
        let name = Ident::from(method.name);
        let ret = Ident::from(method.ret);
        let abi = method.abi;

        let mut params = Vec::new();
        let mut args = Vec::new();
        for param in method.params {
            let name = Ident::from(param.name);
            let ty = Ident::from(param.ty);

            params.push(quote!{#name : #ty,});
            args.push(quote!{#name,});
        }

        method_defs.push(quote!{
            unsafe extern #abi fn #name (this: *const #base_ty, #(#params)*) -> #ret {
                let lt = ();
                recover_self(this, &lt).#name(#(#args)*)
            }
        });
    }

    Ok(quote!{
        #base_methods
        #(#method_defs)*
    })
}

fn gen_inner_vtable(base: &str) -> Result<Tokens> {
    let vtable_ty = Ident::from(format!("{}VTable", base));

    let iface = IFACES.get(base)
        .ok_or(format!("Interface {} does not exist", base))?;

    let methods = iface.methods
        .ok_or(format!("Interface {} cannot be implemented in rust", base))?;

    let base_vtable = if let Some(base) = iface.base {
        let vt = gen_inner_vtable(base)?;
        quote!{__base: #vt,}
    } else {
        quote!{}
    };

    let mut vtable_init = Vec::new();
    for method in methods {
        let name = Ident::from(method.name);
        vtable_init.push(quote!{ #name : #name , });
    }

    Ok(quote!(#vtable_ty {
        #base_vtable
        #(#vtable_init)*
    }))
}

fn gen_root_vtable(name: &Ident, base: &str) -> Result<Tokens> {
    let field = Ident::from(format!("__base_{}", base));
    let vtable_ty = Ident::from(format!("{}VTable", base));
    let methods = gen_vtable_methods(base)?;
    let value = gen_inner_vtable(base)?;

    Ok(quote!{#field: {
        // NOTE: The &'a U dummy lifetime parameter is useful as it easily
        // allows the caller to limit the lifetime of the returned parameter
        // to a local lifetime, preventing the calling of methods with
        // receivers like `&'static self`.
        #[inline]
        unsafe fn recover_self<'a, T>(this: *const T, _: &'a ()) -> &'a #name {
            // Calculate the offset of the field in our struct.
            // XXX: Should we use the fact that our type is #[repr(C)] to avoid
            // this?
            let base = 0x1000;
            let member = &(*(0x1000 as *const #name)).#field
                as *const _ as usize;
            let off = member - base;

            // Offset the pointer by that offset.
            &*((this as usize - off) as *const #name)
        }

        #methods

        static VTABLE: #vtable_ty = #value;
        &VTABLE
    },})
}

fn gen_queryinterface(seen: &mut HashSet<String>,
                      base: &str,
                      name: &Ident,
                      coerce_name: &Ident,
                      vtable_field: &Ident)
                      -> Result<(Tokens, Tokens)> {
    if !seen.insert(base.to_owned()) {
        return Ok((quote!{}, quote!{}));
    }

    let (base_qi, base_coerce) = if let Some(base) = IFACES[base].base {
        gen_queryinterface(seen,
                           base,
                           name,
                           coerce_name,
                           vtable_field)?
    } else {
        (quote!{}, quote!{})
    };

    let base_name = Ident::from(base);

    let qi = quote! {
        #base_qi
        if *uuid == #base_name::iid() {
            // Implement QueryInterface in terms of coersions.
            self.addref();
            *result = self.coerce::<#base_name>()
                as *const #base_name
                as *const ::xpcom::reexports::libc::c_void;
            return ::xpcom::reexports::NS_OK;
        }
    };

    let coerce = quote! {
        #base_coerce

        impl #coerce_name for ::xpcom::#base_name {
            fn coerce_from(v: &#name) -> &Self {
                unsafe {
                    // Get the address of the VTable field. This should be a
                    // pointer to a pointer to a vtable, which we can then cast
                    // into a pointer to our interface.
                    &*(&(v.#vtable_field)
                       as *const *const _
                       as *const ::xpcom::#base_name)
                }
            }
        }
    };

    Ok((qi, coerce))
}

fn xpcom(input: &str) -> Result<Tokens> {
    let init = syn::parse_derive_input(input)?;
    if init.generics != Generics::default() {
        return Err("Cannot #[derive(xpcom)] on a generic type, due to \
                    rust limitations. It is not possible to instantiate \
                    a static with a generic type parameter, meaning that \
                    generic types cannot have their VTables instantiated \
                    correctly.".into());
    }

    let bases = get_bases(&init.attrs)?;
    if bases.is_empty() {
        return Err("Types with #[derive(xpcom)] must implement at least one \
                    interface. Interfaces can be implemented by adding the \
                    #[xpinherits(nsIFoo, nsIBar)] attribute to the struct \
                    declaration".into());
    }

    let real = gen_real_struct(&init, &bases)?;

    let name_init = &init.ident;
    let name = &real.ident;
    let coerce_name = Ident::from(format!("{}Coerce", name.as_ref()));

    let mut vtables = Vec::new();
    for base in &bases {
        vtables.push(gen_root_vtable(name, base)?);
    }

    // Generate the field initializers for the final struct, moving each field
    // out of the original __init struct.
    let inits = get_fields(&init)?.iter().map(|field| {
        let id = &field.ident;
        quote!{ #id : __init.#id, }
    });

    // Generate the implementation for QueryInterface
    let mut seen = HashSet::new();
    let mut qi_impl = Vec::new();
    let mut coerce_impl = Vec::new();
    for base in &bases {
        let (qi, coerce) = gen_queryinterface(&mut seen,
                                              base,
                                              name,
                                              &coerce_name,
                                              &Ident::from(format!("__base_{}", base)))?;
        qi_impl.push(qi);
        coerce_impl.push(coerce);
    }

    Ok(quote! {
        #real

        impl #name {
            fn allocate(__init: #name_init) -> ::xpcom::RefPtr<Self> {
                #[allow(unused_imports)]
                use ::xpcom::*;
                #[allow(unused_imports)]
                use ::xpcom::reexports::{libc, nsACString, nsAString, nsresult};

                unsafe {
                    // NOTE: This is split into multiple lines to make the
                    // output more readable.
                    let value = #name {
                        #(#vtables)*
                        __refcnt: ::xpcom::Refcnt::new(),
                        #(#inits)*
                    };
                    let boxed = ::std::boxed::Box::new(value);
                    let raw = ::std::boxed::Box::into_raw(boxed);
                    ::xpcom::RefPtr::from_raw(raw).unwrap()
                }
            }

            /// Automatically generated implementation of AddRef for nsISupports.
            pub unsafe fn AddRef(&self) -> ::xpcom::nsrefcnt {
                self.__refcnt.inc()
            }

            /// Automatically generated implementation of Release for nsISupports.
            pub unsafe fn Release(&self) -> ::xpcom::nsrefcnt {
                let new = self.__refcnt.dec();
                if new == 0 {
                    // XXX: dealloc
                    ::std::boxed::Box::from_raw(self as *const Self as *mut Self);
                }
                new
            }

            /// Automatically generated implementation of QueryInterface for nsISupports.
            pub unsafe fn QueryInterface(&self,
                                         uuid: *const ::xpcom::nsIID,
                                         // XXX: Will this work in crates which
                                         // don't directly import libc?
                                         result: *mut *const ::xpcom::reexports::libc::c_void)
                                         -> ::xpcom::reexports::nsresult {
                #[allow(unused_imports)]
                use ::xpcom::*;

                #(#qi_impl)*

                ::xpcom::reexports::NS_ERROR_NO_INTERFACE
            }

            /// Coerce this type safely to any of the interfaces which it
            /// implements without AddRefing it.
            pub fn coerce<T: #coerce_name>(&self) -> &T {
                T::coerce_from(self)
            }
        }

        /// This trait is implemented on the interface types which this
        /// `#[derive(xpcom)]` type can be safely ane cheaply coerced to using
        /// the `coerce` method.
        ///
        /// The trait and its method should usually not be used directly, but
        /// rather acts as a trait bound and implementation for the `coerce`
        /// method's.
        pub trait #coerce_name {
            /// Convert a value of the `#[derive(xpcom)]` type into the
            /// implementing interface type.
            fn coerce_from(v: &#name) -> &Self;
        }

        #(#coerce_impl)*

        unsafe impl ::xpcom::RefCounted for #name {
            unsafe fn addref(&self) {
                self.AddRef();
            }

            unsafe fn release(&self) {
                self.Release();
            }
        }
    })
}

#[proc_macro_derive(xpcom, attributes(xpimplements))]
pub fn xpcom_internal(input: TokenStream) -> TokenStream {
    let source = input.to_string();
    let out_src = xpcom(&source).unwrap().to_string();
    out_src.parse().unwrap()
}
