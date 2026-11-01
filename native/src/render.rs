#[cfg(test)]
use crate::tests;
use crate::{
    fonts, generated, memory_checkpoint, overlay, projected_nodes, View, HEIGHT,
    STRIPE_PIXELS, STRIPE_ROWS, WIDTH,
};
use pocketjs_core::{damage::DamageRect, raster, Ui};

pub(super) struct ViewModel;
impl generated::AppViewModel for ViewModel {}
pub(super) fn mount(
    view: &View,
    width: usize,
    height: usize,
) -> Result<(generated::AppApp<'_, ViewModel>, fonts::FontStats), i32> {
    memory_checkpoint(6);
    let mut core = Ui::new_with_layout_capacity(projected_nodes(view));
    #[cfg(test)]
    tests::heap_meter::checkpoint("model-core");
    core.set_viewport(width as f32, height as f32);
    let stats = fonts::load(&mut core, view)?;
    #[cfg(test)]
    tests::heap_meter::checkpoint("fonts");
    memory_checkpoint(1);
    let mut host = microts::Ui::from_core(core);
    let styles = include_bytes!(concat!(env!("BALABALA_GENERATED"), "/styles.bin")).as_slice();
    #[cfg(test)]
    let styles =
        tests::MOUNT_INVALID_STYLES.with(|fail| if fail.replace(false) { &[][..] } else { styles });
    if !host.load_styles(styles) {
        return Err(-2);
    }
    let mut app = generated::AppApp::new(host, view.props(), ViewModel);
    #[cfg(test)]
    tests::heap_meter::checkpoint("tree-mount");
    memory_checkpoint(2);
    app.frame(&microts::Input::default());
    #[cfg(test)]
    assert_eq!(projected_nodes(view), app.ui().core().layout_node_count());
    #[cfg(test)]
    tests::heap_meter::checkpoint("frame-update-layout");
    memory_checkpoint(3);
    app.ui_mut().core_mut().draw();
    #[cfg(test)]
    tests::heap_meter::checkpoint("draw");
    memory_checkpoint(4);
    Ok((app, stats))
}
pub(super) fn render_window(ui: &Ui, pixels: &mut [u16], rect: DamageRect) {
    pixels.fill(0xffff);
    raster::render_scaled_rgb565_window_over(ui, &ui.current_draw_list().words, pixels, 1, rect);
}
fn is_white(pixel: u16) -> bool {
    match pixel {
        0xffff => return true,
        0 => return false,
        _ => {}
    }
    let r = ((pixel >> 11) as u32 & 31) * 255 / 31;
    let g = ((pixel >> 5) as u32 & 63) * 255 / 63;
    let b = (pixel as u32 & 31) * 255 / 31;
    r * 299 + g * 587 + b * 114 >= 128_000
}
pub(super) fn pack_monochrome(pixels: &[u16], bytes: &mut [u8]) {
    for (chunk, byte) in pixels.chunks_exact(8).zip(bytes.iter_mut()) {
        *byte = if chunk == [0xffff; 8] {
            0xff
        } else {
            chunk.iter().enumerate().fold(0, |bits, (i, pixel)| {
                bits | ((is_white(*pixel) as u8) << (7 - i))
            })
        };
    }
}
pub(super) fn render(
    view: &View,
    framebuffer: &mut [u8],
    stripe: &mut [u16],
) -> Result<fonts::FontStats, i32> {
    let stats = raster(view, framebuffer, stripe)?;
    overlay::draw(view, framebuffer);
    #[cfg(test)]
    tests::heap_meter::checkpoint("raster");
    memory_checkpoint(5);
    Ok(stats)
}
/// 只排 MicroTS 文字层；测试用它检查叠加前的文字墨迹位置。
pub(super) fn raster(
    view: &View,
    framebuffer: &mut [u8],
    stripe: &mut [u16],
) -> Result<fonts::FontStats, i32> {
    let (app, stats) = mount(view, WIDTH, HEIGHT)?;
    let ui = app.ui().core();
    for y in (0..HEIGHT).step_by(STRIPE_ROWS) {
        render_window(
            ui,
            stripe,
            DamageRect::new(0, y as i32, WIDTH as i32, (y + STRIPE_ROWS) as i32),
        );
        let start = y * WIDTH / 8;
        pack_monochrome(stripe, &mut framebuffer[start..start + STRIPE_PIXELS / 8]);
    }
    Ok(stats)
}
pub(super) fn update_panel(frame: &mut [u8], status: u32, identity_visible: bool) {
    overlay::panel_status(frame, status);
    overlay::panel_identity(frame, identity_visible);
}
