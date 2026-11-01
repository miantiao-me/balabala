//! 选一选的字号选择与均衡折行。View 生成与导入校验共用同一函数，导入通过即保证能排进模板。
use crate::{fonts, layout::*};
use alloc::{string::String, vec::Vec};
use core::ops::Range;

/// 不能出现在行首的标点：并到前一个断行单位。
const CLOSING: &str = "，。、？！：；）》」』”’…,.?!:;)]}%";
/// 不能出现在行尾的标点：并到后一个断行单位。
const OPENING: &str = "（《「『“‘([{";

struct Unit {
    range: Range<usize>,
    width: usize,
    /// 前面有空格时，同一行内要加上的空格宽度；行首不计。
    gap: usize,
}

fn word_char(c: char) -> bool {
    c.is_alphanumeric() && (c as u32) < 0x2E80
}

/// 拆成不可再分的断行单位：西文词和数字整体，汉字逐字，标点按禁则黏附。
fn units(
    text: &str,
    advance: &impl Fn(char) -> Option<usize>,
    split_words: bool,
) -> Option<Vec<Unit>> {
    let mut out: Vec<Unit> = Vec::new();
    let (mut gap, mut prefix, mut prefix_end, mut prefix_w, mut joinable) =
        (0, None, 0, 0, false);
    for (byte, c) in text.char_indices() {
        let end = byte + c.len_utf8();
        let a = advance(c)?;
        if c == ' ' {
            gap = a;
            joinable = false;
        } else if CLOSING.contains(c) && !out.is_empty() && prefix.is_none() {
            let last = out.last_mut().unwrap();
            last.range.end = end;
            last.width += a;
            joinable = false;
        } else if OPENING.contains(c) {
            prefix.get_or_insert(byte);
            prefix_end = end;
            prefix_w += a;
        } else if joinable && !split_words && word_char(c) && prefix.is_none() && gap == 0 {
            let last = out.last_mut().unwrap();
            last.range.end = end;
            last.width += a;
        } else {
            out.push(Unit {
                range: prefix.take().unwrap_or(byte)..end,
                width: prefix_w + a,
                gap: if out.is_empty() { 0 } else { gap },
            });
            prefix_w = 0;
            gap = 0;
            joinable = word_char(c);
        }
    }
    if let Some(start) = prefix {
        out.push(Unit {
            range: start..prefix_end,
            width: prefix_w,
            gap: if out.is_empty() { 0 } else { gap },
        });
    }
    Some(out)
}

fn line_groups(units: &[Unit], limit: usize) -> impl Iterator<Item = &[Unit]> {
    let mut start = 0;
    core::iter::from_fn(move || {
        if start == units.len() {
            return None;
        }
        let (mut end, mut width) = (start, 0);
        while let Some(unit) = units.get(end) {
            let gap = if end == start { 0 } else { unit.gap };
            if end > start && width + gap + unit.width > limit {
                break;
            }
            width += gap + unit.width;
            end += 1;
        }
        let line = &units[start..end];
        start = end;
        Some(line)
    })
}
fn greedy(text: &str, units: &[Unit], limit: usize) -> Vec<String> {
    line_groups(units, limit)
        .map(|group| {
            let mut line = String::new();
            for unit in group {
                if !line.is_empty() && unit.gap > 0 {
                    line.push(' ');
                }
                for piece in text[unit.range.clone()].split(' ') {
                    line.push_str(piece);
                }
            }
            line
        })
        .collect()
}

/// 在 width 内用最少行数折行，再把行宽上限尽量压低，让各行长度接近。
/// 缺字形、width 为 0、或某个不可拆单位比 width 还宽时返回 None，由调用方换字号或拒绝导入。
/// split_words=true 时西文词也逐字拆开，只用于最小字号兜底。输入不含换行符（导入已禁止控制字符）。
pub fn wrap_balanced(
    text: &str,
    width: usize,
    advance: impl Fn(char) -> Option<usize>,
    split_words: bool,
) -> Option<Vec<String>> {
    let units = units(text, &advance, split_words)?;
    let widest = units.iter().map(|u| u.width).max().unwrap_or(0);
    if width == 0 || widest > width {
        return None;
    }
    let count = line_groups(&units, width).count();
    // 行数随上限单调不增，二分找仍保持最少行数的最小上限。
    let (mut lo, mut hi) = (widest, width);
    while lo < hi {
        let mid = (lo + hi) / 2;
        if line_groups(&units, mid).count() <= count {
            hi = mid
        } else {
            lo = mid + 1
        }
    }
    Some(greedy(text, &units, lo))
}

/// (字号槽, 行距)，从大到小尝试，与 App.vue 中 questionSize / optionSize 的三元分支一一对应。
const QUESTION_TIERS: [(u8, i32); 2] = [
    (fonts::BOLD_48, QUIZ_QUESTION_LEADING[0]),
    (fonts::REGULAR_24, QUIZ_QUESTION_LEADING[1]),
];
const OPTION_TIERS: [(u8, i32); 3] = [
    (fonts::BOLD_48, QUIZ_OPTION_LEADING[0]),
    (fonts::REGULAR_32, QUIZ_OPTION_LEADING[1]),
    (fonts::REGULAR_24, QUIZ_OPTION_LEADING[2]),
];

