use crate::{model::{Mode, Page}, App};

#[repr(C)]
#[derive(Default, Debug, PartialEq)]
pub struct Snapshot {
    revision: u64, displayed: u64, mutation: u64, generation: u64, digest: u64,
    page: u32, selected: u32, subject: u32, held: u32, rendering: u32, dirty: u32, slot: u32,
    mode: u32, cursor: u32, flipped: u32, correct: u32, count: u32, scored: u32, progress: u32,
    answers: [i32; 10],
}
/* 固定字段顺序、长度前缀和LE整数使摘要不依赖指针宽度、分配布局或JSON缓冲。 */
fn number(hash: &mut u64, n: u64) {
    for b in n.to_le_bytes() { *hash = (*hash ^ b as u64).wrapping_mul(1099511628211); }
}
fn text(hash: &mut u64, s: &str) {
    number(hash, s.len() as u64);
    for b in s.bytes() { *hash = (*hash ^ b as u64).wrapping_mul(1099511628211); }
}
#[no_mangle]
pub unsafe extern "C" fn balabala_app_autotest_snapshot(app: *const App, out: *mut Snapshot) -> i32 {
    let (Some(app), Some(out)) = (app.as_ref(), out.as_mut()) else { return -1; };
    let m = &app.model;
    let mut hash = 14695981039346656037;
    for offset in m.state.offsets { number(&mut hash, offset as u64); }
    number(&mut hash, m.state.count as u64);
    number(&mut hash, m.state.threshold as u64);
    for value in m.state.unlocked.iter().chain(&m.state.starts) { number(&mut hash, *value as u64); }
    number(&mut hash, m.state.serial as u64);
    number(&mut hash, m.state.course_id as u64);
    number(&mut hash, m.state.course_version as u64);
    number(&mut hash, m.state.course_crc as u64);
    number(&mut hash, m.state.course.len() as u64);
    for flags in &m.state.course { number(&mut hash, *flags as u64); }
    number(&mut hash, m.state.progress.len() as u64);
    for p in &m.state.progress {
        number(&mut hash, p.subject as u64); text(&mut hash, &p.id); number(&mut hash, p.status as u64);
        number(&mut hash, p.correct_count as u64); number(&mut hash, p.mastered as u64);
    }
    number(&mut hash, m.state.round.is_some() as u64);
    *out = Snapshot {
        revision: m.revision, displayed: m.displayed, mutation: m.mutation, generation: m.generation,
        page: match m.page { Page::Home=>0, Page::Deck=>1, Page::Learn=>2, Page::Done=>3,
            Page::Stats=>4, Page::Settings=>5, Page::Reset=>6, Page::Network=>7,
            Page::DownloadConfirm=>8, Page::DownloadPreparing=>9, Page::DownloadPrompt=>10, Page::Error=>11, Page::Sleep=>12,
            Page::LearningSettings=>13, Page::StartPoint=>14, Page::UnitChoice=>15, Page::StartConfirm=>16, Page::CourseDone=>17, Page::Sources=>18 },
        selected: m.selected as u32, subject: m.subject as u32, held: m.held as u32,
        rendering: m.rendering.is_some() as u32, dirty: m.dirty as u32, slot: m.slot,
        progress: (m.state.progress.len() + m.state.course.iter().filter(|flags| **flags != 0).count()) as u32,
        answers: [-1;10], mode: 2,
        ..Snapshot::default()
    };
    if let Some(r) = &m.state.round {
        out.subject = r.subject as u32; out.mode = (r.mode == Mode::Quiz) as u32;
        out.cursor = r.cursor as u32; out.flipped = r.flipped as u32; out.count = r.ids.len() as u32;
        out.correct = m.card().and_then(|c| r.option_order(r.cursor).iter().position(|option| *option == c.correct))
            .map_or(4, |position| position as u32);
        number(&mut hash, r.subject as u64); number(&mut hash, r.ids.len() as u64);
        for id in &r.ids { text(&mut hash, id); }
        number(&mut hash, r.answers.len() as u64);
        for (i, a) in r.answers.iter().enumerate() {
            if i < 10 { out.answers[i] = *a as i32; }
            number(&mut hash, *a as i64 as u64);
            out.scored += (*a >= 0) as u32;
        }
        number(&mut hash, r.cursor as u64); number(&mut hash, out.mode as u64);
        number(&mut hash, r.flipped as u64);
        for cursor in 0..r.ids.len() { for option in r.option_order(cursor) { number(&mut hash, option as u64); } }
    }
    out.digest = hash;
    0
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn snapshot_is_read_only_and_digest_survives_real_load() {
        let mut app = App {
            model: crate::model::Model::new(),
            panel_cached: false,
            panel_generation: 0,
            panel_identity_generation: 0,
        };
        app.model.start(2, Mode::Quiz, false);
        let mut a = Snapshot::default();
        unsafe { assert_eq!(balabala_app_autotest_snapshot(&app, &mut a), 0); }
        let mut b = Snapshot::default();
        unsafe { assert_eq!(balabala_app_autotest_snapshot(&app, &mut b), 0); }
        assert_eq!(a,b);
        let (_, generation, bytes) = app.model.export().unwrap();
        assert!(app.model.save_ack(generation, true));
        assert!(app.model.load(&bytes, &[]));
        unsafe { assert_eq!(balabala_app_autotest_snapshot(&app, &mut b), 0); }
        assert_eq!(a.digest,b.digest);
        assert_eq!(b.generation,generation);
        assert_eq!(b.dirty,1);
        assert_eq!(core::mem::size_of::<Snapshot>(),136);
    }
}
