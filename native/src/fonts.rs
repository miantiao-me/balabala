use alloc::{string::String, vec::Vec};
include!(concat!(env!("BALABALA_GENERATED"), "/fonts.rs"));
include!(concat!(env!("BALABALA_GENERATED"), "/font_text.rs"));
fn u16_at(b: &[u8], at: usize) -> usize {
    u16::from_le_bytes([b[at], b[at + 1]]) as usize
}
fn entry(font: &[u8], ch: char) -> Option<&[u8]> {
    let mut lo = 0;
    let mut hi = u16_at(font, 6);
    while lo < hi {
        let mid = (lo + hi) / 2;
        let e = &font[16 + mid * 8..24 + mid * 8];
        let cp = u32::from_le_bytes(e[..4].try_into().ok()?);
        if cp < ch as u32 {
            lo = mid + 1
        } else {
            hi = mid
        }
    }
    if lo == u16_at(font, 6) {
        return None;
    }
    let e = font.get(16 + lo * 8..24 + lo * 8)?;
    (u32::from_le_bytes(e[..4].try_into().ok()?) == ch as u32).then_some(e)
}
// 槽12由 text-2xl font-bold 选择，烘焙霞鹜文楷 Regular；槽5保留旧Noto与刷机提示。
pub const LEGACY_24: u8 = 5;
pub const REGULAR_24: u8 = 12;
pub const REGULAR_32: u8 = 6;
/// 原导入宽度契约、刷机最终提示及历史冻结模板使用。
pub const REGULAR_48: u8 = 14;
pub const BOLD_48: u8 = 15;
pub(crate) fn font(slot: u8) -> &'static [u8] {
    FONTS[FONT_SLOTS.iter().position(|s| *s == slot).expect("unknown font slot")]
}
pub fn advance(ch: char, slot: u8) -> Option<u8> {
    entry(font(slot), ch).map(|e| e[6])
}
/// 叠加层直接借用 Flash 中的 1bpp 字形：(每行字节数, 位图, advance, xoff)。
pub fn glyph(ch: char, slot: u8) -> Option<(usize, &'static [u8], u8, u8)> {
    let font = font(slot);
    let e = entry(font, ch)?;
    let (stride, rows) = ((font[8] as usize).div_ceil(8), font[9] as usize);
    let start = 16 + u16_at(font, 6) * 8 + u16_at(e, 4) * rows * stride;
    Some((stride, font.get(start..start + rows * stride)?, e[6], e[7]))
}
pub fn width(text: &str, slot: u8) -> usize {
    text.chars()
        .map(|c| advance(c, slot).unwrap_or(255) as usize)
        .sum()
}
pub struct FontStats {
    /// Logical serialized subset sizes, including bitmap coverage (not copied);
    /// `large` sums every slot other than 24px.
    pub normal: usize,
    pub large: usize,
    pub resident_cmap_bytes: usize,
    /// Original compatibility charge; not a measurement of heap requests.
    pub logical_construction_budget: usize,
}
// 保留83807B门禁；新模板按mono覆盖计费，历史冻结模板保留8bit等效口径。
// Flash bitmap借用不等于heap复制；该门禁不代表C3总heap可用量。
pub const MAX_PAGE_FONT_BYTES: usize = 83_807;
struct GlyphPlan {
    chars: Vec<char>,
    subset_bytes: usize,
}
/// `packed` 按 1bpp 位图计 coverage；旧模板仍按每像素 1 字节的兼容口径，保持冻结数值。
fn glyph_plan(chars: impl Iterator<Item = char>, source: &[u8], packed: bool) -> Result<GlyphPlan, i32> {
    let mut chars: Vec<char> = chars.collect();
    chars.push('\u{fffd}');
    chars.sort_unstable();
    chars.dedup();
    if chars.iter().any(|ch| entry(source, *ch).is_none()) {
        return Err(-2);
    }
    let (w, h) = (source[8] as usize, source[9] as usize);
    let cell = if packed { w.div_ceil(8) * h } else { w * h };
    let subset_bytes = 16 + chars.len() * (8 + cell);
    Ok(GlyphPlan {
        chars,
        subset_bytes,
    })
}
fn construction_bytes(resident: usize, plan: &GlyphPlan) -> usize {
    resident + plan.chars.capacity() * core::mem::size_of::<char>() + plan.subset_bytes - 16
}
fn normal_parts(view: &crate::View) -> impl Iterator<Item = &str> {
    core::iter::once(view.title.as_str())
        .chain(view.lines.iter().map(String::as_str))
        .chain([view.hint.as_str(), view.keys.as_str()])
        .chain(crate::PANEL_TEXT.iter().copied().filter(move |_| view.provisioning))
}
// 新页面按AOT实际分支的字号文字加载，空字号不加载；旧冻结模板保留原两槽与计费，
// 同时维持刷机最终两行提示的原像素。
fn page_plan(view: &crate::View, slot: u8) -> Result<Option<GlyphPlan>, i32> {
    if view.provisioning || view.screen != crate::layout::SCREEN_LEGACY {
        let mut chars = Vec::new();
        font_text(view, slot, &mut |text| chars.extend(text.chars().filter(|c| *c >= ' ')));
        if chars.is_empty() {
            return Ok(None);
        }
        glyph_plan(chars.into_iter(), font(slot), true).map(Some)
    } else if slot == LEGACY_24 {
        glyph_plan(normal_parts(view).flat_map(str::chars), font(slot), false).map(Some)
    } else if slot == REGULAR_48 {
        glyph_plan(view.hero.chars(), font(slot), false).map(Some)
    } else {
        Ok(None)
    }
}
pub fn construction_budget(view: &crate::View) -> Result<usize, i32> {
    let (mut peak, mut resident) = (0, 0);
    for slot in FONT_SLOTS {
        if let Some(plan) = page_plan(view, slot)? {
            peak = peak.max(construction_bytes(resident, &plan));
            resident += plan.subset_bytes - 16;
        }
    }
    Ok(peak)
}
#[cfg(test)]
pub fn load_owned(ui: &mut pocketjs_core::Ui, view: &crate::View) {
    for slot in FONT_SLOTS {
        if let Some(plan) = page_plan(view, slot).unwrap() {
            assert!(ui.load_font_subset(font(slot), &plan.chars));
        }
    }
}
pub fn load(ui: &mut pocketjs_core::Ui, view: &crate::View) -> Result<FontStats, i32> {
    let mut sizes = [0; 2];
    let mut resident = 0;
    let mut cmap_bytes = 0;
    let mut peak = 0;
    for slot in FONT_SLOTS {
        let Some(plan) = page_plan(view, slot)? else {
            continue;
        };
        peak = peak.max(construction_bytes(resident, &plan));
        if !ui.load_font_subset_static(font(slot), &plan.chars) {
            return Err(-2);
        }
        cmap_bytes += plan.chars.len() * core::mem::size_of::<pocketjs_core::text::CmapEntry>();
        sizes[usize::from(slot != REGULAR_24 && slot != LEGACY_24)] += plan.subset_bytes;
        resident += plan.subset_bytes - 16;
    }
    Ok(FontStats {
        normal: sizes[0],
        large: sizes[1],
        resident_cmap_bytes: cmap_bytes,
        logical_construction_budget: peak,
    })
}
