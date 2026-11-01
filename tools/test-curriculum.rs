use super::*;
use model::course::*;

unsafe extern "C" fn course_file(context: *mut core::ffi::c_void, offset: u32, out: *mut u8, length: usize) -> i32 {
    use std::io::{Read, Seek};
    if !context.is_null() { return -1; }
    let path = std::path::Path::new(env!("BALABALA_GENERATED")).join("../sdcard/balabala/curriculum/course.bin");
    let result = std::fs::File::open(path).and_then(|mut file| {
        file.seek(std::io::SeekFrom::Start(u64::from(offset)))?;
        file.read_exact(core::slice::from_raw_parts_mut(out,length))
    });
    if result.is_ok() {0} else {-1}
}
pub(super) fn curriculum() -> Model {
    let mut model = Model::new();
    model.attach_course(CourseReader{read:course_file,context:core::ptr::null_mut()},COURSE_BYTES,COURSE_CRC).unwrap();
    model
}
fn answer_once(model: &mut Model, correct: bool) {
    if !model.ready() { displayed(model); }
    let index = (answer_position(model) + u8::from(!correct))%4;
    tap(model,Key::from_u32(u32::from(index)).unwrap());
}
#[test]
fn correct_evidence_accumulates_across_rounds_and_mastered_cards_retire() {
    for threshold in [1,3,5,7,9] {
        let mut m = Model::new();
        assert!(m.set_threshold(threshold));
        m.decks[0].cards.truncate(1);
        for evidence in 1..=threshold {
            m.start(0,Mode::Read,false);
            displayed(&mut m);
            tap(&mut m,Key::Front4);
            assert_eq!(m.state.progress.first().map_or(0,|p|p.correct_count),evidence-1);
            m.start(0,Mode::Quiz,false);
            answer_once(&mut m,true);
            let revision = m.revision;
            displayed(&mut m);
            for key in [Key::Front1,Key::Front2,Key::Front3,Key::Front4] { tap(&mut m,key); }
            assert_eq!(m.state.progress[0].correct_count,evidence);
            assert!(m.revision >= revision);
            assert_eq!(m.state.progress[0].mastered,evidence == threshold);
        }
        m.start(0,Mode::Quiz,false);
        assert_eq!(m.page,Page::CourseDone);
        assert_eq!(m.subject_counts(0),[1,0,0]);
        assert!(m.set_threshold(9));
        m.start(0,Mode::Quiz,false);
        assert_eq!(m.page,Page::CourseDone);
    }
}
#[test]
fn wrong_answers_preserve_count_and_lower_threshold_retires_existing_evidence() {
    let mut m = Model::new(); m.decks[0].cards.truncate(1);
    for correct in [true,false,true] {
        m.start(0,Mode::Quiz,false); answer_once(&mut m,correct);
    }
    assert_eq!(m.state.progress[0].correct_count,2);
    assert!(!m.state.progress[0].mastered);
    assert!(m.set_threshold(1));
    assert!(m.state.progress[0].mastered);
    assert!(!m.set_threshold(2));
    assert_eq!(m.state.threshold,1);
}
#[test]
fn sd_curriculum_round_is_bounded_unique_shuffled_and_restores_without_scoring() {
    let mut m = curriculum(); m.state.count = 10;
    m.start(0,Mode::Quiz,false);
    let r = m.state.round.as_ref().unwrap();
    assert!(r.ids.len() <= 10);
    assert!(r.ids.iter().enumerate().all(|(i,id)|!r.ids[..i].contains(id)));
    assert!(r.permutations.iter().any(|p|*p != [0,1,2,3]));
    assert!(m.content_bytes() <= model::MAX_ACTIVE_CONTENT_BYTES);
    answer_once(&mut m,true);
    let (_,generation,record) = m.export().unwrap();
    let flags = m.state.course.clone();
    let round = serde_json::to_vec(&m.state.round).unwrap();
    let mut reboot = curriculum();
    assert!(reboot.load(&record,&[]));
    assert_eq!(reboot.generation,generation);
    assert_eq!(reboot.state.course,flags);
    assert_eq!(serde_json::to_vec(&reboot.state.round).unwrap(),round);
    displayed(&mut reboot); tap(&mut reboot,Key::Front4); displayed(&mut reboot);
    tap(&mut reboot,Key::Front1);
    assert_eq!(reboot.state.course,flags);
}
#[test]
fn unit_unlock_requires_ceiling_eighty_percent_and_never_relocks() {
    let mut m = curriculum();
    let unit = unit_for(0,0).unwrap();
    assert!(unit_for(0,1).is_some());
    let needed = (usize::from(unit.count)*4+4)/5;
    for i in 0..needed-1 { m.state.course[usize::from(unit.first)+i] = SEEN|1; }
    m.set_threshold(1); assert_eq!(m.state.unlocked[0],0);
    m.state.course[usize::from(unit.first)+needed-1] = SEEN|1;
    m.set_threshold(1); assert_eq!(m.state.unlocked[0],1);
    m.set_threshold(9); assert_eq!(m.state.unlocked[0],1);
    m.state.starts[0] = 1;
    m.start(0,Mode::Quiz,false);
    assert!(m.state.round.as_ref().unwrap().ids.iter().all(|id| {
        let index = index_for_id(0,id).unwrap();
        COURSE_CARDS[index].unit == 1 || m.state.course[index] != 0
    }));
}
#[test]
fn short_curriculum_round_does_not_pad_with_mastered_or_duplicate_cards() {
    let mut m = curriculum();
    let unit = unit_for(0,0).unwrap();
    for i in usize::from(unit.first)..usize::from(unit.first+unit.count)-1 { m.state.course[i]=SEEN|MASTERED|3; }
    m.start(0,Mode::Quiz,false);
    assert_eq!(m.state.round.as_ref().unwrap().ids.len(),1);
    answer_once(&mut m,false);
    let index = index_for_id(0,&m.card().unwrap().id).unwrap();
    assert_eq!(m.state.course[index],SEEN|WRONG);
}
#[test]
fn missing_or_wrong_curriculum_preserves_latest_save_and_late_ack_cannot_unlock() {
    let mut m = curriculum();
    let (_,initial,_) = m.export().unwrap(); assert!(m.save_ack(initial,true));
    m.start(0,Mode::Quiz,false); answer_once(&mut m,true);
    let (_,generation,record) = m.export().unwrap();
    let mut fallback = Model::new();
    let older = fallback.export().unwrap().2;
    assert!(!fallback.load(&older,&record));
    assert!(fallback.export().is_err());
    assert!(!fallback.save_ack(generation,true));
    let mut broken = curriculum();
    broken.course_reader.as_mut().unwrap().context = core::ptr::without_provenance_mut(1);
    let before = serde_json::to_vec(&broken.state).unwrap();
    assert!(!broken.load(&record,&[]));
    assert_eq!(serde_json::to_vec(&broken.state).unwrap(),before);
    assert!(broken.export().is_err());
    broken.course_reader.as_mut().unwrap().context = core::ptr::null_mut();
    assert!(broken.load(&record,&[]));
    assert!(broken.export().is_ok());
}
#[test]
fn full_curriculum_progress_fits_existing_save_slots_and_large_stats() {
    let mut m = curriculum();
    m.state.course.fill(SEEN|MASTERED|9);
    let (_,_,record) = m.export().unwrap();
    assert!(record.len() <= model::MAX_SAVE);
    let mut reboot = curriculum(); assert!(reboot.load(&record,&[]));
    assert_eq!(reboot.state.course,m.state.course);
    reboot.page = Page::Stats;
    assert!(reboot.view().bars.iter().any(|bar|bar[0]>255));
    pbm("curriculum-stats-full",&reboot.view());
    println!("curriculum cards={} full progress record={} bytes",COURSE_CARDS.len(),record.len());
}
#[test]
fn legacy_known_means_one_correct_evidence_and_inflight_answer_is_not_recounted() {
    let mut old = quiz(); answer_once(&mut old,true);
    old.state.round.as_mut().unwrap().permutations.clear();
    old.state.progress[0].correct_count = 0;
    let record = padded_save_record(&old.state,8);
    let mut m = Model::new(); assert!(m.load(&record,&[]));
    assert_eq!(m.state.progress[0].correct_count,1);
    assert!(!m.state.progress[0].mastered);
    displayed(&mut m); tap(&mut m,Key::Front4); displayed(&mut m);
    tap(&mut m,Key::Front1);
    assert_eq!(m.state.progress[0].correct_count,1);
}
fn rewrite_course_record(record: &[u8], version: u32, unknown_key: u32) -> Vec<u8> {
    let json_len = u32::from_le_bytes(record[24..28].try_into().unwrap()) as usize;
    let mut state: serde_json::Value = serde_json::from_slice(&record[28..28+json_len]).unwrap();
    state["course_version"] = version.into();
    let payload = serde_json::to_vec(&state).unwrap();
    let mut result = record[..24].to_vec();
    result.extend_from_slice(&(payload.len() as u32).to_le_bytes());
    result.extend_from_slice(&payload);
    result.extend_from_slice(&unknown_key.to_le_bytes());
    result.push(SEEN|1);
    let length = result.len()-24;
    result[16..20].copy_from_slice(&(length as u32).to_le_bytes());
    let crc = model::crc32(&[&result[..20],&result[24..]]);
    result[20..24].copy_from_slice(&crc.to_le_bytes());
    result
}
#[test]
fn unknown_course_key_in_older_version_protects_highest_generation_after_reset() {
    let mut source = curriculum();
    let (_,initial,older) = source.export().unwrap(); source.save_ack(initial,true);
    source.start(0,Mode::Quiz,false); answer_once(&mut source,true);
    let (_,_,latest) = source.export().unwrap();
    let unknown = (0..=u32::MAX).find(|key|index_for_key(*key).is_none()).unwrap();
    for version in [0,COURSE_VERSION] {
        let record = rewrite_course_record(&latest,version,unknown);
        let mut target = curriculum();
        let before = serde_json::to_vec(&target.state).unwrap();
        let (_,late,_) = target.export().unwrap();
        assert!(!target.load(&older,&record));
        assert_eq!(serde_json::to_vec(&target.state).unwrap(),before);
        assert!(target.state.course.iter().all(|p|*p==0));
        assert!(!target.save_ack(late,true));
        begin_reset(&mut target); pass_reset_check(&mut target);
        displayed(&mut target); tap(&mut target,Key::Down);
        displayed(&mut target); tap(&mut target,Key::Front4);
        assert!(target.export().is_err());
        assert!(target.load(&older,&latest));
        assert!(target.export().is_ok());
    }
}
#[test]
fn attaching_course_retires_displayed_builtin_round_and_keeps_errors_visible() {
    for in_flight in [false,true] {
        let mut m = quiz();
        if in_flight { m.start(0,Mode::Quiz,false); assert_eq!(m.begin_render(),Some(m.revision)); }
        let old = m.revision;
        m.attach_course(CourseReader{read:course_file,context:core::ptr::null_mut()},COURSE_BYTES,COURSE_CRC).unwrap();
        assert_eq!(m.page,Page::Home); assert!(m.revision>old);
        if in_flight { assert!(m.complete(old)); }
        assert!(!m.ready());
        assert!(m.state.round.is_none());
        for key in [Key::Down,Key::Up,Key::Front1,Key::Front2,Key::Front3,Key::Front4] { tap(&mut m,key); }
        assert!(m.state.course.iter().all(|p|*p==0));
    }
    let mut m = Model::new(); m.show_error("存档读取失败");
    let old = m.revision;
    m.attach_course(CourseReader{read:course_file,context:core::ptr::null_mut()},COURSE_BYTES,COURSE_CRC).unwrap();
    assert_eq!(m.page,Page::Error); assert_eq!(m.error,"存档读取失败"); assert!(m.revision>old);
}
#[test]
fn learning_start_confirmation_defaults_to_cancel_and_reset_preserves_preferences() {
    fn confirm(m: &mut Model) { displayed(m); tap(m,Key::Front4); }
    fn down(m: &mut Model) { displayed(m); tap(m,Key::Down); }
    let mut m = curriculum();
    m.state.course[0] = SEEN|1;
    m.state.threshold = 7;
    m.state.count = 10;
    m.page = Page::LearningSettings;
    m.selected = 2;
    confirm(&mut m); assert_eq!(m.page,Page::StartPoint);
    confirm(&mut m); assert_eq!(m.page,Page::UnitChoice);
    down(&mut m); confirm(&mut m);
    assert_eq!((m.page,m.selected),(Page::StartConfirm,0));
    confirm(&mut m);
    assert_eq!((m.state.starts[0],m.state.unlocked[0]),(0,0));
    down(&mut m); down(&mut m); confirm(&mut m);
    confirm(&mut m); down(&mut m); confirm(&mut m);
    down(&mut m); confirm(&mut m);
    assert_eq!((m.state.starts[0],m.state.unlocked[0]),(1,1));
    assert_eq!(m.state.course[0],SEEN|1);
    begin_reset(&mut m); pass_reset_check(&mut m);
    displayed(&mut m); tap(&mut m,Key::Down);
    confirm(&mut m);
    assert!(m.state.course.iter().all(|p|*p==0));
    assert_eq!((m.state.starts,m.state.unlocked),([0;4],[0;4]));
    assert_eq!((m.state.threshold,m.state.count),(7,10));
}
#[test]
fn retired_english_ids_do_not_transfer_old_evidence_to_different_words() {
    let mut old = Model::new();
    old.state.progress = vec![
        model::Progress{subject:2,id:"english-01".into(),status:2,..Default::default()},
        model::Progress{subject:0,id:"hanzi-01".into(),status:2,..Default::default()},
    ];
    old.state.round = Some(model::Round{subject:2,ids:vec!["english-01".into()],answers:vec![-1],cursor:0,mode:Mode::Quiz,flipped:false,permutations:vec![]});
    let record = padded_save_record(&old.state,9);
    let mut fallback = Model::new();
    assert!(!fallback.load(&record,&[])); assert!(fallback.export().is_err());
    let mut current = curriculum();
    assert!(current.load(&record,&[]));
    assert!(current.state.round.is_none());
    assert_eq!(current.state.course[index_for_id(0,"hanzi-01").unwrap()],SEEN|1);
    for id in ["english-water","english-bread","english-rice"] {
        assert_eq!(current.state.course[index_for_id(2,id).unwrap()],0);
    }
    assert!(current.export().is_ok());
}
#[test]
fn all_course_cards_pass_the_real_decoder_layout_and_font_budgets() {
    let path = std::path::Path::new(env!("BALABALA_GENERATED")).join("../sdcard/balabala/curriculum/course.bin");
    let bytes = std::fs::read(path).unwrap();
    assert_eq!(bytes.len(),COURSE_BYTES as usize);
    assert_eq!(model::crc32(&[&bytes[16..]]),COURSE_CRC);
    for entry in COURSE_CARDS {
        let card: serde_json::Value = serde_json::from_slice(&bytes[entry.offset as usize..entry.offset as usize+usize::from(entry.bytes)]).unwrap();
        let deck = serde_json::json!({"schema":1,"min_format":1,"version":1,"id":"course-test","subject":model::SUBJECTS[usize::from(entry.subject)],"title":"课程","cards":[card]});
        model::parse_deck(&serde_json::to_vec(&deck).unwrap()).unwrap_or_else(|e|panic!("{}: {e}",entry.id));
    }
}

