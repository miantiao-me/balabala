use crate::{generated, layout};
use alloc::string::String;

#[derive(Clone)]
#[cfg_attr(test, derive(serde::Serialize, serde::Deserialize))]
pub struct View {
    pub title: String,
    pub hero: String,
    pub lines: [String; 6],
    pub hint: String,
    pub keys: String,
    pub success: bool,
    #[cfg_attr(test, serde(default))]
    pub download: bool,
    #[cfg_attr(test, serde(default))]
    pub provisioning: bool,
    /// 新模板编号，见 layout::SCREEN_*；0 为旧通用模板。
    #[cfg_attr(test, serde(default))]
    pub screen: u8,
    #[cfg_attr(test, serde(default))]
    pub focus: u8,
    /// 选一选题目与选项的字号槽，由 fit 选出。
    #[cfg_attr(test, serde(default))]
    pub question_size: u8,
    #[cfg_attr(test, serde(default))]
    pub option_size: u8,
    #[cfg_attr(test, serde(default))]
    pub options: [String; 4],
    /// 正面四键键帽图标（overlay::CAP_*），0 表示该键在本页无作用、不画。
    #[cfg_attr(test, serde(default))]
    pub caps: [u8; 4],
    /// 插图编号（overlay::PIC_*），0 为无插图。
    #[cfg_attr(test, serde(default))]
    pub art: u8,
    /// 进度点（overlay::DOT_*）或完成页星星（overlay::STAR_*），0 不画。
    #[cfg_attr(test, serde(default))]
    pub marks: [u8; 10],
    /// 不可选的卡片位掩码，画虚线框。
    #[cfg_attr(test, serde(default))]
    pub muted: u8,
    /// 认一认正面特大字及档位（art::GIANT_PX 下标），由叠加层直接从 Flash 绘制。
    #[cfg_attr(test, serde(default))]
    pub giant: String,
    #[cfg_attr(test, serde(default))]
    pub giant_tier: u8,
    /// 学习情况每科 [认识, 再看看, 新卡] 张数。
    #[cfg_attr(test, serde(default))]
    pub bars: [[u16; 3]; 4],
}
impl View {
    pub(crate) fn new() -> Self {
        Self {
            title: String::new(),
            hero: String::new(),
            lines: core::array::from_fn(|_| String::new()),
            hint: String::new(),
            keys: String::new(),
            success: false,
            download: false,
            provisioning: false,
            screen: layout::SCREEN_LEGACY,
            focus: 0,
            question_size: 0,
            option_size: 0,
            options: core::array::from_fn(|_| String::new()),
            caps: [0; 4],
            art: 0,
            marks: [0; 10],
            muted: 0,
            giant: String::new(),
            giant_tier: 0,
            bars: [[0; 3]; 4],
        }
    }
    pub(crate) fn props(&self) -> generated::AppProps<'_> {
        generated::AppProps {
            title: &self.title,
            hero: &self.hero,
            line0: &self.lines[0],
            line1: &self.lines[1],
            line2: &self.lines[2],
            line3: &self.lines[3],
            line4: &self.lines[4],
            line5: &self.lines[5],
            hint: &self.hint,
            keys: &self.keys,
            success: self.success,
            download: self.download,
            provisioning: self.provisioning,
            screen: self.screen,
            questionSize: self.question_size,
            optionSize: self.option_size,
            opt0: &self.options[0],
            opt1: &self.options[1],
            opt2: &self.options[2],
            opt3: &self.options[3],
        }
    }
}
impl Drop for View {
    fn drop(&mut self) {
        for line in &mut self.lines {
            if self.provisioning || line.starts_with("PoP ") {
                for byte in unsafe { line.as_mut_vec() } {
                    unsafe { core::ptr::write_volatile(byte, 0) };
                }
            }
        }
    }
}
