use super::{Card, Deck, Mode, Model, Page, Round, SUBJECTS};
use crate::layout::{
    MESSAGE_BODY, SCREEN_DECK, SCREEN_HOME, SCREEN_MESSAGE, SCREEN_QUIZ, SCREEN_READ,
    SCREEN_SETTINGS, SCREEN_SLEEP, SCREEN_STATS,
};
use crate::overlay::{
    CAP_BACK, CAP_CHECK, CAP_FLIP, CAP_LEFT, CAP_NEXT, CAP_RIGHT, CHEERS, DOT_DONE, DOT_NOW,
    DOT_TODO, NAV, PIC_LOOK, PIC_MEDAL, PIC_QUESTION, PIC_SLEEP, PIC_TRASH, PIC_USB, STAR_OFF,
    STAR_ON,
};
use alloc::format;

pub(crate) fn page_font_budget(deck: &Deck) -> Result<usize, &'static str> {
    let budget = |mut view: crate::View| {
        let mut peak = 0;
        for status in 0..=2 {
            storage_hint(&mut view, status);
            peak = peak.max(crate::fonts::construction_budget(&view).map_err(|_| "页面缺字")?);
        }
        Ok::<usize, &'static str>(peak)
    };
    let subject = SUBJECTS
        .iter()
        .position(|s| *s == deck.subject)
        .unwrap_or(0);
    let mut peak = budget(deck_view(deck, 0, 0))?;
    for card in &deck.cards {
        let mut r = Round {
            subject,
            ids: alloc::vec![card.id.clone(); 10],
            answers: alloc::vec![-1; 10],
            cursor: 0,
            mode: Mode::Read,
            flipped: false,
            permutations: alloc::vec![],
        };
        for (mode, flipped, answer) in [
            (Mode::Read, false, -1),
            (Mode::Read, true, -1),
            (Mode::Quiz, false, -1),
            (Mode::Quiz, false, 0),
            (Mode::Quiz, false, 1),
        ] {
            r.mode = mode;
            r.flipped = flipped;
            r.answers[0] = answer;
            peak = peak.max(budget(learning_view(card, &r, 0))?);
        }
    }
    Ok(peak)
}
impl Model {
    pub fn view(&self) -> crate::View {
        let mut view = self.base_view();
        if !matches!(self.page, Page::Error | Page::DownloadPrompt | Page::Sleep) {
            storage_hint(&mut view, self.storage);
        }
        view
    }
    fn base_view(&self) -> crate::View {
        let mut v = crate::View::new();
        v.caps = NAV;
        v.focus = self.selected as u8;
        match self.page {
            Page::Home => {
                v.screen = SCREEN_HOME;
                v.hero = "开始".into();
                if let Some(r) = &self.state.round {
                    v.hero = "继续".into();
                    v.title = CHIPS[r.subject].into();
                    v.marks = round_dots(r);
                }
            }
            Page::Deck => {
                let mut view = deck_view(&self.decks[self.subject], self.selected, self.review_count(self.subject));
                if self.course_active() {
                    if let Some(unit) = super::course::unit_for(self.subject,self.state.unlocked[self.subject]) {
                        view.hero = unit.title.into();
                        view.lines[0] = format!("已学会 {} · 第 {} 单元",self.subject_counts(self.subject)[0],unit.unit+1);
                    }
                }
                return view;
            }
            Page::Learn => {
                let (Some(r), Some(c)) = (&self.state.round, self.card()) else {
                    message(
                        &mut v,
                        PIC_QUESTION,
                        "卡片缺失",
                        "按返回键回到首页",
                        ["", ""],
                    );
                    v.caps = [0, 0, CAP_BACK, 0];
                    return v;
                };
                let mut view = learning_view(c, r, self.state.serial);
                let (count, mastered) = if self.course_active() {
                    let flags = super::course::index_for_id(r.subject,&c.id).map_or(0,|i|self.state.course[i]);
                    (flags & super::course::COUNT, flags & super::course::MASTERED != 0)
                } else {
                    self.state.progress.iter().find(|p|p.subject == r.subject && p.id == c.id).map_or((0,false),|p|(p.correct_count,p.mastered))
                };
                view.hint = if mastered { "已学会".into() } else { format!("{count}/{} 次", self.state.threshold) };
                return view;
            }
            Page::Done => {
                let Some(r) = &self.state.round else {
                    return v;
                };
                let count = |n: i8| r.answers.iter().filter(|a| **a == n).count();
                let body = if r.mode == Mode::Quiz {
                    format!(
                        "答对 {} 张，要再看 {} 张，跳过 {} 张",
                        count(1),
                        count(0),
                        count(-1)
                    )
                } else {
                    format!("看完 {} 张卡片", r.ids.len())
                };
                let n = self.review_count(r.subject);
                let again = format!("再看看 {n} 张");
                message(
                    &mut v,
                    PIC_MEDAL,
                    "这一轮完成啦",
                    &body,
                    if n > 0 {
                        [&again, "再来一轮"]
                    } else {
                        ["再来一轮", ""]
                    },
                );
                v.marks = core::array::from_fn(|i| match r.answers.get(i) {
                    None => 0,
                    Some(1) => STAR_ON,
                    Some(_) if r.mode == Mode::Read => STAR_ON,
                    Some(_) => STAR_OFF,
                });
            }
            Page::Settings => {
                let labels = ["学习设置", "学习情况", "重置学习进度", "蓝牙配网", "刷机模式", "课程来源"];
                let first = self.selected.saturating_sub(4);
                v = menu_view("家长设置", core::array::from_fn(|i| labels.get(first + i).copied().unwrap_or("").into()), self.selected - first,
                    "侧面上下选择，确认进入。向下可查看课程来源。电源短按回到首页。");
                if first == 0 { v.lines[0] = format!("{} 张", self.state.count); }
            }
            Page::LearningSettings => {
                v = menu_view("学习设置", [format!("每轮张数：{} 张",self.state.count),format!("学会门槛：{} 次",self.state.threshold),"选择学习起点".into(),format!("清屏间隔：{} 次",self.state.clear_interval),"返回家长设置".into()],self.selected,
                    "清屏按实际刷新次数累计。间隔越长，闪屏越少，残影可能越明显。");
            }
            Page::Sources => {
                let (title, labels, note) = match self.selected {
                    0 => ("课程来源", ["汉字读音", "Unicode Unihan 18.0.0", "词境读音由项目校订", "拼音规则由项目编排", "Unicode License V3"],
                        "Copyright © 2026 Unicode, Inc.。侧面上下翻页，返回或确认回家长设置。"),
                    1 => ("英语来源", ["NGSL 1.2 (2023)", "Browne · Culligan · Phillips", "词表经项目筛选", "释义与练习由项目编排", "CC BY-SA 4.0"],
                        "完整归属、修改说明与许可随 SD 卡课程包提供。侧面上下翻页，确认返回。"),
                    _ => ("编排说明", ["数学：运算生成", "主题与词义：项目编排", "按小学学习主题组织", "非教材官方年级排序", "尚未逐题人工审校"],
                        "完整来源与许可见课程包 NOTICE.md 和 attribution。侧面上下翻页，确认返回。"),
                };
                v = menu_view(title, labels.map(alloc::string::String::from), 255, note);
                v.hint = format!("{} / 3", self.selected + 1);
                v.muted = 0x1f;
            }
            Page::StartPoint => {
                v = menu_view("选择科目", ["汉字","拼音","英语","数学",""].map(alloc::string::String::from), self.selected,
                    "选择起始单元只开放课程，不把前面的卡片标为学会。");
            }
            Page::UnitChoice => {
                let first = self.selected / 5 * 5;
                let labels = core::array::from_fn(|i|super::course::unit_for(self.subject,(first+i) as u16)
                    .map_or_else(alloc::string::String::new,|u|format!("{}年级 {}",u.grade,u.title)));
                v = menu_view("选择起点",labels,self.selected%5,"侧面上下选择单元，确认后再次确认。返回可取消。");
            }
            Page::StartConfirm => {
                let title = super::course::unit_for(self.subject,self.pending_unit).map_or("",|u|u.title);
                message(&mut v,PIC_QUESTION,"从这里开始？",title,["取消","确认起点"]);
            }
            Page::CourseDone => message(&mut v,PIC_MEDAL,"当前课程学完啦","已学会的卡片不会再次出题。可以换一科继续学习。",["回到科目",""]),
            Page::Stats => {
                v.screen = SCREEN_STATS;
                v.caps = [0, 0, CAP_BACK, CAP_CHECK];
                for s in 0..4 {
                    let [known, learning, new] = self.subject_counts(s);
                    v.lines[s] = format!("{known} · {learning} · {new}");
                    v.bars[s] = [known, learning, new].map(|n| n as u16);
                }
            }
            Page::Reset => {
                if let Some(check) = self.reset_check.as_ref().filter(|check| !check.passed()) {
                    v = check.view();
                } else {
                    message(
                        &mut v,
                        PIC_TRASH,
                        "重置学习进度？",
                        "会清空四科全部学习记录，不能恢复。",
                        ["取消，保留进度", "确认清空"],
                    );
                }
            }
            Page::DownloadConfirm => message(
                &mut v,
                PIC_USB,
                "进入刷机模式？",
                "用于通过 USB 更新固件。进入前会保存进度并关闭无线。",
                ["取消", "进入刷机模式"],
            ),
            Page::DownloadPreparing => {
                message(
                    &mut v,
                    PIC_USB,
                    "正在准备刷机模式",
                    "正在保存进度并关闭蓝牙。请保持 USB 连接，不要关机；按返回键取消。",
                    ["", ""],
                );
                v.caps = [0, 0, CAP_BACK, 0];
            }
            Page::DownloadPrompt => {
                v = crate::View::new();
                v.download = true;
                v.hero = "Download Mode".into();
                v.lines[0] = "Keep USB connected".into();
            }
            Page::Network => {
                v = crate::View::new();
                v.provisioning = true;
                v.focus = self.network as u8;
                if self.network_identity_visible() {
                    v.lines[0] = core::str::from_utf8(&self.network_name).unwrap().into();
                    v.lines[1] = core::str::from_utf8(&self.network_pop).unwrap().into();
                }
            }
            Page::Sleep => {
                message(&mut v, PIC_SLEEP, "睡觉啦", "按电源键叫醒我", ["", ""]);
                v.screen = SCREEN_SLEEP;
                v.caps = [0; 4];
            }
            Page::Error => {
                message(&mut v, PIC_QUESTION, "出了点小问题", &self.error, ["", ""]);
                v.caps = [0, 0, CAP_BACK, CAP_CHECK];
            }
        }
        v
    }
}

