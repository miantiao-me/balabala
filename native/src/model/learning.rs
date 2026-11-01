use super::{course, Card, Mode, Model, Page, Round};
use alloc::{string::String, vec::Vec};

impl Model {
    pub(super) fn review_count(&self, subject: usize) -> usize {
        if self.course_active() {
            return course::COURSE_CARDS.iter().zip(&self.state.course)
                .filter(|(c,p)| usize::from(c.subject) == subject && c.unit <= self.state.unlocked[subject] && **p & course::WRONG != 0 && **p & course::MASTERED == 0).count();
        }
        self.state
            .progress
            .iter()
            .filter(|p| p.subject == subject && p.status == 1 && !p.mastered)
            .count()
    }
    /// 完成页“再看看”按钮的张数：本轮科目里所有需要再看的卡。
    pub(super) fn done_review(&self) -> usize {
        self.state
            .round
            .as_ref()
            .map_or(0, |r| self.review_count(r.subject))
    }
    pub fn card(&self) -> Option<&Card> {
        let r = self.state.round.as_ref()?;
        if self.course_active() {
            return self.course_cards.iter().find(|c| Some(&c.id) == r.ids.get(r.cursor));
        }
        self.decks
            .get(r.subject)?
            .cards
            .iter()
            .find(|c| Some(&c.id) == r.ids.get(r.cursor))
    }
    pub fn start(&mut self, subject: usize, mode: Mode, review: bool) {
        if self.course_active() {
            return self.start_course(subject, mode, review);
        }
        let cards = &self.decks[subject].cards;
        let ids: Vec<String> = cards
            .iter()
            .cycle()
            .skip(if review {
                0
            } else {
                self.state.offsets[subject]
            })
            .take(cards.len())
            .filter(|c| {
                let p = self.state.progress.iter().find(|p| p.subject == subject && p.id == c.id);
                !p.is_some_and(|p| p.mastered) && (!review || p.is_some_and(|p| p.status == 1))
            })
            .take(self.state.count)
            .map(|c| c.id.clone())
            .collect();
        if ids.is_empty() {
            if review { self.show_error("还没有需要再看的卡片"); }
            else { self.go(Page::CourseDone); }
            return;
        }
        if !review {
            self.state.offsets[subject] = (self.state.offsets[subject] + ids.len()) % cards.len();
        }
        let answers = alloc::vec![-1;ids.len()];
        let permutations = self.round_permutations(ids.len());
        self.state.round = Some(Round {
            subject,
            ids,
            answers,
            cursor: 0,
            mode,
            flipped: false,
            permutations,
        });
        self.saved_change();
        self.go(Page::Learn);
    }
    pub(super) fn round_permutations(&mut self, count: usize) -> Vec<[u8; 4]> {
        self.state.serial = self.state.serial.wrapping_add(1);
        let mut seed = self.state.serial.wrapping_mul(747796405).wrapping_add(2891336453);
        (0..count).map(|_| {
            let mut order = [0,1,2,3];
            for i in (1..4).rev() {
                seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
                order.swap(i, (seed as usize) % (i+1));
            }
            order
        }).collect()
    }
    pub(super) fn answer(&mut self, answer: u8) {
        let r = self.state.round.as_ref().unwrap();
        if r.mode != Mode::Quiz || r.answers[r.cursor] >= 0 {
            return;
        }
        let Some(c) = self.card() else {
            self.show_error("学习卡片缺失，请重新选卡组");
            return;
        };
        let correct = c.correct == r.option_order(r.cursor)[usize::from(answer)];
        let id = c.id.clone();
        let subject = r.subject;
        let r = self.state.round.as_mut().unwrap();
        r.answers[r.cursor] = if correct { 1 } else { 0 };
        if self.course_active() {
            if let Some(i) = course::index_for_id(subject, &id) {
                self.state.course[i] = course::next_flags(self.state.course[i], correct, self.state.threshold);
                self.unlock_units(subject);
            }
        } else {
            self.record_builtin(subject, id, correct);
        }
        self.saved_change();
        self.changed();
    }
}
