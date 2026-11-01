// 每个测试线程独立计量请求字节，避免其他并行测试污染字体/渲染峰值。
pub(super) mod heap_meter {
    use std::{
        alloc::{GlobalAlloc, Layout, System},
        cell::Cell,
    };
    thread_local! {static STATE:Cell<(bool,usize,usize,usize)>=const{Cell::new((false,0,0,0))};}
    #[derive(Clone, Copy, Default)]
    pub struct Stage {
        pub name: &'static str,
        pub live: usize,
        pub peak: usize,
        pub largest: usize,
    }
    #[derive(Clone, Copy, Default)]
    pub struct Growth {
        pub stage: usize,
        pub old: usize,
        pub new: usize,
        pub overlapping_live: usize,
    }
    #[derive(Clone, Copy, Default)]
    pub struct Request {
        pub stage: usize,
        pub size: usize,
        pub align: usize,
        pub count: usize,
    }
    #[derive(Clone, Copy)]
    pub struct Trace {
        pub requests: [Request; 1024],
        pub requests_len: usize,
        pub stages: [Stage; 12],
        pub count: usize,
        pub events: [Growth; 32],
        pub events_len: usize,
        pub overflow: bool,
        pub peak: usize,
        pub largest: usize,
    }
    const EMPTY: Trace = Trace {
        requests: [Request {
            stage: 0,
            size: 0,
            align: 0,
            count: 0,
        }; 1024],
        requests_len: 0,
        stages: [Stage {
            name: "",
            live: 0,
            peak: 0,
            largest: 0,
        }; 12],
        count: 0,
        events: [Growth {
            stage: 0,
            old: 0,
            new: 0,
            overlapping_live: 0,
        }; 32],
        events_len: 0,
        overflow: false,
        peak: 0,
        largest: 0,
    };
    thread_local! {static TRACE:Cell<Trace>=const{Cell::new(EMPTY)};}
    pub fn checkpoint(name: &'static str) {
        STATE.with(|s| {
            let (on, live, _, _) = s.get();
            if !on {
                return;
            }
            TRACE.with(|t| {
                let mut trace = t.get();
                if trace.count < trace.stages.len() {
                    trace.stages[trace.count] = Stage {
                        name,
                        live,
                        peak: trace.peak,
                        largest: trace.largest,
                    };
                    trace.count += 1;
                } else {
                    trace.overflow = true;
                }
                trace.peak = live;
                trace.largest = 0;
                t.set(trace);
            });
        });
    }
    pub fn trace() -> Trace {
        TRACE.with(Cell::get)
    }
    pub struct Meter;
    fn allocated(bytes: usize, align: usize) {
        let _ = STATE.try_with(|s| {
            let (on, live, peak, block) = s.get();
            if on {
                s.set((on, live + bytes, peak.max(live + bytes), block.max(bytes)));
                TRACE.with(|t| {
                    let mut trace = t.get();
                    trace.peak = trace.peak.max(live + bytes);
                    trace.largest = trace.largest.max(bytes);
                    if let Some(request) = trace.requests[..trace.requests_len]
                        .iter_mut()
                        .find(|r| r.stage == trace.count && r.size == bytes && r.align == align)
                    {
                        request.count += 1;
                    } else if trace.requests_len < trace.requests.len() {
                        trace.requests[trace.requests_len] = Request {
                            stage: trace.count,
                            size: bytes,
                            align,
                            count: 1,
                        };
                        trace.requests_len += 1;
                    } else {
                        trace.overflow = true;
                    }
                    t.set(trace);
                });
            }
        });
    }
    fn freed(bytes: usize) {
        let _ = STATE.try_with(|s| {
            let (on, live, peak, block) = s.get();
            if on {
                s.set((
                    on,
                    live.checked_sub(bytes).expect("untracked free"),
                    peak,
                    block,
                ));
            }
        });
    }
    unsafe impl GlobalAlloc for Meter {
        unsafe fn alloc(&self, l: Layout) -> *mut u8 {
            let p = super::font_arena::alloc(l).unwrap_or_else(|| System.alloc(l));
            if !p.is_null() {
                allocated(l.size(), l.align());
            }
            p
        }
        unsafe fn alloc_zeroed(&self, l: Layout) -> *mut u8 {
            let p = match super::font_arena::alloc(l) {
                Some(p) => {
                    if !p.is_null() {
                        p.write_bytes(0, l.size());
                    }
                    p
                }
                None => System.alloc_zeroed(l),
            };
            if !p.is_null() {
                allocated(l.size(), l.align());
            }
            p
        }
        unsafe fn dealloc(&self, p: *mut u8, l: Layout) {
            freed(l.size());
            if !super::font_arena::dealloc(p, l) {
                System.dealloc(p, l);
            }
        }
        unsafe fn realloc(&self, p: *mut u8, l: Layout, n: usize) -> *mut u8 {
            let new = if super::font_arena::owns(p) {
                let new = super::font_arena::alloc(Layout::from_size_align(n, l.align()).unwrap())
                    .unwrap();
                if !new.is_null() {
                    core::ptr::copy_nonoverlapping(p, new, l.size().min(n));
                    assert!(super::font_arena::dealloc(p, l));
                }
                new
            } else {
                System.realloc(p, l, n)
            };
            if !new.is_null() {
                allocated(n, l.align());
                STATE.with(|s| {
                    let (on, live, _, _) = s.get();
                    if on && (n >= 8192 || l.size() >= 8192) {
                        TRACE.with(|t| {
                            let mut trace = t.get();
                            if trace.events_len < trace.events.len() {
                                trace.events[trace.events_len] = Growth {
                                    stage: trace.count,
                                    old: l.size(),
                                    new: n,
                                    overlapping_live: live,
                                };
                                trace.events_len += 1;
                            } else {
                                trace.overflow = true;
                            }
                            t.set(trace);
                        });
                    }
                });
                freed(l.size());
            }
            new
        }
    }
    #[global_allocator]
    static ALLOCATOR: Meter = Meter;
    pub fn measure<T>(f: impl FnOnce() -> T) -> (T, usize) {
        TRACE.with(|s| s.set(EMPTY));
        STATE.with(|s| s.set((true, 0, 0, 0)));
        let result = f();
        checkpoint("cleanup");
        let peak = STATE.with(|s| {
            let (_, live, peak, block) = s.get();
            s.set((false, live, peak, block));
            peak
        });
        (result, peak)
    }
    pub fn largest_request() -> usize {
        STATE.with(|s| s.get().3)
    }
}
