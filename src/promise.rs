use super::{CoroutineImpl, AnyUserData};
use super::{coroutine_async_enter, coroutine_async_exit, current_coroutine};

pub struct Promise<T: Send + 'static> {
    co: *const CoroutineImpl,
    resolved: bool,
    result: Option<T>
}

unsafe impl<T: Send + 'static> Send for Promise<T> {}

struct Execution<T: Send + 'static, F: FnOnce(Promise<T>) + Send + 'static> {
    promise: Option<Promise<T>>,
    entry: Option<F>
}

impl<T: Send + 'static> Promise<T> {
    extern "C" fn _do_exec<F: FnOnce(Promise<T>) + Send + 'static>(_: *const CoroutineImpl, data: *const AnyUserData) {
        let exec = unsafe { &mut *(data as *mut Execution<T, F>) };
        let promise = exec.promise.take().unwrap();
        let entry = exec.entry.take().unwrap();
        entry(promise);
    }

    pub fn await<F: FnOnce(Promise<T>) + Send + 'static>(f: F) -> T {
        let co = unsafe { current_coroutine() };
        if co.is_null() {
            let (tx, rx) = ::std::sync::mpsc::channel();

            super::fast_spawn(move || {
                tx.send(Self::await(f)).unwrap();
            });

            return rx.recv().unwrap();
        }

        let p = Promise {
            co: co,
            resolved: false,
            result: None
        };

        let mut exec = Execution {
            promise: Some(p),
            entry: Some(f)
        };

        let mut p = unsafe {
            Box::from_raw(
                coroutine_async_enter(
                    co,
                    Self::_do_exec::<F>,
                    &mut exec as *mut Execution<T, F> as *const AnyUserData
                ) as *mut Promise<T>
            )
        };

        p.resolved = true;
        ::std::mem::replace(&mut p.result, None).unwrap()
    }

    pub fn resolve(mut self, result: T) {
        self.result = Some(result);

        unsafe {
            coroutine_async_exit(
                self.co,
                Box::into_raw(Box::new(self)) as *const AnyUserData
            );
        }
    }
}

impl<T: Send + 'static> Drop for Promise<T> {
    fn drop(&mut self) {
        if !self.resolved {
            panic!("Promise dropped without resolve");
        }
    }
}
#[cfg(test)]
mod tests {
    #[test]
    fn test_promise() {
        for _ in 0..100 {
            let result: i32 = super::super::spawn(move || {
                super::Promise::await(move |p| {
                    super::super::spawn(move || {
                        p.resolve(42);
                    });
                })
            }).join().unwrap();
            assert!(result == 42);
        }
    }

    #[test]
    fn test_promise_await_out_of_co() {
        let result: i32 = super::Promise::await(move |p| {
            super::super::spawn(move || {
                p.resolve(42);
            });
        });
        assert!(result == 42);
    }

    #[test]
    fn test_promise_instant_resolve() {
        let result: i32 = super::Promise::await(move |p| {
            p.resolve(42);
        });
        assert!(result == 42);
    }
}
