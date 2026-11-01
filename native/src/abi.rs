use crate::{model, render::*, App, FRAMEBUFFER_BYTES, STRIPE_PIXELS};
use alloc::boxed::Box;
use core::slice;

fn buffers_valid(framebuffer: *mut u8, bytes: usize, stripe: *mut u16, pixels: usize) -> bool {
    !framebuffer.is_null()
        && !stripe.is_null()
        && bytes >= FRAMEBUFFER_BYTES
        && pixels >= STRIPE_PIXELS
        && stripe as usize % 2 == 0
}
/// 有效、互不重叠的缓冲；保留旧探针入口以供回归与平台初始链接。
#[no_mangle]
pub unsafe extern "C" fn balabala_probe_render(
    framebuffer: *mut u8,
    bytes: usize,
    stripe: *mut u16,
    pixels: usize,
) -> i32 {
    if !buffers_valid(framebuffer, bytes, stripe, pixels) {
        return -1;
    }
    let model = model::Model::new();
    match render(
        &model.view(),
        slice::from_raw_parts_mut(framebuffer, FRAMEBUFFER_BYTES),
        slice::from_raw_parts_mut(stripe, STRIPE_PIXELS),
    ) {
        Ok(_) => 0,
        Err(e) => e,
    }
}
#[no_mangle]
pub extern "C" fn balabala_app_create() -> *mut App {
    Box::into_raw(Box::new(App {
        model: model::Model::new(),
        panel_cached: false,
        panel_generation: 0,
        panel_identity_generation: 0,
    }))
}
#[no_mangle]
pub unsafe extern "C" fn balabala_app_destroy(app: *mut App) {
    if !app.is_null() {
        drop(Box::from_raw(app));
    }
}
#[no_mangle]
pub unsafe extern "C" fn balabala_app_key(app: *mut App, key: u32, pressed: u32) -> i32 {
    let (Some(app), Some(key)) = (app.as_mut(), model::Key::from_u32(key)) else {
        return -1;
    };
    if pressed > 1 {
        return -1;
    }
    app.model.key(key, pressed != 0);
    0
}
#[no_mangle]
pub unsafe extern "C" fn balabala_app_storage_status(app: *mut App, status: u32) -> i32 {
    match app.as_mut() {
        Some(app) if status <= 2 => {
            app.model.storage_status(status);
            0
        }
        _ => -1,
    }
}
#[no_mangle]
pub unsafe extern "C" fn balabala_app_status(app: *const App) -> u32 {
    let Some(app) = app.as_ref() else {
        return 0;
    };
    let m = &app.model;
    (m.revision != m.displayed) as u32
        | ((m.rendering.is_some() as u32) << 1)
        | ((m.dirty as u32) << 2)
        | ((m.download_active() as u32) << 3)
        | (((m.page == model::Page::DownloadPrompt) as u32) << 4)
        | ((m.network_owned as u32) << 5)
}
#[no_mangle]
pub unsafe extern "C" fn balabala_app_render(
    app: *mut App,
    framebuffer: *mut u8,
    bytes: usize,
    stripe: *mut u16,
    pixels: usize,
    revision: *mut u64,
) -> i32 {
    if !buffers_valid(framebuffer, bytes, stripe, pixels) || revision.is_null() {
        return -1;
    }
    let Some(app) = app.as_mut() else {
        return -1;
    };
    let Some(rev) = app.model.begin_render() else {
        return 1;
    };
    let panel = app.model.page == model::Page::Network;
    let identity_frame = panel
        && app.model.network_phase == 2
        && app.panel_identity_generation != app.model.network_generation;
    let frame = slice::from_raw_parts_mut(framebuffer, FRAMEBUFFER_BYTES);
    let result = if panel
        && app.panel_cached
        && app.panel_generation == app.model.network_generation
        && !identity_frame
    {
        update_panel(
            frame,
            app.model.network,
            app.model.network_identity_visible(),
        );
        Ok(())
    } else if app.model.network_owned && (!panel || !matches!(app.model.network_phase, 1 | 2)) {
        Err(-2)
    } else {
        // mount失败发生在写帧之前；raster开始后没有可恢复的失败分支。
        render(
            &app.model.view(),
            frame,
            slice::from_raw_parts_mut(stripe, STRIPE_PIXELS),
        )
        .map(|_| {
            app.panel_cached = panel;
            app.panel_generation = app.model.network_generation;
            if panel {
                if identity_frame {
                    app.panel_identity_generation = app.model.network_generation;
                }
                crate::overlay::panel_identity(frame, app.model.network_identity_visible());
            }
        })
    };
    match result {
        Ok(_) => {
            *revision = rev;
            0
        }
        Err(error) => {
            app.model.rendering = None;
            error
        }
    }
}
/// 平台已确认网络释放后请求休眠屏，返回待全刷 revision；0 表示模型门禁拒绝。
#[no_mangle]
pub unsafe extern "C" fn balabala_app_sleep_ready(app: *mut App) -> u64 {
    app.as_mut()
        .and_then(|a| a.model.sleep_ready())
        .unwrap_or(0)
}
/// 平台因接入外部供电取消自动休眠；撤销休眠帧的确认资格并回首页。
#[no_mangle]
pub unsafe extern "C" fn balabala_app_sleep_cancel(app: *mut App) -> i32 {
    let Some(app) = app.as_mut() else {
        return -1;
    };
    app.model.sleep_cancel() as i32
}
/// 返回专属提示页 revision；0 表示事务已取消或进度仍未保存。
#[no_mangle]
pub unsafe extern "C" fn balabala_app_download_ready(app: *mut App) -> u64 {
    app.as_mut()
        .and_then(|a| a.model.download_ready())
        .unwrap_or(0)
}
#[no_mangle]
pub unsafe extern "C" fn balabala_app_download_failed(app: *mut App, reason: u32) -> i32 {
    let Some(app) = app.as_mut() else {
        return -1;
    };
    if !(1..=13).contains(&reason) {
        return -1;
    }
    app.model.download_failed(reason);
    0
}
/// 平台卡组读取失败；保留更具体的既有错误，不能用路径或errno作页面文案。
#[no_mangle]
pub unsafe extern "C" fn balabala_app_storage_error(app: *mut App) -> i32 {
    let Some(app) = app.as_mut() else {
        return -1;
    };
    if app.model.page != model::Page::Error {
        app.model.show_error("卡组无法读取，请检查存储卡");
    }
    0
}
#[no_mangle]
pub unsafe extern "C" fn balabala_app_refresh_partial(app: *const App) -> u32 {
    app.as_ref().is_some_and(|app| app.model.refresh_partial()) as u32
}
#[no_mangle]
pub unsafe extern "C" fn balabala_app_clear_interval(app: *const App) -> u32 {
    app.as_ref().map_or(8, |app| u32::from(app.model.state.clear_interval))
}
#[no_mangle]
pub unsafe extern "C" fn balabala_app_display_complete(app: *mut App, revision: u64) -> i32 {
    match app.as_mut() {
        Some(app) => {
            if app.model.complete(revision) {
                0
            } else {
                -1
            }
        }
        _ => -1,
    }
}
#[no_mangle]
pub unsafe extern "C" fn balabala_app_curriculum_info(bytes: *mut u32, crc: *mut u32) -> i32 {
    if bytes.is_null() || crc.is_null() { return -1; }
    *bytes = model::course::COURSE_BYTES;
    *crc = model::course::COURSE_CRC;
    i32::from(model::course::COURSE_CARDS.is_empty())
}
/// reader/context由同一平台任务持有至app销毁，不得并发或重入调用app。
#[no_mangle]
pub unsafe extern "C" fn balabala_app_curriculum_attach(
    app: *mut App, reader: Option<model::course::CourseRead>, context: *mut core::ffi::c_void,
    bytes: u32, crc: u32,
) -> i32 {
    let (Some(app),Some(read)) = (app.as_mut(),reader) else { return -1; };
    if app.model.network_owned { return -1; }
    match app.model.attach_course(model::course::CourseReader{read,context},bytes,crc) {
        Ok(()) => 0,
        Err(error) => { app.model.show_error(error); -2 }
    }
}
#[no_mangle]
pub unsafe extern "C" fn balabala_app_import_deck(
    app: *mut App,
    bytes: *const u8,
    length: usize,
) -> i32 {
    let Some(app) = app.as_mut() else {
        return -1;
    };
    if length > model::MAX_PACKAGE {
        app.model.show_error("卡组超过大小上限");
        return -1;
    }
    if bytes.is_null() && length > 0 {
        return -1;
    }
    let data = if length == 0 {
        &[]
    } else {
        slice::from_raw_parts(bytes, length)
    };
    match app.model.import(data) {
        Ok(()) => 0,
        Err(error) => {
            app.model.show_error(error);
            -2
        }
    }
}
unsafe fn input_bytes<'a>(ptr: *const u8, len: usize) -> Option<&'a [u8]> {
    if len > model::MAX_SAVE {
        return None;
    }
    if len == 0 {
        Some(&[])
    } else if ptr.is_null() {
        None
    } else {
        Some(slice::from_raw_parts(ptr, len))
    }
}
#[no_mangle]
pub unsafe extern "C" fn balabala_app_load(
    app: *mut App,
    a: *const u8,
    alen: usize,
    b: *const u8,
    blen: usize,
) -> i32 {
    let (Some(app), Some(a), Some(b)) = (app.as_mut(), input_bytes(a, alen), input_bytes(b, blen))
    else {
        return -1;
    };
    if app.model.load(a, b) {
        0
    } else {
        if app.model.page != model::Page::Error {
            app.model.show_error("存档不可用，已保留当前进度");
        }
        1
    }
}
#[no_mangle]
pub unsafe extern "C" fn balabala_app_save_protect(app: *mut App) -> i32 {
    let Some(app) = app.as_mut() else {
        return -1;
    };
    app.model.save_protect();
    0
}
#[no_mangle]
pub unsafe extern "C" fn balabala_app_save_export(
    app: *mut App,
    out: *mut u8,
    capacity: usize,
    slot: *mut u32,
    generation: *mut u64,
) -> i32 {
    if out.is_null() || capacity < model::MAX_SAVE || slot.is_null() || generation.is_null() {
        return -1;
    }
    let Some(app) = app.as_mut() else {
        return -1;
    };
    match app
        .model
        .export_into(slice::from_raw_parts_mut(out, capacity))
    {
        Ok((s, g, len)) => {
            *slot = s;
            *generation = g;
            len as i32
        }
        Err(_) => -3,
    }
}
#[no_mangle]
pub unsafe extern "C" fn balabala_app_save_ack(
    app: *mut App,
    generation: u64,
    success: u32,
) -> i32 {
    if success > 1 {
        return -1;
    }
    match app.as_mut() {
        Some(app) => {
            if app.model.save_ack(generation, success == 1) {
                0
            } else {
                -1
            }
        }
        _ => -1,
    }
}
#[no_mangle]
pub unsafe extern "C" fn balabala_app_platform_requests(app: *mut App) -> u32 {
    let Some(app) = app.as_mut() else {
        return 0;
    };
    let result = app.model.platform_requests;
    app.model.platform_requests = 0;
    result
}
#[no_mangle]
pub unsafe extern "C" fn balabala_app_network_shutdown_blocked(app: *mut App) -> i32 {
    let Some(app) = app.as_mut() else {
        return -1;
    };
    app.model.network_shutdown_blocked();
    0
}
/// flags: bit0会话活动，bit1资源确认释放，bit2锁止重开。
#[no_mangle]
pub unsafe extern "C" fn balabala_app_network_phase(
    app: *mut App,
    generation: u32,
    status: u32,
    phase: u32,
    flags: u32,
) -> u64 {
    let Some(app) = app.as_mut() else {
        return 0;
    };
    app.model.network_phase(generation, status, phase, flags)
}
#[no_mangle]
pub unsafe extern "C" fn balabala_app_network_ready(
    app: *const App,
    generation: u32,
    phase: u32,
    revision: u64,
) -> u32 {
    app.as_ref()
        .is_some_and(|a| a.model.network_ready(generation, phase, revision)) as u32
}
#[no_mangle]
pub unsafe extern "C" fn balabala_app_network_save_failed(app: *mut App) -> i32 {
    let Some(app) = app.as_mut() else {
        return -1;
    };
    app.model.network_owned = false;
    app.model.show_error("进度保存失败，无法开始配网");
    0
}
#[no_mangle]
pub unsafe extern "C" fn balabala_app_network_status(app: *mut App, status: u32) -> i32 {
    match app.as_mut() {
        Some(app) => {
            if app.model.network_status(status) {
                0
            } else {
                -1
            }
        }
        _ => -1,
    }
}
#[no_mangle]
pub unsafe extern "C" fn balabala_app_network_info(
    app: *mut App,
    name: *const u8,
    name_len: usize,
    pop: *const u8,
    pop_len: usize,
) -> i32 {
    let Some(app) = app.as_mut() else {
        return -1;
    };
    if name_len == 0 && pop_len == 0 {
        return if app.model.network_info(&[], &[]) {
            0
        } else {
            -1
        };
    }
    if name_len != 11 || pop_len != 6 || name.is_null() || pop.is_null() {
        return -1;
    }
    if app.model.network_info(
        slice::from_raw_parts(name, name_len),
        slice::from_raw_parts(pop, pop_len),
    ) {
        0
    } else {
        -1
    }
}
