std::thread_local! { pub(super) static MOUNT_INVALID_STYLES: std::cell::Cell<bool> = const { std::cell::Cell::new(false) }; }

#[test]
fn staged_panel_mount_failure_preserves_frame_and_release_gates() {
    let mut frame = vec![0xa5; FRAMEBUFFER_BYTES];
    let mut stripe = vec![0; STRIPE_PIXELS];
    unsafe {
        for phase in [1, 2] {
            let app = provisioning_memory_app(7, 0, None);
            balabala_app_network_info(app, core::ptr::null(), 0, core::ptr::null(), 0);
            balabala_app_network_phase(app, 42, 7, 1, 3);
            if phase == 2 {
                provisioning_memory_render(app, &mut frame, &mut stripe);
                assert_eq!(balabala_app_display_complete(app, (*app).model.rendering.unwrap()), 0);
                balabala_app_network_info(app, b"PROV_A1B2C3".as_ptr(), 11, b"012345".as_ptr(), 6);
                balabala_app_network_phase(app, 42, 7, 2, 1);
            }
            let before = frame.clone();
            let mut revision = 0;
            MOUNT_INVALID_STYLES.with(|fail| fail.set(true));
            assert_eq!(balabala_app_render(app, frame.as_mut_ptr(), frame.len(), stripe.as_mut_ptr(), stripe.len(), &mut revision), -2);
            assert!(!MOUNT_INVALID_STYLES.with(|fail| fail.get())); /* 实际进入mount/load_styles失败分支。 */
            assert_eq!(frame, before);
            assert_eq!(revision, 0);
            assert!((*app).model.rendering.is_none());
            assert_eq!((*app).panel_cached, phase == 2);
            assert_eq!(balabala_app_network_ready(app, 42, phase, (*app).model.revision), 0);
            balabala_app_network_phase(app, 42, 9, 0, 5);
            assert_ne!(balabala_app_status(app) & (1 << 5), 0);
            if phase == 2 {
                let (_, peak) = heap_meter::measure(|| provisioning_memory_render(app, &mut frame, &mut stripe));
                assert_eq!(peak, 0);
                assert_eq!(heap_meter::largest_request(), 0);
                assert_eq!(balabala_app_display_complete(app, (*app).model.rendering.unwrap()), 0);
            } else {
                assert_eq!(balabala_app_render(app, frame.as_mut_ptr(), frame.len(), stripe.as_mut_ptr(), stripe.len(), &mut revision), -2);
                assert_eq!(frame, before);
            }
            balabala_app_key(app, Key::Home as u32, 1);
            balabala_app_key(app, Key::Home as u32, 0);
            assert_eq!((*app).model.page, Page::Network);
            assert_ne!(balabala_app_status(app) & (1 << 5), 0);
            balabala_app_network_phase(app, 42, 4, 0, 6);
            assert_eq!(balabala_app_status(app) & (1 << 5), 0);
            assert_eq!((*app).model.page, Page::Error);
            balabala_app_key(app, Key::Home as u32, 1);
            assert_eq!((*app).model.page, Page::Home);
            balabala_app_destroy(app);
        }
    }
}

fn provisioning_abi_pbm(name: &str, frame: &[u8]) {
    let dir = std::env::var_os("BALABALA_UI_MEMORY_OUT").map(std::path::PathBuf::from)
        .unwrap_or_else(|| std::path::PathBuf::from(env!("BALABALA_GENERATED")).join("../provisioning-ui-memory-current"))
        .join("abi-pages");
    std::fs::create_dir_all(&dir).unwrap();
    let mut bytes = format!("P4\n{WIDTH} {HEIGHT}\n").into_bytes();
    bytes.extend(frame.iter().map(|b| !b));
    std::fs::write(dir.join(format!("{name}.pbm")), bytes).unwrap();
}

