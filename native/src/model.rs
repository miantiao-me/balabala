use alloc::{
    string::{String, ToString},
    vec::Vec,
};
use serde::{Deserialize, Serialize};

mod content;
pub mod course;
mod decode;
mod learning;
mod network;
mod reset;
mod save;
mod view;

pub use content::{
    parse_deck, wrap, Card, Deck, MAX_ACTIVE_CONTENT_BYTES, MAX_PACKAGE, SUBJECTS, SUBJECT_NAMES,
};
pub use save::{crc32, Progress, Round, Snapshot, MAX_SAVE};
#[cfg(test)]
pub(crate) use view::page_font_budget;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Page {
    Home,
    Deck,
    Learn,
    Done,
    Stats,
    Settings,
    LearningSettings,
    StartPoint,
    UnitChoice,
    StartConfirm,
    CourseDone,
    Reset,
    Network,
    DownloadConfirm,
    DownloadPreparing,
    DownloadPrompt,
    Error,
    Sleep,
    Sources,
}
pub const CLEAR_INTERVALS: [u8; 5] = [8, 16, 32, 64, 128];
pub fn partial_allowed(previous: Option<Page>, next: Page) -> bool {
    let ordinary = |page| !matches!(
        page,
        Page::Network | Page::DownloadPreparing | Page::DownloadPrompt | Page::Error | Page::Sleep
    );
    previous.is_some_and(ordinary) && ordinary(next)
}
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub enum Mode {
    Read,
    Quiz,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u32)]
pub enum Key {
    Front1 = 0,
    Front2 = 1,
    Front3 = 2,
    Front4 = 3,
    Up = 4,
    Down = 5,
    Home = 6,
    Shutdown = 7,
    /// 首页按住返回键满 2 秒，由输入层单独发出一次。
    Parent = 8,
}
impl Key {
    pub fn from_u32(n: u32) -> Option<Self> {
        Some(match n {
            0 => Self::Front1,
            1 => Self::Front2,
            2 => Self::Front3,
            3 => Self::Front4,
            4 => Self::Up,
            5 => Self::Down,
            6 => Self::Home,
            7 => Self::Shutdown,
            8 => Self::Parent,
            _ => return None,
        })
    }
}

