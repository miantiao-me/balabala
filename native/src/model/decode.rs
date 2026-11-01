use super::content::{text_ok, valid_id};
use super::{Card, Mode, SUBJECTS};
use alloc::{
    string::{String, ToString},
    vec::Vec,
};
use core::cell::Cell;
use serde::{
    de::{DeserializeSeed, Error as _, SeqAccess, Visitor},
    Deserialize,
};
use serde_json::value::RawValue;

// RawValue借用输入；先限制合法文本可能占用的原始跨度，再解转义并创建拥有型字符串。
// 字段名和未知字段的serde scratch仍不受这些字段限制约束。

pub(super) struct Budget {
    pub(super) used: Cell<usize>,
    pub(super) limit: usize,
    pub(super) exceeded: Cell<bool>,
}
impl Budget {
    pub(super) fn charge(&self, bytes: usize) -> Result<(), &'static str> {
        let used = self.used.get().checked_add(bytes).ok_or("content limit")?;
        if used > self.limit {
            self.exceeded.set(true);
            return Err("content limit");
        }
        self.used.set(used);
        Ok(())
    }
}
#[derive(Clone, Copy)]
pub(super) enum StringRule {
    Id,
    Text(usize),
    Subject,
}
fn raw_string(
    raw: &RawValue,
    rule: StringRule,
    budget: Option<&Budget>,
) -> Result<String, serde_json::Error> {
    let max = match rule {
        StringRule::Id => 32,
        StringRule::Subject => 7,
        StringRule::Text(_) => 192,
    };
    if raw.get().len() > 6 * max + 2 {
        return Err(serde_json::Error::custom("string raw limit"));
    }
    struct Text<'a> {
        rule: StringRule,
        budget: Option<&'a Budget>,
    }
    impl<'de> Visitor<'de> for Text<'_> {
        type Value = String;
        fn expecting(&self, f: &mut core::fmt::Formatter) -> core::fmt::Result {
            f.write_str("bounded string")
        }
        fn visit_str<E: serde::de::Error>(self, value: &str) -> Result<String, E> {
            let valid = match self.rule {
                StringRule::Id => valid_id(value),
                StringRule::Subject => SUBJECTS.contains(&value),
                StringRule::Text(chars) => text_ok(value, chars),
            };
            if !valid {
                return Err(E::custom("string limit or format"));
            }
            if let Some(budget) = self.budget {
                budget.charge(value.len()).map_err(E::custom)?;
            }
            Ok(value.to_string())
        }
    }
    serde::Deserializer::deserialize_str(
        &mut serde_json::Deserializer::from_str(raw.get()),
        Text { rule, budget },
    )
}
fn string_field<'de, D: serde::Deserializer<'de>>(
    d: D,
    rule: StringRule,
) -> Result<String, D::Error> {
    raw_string(<&RawValue>::deserialize(d)?, rule, None)
        .map_err(|_| D::Error::custom("string limit or format"))
}
pub(super) fn bounded_id<'de, D: serde::Deserializer<'de>>(d: D) -> Result<String, D::Error> {
    string_field(d, StringRule::Id)
}
pub(super) fn bounded_subject<'de, D: serde::Deserializer<'de>>(d: D) -> Result<String, D::Error> {
    string_field(d, StringRule::Subject)
}
pub(super) fn bounded_text<'de, D: serde::Deserializer<'de>, const N: usize>(
    d: D,
) -> Result<String, D::Error> {
    string_field(d, StringRule::Text(N))
}
pub(super) fn bounded_mode<'de, D: serde::Deserializer<'de>>(d: D) -> Result<Mode, D::Error> {
    let raw = <&RawValue>::deserialize(d)?;
    if raw.get().len() > 6 * 4 + 2 {
        return Err(D::Error::custom("mode limit"));
    }
    serde_json::from_str(raw.get()).map_err(|_| D::Error::custom("mode format"))
}
struct RejectExtra;
impl<'de> Deserialize<'de> for RejectExtra {
    fn deserialize<D: serde::Deserializer<'de>>(_: D) -> Result<Self, D::Error> {
        Err(D::Error::custom("collection limit"))
    }
}
pub(super) fn bounded_vec<'de, D: serde::Deserializer<'de>, T: Deserialize<'de>, const N: usize>(
    d: D,
) -> Result<Vec<T>, D::Error> {
    struct Items<T, const N: usize>(core::marker::PhantomData<T>);
    impl<'de, T: Deserialize<'de>, const N: usize> Visitor<'de> for Items<T, N> {
        type Value = Vec<T>;
        fn expecting(&self, f: &mut core::fmt::Formatter) -> core::fmt::Result {
            f.write_str("bounded collection")
        }
        fn visit_seq<A: SeqAccess<'de>>(self, mut seq: A) -> Result<Vec<T>, A::Error> {
            let mut values = Vec::new();
            while values.len() < N {
                match seq.next_element()? {
                    Some(value) => values.push(value),
                    None => return Ok(values),
                }
            }
            let _ = seq.next_element::<RejectExtra>()?;
            Ok(values)
        }
    }
    d.deserialize_seq(Items::<T, N>(core::marker::PhantomData))
}
#[derive(Deserialize)]
struct SavedId(#[serde(deserialize_with = "bounded_id")] String);
pub(super) fn bounded_ids<'de, D: serde::Deserializer<'de>>(d: D) -> Result<Vec<String>, D::Error> {
    bounded_vec::<D, SavedId, 10>(d).map(|ids| ids.into_iter().map(|id| id.0).collect())
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub(super) struct DeckEnvelope<'a> {
    pub(super) schema: u8,
    pub(super) min_format: u8,
    pub(super) version: u32,
    #[serde(deserialize_with = "bounded_id")]
    pub(super) id: String,
    #[serde(deserialize_with = "bounded_subject")]
    pub(super) subject: String,
    #[serde(deserialize_with = "bounded_text::<_,16>")]
    pub(super) title: String,
    #[serde(borrow)]
    pub(super) cards: &'a RawValue,
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct RawCard<'a> {
    #[serde(borrow)]
    id: &'a RawValue,
    #[serde(borrow)]
    front: &'a RawValue,
    #[serde(borrow)]
    back: &'a RawValue,
    #[serde(borrow)]
    question: &'a RawValue,
    #[serde(borrow)]
    options: [&'a RawValue; 4],
    correct: u8,
    #[serde(borrow)]
    explanation: &'a RawValue,
}
pub(super) struct CardsSeed<'a>(pub(super) &'a Budget);
impl<'de> Visitor<'de> for CardsSeed<'_> {
    type Value = Vec<Card>;
    fn expecting(&self, f: &mut core::fmt::Formatter) -> core::fmt::Result {
        f.write_str("1..40 cards")
    }
    fn visit_seq<A: SeqAccess<'de>>(self, mut seq: A) -> Result<Vec<Card>, A::Error> {
        let mut cards = Vec::new();
        while cards.len() < 40 {
            let Some(raw) = seq.next_element::<&RawValue>()? else {
                return Ok(cards);
            };
            let old_capacity = cards.capacity();
            cards.reserve(1);
            self.0
                .charge((cards.capacity() - old_capacity) * 112)
                .map_err(A::Error::custom)?;
            let c: RawCard =
                serde_json::from_str(raw.get()).map_err(|_| A::Error::custom("invalid card"))?;
            let string = |raw, rule| {
                raw_string(raw, rule, Some(self.0))
                    .map_err(|_| A::Error::custom("card string limit or format"))
            };
            let [a, b, c0, d] = c.options;
            cards.push(Card {
                id: string(c.id, StringRule::Id)?,
                front: string(c.front, StringRule::Text(12))?,
                back: string(c.back, StringRule::Text(64))?,
                question: string(c.question, StringRule::Text(32))?,
                options: [
                    string(a, StringRule::Text(16))?,
                    string(b, StringRule::Text(16))?,
                    string(c0, StringRule::Text(16))?,
                    string(d, StringRule::Text(16))?,
                ],
                correct: c.correct,
                explanation: string(c.explanation, StringRule::Text(64))?,
            });
        }
        let _ = seq.next_element::<RejectExtra>()?;
        Ok(cards)
    }
}
impl<'de> DeserializeSeed<'de> for CardsSeed<'_> {
    type Value = Vec<Card>;
    fn deserialize<D: serde::Deserializer<'de>>(self, d: D) -> Result<Vec<Card>, D::Error> {
        d.deserialize_seq(self)
    }
}

pub(super) fn bounded_cards<'de, D: serde::Deserializer<'de>>(d: D) -> Result<Vec<Card>, D::Error> {
    bounded_vec::<D, Card, 40>(d)
}
pub(super) fn bounded_options<'de, D: serde::Deserializer<'de>>(
    d: D,
) -> Result<[String; 4], D::Error> {
    let [a, b, c, e] = <&RawValue>::deserialize(d).and_then(|raw| {
        serde_json::from_str::<[&RawValue; 4]>(raw.get())
            .map_err(|_| D::Error::custom("options format"))
    })?;
    let read = |raw| {
        raw_string(raw, StringRule::Text(16), None)
            .map_err(|_| D::Error::custom("option limit or format"))
    };
    Ok([read(a)?, read(b)?, read(c)?, read(e)?])
}
