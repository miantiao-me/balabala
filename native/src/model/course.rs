use super::{crc32, Card, Mode, Model, Page, Progress, Round};
use alloc::{string::String, vec, vec::Vec};
use core::ffi::c_void;

pub const MAX_COURSE_CARDS: usize = 4096;
pub const MASTERED: u8 = 0x20;
pub const WRONG: u8 = 0x10;
pub const SEEN: u8 = 0x40;
pub const COUNT: u8 = 0x0f;

pub struct CourseCard {
    pub id: &'static str,
    pub subject: u8,
    pub unit: u16,
    pub offset: u32,
    pub bytes: u16,
    pub crc: u32,
    pub key: u32,
}
pub struct CourseUnit {
    pub subject: u8,
    pub unit: u16,
    pub grade: u8,
    pub title: &'static str,
    pub first: u16,
    pub count: u16,
}
include!(concat!(env!("BALABALA_GENERATED"), "/course.rs"));

pub type CourseRead = unsafe extern "C" fn(*mut c_void, u32, *mut u8, usize) -> i32;
#[derive(Clone, Copy)]
pub struct CourseReader {
    pub read: CourseRead,
    pub context: *mut c_void,
}
pub fn index_for_key(key: u32) -> Option<usize> {
    COURSE_KEYS
        .binary_search_by_key(&key, |i| COURSE_CARDS[usize::from(*i)].key)
        .ok()
        .map(|i| usize::from(COURSE_KEYS[i]))
}
pub fn index_for_id(subject: usize, id: &str) -> Option<usize> {
    let name = super::SUBJECTS.get(subject)?;
    let mut key = 0x811c9dc5u32;
    for byte in name.bytes().chain(core::iter::once(b':')).chain(id.bytes()) {
        key = (key ^ u32::from(byte)).wrapping_mul(0x01000193);
    }
    let index = index_for_key(key)?;
    let card = &COURSE_CARDS[index];
    (usize::from(card.subject) == subject && card.id == id).then_some(index)
}
pub fn unit_for(subject: usize, unit: u16) -> Option<&'static CourseUnit> {
    COURSE_UNITS.iter().find(|u| usize::from(u.subject) == subject && u.unit == unit)
}
pub fn next_flags(flags: u8, correct: bool, threshold: u8) -> u8 {
    if flags & MASTERED != 0 { return flags; }
    let count = (flags & COUNT).saturating_add(u8::from(correct)).min(9);
    SEEN | count | if count >= threshold { MASTERED } else if !correct { WRONG } else { 0 }
}
pub fn valid_flags(flags: u8) -> bool {
    flags & 0x80 == 0 && flags & COUNT <= 9
        && (flags == 0 || flags & SEEN != 0)
        && (flags & MASTERED == 0 || flags & COUNT > 0 && flags & WRONG == 0)
}
impl Model {
    pub fn course_active(&self) -> bool { self.course_reader.is_some() }
    pub fn attach_course(&mut self, reader: CourseReader, bytes: u32, crc: u32) -> Result<(), &'static str> {
        if COURSE_CARDS.is_empty() || bytes != COURSE_BYTES || crc != COURSE_CRC {
            return Err("课程包与固件不匹配，请重新复制");
        }
        if self.course_reader.is_some() { return Err("课程已经载入"); }
        self.course_reader = Some(reader);
        self.state.course.resize(COURSE_CARDS.len(), 0);
        self.state.course_id = COURSE_ID;
        self.state.course_version = COURSE_VERSION;
        self.state.course_crc = COURSE_CRC;
        // 原示范ID继续使用；累计证据不足的旧“认识”只迁移为一次。
        for p in &self.state.progress {
            if let Some(i) = index_for_id(p.subject, &p.id) {
                let n = p.correct_count.max(u8::from(p.status == 2));
                self.state.course[i] = SEEN | n | if p.mastered { MASTERED } else if p.status == 1 { WRONG } else { 0 };
            }
        }
        self.state.progress.clear();
        for subject in 0..4 { self.unlock_units(subject); }
        // 完整匹配的四科目录是已验证替换；示范正文不与SD活动轮同时常驻。
        self.imported_subjects = 0x0f;
        for deck in &mut self.decks { deck.cards = Vec::new(); }
        self.state.round = None;
        self.state.offsets = [0; 4];
        self.saved_change();
        if self.page == Page::Error { self.changed(); } else { self.go(Page::Home); }
        Ok(())
    }
    fn read_course_card(&self, index: usize) -> Result<Card, &'static str> {
        let entry = COURSE_CARDS.get(index).ok_or("学习卡片缺失，请重新选卡组")?;
        let reader = self.course_reader.ok_or("课程无法读取，请检查存储卡")?;
        if entry.bytes > 2048 { return Err("课程卡片超过大小上限"); }
        let mut bytes = vec![0; usize::from(entry.bytes)];
        if unsafe { (reader.read)(reader.context, entry.offset, bytes.as_mut_ptr(), bytes.len()) } != 0 {
            return Err("课程无法读取，请检查存储卡");
        }
        if crc32(&[&bytes]) != entry.crc { return Err("课程卡片损坏，请重新复制"); }
        let card: Card = serde_json::from_slice(&bytes).map_err(|_| "课程卡片格式损坏")?;
        if card.id != entry.id { return Err("课程卡片标识不匹配"); }
        super::content::validate_card(&card)?;
        Ok(card)
    }
    pub(super) fn read_course_round(&self, round: Option<&Round>) -> Result<Vec<Card>, &'static str> {
        let Some(r) = round else { return Ok(Vec::new()); };
        let mut cards = Vec::with_capacity(r.ids.len());
        for id in &r.ids {
            let i = index_for_id(r.subject, id).ok_or("课程卡片缺失，请重新复制")?;
            cards.push(self.read_course_card(i)?);
        }
        let bytes = cards.iter().map(super::content::card_content_bytes).sum::<usize>();
        if bytes + self.content_bytes() - self.course_cards.iter().map(super::content::card_content_bytes).sum::<usize>() > super::MAX_ACTIVE_CONTENT_BYTES { return Err("本轮卡片超出内存上限"); }
        Ok(cards)
    }
    pub(super) fn start_course(&mut self, subject: usize, mode: Mode, review: bool) {
        let unlocked = self.state.unlocked[subject];
        let start = self.state.starts[subject];
        let eligible = |i: usize| {
            let c = &COURSE_CARDS[i];
            let flags = self.state.course[i];
            usize::from(c.subject) == subject && c.unit <= unlocked
                && (c.unit >= start || flags != 0) && flags & MASTERED == 0
                && (!review || flags & WRONG != 0)
        };
        if !(0..COURSE_CARDS.len()).any(eligible) {
            if review { self.show_error("还没有需要再看的卡片"); }
            else { self.go(Page::CourseDone); }
            return;
        }
        // 以2:2:1交错错题、新卡与未熟练卡；没有某一类时自动补位。
        let rotation = self.state.offsets[subject] % COURSE_CARDS.len();
        let mut chosen: Vec<usize> = Vec::with_capacity(self.state.count);
        for turn in 0..self.state.count {
            let wanted = [WRONG, 0, SEEN, WRONG, 0][turn % 5];
            let pick = |kind: Option<u8>| (0..COURSE_CARDS.len()).map(|j| (rotation+j)%COURSE_CARDS.len()).find(|i| {
                let flags = self.state.course[*i];
                eligible(*i) && !chosen.contains(i) && kind.map_or(true, |k| if k == 0 { flags == 0 } else if k == WRONG { flags & WRONG != 0 } else { flags != 0 && flags & WRONG == 0 })
            });
            let Some(index) = pick(Some(wanted)).or_else(||pick(None)) else { break; };
            chosen.push(index);
        }
        let mut cards = Vec::with_capacity(chosen.len());
        for &i in &chosen {
            match self.read_course_card(i) {
                Ok(card) => cards.push(card),
                Err(error) => { self.save_protect(); self.show_error(error); return; }
            }
        }
        let charge = cards.iter().map(super::content::card_content_bytes).sum::<usize>()
            + self.decks.iter().map(|d|64+d.id.capacity()+d.subject.capacity()+d.title.capacity()).sum::<usize>();
        if charge > super::MAX_ACTIVE_CONTENT_BYTES {
            self.show_error("本轮卡片超出内存上限"); return;
        }
        let ids = cards.iter().map(|c|c.id.clone()).collect::<Vec<String>>();
        let permutations = self.round_permutations(ids.len());
        self.state.round = Some(Round {subject,answers:vec![-1;ids.len()],ids,cursor:0,mode,flipped:false,permutations});
        self.course_cards = cards;
        self.state.offsets[subject] = (chosen.last().copied().unwrap_or(rotation) + 1) % COURSE_CARDS.len();
        self.saved_change();
        self.go(Page::Learn);
    }
    pub(super) fn unlock_units(&mut self, subject: usize) {
        while let Some(unit) = unit_for(subject, self.state.unlocked[subject]) {
            let first = usize::from(unit.first);
            let count = usize::from(unit.count);
            let mastered = self.state.course[first..first+count].iter().filter(|p| **p & MASTERED != 0).count();
            if mastered * 5 < count * 4 || unit_for(subject, unit.unit + 1).is_none() { break; }
            self.state.unlocked[subject] += 1;
        }
    }
    pub fn set_threshold(&mut self, threshold: u8) -> bool {
        if ![1,3,5,7,9].contains(&threshold) { return false; }
        self.state.threshold = threshold;
        for p in &mut self.state.progress {
            if p.correct_count >= threshold { p.mastered = true; }
        }
        for p in &mut self.state.course {
            if *p & COUNT >= threshold { *p = (*p | MASTERED) & !WRONG; }
        }
        if self.course_active() { for subject in 0..4 { self.unlock_units(subject); } }
        self.saved_change(); self.changed(); true
    }
    pub fn subject_counts(&self, subject: usize) -> [usize; 3] {
        if self.course_active() {
            let mut counts = [0;3];
            for (c,flags) in COURSE_CARDS.iter().zip(&self.state.course) {
                if usize::from(c.subject) == subject {
                    counts[if flags & MASTERED != 0 {0} else if *flags != 0 {1} else {2}] += 1;
                }
            }
            counts
        } else {
            let progress = self.state.progress.iter().filter(|p|p.subject == subject);
            let known = progress.clone().filter(|p|p.mastered).count();
            let learning = progress.count() - known;
            [known,learning,self.decks[subject].cards.len().saturating_sub(known+learning)]
        }
    }
    pub(super) fn record_builtin(&mut self, subject: usize, id: String, correct: bool) {
        let index = self.state.progress.iter().position(|p|p.subject == subject && p.id == id);
        let p = if let Some(i) = index { &mut self.state.progress[i] } else {
            self.state.progress.push(Progress {subject,id,status:1,correct_count:0,mastered:false});
            self.state.progress.last_mut().unwrap()
        };
        if !p.mastered {
            p.status = if correct {2} else {1};
            p.correct_count = p.correct_count.saturating_add(u8::from(correct)).min(9);
            p.mastered = p.correct_count >= self.state.threshold;
        }
    }
}