#[test]
fn staged_panel_abi_frames_cover_all_states_storage_and_preparation() {
    let mut frame = vec![0; FRAMEBUFFER_BYTES];
    let mut stripe = vec![0; STRIPE_PIXELS];
    let mut rows = Vec::new();
    unsafe {
        for storage in 0..=2 { for status in 1..=9 {
            let app = provisioning_memory_app(7, storage, None);
            balabala_app_network_info(app, core::ptr::null(), 0, core::ptr::null(), 0);
            assert_eq!(balabala_app_platform_requests(app), 2);
            let mut empty = Vec::new();
            for (stage, current, phase, flags) in [("empty", 7, 1, 3), ("identity", 7, 2, 1),
                ("state", status, 0, if matches!(status, 4..=6) { 2 } else if status == 9 { 5 } else { 1 })] {
                if stage == "identity" {
                    assert_eq!(balabala_app_network_info(app, b"PROV_A1B2C3".as_ptr(), 11, b"012345".as_ptr(), 6), 0);
                }
                let expected = balabala_app_network_phase(app, 42, current, phase, flags);
                provisioning_memory_render(app, &mut frame, &mut stripe);
                assert_eq!((*app).model.rendering, Some(expected));
                // 首次可提交的完整帧已经填好唯一标记，不能先显示空框再补overlay。
                for &(marked, [x, y, w, h]) in PANEL_MARKERS {
                    for row in y..y+h { for col in x..x+w {
                        assert_eq!(frame[row*WIDTH/8+col/8] & (0x80>>(col%8)) == 0, marked == current);
                    }}
                }
                if stage == "empty" { empty = frame.clone(); }
                if stage == "state" && !matches!(status, 1 | 2 | 7) {
                    for &[x,y,w,h] in PANEL_IDENTITIES { for row in y..y+h { for col in x..x+w {
                        let at=row*WIDTH/8+col/8; let bit=0x80>>(col%8);
                        assert_eq!(frame[at]&bit, empty[at]&bit);
                    }}}
                }
                provisioning_abi_pbm(&format!("state-{status}-storage-{storage}-{stage}"), &frame);
                assert_eq!(balabala_app_display_complete(app, expected), 0);
                if phase != 0 { assert_eq!(balabala_app_network_ready(app, 42, phase, expected), 1); }
                rows.push(serde_json::json!({"state":status,"storage":storage,"stage":stage,"generation":42,
                    "status":current,"phase":phase,"flags":flags,"revision":expected}));
            }
            balabala_app_destroy(app);
        }}
    }
    provisioning_memory_output("abi-frames.json", &serde_json::json!({
        "identity":"fixed synthetic PROV_A1B2C3 / 012345", "rows":rows}));
}

