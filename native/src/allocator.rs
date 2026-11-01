use core::{
    alloc::{GlobalAlloc, Layout},
    ffi::c_void,
    fmt::Write,
};
extern "C" {
    fn balabala_alloc(size: usize, align: usize) -> *mut c_void;
    fn balabala_free(ptr: *mut c_void);
    fn balabala_fatal(message: *const u8, length: usize) -> !;
}
struct Allocator;
unsafe impl GlobalAlloc for Allocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        // C 端 OOM 记录原因并 abort；不让空指针进入 Rust 容器。
        balabala_alloc(layout.size(), layout.align()).cast()
    }
    unsafe fn dealloc(&self, ptr: *mut u8, _: Layout) {
        balabala_free(ptr.cast());
    }
}
#[global_allocator]
static ALLOCATOR: Allocator = Allocator;

struct Message {
    bytes: [u8; 192],
    len: usize,
}
impl Write for Message {
    fn write_str(&mut self, s: &str) -> core::fmt::Result {
        let count = s.len().min(self.bytes.len() - self.len);
        self.bytes[self.len..self.len + count].copy_from_slice(&s.as_bytes()[..count]);
        self.len += count;
        Ok(())
    }
}
#[panic_handler]
fn panic(info: &core::panic::PanicInfo) -> ! {
    let mut message = Message {
        bytes: [0; 192],
        len: 0,
    };
    let _ = write!(message, "{info}");
    unsafe { balabala_fatal(message.bytes.as_ptr(), message.len) }
}
