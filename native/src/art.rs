//! 构建期由 SVG 线稿与字体轮廓栅格化的 1bpp 位图（见 tools/sprites.ts），叠加层直接借用 Flash。
#[derive(Clone, Copy)]
pub struct Sprite {
    pub w: u16,
    pub h: u16,
    /// 特大字相对笔位与基线的墨迹偏移；线稿为 0。
    pub x: i16,
    pub y: i16,
    pub advance: u16,
    pub at: u32,
}
include!(concat!(env!("BALABALA_GENERATED"), "/sprites.rs"));
impl Sprite {
    pub fn stride(&self) -> usize {
        usize::from(self.w).div_ceil(8)
    }
    pub fn bits(&self) -> &'static [u8] {
        let start = self.at as usize;
        &SPRITE_BITS[start..start + self.stride() * usize::from(self.h)]
    }
}
/// 特大字第 `tier` 档的字形；不在内置卡组 front 字符集内时为 None。
pub fn giant(ch: char, tier: usize) -> Option<Sprite> {
    let table = GIANT[tier];
    table.binary_search_by_key(&ch, |(c, _)| *c).ok().map(|i| table[i].1)
}
pub fn giant_width(text: &str, tier: usize) -> Option<usize> {
    text.chars().map(|c| giant(c, tier).map(|s| usize::from(s.advance))).sum()
}