// 固定虚构身份；通过实际模型入口和 ABI，终态仍由模型清零身份。
unsafe fn provisioning_memory_app(status: u32, storage: u32, content: Option<&[u8]>) -> *mut App {
    let app = balabala_app_create();
    if let Some(content) = content { (*app).model.import(content).unwrap(); }
    (*app).model.page = Page::Settings;
    (*app).model.selected = 3;
    displayed(&mut (*app).model);
    assert_eq!(balabala_app_key(app, Key::Front4 as u32, 1), 0);
    assert_eq!(balabala_app_key(app, Key::Front4 as u32, 0), 0);
    assert_eq!((*app).model.page, Page::Network);
    assert_eq!((*app).model.network, 7);
    assert_eq!(balabala_app_network_info(app, b"PROV_A1B2C3".as_ptr(), 11,
        b"012345".as_ptr(), 6), 0);
    if status != 7 { assert_eq!(balabala_app_network_status(app, status), 0); }
    assert_eq!(balabala_app_storage_status(app, storage), 0);
    app
}
unsafe fn provisioning_memory_render(app: *mut App, frame: &mut [u8], stripe: &mut [u16]) {
    let mut revision = 0;
    assert_eq!(balabala_app_render(app, frame.as_mut_ptr(), frame.len(),
        stripe.as_mut_ptr(), stripe.len(), &mut revision), 0);
    assert!(revision != 0);
}
fn provisioning_memory_sample(body: impl FnOnce()) -> serde_json::Value {
    let (_, peak) = heap_meter::measure(body);
    let trace = heap_meter::trace();
    assert!(!trace.overflow);
    assert_eq!(trace.stages[trace.count - 1].name, "cleanup");
    assert_eq!(trace.stages[trace.count - 1].live, 0);
    let stages: Vec<_> = trace.stages[..trace.count].iter().map(|s|
        serde_json::json!({"end":s.name,"live":s.live,"peak":s.peak,"largestRequest":s.largest})).collect();
    let requests: Vec<_> = trace.requests[..trace.requests_len].iter().map(|r|
        serde_json::json!({"stage":trace.stages[r.stage].name,"size":r.size,"align":r.align,"count":r.count})).collect();
    serde_json::json!({"peak":peak,"largestRequest":heap_meter::largest_request(),"stages":stages,"requests":requests})
}
fn provisioning_memory_output(name: &str, data: &serde_json::Value) {
    let dir = std::env::var_os("BALABALA_UI_MEMORY_OUT").map(std::path::PathBuf::from)
        .unwrap_or_else(|| std::path::PathBuf::from(env!("BALABALA_GENERATED")).join("../provisioning-ui-memory-current"));
    std::fs::create_dir_all(&dir).unwrap();
    std::fs::write(dir.join(name), serde_json::to_vec_pretty(data).unwrap()).unwrap();
}
#[test]
fn provisioning_ui_memory_real_identity_stages_and_cleanup() {
    let mut frame = vec![0; FRAMEBUFFER_BYTES];
    let mut stripe = vec![0; STRIPE_PIXELS];
    let near = memory_round1_fixture();
    let mut rows = Vec::new();
    for content in [None, Some(near.as_slice())] {
        for status in 1..=9 {
            for storage in 0..=2 {
                unsafe {
                    let app = provisioning_memory_app(status, storage, content);
                    let view = (*app).model.view();
                    assert_eq!(view.lines[0] == "PROV_A1B2C3", matches!(status, 1 | 2 | 7));
                    assert_eq!(view.lines[1] == "012345", matches!(status, 1 | 2 | 7));
                    if storage != 0 { assert_eq!(view.hint, if storage == 1 { "存储不可用" } else { "保存失败" }); }
                    let projected = projected_nodes(&view);
                    let (mounted, _) = mount(&view, WIDTH, HEIGHT).unwrap();
                    let actual = mounted.ui().core().layout_node_count();
                    assert_eq!(projected, actual);
                    drop(mounted);
                    if content.is_none() { pbm(&format!("provisioning-{status}-storage-{storage}"), &view); }
                    drop(view);
                    let render_only = provisioning_memory_sample(|| provisioning_memory_render(app, &mut frame, &mut stripe));
                    let content_bytes = (*app).model.content_bytes();
                    balabala_app_destroy(app);
                    let lifecycle = provisioning_memory_sample(|| {
                        let app = provisioning_memory_app(status, storage, content);
                        provisioning_memory_render(app, &mut frame, &mut stripe);
                        balabala_app_destroy(app);
                    });
                    rows.push(serde_json::json!({"status":status,"storage":storage,
                        "content":if content.is_some(){"near-limit"}else{"builtin"},"contentBytes":content_bytes,
                        "projectedNodes":projected,"layoutNodes":actual,"renderOnly":render_only,"lifecycle":lifecycle}));
                }
            }
        }
    }
    provisioning_memory_output("stages.json", &serde_json::json!({
        "measurement":"64-bit host allocation requests; realloc conservatively overlaps old+new; not C3 heap",
        "renderOnly":"Model already exists; includes View, mount, frame, draw, raster, UI/View drop; excludes fixed framebuffer/stripe",
        "lifecycle":"App Box, Model, optional near-limit import, real identity/status ABI, View/render/drop; excludes input fixture and fixed buffers",
        "rows":rows}));
}
#[test]
fn provisioning_ui_memory_arena_child() {
    let Ok(reserve) = std::env::var("BALABALA_UI_ARENA_RESERVE") else { return; };
    let reserve: usize = reserve.parse().unwrap();
    assert!([0, 8192, 12288].contains(&reserve));
    let status: u32 = std::env::var("BALABALA_UI_ARENA_STATUS").unwrap().parse().unwrap();
    let storage: u32 = std::env::var("BALABALA_UI_ARENA_STORAGE").unwrap().parse().unwrap();
    let mut frame = vec![0; FRAMEBUFFER_BYTES];
    let mut stripe = vec![0; STRIPE_PIXELS];
    unsafe {
        let app = provisioning_memory_app(status, storage, None);
        // 从第一块预占新增无线假设容量；两个区域仍不可相互合并。
        let regions = [17408 - reserve, 8532];
        let stats = font_arena::run(&regions, || {
            provisioning_memory_render(app, &mut frame, &mut stripe);
        });
        balabala_app_destroy(app);
        assert_eq!(stats.restored, 25940 - reserve);
        println!("synthetic render-only: reserve={reserve} regions={regions:?} {stats:?}");
    }
}
#[test]
fn provisioning_ui_memory_arena_sensitivity_report() {
    let exe = std::env::current_exe().unwrap();
    let mut rows = Vec::new();
    for reserve in [0, 8192, 12288] {
        for status in 1..=9 {
            for storage in 0..=2 {
                let output = std::process::Command::new(&exe)
                    .args(["--exact", "tests::provisioning_ui_memory_arena_child", "--nocapture", "--test-threads=1"])
                    .env("BALABALA_UI_ARENA_RESERVE", reserve.to_string())
                    .env("BALABALA_UI_ARENA_STATUS", status.to_string())
                    .env("BALABALA_UI_ARENA_STORAGE", storage.to_string())
                    .current_dir(std::env::temp_dir()).output().unwrap();
                let stderr = String::from_utf8_lossy(&output.stderr);
                assert!(output.status.success() || stderr.contains("memory allocation of"), "{stderr}");
                rows.push(serde_json::json!({"reserve":reserve,"regions":[17408-reserve,8532],
                    "status":status,"storage":storage,"passed":output.status.success(),
                    "processStatus":output.status.to_string(),"stdout":String::from_utf8_lossy(&output.stdout),"stderr":stderr}));
            }
        }
    }
    provisioning_memory_output("arena.json", &serde_json::json!({
        "warning":"Sensitivity assumptions only, not measured Security 1 needs. Synthetic regions do not reproduce C3 layout, allocator or actual free-block distribution. Child OOM is an experimental rejection, not a test pass for the memory budget.",
        "rows":rows}));
}

