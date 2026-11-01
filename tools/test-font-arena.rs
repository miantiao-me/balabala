// 测试专用、每线程隔离的多区域 arena；不模拟 C3 的对象布局或 allocator 元数据。
use std::{alloc::Layout, cell::UnsafeCell};
const BYTES: usize = 256 * 1024;
const BLOCKS: usize = 1024;
#[derive(Clone, Copy)]
struct Block { start: usize, end: usize, region: usize }
const EMPTY: Block = Block { start: 0, end: 0, region: 0 };
#[repr(align(4096))]
struct Storage([u8; BYTES]);
struct Arena {
    storage: Storage,
    free: [Block; BLOCKS],
    count: usize,
    active: bool,
    live: usize,
    peak: usize,
    largest: usize,
}
thread_local! {
    static ARENA: UnsafeCell<Arena> = const { UnsafeCell::new(Arena {
        storage: Storage([0; BYTES]), free: [EMPTY; BLOCKS], count: 0,
        active: false, live: 0, peak: 0, largest: 0,
    }) };
}
impl Arena {
    fn insert(&mut self, at: usize, block: Block) {
        assert!(self.count < BLOCKS);
        self.free.copy_within(at..self.count, at + 1);
        self.free[at] = block;
        self.count += 1;
    }
    fn remove(&mut self, at: usize) {
        self.free.copy_within(at + 1..self.count, at);
        self.count -= 1;
    }
}
pub fn alloc(layout: Layout) -> Option<*mut u8> {
    ARENA.try_with(|state| unsafe {
        let a = &mut *state.get();
        if !a.active { return None; }
        a.largest = a.largest.max(layout.size());
        let base = a.storage.0.as_mut_ptr() as usize;
        for i in 0..a.count {
            let b = a.free[i];
            let start = (base + b.start + layout.align() - 1) & !(layout.align() - 1);
            let start = start - base;
            let end = start + layout.size();
            if end > b.end { continue; }
            a.remove(i);
            if end < b.end { a.insert(i, Block { start: end, ..b }); }
            if start > b.start { a.insert(i, Block { end: start, ..b }); }
            a.live += layout.size();
            a.peak = a.peak.max(a.live);
            return Some((base + start) as *mut u8);
        }
        Some(std::ptr::null_mut())
    }).ok().flatten()
}
pub fn dealloc(ptr: *mut u8, layout: Layout) -> bool {
    ARENA.try_with(|state| unsafe {
        let a = &mut *state.get();
        let base = a.storage.0.as_ptr() as usize;
        let p = ptr as usize;
        if p < base || p >= base + BYTES { return false; }
        assert!(a.active);
        let start = p - base;
        let end = start + layout.size();
        let i = a.free[..a.count].partition_point(|b| b.start < start);
        // Determine region from immutable boundaries, retained separately below.
        let region = REGIONS.with(|r| (&*r.get()).partition_point(|boundary| *boundary <= start));
        a.insert(i, Block { start, end, region });
        if i + 1 < a.count && a.free[i].end == a.free[i + 1].start && a.free[i].region == a.free[i + 1].region {
            a.free[i].end = a.free[i + 1].end;
            a.remove(i + 1);
        }
        if i > 0 && a.free[i - 1].end == a.free[i].start && a.free[i - 1].region == a.free[i].region {
            a.free[i - 1].end = a.free[i].end;
            a.remove(i);
        }
        a.live -= layout.size();
        true
    }).unwrap_or(false)
}
thread_local! { static REGIONS: UnsafeCell<[usize; 16]> = const { UnsafeCell::new([usize::MAX;16]) }; }
pub fn owns(ptr: *mut u8) -> bool {
    ARENA.try_with(|state| unsafe {
        let base = (*state.get()).storage.0.as_ptr() as usize;
        (base..base + BYTES).contains(&(ptr as usize))
    }).unwrap_or(false)
}
#[derive(Debug)]
pub struct Stats { pub peak: usize, pub largest: usize, pub restored: usize }
pub fn run(regions: &[usize], f: impl FnOnce()) -> Stats {
    assert!(regions.len() <= 16 && regions.iter().sum::<usize>() <= BYTES);
    ARENA.with(|state| unsafe {
        let a = &mut *state.get();
        assert!(!a.active);
        a.count = 0; a.live = 0; a.peak = 0; a.largest = 0;
        let mut start = 0;
        REGIONS.with(|r| {
            *r.get() = [usize::MAX; 16];
            for (region, &len) in regions.iter().enumerate() {
                a.insert(region, Block { start, end: start + len, region });
                start += len;
                (*r.get())[region] = start;
            }
        });
        a.active = true;
    });
    f();
    ARENA.with(|state| unsafe {
        let a = &mut *state.get();
        a.active = false;
        assert_eq!(a.live, 0, "arena lifecycle leaked allocations");
        assert_eq!(a.count, regions.len(), "arena failed to coalesce");
        for (b, &len) in a.free[..a.count].iter().zip(regions) { assert_eq!(b.end - b.start, len); }
        Stats { peak: a.peak, largest: a.largest, restored: a.free[..a.count].iter().map(|b| b.end-b.start).sum() }
    })
}
