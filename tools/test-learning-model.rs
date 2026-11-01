#[test]
fn read_flip_skip_and_back_do_not_score() {
    let mut m = Model::new();
    m.start(0, Mode::Read, false);
    displayed(&mut m);
    tap(&mut m, Key::Front4);
    assert!(m.state.round.as_ref().unwrap().flipped);
    assert!(m.state.progress.is_empty());
    tap(&mut m, Key::Down);
    tap(&mut m, Key::Up);
    assert!(m.state.progress.is_empty());
    assert!(m
        .state
        .round
        .as_ref()
        .unwrap()
        .answers
        .iter()
        .all(|a| *a == -1));
}
#[test]
fn four_options_score_once_feedback_waits_and_revisit_preserves_result() {
    for answer in 0..4 {
        for repeated in 0..4 {
            let mut m = quiz();
            tap(&mut m, Key::from_u32(answer).unwrap());
            let r = m.state.round.as_ref().unwrap();
            assert_eq!(r.answers[0], if answer == 0 { 1 } else { 0 });
            assert_eq!(r.cursor, 0);
            assert_eq!(m.view().success, answer == 0);
            let result = r.answers[0];
            let feedback = m.begin_render().unwrap();
            tap(&mut m, Key::from_u32(repeated).unwrap());
            assert_eq!(m.state.round.as_ref().unwrap().answers[0], result);
            assert_eq!(m.state.round.as_ref().unwrap().cursor, 0);
            assert_eq!(m.state.progress.len(), 1);
            assert!(m.complete(feedback));
            tap(&mut m, Key::Down);
            tap(&mut m, Key::Up);
            displayed(&mut m);
            tap(&mut m, Key::Front2);
            assert_eq!(m.state.progress.len(), 1);
            assert_eq!(m.state.round.as_ref().unwrap().answers[0], result);
            assert_eq!(m.state.round.as_ref().unwrap().cursor, 1);
            displayed(&mut m);
            tap(&mut m, Key::Front2);
            assert_eq!(m.state.progress.len(), 2);
        }
    }
}
#[test]
fn feedback_front_keys_navigate_without_rescoring_and_held_confirm_cannot_answer_next() {
    let mut m = quiz();
    tap(&mut m, Key::Front1);
    let revision = m.revision;
    assert_eq!(m.state.round.as_ref().unwrap().cursor, 0);
    displayed(&mut m);
    assert_eq!(m.revision, revision);
    assert!(m.view().success);
    m.key(Key::Front4, true);
    assert_eq!(m.state.round.as_ref().unwrap().cursor, 1);
    assert_eq!(m.state.progress.len(), 1);
    let next = m.begin_render().unwrap();
    m.key(Key::Front4, true);
    assert!(m.complete(next));
    tap(&mut m, Key::Front2);
    assert_eq!(m.state.progress.len(), 1);
    m.key(Key::Front4, false);
    tap(&mut m, Key::Front2);
    assert_eq!(m.state.progress.len(), 2);
    displayed(&mut m);
    tap(&mut m, Key::Front1);
    assert_eq!(m.state.round.as_ref().unwrap().cursor, 0);
    displayed(&mut m);
    tap(&mut m, Key::Front2);
    assert_eq!(m.state.round.as_ref().unwrap().cursor, 1);
    assert_eq!(m.state.progress.len(), 2);
    displayed(&mut m);
    tap(&mut m, Key::Front3);
    assert_eq!(m.page, Page::Home);
    displayed(&mut m);
    tap(&mut m, Key::Front4);
    for _ in 1..5 {
        displayed(&mut m);
        if m.state.round.as_ref().unwrap().answers[m.state.round.as_ref().unwrap().cursor] < 0 {
            tap(&mut m, Key::Front1);
            displayed(&mut m);
        }
        tap(&mut m, Key::Front4);
    }
    assert_eq!(m.page, Page::Done);
    assert_eq!(m.state.progress.len(), 5);
}
#[test]
fn display_revision_and_old_key_release_both_gate_new_answers() {
    let mut m = quiz();
    m.key(Key::Front1, true);
    assert_eq!(m.state.progress.len(), 1);
    m.key(Key::Down, true);
    let next = m.begin_render().unwrap();
    m.key(Key::Down, false);
    m.key(Key::Front2, true);
    assert_eq!(m.state.progress.len(), 1);
    m.key(Key::Front2, false);
    assert!(!m.complete(next - 1));
    assert!(m.complete(next));
    m.key(Key::Front3, true);
    assert_eq!(m.state.progress.len(), 1);
    m.key(Key::Front3, false);
    m.key(Key::Front1, false);
    tap(&mut m, Key::Front2);
    assert_eq!(m.state.progress.len(), 2);
}
#[test]
fn busy_navigation_coalesces_and_home_shutdown_are_never_lost() {
    let mut m = quiz();
    tap(&mut m, Key::Down);
    let old = m.begin_render().unwrap();
    tap(&mut m, Key::Down);
    tap(&mut m, Key::Up);
    assert!(m.begin_render().is_none());
    assert!(m.complete(old));
    assert_eq!(m.state.round.as_ref().unwrap().cursor, 1);
    assert!(!m.ready());
    displayed(&mut m);
    tap(&mut m, Key::Shutdown);
    assert_eq!(m.page, Page::Home);
    assert_ne!(m.platform_requests & 1, 0);
    assert_eq!(m.state.round.as_ref().unwrap().cursor, 1);
    assert!(m.dirty);
}
#[test]
fn all_menu_paths_settings_reset_review_and_round_rotation_work() {
    let mut m = Model::new();
    displayed(&mut m);
    m.key(Key::Front3, true);
    m.key(Key::Parent, true);
    m.key(Key::Parent, false);
    m.key(Key::Front3, false);
    assert_eq!(m.page, Page::Settings);
    displayed(&mut m);
    tap(&mut m, Key::Front4);
    assert_eq!(m.page, Page::LearningSettings);
    displayed(&mut m);
    tap(&mut m, Key::Front4);
    assert_eq!(m.state.count, 10);
    m.start(0, Mode::Quiz, false);
    displayed(&mut m);
    tap(&mut m, Key::Front2);
    assert_eq!(m.state.progress[0].status, 1);
    m.start(0, Mode::Quiz, true);
    assert_eq!(m.state.round.as_ref().unwrap().ids.len(), 1);
    begin_reset(&mut m);
    pass_reset_check(&mut m);
    displayed(&mut m);
    tap(&mut m, Key::Front4);
    assert_eq!(m.state.progress.len(), 1);
    begin_reset(&mut m);
    pass_reset_check(&mut m);
    displayed(&mut m);
    tap(&mut m, Key::Down);
    displayed(&mut m);
    tap(&mut m, Key::Front4);
    assert!(m.state.progress.is_empty());
    assert!(m.state.round.is_none());
    m.start(0, Mode::Read, false);
    let first = m.state.round.as_ref().unwrap().ids.clone();
    m.start(0, Mode::Read, false);
    assert_ne!(first, m.state.round.as_ref().unwrap().ids);
}
#[test]
fn reordered_deck_load_keeps_round_ids_cursor_answers_and_progress() {
    let mut m = quiz();
    tap(&mut m, Key::Front2);
    tap(&mut m, Key::Down);
    displayed(&mut m);
    tap(&mut m, Key::Front2);
    let (_, _, save) = m.export().unwrap();
    let before = serde_json::to_vec(&m.state).unwrap();
    let current = m.card().unwrap().id.clone();
    let mut v: serde_json::Value =
        serde_json::from_slice(include_bytes!("../content/hanzi.json")).unwrap();
    v["version"] = 2.into();
    v["cards"].as_array_mut().unwrap().reverse();
    let mut reboot = Model::new();
    reboot.import(&serde_json::to_vec(&v).unwrap()).unwrap();
    assert!(reboot.load(&[], &save));
    assert_eq!(serde_json::to_vec(&reboot.state).unwrap(), before);
    displayed(&mut reboot);
    tap(&mut reboot, Key::Front4);
    assert_eq!(reboot.card().unwrap().id, current);
    assert_eq!(reboot.state.round.as_ref().unwrap().answers[1], 1);
    displayed(&mut reboot);
    tap(&mut reboot, Key::Up);
    assert_eq!(reboot.state.round.as_ref().unwrap().answers[0], 0);
    assert_eq!(reboot.state.progress.len(), 2);
}
