//! 静态框线、键帽、固定标签与焦点反白直接写入 1bpp 帧缓冲：不建节点、不占 heap。
//! MicroTS 只排随内容变化的文字；与模板共用的坐标来自 tools/ui-layout.ts 生成的 layout。
use crate::art::{self, Sprite};
use crate::layout::*;
use crate::{fonts, View, HEIGHT, WIDTH};

const TILE_GLYPHS: [&str; 4] = ["字", "ā", "Aa", "123"];

#[cfg(test)]
#[path = "../../tools/test-overlay.rs"]
mod tests;

/// 键帽图标编号：View::caps 的取值。
pub const CAP_LEFT: u8 = 1;
pub const CAP_RIGHT: u8 = 2;
pub const CAP_BACK: u8 = 3;
pub const CAP_CHECK: u8 = 4;
pub const CAP_FLIP: u8 = 5;
pub const CAP_NEXT: u8 = 6;
/// 非答题页正面四键：左 / 右 / 返回 / 确认。
pub const NAV: [u8; 4] = [CAP_LEFT, CAP_RIGHT, CAP_BACK, CAP_CHECK];
/// 插图编号：View::art 的取值。
pub const PIC_SUN: u8 = 1;
pub const PIC_CAT: u8 = 2;
pub const PIC_STAR: u8 = 3;
pub const PIC_LOOK: u8 = 4;
pub const PIC_USB: u8 = 5;
pub const PIC_QUESTION: u8 = 6;
pub const PIC_SLEEP: u8 = 7;
pub const PIC_MEDAL: u8 = 8;
pub const PIC_TRASH: u8 = 9;
pub const PIC_RABBIT: u8 = 10;
pub const PIC_FLOWER: u8 = 11;
pub const PIC_BUTTERFLY: u8 = 12;
pub const PIC_ROCKET: u8 = 13;
pub const PIC_WHALE: u8 = 14;
pub const PIC_BEAR: u8 = 15;
pub const PIC_OWL: u8 = 16;
pub const PIC_SPROUT: u8 = 17;
pub const PIC_KITE: u8 = 18;
/// 答对时轮换的鼓励图。
pub const CHEERS: [u8; 12] = [
    PIC_SUN, PIC_CAT, PIC_STAR, PIC_RABBIT, PIC_FLOWER, PIC_BUTTERFLY,
    PIC_ROCKET, PIC_WHALE, PIC_BEAR, PIC_OWL, PIC_SPROUT, PIC_KITE,
];
/// 进度点：未看、当前、已过。
pub const DOT_TODO: u8 = 1;
pub const DOT_NOW: u8 = 2;
pub const DOT_DONE: u8 = 3;
/// 完成页星星：空心、实心。
pub const STAR_OFF: u8 = 1;
pub const STAR_ON: u8 = 2;
const DOT_R: i32 = 7;
const DOT_PITCH: i32 = 22;
const STAR_PITCH: i32 = 40;

fn cap_icon(id: u8) -> Option<Sprite> {
    Some(match id {
        CAP_LEFT => art::ART_LEFT,
        CAP_RIGHT => art::ART_RIGHT,
        CAP_BACK => art::ART_BACK,
        CAP_CHECK => art::ART_CHECK,
        CAP_FLIP => art::ART_FLIP,
        CAP_NEXT => art::ART_NEXT,
        _ => return None,
    })
}
fn picture(id: u8) -> Option<Sprite> {
    Some(match id {
        PIC_SUN => art::ART_CHEER_SUN,
        PIC_CAT => art::ART_CHEER_CAT,
        PIC_STAR => art::ART_CHEER_STAR,
        PIC_LOOK => art::ART_LOOK,
        PIC_USB => art::ART_USB,
        PIC_QUESTION => art::ART_CARD_QUESTION,
        PIC_SLEEP => art::ART_SLEEP,
        PIC_MEDAL => art::ART_MEDAL,
        PIC_TRASH => art::ART_TRASH,
        PIC_RABBIT => art::ART_CHEER_RABBIT,
        PIC_FLOWER => art::ART_CHEER_FLOWER,
        PIC_BUTTERFLY => art::ART_CHEER_BUTTERFLY,
        PIC_ROCKET => art::ART_CHEER_ROCKET,
        PIC_WHALE => art::ART_CHEER_WHALE,
        PIC_BEAR => art::ART_CHEER_BEAR,
        PIC_OWL => art::ART_CHEER_OWL,
        PIC_SPROUT => art::ART_CHEER_SPROUT,
        PIC_KITE => art::ART_CHEER_KITE,
        _ => return None,
    })
}