#[test]
fn course_readings_follow_the_word_context() {
    let path = std::path::Path::new(env!("BALABALA_GENERATED")).join("../sdcard/balabala/curriculum/course.bin");
    let bytes = std::fs::read(path).unwrap();
    for (id, word, reading) in [("hz-u554a", "看啊", "a"), ("hz-u5319", "钥匙", "shi"), ("hz-u5566", "哗啦", "lā")] {
        let entry = &COURSE_CARDS[index_for_id(0, id).unwrap()];
        let card: serde_json::Value = serde_json::from_slice(&bytes[entry.offset as usize..entry.offset as usize + usize::from(entry.bytes)]).unwrap();
        let correct = card["correct"].as_u64().unwrap() as usize;
        assert!(card["question"].as_str().unwrap().contains(word));
        assert_eq!(card["options"][correct], reading, "{word}");
    }
}

#[test]
fn course_id_lookup_matches_the_entire_directory() {
    assert_eq!(COURSE_CARDS.len(), 4000);
    for (index, card) in COURSE_CARDS.iter().enumerate() {
        let subject = usize::from(card.subject);
        assert_eq!(index_for_id(subject, card.id), Some(index));
        for wrong in 0..4 {
            if wrong != subject { assert_eq!(index_for_id(wrong, card.id), None); }
        }
    }
    for subject in [0, 1, 2, 3, 4, usize::MAX] {
        for id in ["", "unknown-card", "未知卡片", "math-01\0"] {
            assert_eq!(index_for_id(subject, id), None);
        }
    }
    // 未知 ID 的 key 即使与目录碰撞，也须核对完整身份。
    assert_eq!(index_for_id(0, "unknown-f207063e"), None);
    assert_eq!(index_for_id(4, COURSE_CARDS[0].id), None);
    assert_eq!(index_for_id(usize::MAX, COURSE_CARDS[0].id), None);
}

