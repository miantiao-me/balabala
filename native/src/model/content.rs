use super::view::page_font_budget;
use super::{decode::*, Model, Page};
use alloc::{string::String, vec::Vec};
use core::cell::Cell;
use serde::{de::DeserializeSeed, Deserialize};

pub const SUBJECTS: [&str; 4] = ["hanzi", "pinyin", "english", "math"];
pub const SUBJECT_NAMES: [&str; 4] = ["汉字", "拼音", "英语", "数学"];
pub const MAX_PACKAGE: usize = 65536;
// C3容量计费：每Deck预留64B、每Card槽112B，加所有字符串实际capacity。
// 仅活动内容32KiB；存档/进度、导入重叠、任务栈及平台预留另计。
pub const MAX_ACTIVE_CONTENT_BYTES: usize = 32768;

#[derive(Clone, Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Card {
    #[serde(deserialize_with = "bounded_id")]
    pub id: String,
    #[serde(deserialize_with = "bounded_text::<_,12>")]
    pub front: String,
    #[serde(deserialize_with = "bounded_text::<_,64>")]
    pub back: String,
    #[serde(deserialize_with = "bounded_text::<_,32>")]
    pub question: String,
    #[serde(deserialize_with = "bounded_options")]
    pub options: [String; 4],
    pub correct: u8,
    #[serde(deserialize_with = "bounded_text::<_,64>")]
    pub explanation: String,
}
#[derive(Clone, Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Deck {
    pub schema: u8,
    pub min_format: u8,
    pub version: u32,
    #[serde(deserialize_with = "bounded_id")]
    pub id: String,
    #[serde(deserialize_with = "bounded_subject")]
    pub subject: String,
    #[serde(deserialize_with = "bounded_text::<_,16>")]
    pub title: String,
    #[serde(deserialize_with = "bounded_cards")]
    pub cards: Vec<Card>,
}
#[cfg(target_pointer_width = "32")]
const _: () = {
    assert!(core::mem::size_of::<Card>() <= 112);
    assert!(core::mem::size_of::<Deck>() <= 64);
};
pub(super) fn valid_id(id: &str) -> bool {
    !id.is_empty()
        && id.len() <= 32
        && id
            .bytes()
            .all(|b| b.is_ascii_lowercase() || b.is_ascii_digit() || b == b'-')
}
pub(super) fn text_ok(text: &str, max_chars: usize) -> bool {
    text.chars().any(|c| !c.is_whitespace())
        && text.len() <= 192
        && text.chars().count() <= max_chars
        && text.chars().all(|c| {
            !c.is_control() && crate::fonts::advance(c, crate::fonts::REGULAR_24).is_some()
        })
}
pub fn parse_deck(bytes: &[u8]) -> Result<Deck, &'static str> {
    parse_deck_limited(bytes, None)
}
fn parse_deck_limited(bytes: &[u8], limits: Option<[usize; 4]>) -> Result<Deck, &'static str> {
    if bytes.is_empty() {
        return Err("卡组缺失，请重新导入");
    }
    if bytes.len() > MAX_PACKAGE {
        return Err("卡组超过大小上限");
    }
    let h: DeckEnvelope = serde_json::from_slice(bytes).map_err(|_| "卡组格式损坏或数量超限")?;
    let subject = SUBJECTS
        .iter()
        .position(|s| *s == h.subject)
        .ok_or("卡组版本或标识无效")?;
    let budget = Budget {
        used: Cell::new(0),
        limit: limits.map_or(usize::MAX, |l| l[subject]),
        exceeded: Cell::new(false),
    };
    budget
        .charge(64 + h.id.capacity() + h.subject.capacity() + h.title.capacity())
        .map_err(|_| "全部卡组超出常驻内存上限")?;
    let cards = CardsSeed(&budget)
        .deserialize(&mut serde_json::Deserializer::from_str(h.cards.get()))
        .map_err(|_| {
            if budget.exceeded.get() {
                "全部卡组超出常驻内存上限"
            } else {
                "卡组格式损坏或数量超限"
            }
        })?;
    let deck = Deck {
        schema: h.schema,
        min_format: h.min_format,
        version: h.version,
        id: h.id,
        subject: h.subject,
        title: h.title,
        cards,
    };
    if deck.schema != 1
        || deck.min_format != 1
        || deck.version == 0
        || !valid_id(&deck.id)
        || !SUBJECTS.contains(&deck.subject.as_str())
        || deck.cards.is_empty()
        || deck.cards.len() > 40
    {
        return Err("卡组版本或标识无效");
    }
    if !text_ok(&deck.title, 16) || crate::fonts::width(&deck.title, crate::fonts::LEGACY_24) > 280
    {
        return Err("卡组标题过长或缺字");
    }
    for (i, c) in deck.cards.iter().enumerate() {
        if !valid_id(&c.id) || deck.cards[..i].iter().any(|a| a.id == c.id) {
            return Err("卡片标识无效或重复");
        }
        validate_card(c)?;
    }
    if page_font_budget(&deck)? > crate::fonts::MAX_PAGE_FONT_BYTES {
        return Err("页面字体超出内存上限");
    }
    Ok(deck)
}