/// 在 rect 内按该档字号折行；最小一档的整词排版超宽或超行数时才逐字拆词。
fn fit_in(text: &str, rect: [i32; 4], (slot, leading): (u8, i32), last: bool) -> Option<String> {
    let advance = |c| fonts::advance(c, slot).map(usize::from);
    let fits =
        |lines: &Vec<String>| !lines.is_empty() && lines.len() <= (rect[3] / leading) as usize;
    let width = rect[2] as usize;
    wrap_balanced(text, width, advance, false)
        .filter(fits)
        .or_else(|| {
            last.then(|| wrap_balanced(text, width, advance, true).filter(fits))
                .flatten()
        })
        .map(|lines| lines.join("\n"))
}
pub enum Front {
    /// 特大字档位，见 art::GIANT_PX。
    Giant(usize),
    Bold(String),
}
/// 认一认正面：先试特大字两档（字符须在内置字表内），否则按 48px Bold 折行。
pub fn front(text: &str) -> Option<Front> {
    let [_, _, w, h] = READ_FRONT;
    (0..crate::art::GIANT_PX.len())
        .find(|&tier| {
            crate::art::GIANT_PX[tier] <= h
                && crate::art::giant_width(text, tier).is_some_and(|g| g <= w as usize)
        })
        .map(Front::Giant)
        .or_else(|| {
            fit_in(
                text,
                READ_FRONT,
                (fonts::BOLD_48, QUIZ_QUESTION_LEADING[0]),
                true,
            )
            .map(Front::Bold)
        })
}
/// 返回 (字号槽, 含换行的文字)；None 表示任何字号都排不下。
pub fn question(text: &str) -> Option<(u8, String)> {
    let last = QUESTION_TIERS.len() - 1;
    QUESTION_TIERS
        .iter()
        .enumerate()
        .find_map(|(i, &tier)| fit_in(text, QUIZ_QUESTION, tier, i == last).map(|t| (tier.0, t)))
}
/// 四个选项共用一档字号：取四项都放得下的最大一档，避免同一题大小不一。
pub fn options(options: &[String; 4]) -> Option<(u8, [String; 4])> {
    let last = OPTION_TIERS.len() - 1;
    OPTION_TIERS.iter().enumerate().find_map(|(i, &tier)| {
        let mut out: [String; 4] = Default::default();
        for (slot, text) in out.iter_mut().zip(options) {
            *slot = fit_in(text, QUIZ_OPTIONS[0], tier, i == last)?;
        }
        Some((tier.0, out))
    })
}