fn menu_view(title: &str, labels: [alloc::string::String; 5], selected: usize, note: &str) -> crate::View {
    let mut v = crate::View::new();
    v.screen = SCREEN_SETTINGS;
    v.hero = title.into();
    v.focus = selected as u8;
    v.caps = NAV;
    for (i,label) in labels.into_iter().enumerate() {
        if i < 4 { v.options[i] = label; } else { v.lines[4] = label; }
    }
    v.lines[5] = crate::fit::body(note, crate::layout::SETTINGS_NOTE[2]);
    v
}
fn deck_view(deck: &Deck, selected: usize, n: usize) -> crate::View {
    let mut v = crate::View::new();
    v.screen = SCREEN_DECK;
    v.caps = NAV;
    v.focus = selected as u8;
    let subject = SUBJECTS
        .iter()
        .position(|s| *s == deck.subject)
        .unwrap_or(0);
    v.title = CHIPS[subject].into();
    v.hero = deck.title.clone();
    v.lines[0] = if n > 0 {
        format!("{n} 张需要再看看")
    } else {
        "现在没有要再看的".into()
    };
    v.muted = if n == 0 { 1 << 2 } else { 0 };
    v
}
/// 科目标签：首页四宫格、科目页与学习页左上角共用。
const CHIPS: [&str; 4] = ["字", "ā", "Aa", "123"];
fn storage_hint(view: &mut crate::View, status: u32) {
    match status {
        1 => view.hint = "存储不可用".into(),
        2 => view.hint = "保存失败".into(),
        _ => {}
    }
}
fn message(v: &mut crate::View, art: u8, title: &str, body: &str, buttons: [&str; 2]) {
    v.screen = SCREEN_MESSAGE;
    v.art = art;
    v.hero = title.into();
    v.lines[0] = crate::fit::body(body, MESSAGE_BODY[2]);
    v.options[0] = buttons[0].into();
    v.options[1] = buttons[1].into();
}
fn round_dots(r: &Round) -> [u8; 10] {
    core::array::from_fn(|i| match i {
        _ if i >= r.ids.len() => 0,
        _ if i < r.cursor => DOT_DONE,
        _ if i == r.cursor => DOT_NOW,
        _ => DOT_TODO,
    })
}
fn learning_view(card: &Card, r: &Round, serial: u32) -> crate::View {
    let mut v = crate::View::new();
    v.title = CHIPS[r.subject].into();
    v.marks = round_dots(r);
    if r.mode == Mode::Read {
        v.screen = SCREEN_READ;
        v.caps = [CAP_LEFT, CAP_RIGHT, CAP_BACK, CAP_FLIP];
        if r.flipped {
            if let Some(((front_size, front), (back_size, back))) =
                crate::fit::back(&card.front, &card.back)
            {
                (v.option_size, v.lines[0], v.question_size, v.lines[1]) =
                    (front_size, front, back_size, back);
            }
        } else {
            match crate::fit::front(&card.front) {
                Some(crate::fit::Front::Giant(tier)) => {
                    (v.giant, v.giant_tier) = (card.front.clone(), tier as u8)
                }
                Some(crate::fit::Front::Bold(text)) => v.hero = text,
                None => {}
            }
        }
        return v;
    }
    v.screen = SCREEN_QUIZ;
    let order = r.option_order(r.cursor);
    let options = core::array::from_fn(|i|card.options[usize::from(order[i])].clone());
    if let Some((size, options)) = crate::fit::options(&options) {
        (v.option_size, v.options) = (size, options);
    }
    let answer = r.answers[r.cursor];
    if answer < 0 {
        if let Some((size, question)) = crate::fit::question(&card.question) {
            (v.question_size, v.hero) = (size, question);
        }
    } else {
        v.success = answer == 1;
        v.art = if v.success {
            // 轮次序号已随存档保存：跨轮换图，回看和恢复仍显示同一幅。
            CHEERS[(serial as usize % CHEERS.len() + r.cursor) % CHEERS.len()]
        } else {
            PIC_LOOK
        };
        v.lines[0] = (if v.success {
            "答对啦！"
        } else {
            "我们一起看看"
        })
        .into();
        v.lines[1] = crate::fit::explanation(&card.explanation).unwrap_or_default();
        v.focus = order.iter().position(|p|*p == card.correct).unwrap_or(0) as u8;
        v.caps = [CAP_LEFT, CAP_RIGHT, CAP_BACK, CAP_NEXT];
    }
    v
}