pub struct Model {
    pub decks: Vec<Deck>,
    pub state: Snapshot,
    pub page: Page,
    pub selected: usize,
    pub subject: usize,
    pub revision: u64,
    pub displayed: u64,
    pub rendering: Option<u64>,
    rendering_page: Option<Page>,
    displayed_page: Option<Page>,
    pub held: u8,
    /// 返回键是在首页按下的：只有这时长按才进入家长区。
    parent_armed: bool,
    pub dirty: bool,
    pub mutation: u64,
    pub generation: u64,
    pub slot: u32,
    pub pending_save: Option<(u64, u64, u32)>,
    save_protected: bool,
    imported_subjects: u8,
    pub platform_requests: u32,
    pub network: u32,
    pub network_generation: u32,
    pub network_phase: u32,
    pub network_owned: bool,
    network_exit: u8,
    network_name: [u8; 11],
    network_pop: [u8; 6],
    pub storage: u32,
    pub error: String,
    pub course_reader: Option<course::CourseReader>,
    pub course_cards: Vec<Card>,
    pending_unit: u16,
    reset_check: Option<reset::ResetCheck>,
}
impl Model {
    pub fn new() -> Self {
        Self {
            decks: crate::builtin_decks(),
            state: Snapshot::default(),
            page: Page::Home,
            selected: 0,
            subject: 0,
            revision: 1,
            displayed: 0,
            rendering: None,
            rendering_page: None,
            displayed_page: None,
            held: 0,
            parent_armed: false,
            dirty: true,
            mutation: 1,
            generation: 0,
            slot: 1,
            pending_save: None,
            save_protected: false,
            imported_subjects: 0,
            platform_requests: 0,
            network: 0,
            network_generation: 0,
            network_phase: 0,
            network_owned: false,
            network_exit: 0,
            network_name: [0; 11],
            network_pop: [0; 6],
            storage: 0,
            error: String::new(),
            course_reader: None,
            course_cards: Vec::new(),
            pending_unit: 0,
            reset_check: None,
        }
    }
    fn changed(&mut self) {
        self.revision += 1;
    }
    fn saved_change(&mut self) {
        self.dirty = true;
        self.mutation += 1;
    }
    fn go(&mut self, page: Page) {
        if self.page == Page::Network && page != Page::Network {
            self.clear_network_info();
        }
        if page != Page::Reset {
            self.reset_check = None;
        }
        self.page = page;
        self.selected = 0;
        self.parent_armed = false;
        self.changed();
    }
    pub fn show_error(&mut self, error: &str) {
        self.error = error.to_string();
        self.go(Page::Error);
    }
    pub fn ready(&self) -> bool {
        self.displayed == self.revision && self.rendering.is_none()
    }
    pub fn complete(&mut self, revision: u64) -> bool {
        if self.rendering != Some(revision) {
            return false;
        }
        self.rendering = None;
        self.displayed_page = self.rendering_page.take();
        self.displayed = revision;
        true
    }
    pub fn begin_render(&mut self) -> Option<u64> {
        if self.rendering.is_some() || self.displayed == self.revision {
            return None;
        }
        self.rendering = Some(self.revision);
        self.rendering_page = Some(self.page);
        Some(self.revision)
    }
    pub fn refresh_partial(&self) -> bool {
        self.rendering.is_some()
            && self
                .rendering_page
                .is_some_and(|page| partial_allowed(self.displayed_page, page))
    }
    fn menu_len(&self) -> usize {
        match self.page {
            Page::Home => 5,
            Page::Deck => 2 + usize::from(self.review_count(self.subject) > 0),
            Page::Settings => 6,
            Page::LearningSettings => 5,
            Page::StartPoint => 4,
            Page::Sources => 3,
            Page::UnitChoice => course::COURSE_UNITS.iter().filter(|u| usize::from(u.subject) == self.subject).count(),
            Page::Reset if self.reset_check.as_ref().is_some_and(|check| !check.passed()) => 0,
            Page::Reset | Page::DownloadConfirm | Page::StartConfirm => 2,
            Page::CourseDone => 1,
            Page::Done => 1 + usize::from(self.done_review() > 0),
            _ => 0,
        }
    }
    fn navigate(&mut self, down: bool) {
        if self.page == Page::Learn {
            let r = self.state.round.as_mut().unwrap();
            if !down && r.cursor == 0 && !r.flipped {
                return;
            }
            if down && r.cursor + 1 == r.ids.len() {
                self.go(Page::Done);
            } else {
                r.cursor = if down {
                    r.cursor + 1
                } else {
                    r.cursor.saturating_sub(1)
                };
                r.flipped = false;
                self.changed();
            }
            self.saved_change();
        } else if self.page == Page::Done && !down && self.selected == 0 {
            self.go(Page::Learn);
        } else {
            let n = self.menu_len();
            if n > 1 {
                self.selected = if down {
                    (self.selected + 1) % n
                } else {
                    (self.selected + n - 1) % n
                };
                self.changed();
            }
        }
    }
    fn confirm(&mut self) {
        match self.page {
            Page::Home => match self.selected {
                0 => {
                    if self.state.round.is_some() {
                        self.go(Page::Learn)
                    } else {
                        self.subject = 0;
                        self.go(Page::Deck)
                    }
                }
                _ => {
                    self.subject = self.selected - 1;
                    self.go(Page::Deck)
                }
            },
            Page::Deck => match self.selected {
                0 => self.start(self.subject, Mode::Read, false),
                1 => self.start(self.subject, Mode::Quiz, false),
                _ => self.start(self.subject, Mode::Quiz, true),
            },
            Page::Stats | Page::Sources => self.go(Page::Settings),
            Page::Learn => {
                if self.state.round.as_ref().unwrap().mode == Mode::Read {
                    let r = self.state.round.as_mut().unwrap();
                    r.flipped = !r.flipped;
                    self.saved_change();
                    self.changed();
                } else {
                    self.navigate(true);
                }
            }
            Page::Done => {
                let Some(r) = &self.state.round else {
                    return self.go(Page::Home);
                };
                let (subject, mode) = (r.subject, r.mode);
                if self.selected == 0 && self.done_review() > 0 {
                    self.start(subject, Mode::Quiz, true);
                } else {
                    self.start(subject, mode, false);
                }
            }
            Page::Settings => match self.selected {
                0 => self.go(Page::LearningSettings),
                1 => self.go(Page::Stats),
                2 => self.start_reset_check(),
                3 => {
                    self.clear_network_info();
                    self.network = 7;
                    self.network_phase = 1;
                    self.network_owned = true;
                    self.network_exit = 0;
                    self.platform_requests |= 2;
                    self.go(Page::Network);
                }
                4 => self.go(Page::DownloadConfirm),
                _ => self.go(Page::Sources),
            },
            Page::LearningSettings => match self.selected {
                0 => { self.state.count = if self.state.count == 5 {10} else {5}; self.saved_change(); self.changed(); }
                1 => { let n = [1,3,5,7,9].iter().position(|n| *n == self.state.threshold).unwrap_or(1); self.set_threshold([1,3,5,7,9][(n+1)%5]); }
                2 if self.course_active() => self.go(Page::StartPoint),
                2 => self.show_error("请先放入完整课程包"),
                3 => {
                    let n = CLEAR_INTERVALS.iter().position(|n| *n == self.state.clear_interval).unwrap_or(0);
                    self.state.clear_interval = CLEAR_INTERVALS[(n + 1) % CLEAR_INTERVALS.len()];
                    self.saved_change();
                    self.changed();
                }
                _ => self.go(Page::Settings),
            },
            Page::StartPoint => { self.subject = self.selected; self.go(Page::UnitChoice); }
            Page::UnitChoice => { self.pending_unit = self.selected as u16; self.go(Page::StartConfirm); }
            Page::StartConfirm => {
                if self.selected == 1 {
                    self.state.starts[self.subject] = self.pending_unit;
                    self.state.unlocked[self.subject] = self.state.unlocked[self.subject].max(self.pending_unit);
                    self.state.offsets[self.subject] = 0;
                    self.state.round = None;
                    self.course_cards.clear();
                    self.saved_change();
                }
                self.go(Page::LearningSettings);
            }
            Page::CourseDone => self.go(Page::Deck),
            Page::DownloadConfirm => {
                if self.selected == 1 {
                    self.platform_requests |= 8;
                    self.go(Page::DownloadPreparing);
                } else {
                    self.go(Page::Settings);
                }
            }
            Page::DownloadPreparing | Page::DownloadPrompt | Page::Sleep => {}
            Page::Reset => {
                if self.selected == 1 && self.reset_check.as_ref().is_some_and(|check| check.passed()) {
                    self.state.progress.clear();
                    self.state.round = None;
                    self.state.offsets = [0; 4];
                    self.state.unlocked = [0; 4];
                    self.state.starts = [0; 4];
                    self.state.course.fill(0);
                    self.course_cards.clear();
                    self.saved_change();
                }
                self.go(Page::Settings);
            }
            Page::Network => self.network_exit(1),
            Page::Error => self.go(Page::Home),
        }
    }
    pub fn key(&mut self, key: Key, pressed: bool) {
        if self.page == Page::Sleep {
            return;
        }
        if key == Key::Parent {
            // 从其他页按返回回到首页后继续按住，不能顺势进入家长区。
            if pressed && self.parent_armed && self.page == Page::Home {
                self.go(Page::Settings);
            }
            return;
        }
        if self.page == Page::Network {
            let bit = 1u8 << key as u32;
            if !pressed {
                self.held &= !bit;
                return;
            }
            if self.held & bit != 0 {
                return;
            }
            self.held |= bit;
            match key {
                Key::Front4 => self.network_exit(1),
                Key::Front3 | Key::Home => self.network_exit(2),
                Key::Shutdown => self.network_exit(3),
                _ => {}
            }
            return;
        }
        if key == Key::Home || key == Key::Shutdown {
            if !pressed {
                self.held &= !(1u8 << key as u32);
                return;
            }
            if self.download_active() {
                self.cancel_download();
            }
            if key == Key::Shutdown {
                self.platform_requests |= 1;
                self.saved_change();
            }
            if self.page == Page::Home && self.selected == 0 {
                self.parent_armed = false;
            } else {
                self.go(Page::Home);
            }
            return;
        }
        let bit = 1u8 << (key as u32);
        if !pressed {
            self.held &= !bit;
            if key == Key::Front3 {
                self.parent_armed = false;
            }
            return;
        }
        // BUSY/休眠等待也必须立即接受返回取消，不依赖显示完成。
        if self.download_active() {
            if key == Key::Front3 {
                self.cancel_download();
                self.go(Page::Home);
            }
            self.held |= bit;
            return;
        }
        if self.held & bit != 0 {
            return;
        }
        let clean = self.held == 0;
        self.held |= bit;
        if key == Key::Up || key == Key::Down {
            self.navigate(key == Key::Down);
            return;
        }
        // 导航可以合并；正面作答必须对应已经显示且无旧按键的页面。
        if !clean || !self.ready() {
            return;
        }
        if self.page == Page::Reset && self.reset_check.as_ref().is_some_and(|check| !check.passed()) {
            self.answer_reset_check(key as u8);
            return;
        }
        if self.page == Page::Learn {
            let round = self.state.round.as_ref().unwrap();
            if round.mode == Mode::Quiz && round.answers[round.cursor] < 0 {
                self.answer(key as u8);
                return;
            }
        }
        match key {
            Key::Front1 => self.navigate(false),
            Key::Front2 => self.navigate(true),
            // 首页的返回键只用于长按进入家长区，短按不重绘。
            Key::Front3 if self.page == Page::Home => self.parent_armed = true,
            Key::Front3 if self.page == Page::Sources => self.go(Page::Settings),
            Key::Front3 => self.go(Page::Home),
            Key::Front4 => self.confirm(),
            _ => {}
        }
    }
    pub fn download_active(&self) -> bool {
        matches!(self.page, Page::DownloadPreparing | Page::DownloadPrompt)
    }
    fn cancel_download(&mut self) {
        self.platform_requests = (self.platform_requests & !8) | 16;
    }
    pub fn download_ready(&mut self) -> Option<u64> {
        if self.page != Page::DownloadPreparing || self.dirty {
            return None;
        }
        self.go(Page::DownloadPrompt);
        Some(self.revision)
    }
    pub fn download_failed(&mut self, reason: u32) {
        if !self.download_active() {
            return;
        }
        let message = match reason {
            2 => "芯片已禁用软件进入刷机模式",
            3 => "芯片已禁用刷机模式",
            4 => "芯片已禁用USB串口",
            5 => "芯片已禁用USB下载",
            6 => "安全下载受限，无法使用普通USB下载",
            7 => "进度保存失败，无法进入刷机模式",
            8 => "存储卡不可用，无法进入刷机模式",
            9 => "蓝牙清理失败，无法进入刷机模式",
            10 => "屏幕刷新失败，无法进入刷机模式",
            11 => "屏幕休眠失败，无法进入刷机模式",
            12 => "刷机入口异常，本次开机已停用",
            13 => "进度尚未保存，无法进入刷机模式",
            _ => "当前平台不支持刷机模式",
        };
        self.show_error(message);
    }
    /// 平台已确认无线释放后调用；存档和待显示帧仍由模型独立复核。
    pub fn sleep_ready(&mut self) -> Option<u64> {
        if self.dirty
            || self.storage != 0
            || self.network_owned
            || self.rendering.is_some()
            || self.download_active()
        {
            return None;
        }
        self.go(Page::Sleep);
        Some(self.revision)
    }
    pub fn sleep_cancel(&mut self) -> bool {
        if self.page != Page::Sleep {
            return false;
        }
        self.rendering = None;
        self.rendering_page = None;
        self.go(Page::Home);
        true
    }
    pub fn storage_status(&mut self, status: u32) {
        if self.storage != status {
            self.storage = status;
            if self.page != Page::Error {
                self.changed();
            }
        }
    }
}

#[cfg(test)]
#[path = "model/refresh_tests.rs"]
mod refresh_tests;