/// 24px 正文均衡折行；整词放不下时逐字拆开。缺字时原样返回，由 MicroTS 按缺字形处理。
pub fn body(text: &str, width: i32) -> String {
    let advance = |c| fonts::advance(c, fonts::REGULAR_24).map(usize::from);
    wrap_balanced(text, width as usize, advance, false)
        .or_else(|| wrap_balanced(text, width as usize, advance, true))
        .map_or_else(|| text.into(), |lines| lines.join("\n"))
}
/// 选一选作答后的解释：24px，限定在反馈区行数内。
pub fn explanation(text: &str) -> Option<String> {
    fit_in(
        text,
        QUIZ_FEEDBACK_BODY,
        (fonts::REGULAR_24, QUIZ_QUESTION_LEADING[1]),
        true,
    )
}
/// 认一认背面：上方小号正面一行（48 Bold 放不下退回 24px），下方背面文字 48 Bold 最多 READ_BOLD_BACK_LINES 行，否则 24px。
pub fn back(front: &str, back: &str) -> Option<((u8, String), (u8, String))> {
    let bold = (fonts::BOLD_48, READ_LEADING[0]);
    let small = (fonts::REGULAR_24, READ_LEADING[1]);
    let front = fit_in(front, READ_BACK_FRONT, bold, false)
        .map(|t| (bold.0, t))
        .or_else(|| fit_in(front, READ_BACK_FRONT, small, true).map(|t| (small.0, t)))?;
    let bold_rect = [
        READ_BACK[0],
        READ_BACK[1],
        READ_BACK[2],
        READ_LEADING[0] * READ_BOLD_BACK_LINES,
    ];
    let back = fit_in(back, bold_rect, bold, false)
        .map(|t| (bold.0, t))
        .or_else(|| fit_in(back, READ_BACK, small, true).map(|t| (small.0, t)))?;
    Some((front, back))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn builtin_cards_fit_and_import_limits_always_fit() {
        let mut sizes = alloc::collections::BTreeMap::new();
        for deck in crate::builtin_decks() {
            for card in &deck.cards {
                let (q, text) = question(&card.question).expect(&card.question);
                assert!(text
                    .split('\n')
                    .all(|l| fonts::width(l, q) <= QUIZ_QUESTION[2] as usize));
                let (o, _) = options(&card.options).expect(&card.id);
                *sizes.entry((deck.subject.clone(), o)).or_insert(0) += 1;
            }
        }
        println!("option sizes by subject: {sizes:?}");
        // 导入上限（≤16 字、24px ≤370px）内的最宽选项也必须排得下，选一选不能收窄导入契约。
        for text in [
            "山".repeat(15),
            "W".repeat(16),
            "m".repeat(16),
            "@".repeat(16),
            "abcdefghijklmnop".into(),
            "pneumonia pneumonia".into(),
        ] {
            assert!(fonts::width(&text, fonts::REGULAR_24) <= 370);
            let opts = [text.clone(), "a".into(), "b".into(), "c".into()];
            assert_eq!(
                options(&opts).map(|o| o.0),
                Some(fonts::REGULAR_24),
                "{text}"
            );
        }
        assert_eq!(
            question(&"山".repeat(17)).unwrap(),
            (
                fonts::BOLD_48,
                format!("{}\n{}", "山".repeat(9), "山".repeat(8))
            )
        );
    }

    // 近似 48px：全角 48，ASCII 字母/数字 26，空格 12。
    fn adv(c: char) -> Option<usize> {
        match c {
            '缺' => None,
            ' ' => Some(12),
            c if c.is_ascii() => Some(26),
            _ => Some(48),
        }
    }
    fn wrap(s: &str, w: usize) -> Option<Vec<String>> {
        wrap_balanced(s, w, adv, false)
    }

    #[test]
    fn seventeen_chars_use_two_lines_not_three() {
        let s = "山".repeat(17);
        assert_eq!(wrap(&s, 432).unwrap(), vec!["山".repeat(9), "山".repeat(8)]);
    }

    #[test]
    fn consecutive_closing_marks_never_start_a_line() {
        let s = format!("{}？？{}", "山".repeat(8), "山".repeat(7));
        for line in wrap(&s, 432).unwrap() {
            assert!(!line.starts_with('？'), "{line}");
        }
    }

    #[test]
    fn opening_mark_never_ends_a_line() {
        let s = format!("{}《山》", "山".repeat(8));
        for line in wrap(&s, 432).unwrap() {
            assert!(!line.ends_with('《'), "{line}");
        }
    }

    #[test]
    fn edge_cases() {
        assert_eq!(wrap("", 432).unwrap(), Vec::<String>::new());
        assert_eq!(wrap(&"山".repeat(9), 432).unwrap(), vec!["山".repeat(9)]);
        assert_eq!(
            wrap(&"山".repeat(10), 432).unwrap(),
            vec!["山".repeat(5), "山".repeat(5)]
        );
        assert_eq!(wrap("山", 0), None);
        assert_eq!(wrap("山缺", 432), None);
    }

    #[test]
    fn english_breaks_at_spaces_and_keeps_words_whole() {
        assert_eq!(
            wrap("red apple tree", 240).unwrap(),
            vec!["red apple", "tree"]
        );
        assert_eq!(wrap("strawberry", 96), None);
        let split = wrap_balanced("strawberry", 96, adv, true).unwrap();
        assert!(split.len() > 1 && split.concat() == "strawberry");
    }

    #[test]
    fn spaces_normalize_without_changing_measured_line_width() {
        let expected = wrap("red apple tree", 240).unwrap();
        assert_eq!(wrap("  red   apple   tree  ", 240).unwrap(), expected);
        assert!(expected
            .iter()
            .all(|line| line.chars().map(|c| adv(c).unwrap()).sum::<usize>() <= 240));
        assert!(question("   ").is_none());
        assert!(explanation("   ").is_none());
    }

    #[test]
    fn punctuation_preserves_spacing_and_utf8_word_boundaries() {
        for (input, expected) in [
            ("《 山 》", "《山》"),
            ("red ,apple", "red, apple"),
            ("《 （山） 》", "《（山）》"),
            ("山 ？！", "山？！"),
            (" 《 （  ", "《（"),
            ("山 《 （  ", "山 《（"),
            ("café naïve", "café naïve"),
            ("山\t水", "山\t水"),
        ] {
            assert_eq!(wrap(input, 2000).unwrap(), vec![expected], "{input:?}");
        }
        assert_eq!(wrap("café", 160).unwrap(), vec!["café"]);
        assert_eq!(wrap("café", 100), None);
        assert_eq!(wrap_balanced("café", 100, adv, true).unwrap(), vec!["ca", "fé"]);
        assert_eq!(wrap_balanced("《 山 》 red ,apple", 2000, adv, true).unwrap(), vec!["《山》 red, apple"]);
        // 即使空格被归一化或并入标点，也必须检查原文的每个字形。
        for input in [" ", "《 山 》", "red ,apple", "《  "] {
            assert_eq!(wrap_balanced(input, 2000, |c| if c == ' ' { None } else { adv(c) }, false), None);
        }
    }

    #[test]
    fn mixed_question_keeps_numbers_together() {
        let lines = wrap("9根小棒再添10根是几根？", 432).unwrap();
        assert!(lines.iter().all(|l| !l.starts_with('？')));
        assert!(lines.iter().any(|l| l.contains("10")));
    }
}
