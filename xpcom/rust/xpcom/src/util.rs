use nserror::{nsresult, NS_OK};
use super::RefPtr;

#[derive(xpcom)]
#[xpimplements(nsIRunnable)]
pub struct InitRunnableFunction {
    // NOTE: For very unfortunate reasons, we cannot use a generic type under
    // derive(xpcom), as we cannot generate vtables in static memory based on
    // generic instantiation. If this is changed, we can change this.
    f: Box<Fn()>
}

impl RunnableFunction {
    pub fn new<T: Fn() + 'static>(f: T) -> RefPtr<RunnableFunction> {
        Self::from_box(Box::new(f))
    }

    pub fn from_box(f: Box<Fn()>) -> RefPtr<Self> {
        Self::allocate(InitRunnableFunction {
            f: f,
        })
    }

    fn run(&self) -> nsresult {
        (self.f)();
        NS_OK
    }
}