#[test]
fn staged_panel_fixed_updates_are_zero_allocation_and_touch_only_contract_rectangles() {
    let mut frame = vec![0xff; FRAMEBUFFER_BYTES+1];
    let mut stripe = vec![0; STRIPE_PIXELS];
    unsafe {
        let app = provisioning_memory_app(7,0,None);
        balabala_app_network_info(app,core::ptr::null(),0,core::ptr::null(),0);
        balabala_app_network_phase(app,42,7,1,3);
        let empty_render=provisioning_memory_sample(|| provisioning_memory_render(app,&mut frame,&mut stripe));
        let empty=frame.clone();
        let dir=std::path::Path::new(env!("BALABALA_GENERATED")).join("../pages");
        let mut bytes=format!("P4\n{WIDTH} {HEIGHT}\n").into_bytes();
        bytes.extend(frame[..FRAMEBUFFER_BYTES].iter().map(|b|!b));
        std::fs::write(dir.join("staged-panel-empty.pbm"),bytes).unwrap();
        let revision=(*app).model.rendering.unwrap();
        assert_eq!(balabala_app_display_complete(app,revision),0);
        assert_eq!(balabala_app_network_info(app,b"PROV_A1B2C3".as_ptr(),11,b"012345".as_ptr(),6),0);
        balabala_app_network_phase(app,42,7,2,1);
        let identity_render=provisioning_memory_sample(|| provisioning_memory_render(app,&mut frame,&mut stripe));
        provisioning_memory_output("preparation.json",&serde_json::json!({
            "measurement":"64-bit host actual two preparation renders; includes View/UI drop; fixed buffers and existing Model excluded",
            "empty":empty_render,"identity":identity_render}));
        assert_eq!(balabala_app_display_complete(app,(*app).model.rendering.unwrap()),0);
        let initial=frame.clone();
        let white = |bytes:&[u8],x:usize,y:usize| bytes[y*WIDTH/8+x/8] & (0x80>>(x%8)) != 0;
        let contains = |r:[usize;4],x:usize,y:usize| x>=r[0] && x<r[0]+r[2] && y>=r[1] && y<r[1]+r[3];
        // 每块身份擦除区确有字形；固定框线不在擦除区内。
        for &rect in PANEL_IDENTITIES {
            assert!((rect[1]..rect[1]+rect[3]).any(|y|(rect[0]..rect[0]+rect[2]).any(|x|!white(&initial,x,y))));
        }
        for status in [7,1,2,3,8,4,5,6,9] {
            balabala_app_network_phase(app,42,status,0,1);
            let (_,peak)=heap_meter::measure(|| provisioning_memory_render(app,&mut frame,&mut stripe));
            assert_eq!(peak,0,"active status {status} allocated");
            assert_eq!(heap_meter::largest_request(),0);
            assert_eq!(frame[FRAMEBUFFER_BYTES],0xff);
            for y in 0..HEIGHT { for x in 0..WIDTH {
                let marker=PANEL_MARKERS.iter().find(|(_,r)|contains(*r,x,y));
                if let Some((state,_))=marker { assert_eq!(white(&frame,x,y),*state!=status); }
                else if PANEL_IDENTITIES.iter().any(|r|contains(*r,x,y)) {
                    if !matches!(status,1|2|7) { assert!(white(&frame,x,y)); }
                } else if !PANEL_UPDATES.iter().any(|r| contains(*r,x,y)) {
                    assert_eq!(white(&frame,x,y),white(&initial,x,y),"outside contract at {x},{y}");
                }
                // 身份文字没有溢出擦除区，框线和标签保持与空身份帧相同。
                if !matches!(status,1|2|7) && !PANEL_UPDATES.iter().any(|r| contains(*r,x,y)) {
                    assert_eq!(white(&frame,x,y),white(&empty,x,y),"erase differs from empty identity at {x},{y}");
                }
            }}
            assert!(frame[..FRAMEBUFFER_BYTES].iter().any(|b| *b != 0xff));
            let dir=std::path::Path::new(env!("BALABALA_GENERATED")).join("../pages");
            let mut bytes=format!("P4\n{WIDTH} {HEIGHT}\n").into_bytes();
            bytes.extend(frame[..FRAMEBUFFER_BYTES].iter().map(|b|!b));
            std::fs::write(dir.join(format!("staged-panel-{status}.pbm")),bytes).unwrap();
            // 逻辑擦除尚未ack，ready不能误报；每状态仍走真实revision完成协议。
            assert_ne!(balabala_app_status(app)&2,0);
            assert_eq!(balabala_app_display_complete(app,(*app).model.rendering.unwrap()),0);
        }
        balabala_app_destroy(app);
    }
}
