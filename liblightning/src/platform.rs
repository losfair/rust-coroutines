use std::ptr::null_mut;
use libc;

lazy_static! {
    pub static ref PAGE_SIZE: usize = {
        unsafe {
            libc::sysconf(libc::_SC_PAGESIZE) as usize
        }
    };
}

fn round_up_stack_size(size: usize) -> usize {
    let page_size: usize = *PAGE_SIZE;

    let rem = size % page_size;
    if rem > 0 {
        size - rem + page_size
    } else {
        size
    }
}

pub unsafe fn setup_stack_guard_page(stack: *mut [u8]) {
    let stack = &mut *stack;
    let ret = libc::mprotect(
        &mut stack[0] as *mut u8 as *mut libc::c_void,
        *PAGE_SIZE,
        libc::PROT_NONE
    );
    if ret != 0 {
        panic!("mprotect failed");
    }
}

pub fn setup_stack(size: usize) -> *mut [u8] {
    if size == 0 {
        panic!("stack size must be greater than zero");
    }

    let size = round_up_stack_size(size);

    let stack = unsafe { libc::mmap(
        null_mut(),
        size,
        libc::PROT_READ | libc::PROT_WRITE,
        libc::MAP_PRIVATE | libc::MAP_ANONYMOUS,
        -1,
        0
    ) as *mut u8 };
    if stack.is_null() {
        panic!("mmap failed");
    }

    unsafe {
        ::std::slice::from_raw_parts_mut(stack, size) as *mut [u8]
    }
}

pub unsafe fn free_stack(stack: *mut [u8]) {
    let stack = &mut *stack;
    let ptr = &mut stack[0] as *mut u8;
    let size = stack.len();
    let ret = libc::munmap(ptr as *mut libc::c_void, size);
    if ret != 0 {
        panic!("munmap failed");
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn stack_size_should_be_rounded_up() {
        let page_size: usize = *PAGE_SIZE;
        assert_eq!(round_up_stack_size(123), page_size);
        assert_eq!(round_up_stack_size(page_size), page_size);
        assert_eq!(round_up_stack_size(page_size + 1), page_size * 2);
        assert_eq!(round_up_stack_size(page_size * 2 - 1), page_size * 2);
        assert_eq!(round_up_stack_size(page_size * 2), page_size * 2);
        assert_eq!(round_up_stack_size(page_size * 2 + 1), page_size * 3);
    }

    #[test]
    fn stack_should_be_writable() {
        const LEN: usize = 20000;

        let stack = setup_stack(LEN);
        for i in 0..LEN {
            unsafe {
                (&mut *stack)[i] = 42;
            }
        }
        unsafe {
            free_stack(stack);
        }
    }

    #[test]
    fn setup_stack_guard_page_should_succeed() {
        let stack = setup_stack(*PAGE_SIZE);
        unsafe {
            setup_stack_guard_page(stack);
            free_stack(stack);
        }
    }
}