fn mastered_math_demo(count: usize) -> Model {
    let mut model = Model::new();
    model.set_threshold(1);
    for _ in 0..count {
        if model.state.round.as_ref().is_none_or(|r| r.cursor + 1 == r.ids.len() && r.answers[r.cursor] >= 0) {
            model.start(3, Mode::Quiz, false);
        } else if model.state.round.as_ref().unwrap().answers[model.state.round.as_ref().unwrap().cursor] >= 0 {
            tap(&mut model, Key::Down);
        }
        answer_once(&mut model, true);
    }
    assert_eq!(model.state.progress.iter().filter(|p|p.mastered).count(), count);
    model
}
fn assert_migrated_math(model: &mut Model, count: usize) {
    assert_eq!(unit_for(3, 0).unwrap().count, 32);
    assert_eq!(model.subject_counts(3)[0], count);
    assert_eq!(model.state.unlocked[3], u16::from(count >= 26));
    assert!(model.state.progress.is_empty());
    model.start(3, Mode::Quiz, false);
    assert_eq!(model.page, Page::Learn);
    let round = model.state.round.as_ref().unwrap();
    assert!(!round.ids.is_empty());
    if count == 32 {
        assert!(round.ids.iter().all(|id| COURSE_CARDS[index_for_id(3,id).unwrap()].unit == 1));
    }
}
fn check_demo_v2_migration(attach: bool) {
    for count in [25, 26, 32] {
        let mut demo = mastered_math_demo(count);
        let (_, generation, record) = demo.export().unwrap();
        assert!(demo.save_ack(generation, true));
        assert_eq!(&record[..8], b"BALA\x02\0\0\0");
        let mut model = if attach {
            demo.attach_course(CourseReader{read:course_file,context:core::ptr::null_mut()},COURSE_BYTES,COURSE_CRC).unwrap();
            demo
        } else {
            let mut loaded = curriculum();
            assert!(loaded.load(&record, &[]));
            loaded
        };
        assert_migrated_math(&mut model, count);
        let (_, _, migrated) = model.export().unwrap();
        let mut reboot = curriculum();
        assert!(reboot.load(&migrated, &[]));
        assert_migrated_math(&mut reboot, count);
    }
}
#[test]
fn demo_v2_load_unlocks_mastered_math_units() {
    check_demo_v2_migration(false);
}
#[test]
fn demo_v2_attach_unlocks_mastered_math_units() {
    check_demo_v2_migration(true);
}
#[test]
fn migration_read_failure_preserves_state_and_protection_until_restore() {
    let mut demo = mastered_math_demo(26);
    let (_, _, record) = demo.export().unwrap();
    let mut model = curriculum();
    model.start(0, Mode::Read, false);
    let before = serde_json::to_vec(&model.state).unwrap();
    let flags = model.state.course.clone();
    let cards: Vec<_> = model.course_cards.iter().map(|c|c.id.clone()).collect();
    let (_, pending, _) = model.export().unwrap();
    model.course_reader.as_mut().unwrap().context = core::ptr::without_provenance_mut(1);
    assert!(!model.load(&record, &[]));
    assert_eq!(serde_json::to_vec(&model.state).unwrap(), before);
    assert_eq!(model.state.course, flags);
    assert_eq!(model.course_cards.iter().map(|c|c.id.clone()).collect::<Vec<_>>(), cards);
    assert!(!model.save_ack(pending, true));
    assert!(model.export().is_err());
    model.course_reader.as_mut().unwrap().context = core::ptr::null_mut();
    assert!(model.load(&record, &[]));
    assert_migrated_math(&mut model, 26);
    assert!(model.export().is_ok());
}
#[test]
fn v1_known_migrates_only_one_correct_to_course() {
    let mut demo = Model::new();
    demo.state.progress.push(model::Progress{subject:3,id:"math-01".into(),status:2,..Default::default()});
    let record = padded_save_record(&demo.state, 3);
    let mut model = curriculum();
    assert!(model.load(&record, &[]));
    assert_eq!(model.state.course[index_for_id(3,"math-01").unwrap()], SEEN | 1);
    assert_eq!(model.state.unlocked[3], 0);
    let mut builtin = Model::new();
    assert!(builtin.load(&record, &[]));
    builtin.attach_course(CourseReader{read:course_file,context:core::ptr::null_mut()},COURSE_BYTES,COURSE_CRC).unwrap();
    assert_eq!(builtin.state.course[index_for_id(3,"math-01").unwrap()], SEEN | 1);
}