pub(super) fn validate_card(c: &Card) -> Result<(), &'static str> {
    if c.correct > 3 {
        return Err("选项重复或答案无效");
    }
    if !text_ok(&c.front, 12)
        || c.front
            .chars()
            .any(|ch| crate::fonts::advance(ch, crate::fonts::REGULAR_48).is_none())
        || crate::fonts::width(&c.front, crate::fonts::REGULAR_48) > 424
    {
        return Err("卡片正面过长或缺字");
    }
    if !text_ok(&c.question, 32)
        || crate::fonts::width(&c.question, crate::fonts::LEGACY_24) > 424
    {
        return Err("题目过长或缺字，请缩短");
    }
    for option in &c.options {
        if !text_ok(option, 16) || crate::fonts::width(option, crate::fonts::LEGACY_24) > 370 {
            return Err("选项过长或缺字，请缩短");
        }
    }
    for text in [&c.back, &c.explanation] {
        if !text_ok(text, 64) || wrap(text, 424).len() > 3 {
            return Err("解释过长或缺字，请缩短");
        }
    }
    // 新模板与导入共用同一排版函数：上面的限额内总能排下，这里保证导入通过即可显示。
    if crate::fit::front(&c.front).is_none() {
        return Err("卡片正面过长或缺字");
    }
    if crate::fit::question(&c.question).is_none() {
        return Err("题目过长或缺字，请缩短");
    }
    let (_, options) = crate::fit::options(&c.options).ok_or("选项过长或缺字，请缩短")?;
    if options
        .iter()
        .enumerate()
        .any(|(n, option)| options[..n].contains(option))
    {
        return Err("选项重复或答案无效");
    }
    if crate::fit::explanation(&c.explanation).is_none()
        || crate::fit::back(&c.front, &c.back).is_none()
    {
        return Err("解释过长或缺字，请缩短");
    }
    Ok(())
}
pub(super) fn card_content_bytes(c: &Card) -> usize {
    112 + c.id.capacity() + c.front.capacity() + c.back.capacity() + c.question.capacity()
        + c.options.iter().map(String::capacity).sum::<usize>() + c.explanation.capacity()
}
fn deck_content_bytes(deck: &Deck) -> usize {
    64 + deck.id.capacity()
        + deck.subject.capacity()
        + deck.title.capacity()
        + (deck.cards.capacity() - deck.cards.len()) * 112
        + deck.cards.iter().map(card_content_bytes).sum::<usize>()
}

pub fn wrap(text: &str, width: usize) -> Vec<String> {
    let mut lines = Vec::new();
    let mut line = String::new();
    let mut w = 0;
    for c in text.chars() {
        let advance = crate::fonts::advance(c, crate::fonts::LEGACY_24).unwrap_or(24) as usize;
        if w + advance > width && !line.is_empty() {
            lines.push(line);
            line = String::new();
            w = 0;
        }
        line.push(c);
        w += advance;
    }
    if !line.is_empty() {
        lines.push(line);
    }
    lines
}

impl Model {
    pub fn content_bytes(&self) -> usize {
        self.course_cards.iter().map(card_content_bytes).sum::<usize>()
            + (self.course_cards.capacity()-self.course_cards.len())*112
            + self.decks.capacity() * 64
            + self
                .decks
                .iter()
                .map(|deck| deck_content_bytes(deck) - 64)
                .sum::<usize>()
    }
    pub fn import(&mut self, bytes: &[u8]) -> Result<(), &'static str> {
        if self.course_active() { return Err("请使用当前完整课程包"); }
        let current = self.content_bytes();
        let limits = core::array::from_fn(|s| {
            (MAX_ACTIVE_CONTENT_BYTES + deck_content_bytes(&self.decks[s])).saturating_sub(current)
        });
        let deck = parse_deck_limited(bytes, Some(limits))?;
        let subject = SUBJECTS.iter().position(|s| *s == deck.subject).unwrap();
        let old = &self.decks[subject];
        if deck.id == old.id && deck.version < old.version {
            return Err("卡组版本过旧");
        }
        let content_bytes = self.decks.capacity() * 64
            + self
                .decks
                .iter()
                .enumerate()
                .map(|(s, old)| deck_content_bytes(if s == subject { &deck } else { old }) - 64)
                .sum::<usize>();
        if content_bytes > MAX_ACTIVE_CONTENT_BYTES {
            return Err("全部卡组超出常驻内存上限");
        }
        if let Some(r) = &self.state.round {
            if r.subject == subject
                && r.ids
                    .iter()
                    .any(|id| !deck.cards.iter().any(|c| &c.id == id))
            {
                // 保留所有仍存在的ID；缺失题不能移位后继续评分。
                self.state.round = None;
            }
        }
        self.state
            .progress
            .retain(|p| p.subject != subject || deck.cards.iter().any(|c| c.id == p.id));
        self.decks[subject] = deck;
        self.imported_subjects |= 1 << subject;
        self.saved_change();
        if self.page == Page::Error {
            self.changed();
        } else {
            self.go(Page::Home);
        }
        Ok(())
    }
}
