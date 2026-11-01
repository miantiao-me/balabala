use super::*;
use model::{Key, Mode, Model, Page};
mod font_arena {
    include!("test-font-arena.rs");
}
include!("test-heap-meter.rs");
fn displayed(m: &mut Model) {
    let r = m.begin_render().unwrap();
    assert!(m.complete(r));
}
fn tap(m: &mut Model, key: Key) {
    m.key(key, true);
    m.key(key, false);
}
fn reset_answer(view: &View) -> Key {
    let numbers: Vec<u32> = view.hero.split_whitespace().filter_map(|s| s.parse().ok()).collect();
    assert_eq!(numbers.len(), 3);
    let answer = numbers[0] * numbers[1] + numbers[2];
    let position = view.options.iter().position(|s| s.parse::<u32>() == Ok(answer)).unwrap();
    Key::from_u32(position as u32).unwrap()
}
fn begin_reset(m: &mut Model) {
    tap(m, Key::Home);
    if !m.ready() { displayed(m); }
    m.key(Key::Front3, true);
    m.key(Key::Parent, true);
    m.key(Key::Front3, false);
    assert_eq!(m.page, Page::Settings);
    tap(m, Key::Down);
    tap(m, Key::Down);
    displayed(m);
    tap(m, Key::Front4);
    assert_eq!(m.page, Page::Reset);
}
fn pass_reset_check(m: &mut Model) {
    for _ in 0..3 {
        if !m.ready() { displayed(m); }
        let answer = reset_answer(&m.view());
        tap(m, answer);
    }
    assert_eq!((m.page, m.selected), (Page::Reset, 0));
}
fn quiz() -> Model {
    let mut m = Model::new();
    m.start(0, Mode::Quiz, false);
    // 既有评分/按键用例固定题面；选项洗牌及恢复由课程用例独立覆盖。
    m.state.round.as_mut().unwrap().permutations.clear();
    displayed(&mut m);
    m
}
fn answer_position(m: &Model) -> u8 {
    let r = m.state.round.as_ref().unwrap();
    r.option_order(r.cursor).iter().position(|p| *p == m.card().unwrap().correct).unwrap() as u8
}
fn pbm(name: &str, view: &View) -> fonts::FontStats {
    let mut framebuffer = vec![0xff; FRAMEBUFFER_BYTES + 1];
    let mut stripe = vec![0; STRIPE_PIXELS];
    let stats = render(view, &mut framebuffer, &mut stripe).unwrap();
    assert_eq!(framebuffer[FRAMEBUFFER_BYTES], 0xff);
    if !(view.download || view.provisioning || view.screen != layout::SCREEN_LEGACY) {
        assert_page_rules(&framebuffer[..FRAMEBUFFER_BYTES]);
    }
    let black: u32 = framebuffer[..FRAMEBUFFER_BYTES]
        .iter()
        .map(|b| 8 - b.count_ones())
        .sum();
    assert!(black > 500, "blank page {name}");
    let mut pbm = format!("P4\n{WIDTH} {HEIGHT}\n").into_bytes();
    pbm.extend(framebuffer[..FRAMEBUFFER_BYTES].iter().map(|b| !b));
    let dir = std::path::Path::new(env!("BALABALA_GENERATED")).join("../pages");
    std::fs::create_dir_all(&dir).unwrap();
    std::fs::write(dir.join(format!("{name}.pbm")), pbm).unwrap();
    std::fs::write(
        dir.join(format!("{name}.view.json")),
        serde_json::to_vec(view).unwrap(),
    )
    .unwrap();
    stats
}
fn assert_page_rules(framebuffer: &[u8]) {
    for y in [76, 704] {
        for x in 28..452 {
            assert_eq!(
                framebuffer[y * WIDTH / 8 + x / 8] & (1 << (7 - x % 8)),
                0,
                "missing separator at {x},{y}"
            );
        }
    }
}

include!("test-content-import.rs");
include!("test-learning-model.rs");
include!("test-reset-check.rs");
include!("test-parent-settings.rs");
include!("test-network-app.rs");
include!("test-save.rs");
include!("test-render-core.rs");
include!("test-fixtures.rs");
include!("test-native-memory.rs");
include!("test-performance.rs");
include!("test-render-layout.rs");
include!("test-download.rs");

mod curriculum {
    include!("test-curriculum.rs");
}
mod redesign {
    include!("test-redesign.rs");
}
include!("test-font-flash.rs");
include!("test-provisioning-ui-memory.rs");