#[derive(Clone, Copy)]
enum Ink {
    Black,
    White,
    Invert,
}
// 帧缓冲位为 1 表示白（见 pack_monochrome）。
fn plot(frame: &mut [u8], x: i32, y: i32, ink: Ink) {
    if x < 0 || y < 0 || x >= WIDTH as i32 || y >= HEIGHT as i32 {
        return;
    }
    let (byte, bit) = (y as usize * WIDTH / 8 + x as usize / 8, 0x80 >> (x % 8));
    match ink {
        Ink::Black => frame[byte] &= !bit,
        Ink::White => frame[byte] |= bit,
        Ink::Invert => frame[byte] ^= bit,
    }
}
fn inside(px: i32, py: i32, [x, y, w, h]: [i32; 4], r: i32) -> bool {
    if px < x || py < y || px >= x + w || py >= y + h {
        return false;
    }
    // 以像素中心的二倍坐标计算，圆角左右对称且 2r 等于边长时不退化。
    let (cx, cy) = (2 * px + 1, 2 * py + 1);
    let dx = cx - cx.clamp(2 * (x + r), 2 * (x + w - r));
    let dy = cy - cy.clamp(2 * (y + r), 2 * (y + h - r));
    dx * dx + dy * dy <= 4 * r * r
}
fn inset([x, y, w, h]: [i32; 4], d: i32) -> [i32; 4] {
    [x + d, y + d, w - 2 * d, h - 2 * d]
}
// 两端按位裁剪，中间整字节写入，避免为焦点反白逐像素计算坐标。
fn span(frame: &mut [u8], y: i32, left: i32, right: i32, ink: Ink) {
    let (left, right) = (left.max(0), right.min(WIDTH as i32));
    if !(0..HEIGHT as i32).contains(&y) || left >= right {
        return;
    }
    let row = &mut frame[y as usize * WIDTH / 8..(y as usize + 1) * WIDTH / 8];
    let (first, last) = (left as usize / 8, (right as usize - 1) / 8);
    let (head, tail) = (0xff >> (left % 8), 0xff << (7 - (right - 1) % 8));
    let apply = |byte: &mut u8, mask: u8| match ink {
        Ink::Black => *byte &= !mask,
        Ink::White => *byte |= mask,
        Ink::Invert => *byte ^= mask,
    };
    if first == last {
        apply(&mut row[first], head & tail);
    } else {
        apply(&mut row[first], head);
        match ink {
            Ink::Black => row[first + 1..last].fill(0),
            Ink::White => row[first + 1..last].fill(0xff),
            Ink::Invert => row[first + 1..last].iter_mut().for_each(|byte| *byte = !*byte),
        }
        apply(&mut row[last], tail);
    }
}
pub(crate) fn panel_rect(frame: &mut [u8], [x, y, w, h]: [usize; 4], white: bool) {
    let ink = if white { Ink::White } else { Ink::Black };
    for row in y..(y + h).min(HEIGHT) {
        span(frame, row as i32, x as i32, (x + w) as i32, ink);
    }
}
pub(crate) fn panel_identity(frame: &mut [u8], visible: bool) {
    if !visible {
        for &rect in crate::PANEL_IDENTITIES {
            panel_rect(frame, rect, true);
        }
    }
}
fn fill(frame: &mut [u8], rect: [i32; 4], r: i32, ink: Ink) {
    let [x, y, w, h] = rect;
    for py in y.max(0)..(y + h).min(HEIGHT as i32) {
        let mut left = x;
        while left < x + w && !inside(left, py, rect, r) {
            left += 1;
        }
        // 像素中心的圆角关于矩形中线对称，只需找一侧边界。
        span(frame, py, left, x + w - (left - x), ink);
    }
}
fn ring(frame: &mut [u8], rect: [i32; 4], r: i32, stroke: i32) {
    dashed(frame, rect, r, stroke, 0);
}
/// dash>0 时按 (x+y)/dash 的奇偶留空，直边上即为等长虚线。
fn dashed(frame: &mut [u8], rect: [i32; 4], r: i32, stroke: i32, dash: i32) {
    let [x, y, w, h] = rect;
    let hole = inset(rect, stroke);
    let band = r.max(stroke);
    for py in y..y + h {
        // 圆角以外的行只有左右两段直边，跳过内部避免逐像素扫整块面积。
        let straight = py >= y + band && py < y + h - band;
        let mut px = x;
        while px < x + w {
            if straight && px == x + stroke {
                px = x + w - stroke;
            }
            if (dash == 0 || (px + py) / dash % 2 == 0) && inside(px, py, rect, r) && !inside(px, py, hole, (r - stroke).max(0)) {
                plot(frame, px, py, Ink::Black);
            }
            px += 1;
        }
    }
}
/// 焦点保留外框，把框内已画好的黑字白底整体反白，模板无需按焦点切换样式。
fn invert_inside(frame: &mut [u8], rect: [i32; 4]) {
    fill(frame, inset(rect, STROKE), RADIUS - STROKE, Ink::Invert);
}
// 字形和素材都已是1bpp；每个源字节最多合并到两个目标字节。
fn blit_rows(frame: &mut [u8], bits: &[u8], stride: usize, width: usize, x: i32, y: i32) {
    if width == 0 {
        return;
    }
    let (start, shift) = (x.div_euclid(8), x.rem_euclid(8));
    let columns = width.div_ceil(8);
    let tail = if width % 8 == 0 { 0xff } else { 0xff << (8 - width % 8) };
    for (row, bits) in bits.chunks_exact(stride).enumerate() {
        let py = y + row as i32;
        if !(0..HEIGHT as i32).contains(&py) {
            continue;
        }
        let target = &mut frame[py as usize * WIDTH / 8..(py as usize + 1) * WIDTH / 8];
        for (i, &byte) in bits[..columns].iter().enumerate() {
            let byte = byte & if i + 1 == columns { tail } else { 0xff };
            if byte == 0 {
                continue;
            }
            let ink = u16::from(byte) << (8 - shift);
            let column = start + i as i32;
            if (0..WIDTH as i32 / 8).contains(&column) {
                target[column as usize] &= !(ink >> 8) as u8;
            }
            if (0..WIDTH as i32 / 8).contains(&(column + 1)) {
                target[(column + 1) as usize] &= !(ink as u8);
            }
        }
    }
}
fn text(frame: &mut [u8], cx: i32, top: i32, s: &str, slot: u8) {
    let mut pen = cx - fonts::width(s, slot) as i32 / 2;
    for ch in s.chars() {
        let Some((stride, rows, advance, xoff)) = fonts::glyph(ch, slot) else {
            continue;
        };
        blit_rows(frame, rows, stride, stride * 8, pen - xoff as i32, top);
        pen += advance as i32;
    }
}
fn blit(frame: &mut [u8], sprite: Sprite, x: i32, y: i32) {
    if sprite.w != 0 {
        blit_rows(frame, sprite.bits(), sprite.stride(), usize::from(sprite.w), x, y);
    }
}
fn blit_center(frame: &mut [u8], sprite: Sprite, [x, y, w, h]: [i32; 4]) {
    blit(frame, sprite, x + (w - i32::from(sprite.w)) / 2, y + (h - i32::from(sprite.h)) / 2);
}
/// 特大字整行墨迹外框在 rect 内居中；调用方已用 art::giant_width 确认字符齐全且放得下。
pub fn giant_text(frame: &mut [u8], rect: [i32; 4], text: &str, tier: usize) {
    let glyphs = || text.chars().filter_map(|c| art::giant(c, tier));
    let (mut pen, mut left, mut right, mut top, mut bottom) = (0, i32::MAX, i32::MIN, i32::MAX, i32::MIN);
    for g in glyphs() {
        if g.w > 0 {
            left = left.min(pen + i32::from(g.x));
            right = right.max(pen + i32::from(g.x) + i32::from(g.w));
            top = top.min(i32::from(g.y));
            bottom = bottom.max(i32::from(g.y) + i32::from(g.h));
        }
        pen += i32::from(g.advance);
    }
    if left > right {
        return;
    }
    let origin_x = rect[0] + (rect[2] - (right - left)) / 2 - left;
    let baseline = rect[1] + (rect[3] - (bottom - top)) / 2 - top;
    let mut pen = origin_x;
    for g in glyphs() {
        blit(frame, g, pen + i32::from(g.x), baseline + i32::from(g.y));
        pen += i32::from(g.advance);
    }
}
fn hatch(frame: &mut [u8], [x, y, w, h]: [i32; 4]) {
    for py in y..y + h {
        for px in x..x + w {
            if (px + py) % 6 < 2 {
                plot(frame, px, py, Ink::Black);
            }
        }
    }
}
fn dots(frame: &mut [u8], marks: &[u8; 10], left: i32, cy: i32) {
    for (i, &m) in marks.iter().take_while(|m| **m != 0).enumerate() {
        let rect = [left + i as i32 * DOT_PITCH, cy - DOT_R, 2 * DOT_R, 2 * DOT_R];
        if m == DOT_DONE {
            fill(frame, rect, DOT_R, Ink::Black);
        } else {
            ring(frame, rect, DOT_R, 2);
        }
        if m == DOT_NOW {
            fill(frame, inset(rect, 4), DOT_R - 4, Ink::Black);
        }
    }
}
fn marks_len(view: &View) -> i32 {
    view.marks.iter().take_while(|m| **m != 0).count() as i32
}
/// 顶部：科目标签反白；有存储提示时让出右侧，不画进度点。
fn head(view: &View, frame: &mut [u8]) {
    if !view.title.is_empty() {
        fill(frame, HEAD_CHIP, 8, Ink::Invert);
    }
    if view.hint.is_empty() {
        let width = marks_len(view) * DOT_PITCH - DOT_PITCH + 2 * DOT_R;
        dots(frame, &view.marks, HEAD_DOTS[0] - width, HEAD_DOTS[1]);
    }
}
fn caps(view: &View, frame: &mut [u8]) {
    for (rect, &id) in CAPS.iter().zip(&view.caps) {
        if let Some(icon) = cap_icon(id) {
            ring(frame, *rect, 12, 2);
            blit_center(frame, icon, *rect);
        }
    }
}
fn home(view: &View, frame: &mut [u8]) {
    ring(frame, HOME_CARD, RADIUS, STROKE);
    blit(frame, art::ART_PLAY, HOME_PLAY[0], HOME_PLAY[1]);
    // 模板把科目字画成黑字，这里反白成黑底标签。
    if !view.title.is_empty() {
        fill(frame, HOME_CHIP, 8, Ink::Invert);
    }
    dots(frame, &view.marks, HOME_DOTS[0], HOME_DOTS[1]);
    for ((rect, glyph), top) in HOME_TILES.iter().zip(TILE_GLYPHS).zip(HOME_TILE_GLYPHS) {
        ring(frame, *rect, RADIUS, STROKE);
        text(frame, rect[0] + rect[2] / 2, top, glyph, fonts::BOLD_48);
    }
    caps(view, frame);
    match view.focus {
        0 => invert_inside(frame, HOME_CARD),
        n @ 1..=4 => invert_inside(frame, HOME_TILES[n as usize - 1]),
        _ => {}
    }
}
fn deck(view: &View, frame: &mut [u8]) {
    head(view, frame);
    let icons = [art::ART_MODE_READ, art::ART_MODE_QUIZ, art::ART_MODE_REVIEW];
    for (i, ((card, icon), sprite)) in DECK_CARDS.iter().zip(DECK_ICONS).zip(icons).enumerate() {
        dashed(frame, *card, RADIUS, STROKE, if view.muted & (1 << i) != 0 { 10 } else { 0 });
        blit_center(frame, sprite, icon);
    }
    if let Some(card) = DECK_CARDS.get(usize::from(view.focus)) {
        invert_inside(frame, *card);
    }
    caps(view, frame);
}
fn read(view: &View, frame: &mut [u8]) {
    head(view, frame);
    ring(frame, READ_CARD, RADIUS, STROKE);
    if !view.giant.is_empty() {
        giant_text(frame, READ_FRONT, &view.giant, usize::from(view.giant_tier));
    }
    if !view.lines[1].is_empty() {
        fill(frame, READ_DIVIDER, 0, Ink::Black);
    }
    caps(view, frame);
}
/// 作答后列底换成导航键帽，正确列分隔线以上反白；未作答时列底是 A–D。
fn quiz(view: &View, frame: &mut [u8]) {
    head(view, frame);
    let answered = view.caps != [0; 4];
    if let Some(p) = picture(view.art) {
        blit_center(frame, p, QUIZ_ART);
    }
    for i in 0..4 {
        ring(frame, QUIZ_COLUMNS[i], RADIUS, STROKE);
        fill(frame, QUIZ_DIVIDERS[i], 0, Ink::Black);
        match cap_icon(view.caps[i]) {
            Some(icon) => blit_center(frame, icon, QUIZ_SLOTS[i]),
            None if !answered => text(frame, KEY_X[i], QUIZ_LETTER_TOP, ["A", "B", "C", "D"][i], fonts::REGULAR_24),
            None => {}
        }
    }
    if answered {
        if let Some(rect) = QUIZ_ANSWERS.get(usize::from(view.focus)) {
            invert_inside(frame, *rect);
        }
    }
}
fn settings(view: &View, frame: &mut [u8]) {
    for (i, row) in SETTINGS_ROWS.iter().enumerate() {
        let label = if i < 4 { &view.options[i] } else { &view.lines[4] };
        if label.is_empty() { continue; }
        ring(frame, *row, 12, 2);
        if i > 0 && view.muted & (1 << i) == 0 {
            blit(frame, art::ART_CHEVRON, SETTINGS_CHEVRONS[i][0], SETTINGS_CHEVRONS[i][1]);
        }
    }
    if let Some(row) = SETTINGS_ROWS.get(usize::from(view.focus)) {
        fill(frame, inset(*row, 2), 10, Ink::Invert);
    }
    caps(view, frame);
}
/// 进度条按 认识 / 再看看 / 新卡 比例分段：实心、斜线、留白。
fn bar(frame: &mut [u8], rect: [i32; 4], [known, review, new]: [u16; 3]) {
    ring(frame, rect, 0, 2);
    let [x, y, w, h] = inset(rect, 2);
    let total = i32::from(known) + i32::from(review) + i32::from(new);
    if total == 0 {
        return;
    }
    let a = w * i32::from(known) / total;
    let b = w * (i32::from(known) + i32::from(review)) / total;
    fill(frame, [x, y, a, h], 0, Ink::Black);
    hatch(frame, [x + a, y, b - a, h]);
}
fn stats(view: &View, frame: &mut [u8]) {
    for i in 0..4 {
        let chip = STATS_CHIPS[i];
        text(frame, chip[0] + chip[2] / 2, chip[1] + 2, TILE_GLYPHS[i], fonts::REGULAR_24);
        fill(frame, chip, 8, Ink::Invert);
        let name = STATS_NAMES[i];
        text(frame, name[0] + 48, name[1], ["汉字", "拼音", "英语", "数学"][i], fonts::REGULAR_24);
        bar(frame, STATS_BARS[i], view.bars[i]);
    }
    for (rect, label) in STATS_LEGEND_LABELS.iter().zip(["已学会", "学习中", "新卡"]) {
        text(frame, rect[0] + fonts::width(label, fonts::REGULAR_24) as i32 / 2, rect[1], label, fonts::REGULAR_24);
    }
    fill(frame, STATS_LEGEND[0], 0, Ink::Black);
    ring(frame, STATS_LEGEND[1], 0, 2);
    hatch(frame, inset(STATS_LEGEND[1], 2));
    ring(frame, STATS_LEGEND[2], 0, 2);
    caps(view, frame);
}
/// 提示页：插图、标题、正文、至多两个按钮。完成页插图为奖牌加一排星星。
fn message(view: &View, frame: &mut [u8]) {
    let [_, top, _, _] = MESSAGE_ART;
    if view.art == PIC_MEDAL {
        let medal = art::ART_MEDAL;
        blit(frame, medal, (WIDTH as i32 - i32::from(medal.w)) / 2, top);
        let n = marks_len(view);
        let left = (WIDTH as i32 - (n * STAR_PITCH - (STAR_PITCH - i32::from(art::ART_STAR_ON.w)))) / 2;
        for (i, &m) in view.marks.iter().take(n as usize).enumerate() {
            let star = if m == STAR_ON { art::ART_STAR_ON } else { art::ART_STAR_OFF };
            blit(frame, star, left + i as i32 * STAR_PITCH, top + i32::from(medal.h) + 16);
        }
    } else if let Some(p) = picture(view.art) {
        blit_center(frame, p, MESSAGE_ART);
    }
    for (button, text) in MESSAGE_BUTTONS.iter().zip(&view.options) {
        if !text.is_empty() {
            ring(frame, *button, RADIUS, STROKE);
        }
    }
    let focus = usize::from(view.focus);
    if view.options.get(focus).is_some_and(|t| !t.is_empty()) {
        invert_inside(frame, MESSAGE_BUTTONS[focus]);
    }
    caps(view, frame);
}
/// 配网活跃期只重绘固定状态块，直接读 Flash，不创建 View/UI 或分配内存。
pub fn panel_status(frame: &mut [u8], status: u32) {
    for &(state, rect, label) in crate::PANEL_STATES {
        panel_rect(frame, rect.map(|n| n as usize), true);
        ring(frame, rect, 8, 2);
        text(frame, rect[0] + rect[2] / 2, rect[1] + (rect[3] - i32::from(fonts::font(fonts::REGULAR_24)[9])) / 2, label, fonts::REGULAR_24);
        if state == status { fill(frame, inset(rect, 2), 6, Ink::Invert); }
    }
}
fn provisioning(view: &View, frame: &mut [u8]) {
    let [x, y, pitch, w, h] = crate::PANEL_POP;
    for i in 0..6 {
        let rect = [x + i * pitch, y, w, h];
        ring(frame, rect, 12, 2);
        if let Some(digit) = view.lines[1].get(i as usize..i as usize + 1) {
            text(frame, rect[0] + w / 2, y + (h - i32::from(fonts::font(fonts::BOLD_48)[9])) / 2, digit, fonts::BOLD_48);
        }
    }
    panel_status(frame, u32::from(view.focus));
}
/// 与 App.vue 分支优先级一致：刷机和配网页面优先于新模板。
pub fn draw(view: &View, frame: &mut [u8]) {
    if view.download { return; }
    if view.provisioning {
        provisioning(view, frame);
        return;
    }
    match view.screen {
        SCREEN_HOME => home(view, frame),
        SCREEN_DECK => deck(view, frame),
        SCREEN_READ => read(view, frame),
        SCREEN_QUIZ => quiz(view, frame),
        SCREEN_SETTINGS => settings(view, frame),
        SCREEN_STATS => stats(view, frame),
        SCREEN_MESSAGE => message(view, frame),
        SCREEN_SLEEP => blit_center(frame, art::ART_SLEEP, SLEEP_ART),
        _ => {}
    }
}
