use super::{Model, Page};
use alloc::{format, string::ToString};

pub(super) struct ResetCheck {
    seed: u32,
    step: u8,
}
impl ResetCheck {
    pub(super) fn new(seed: u32) -> Self {
        Self { seed, step: 0 }
    }
    pub(super) fn passed(&self) -> bool {
        self.step == 3
    }
    fn correct(&self) -> u8 {
        ((self.seed % 4) as u8 + self.step) % 4
    }
    pub(super) fn view(&self) -> crate::View {
        let n = self.seed.wrapping_mul(1664525)
            .wrapping_add(1013904223u32.wrapping_mul(u32::from(self.step) + 1));
        let a = 12 + n % 25;
        let b = 12 + (n / 25) % 15;
        let c = 13 + (n / 375) % 85;
        let answer = (a * b + c) as i32;
        let options = core::array::from_fn(|i| {
            (answer + (i as i32 - i32::from(self.correct())) * 17).to_string()
        });
        let mut v = crate::View::new();
        v.screen = crate::layout::SCREEN_QUIZ;
        v.title = "家长".into();
        v.hint = format!("验证 {}/3 · 电源取消", self.step + 1);
        (v.question_size, v.hero) = crate::fit::question(&format!("{a} × {b} + {c} = ?")).unwrap();
        (v.option_size, v.options) = crate::fit::options(&options).unwrap();
        v
    }
}
impl Model {
    pub(super) fn start_reset_check(&mut self) {
        // 只用临时种子换题；验证不属于学习记录，也不能随存档恢复。
        let seed = self.revision as u32 ^ self.state.serial;
        self.reset_check = Some(ResetCheck::new(seed));
        self.go(Page::Reset);
    }
    pub(super) fn answer_reset_check(&mut self, answer: u8) {
        let check = self.reset_check.as_mut().unwrap();
        if answer != check.correct() {
            self.go(Page::Settings);
        } else {
            check.step += 1;
            self.selected = 0;
            self.changed();
        }
    }
}
