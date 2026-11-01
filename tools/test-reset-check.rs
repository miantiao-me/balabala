#[test]
fn reset_check_requires_three_answers_then_separate_confirmation() {
    let mut m = quiz();
    tap(&mut m, Key::Front1);
    m.state.count = 10;
    m.state.threshold = 7;
    let before = serde_json::to_vec(&m.state).unwrap();
    let mutation = m.mutation;
    begin_reset(&mut m);
    pass_reset_check(&mut m);
    assert_eq!(serde_json::to_vec(&m.state).unwrap(), before);
    assert_eq!(m.mutation, mutation);
    displayed(&mut m);
    tap(&mut m, Key::Front4);
    assert_eq!(m.page, Page::Settings);
    assert_eq!(serde_json::to_vec(&m.state).unwrap(), before);
    begin_reset(&mut m);
    pass_reset_check(&mut m);
    displayed(&mut m);
    tap(&mut m, Key::Down);
    displayed(&mut m);
    tap(&mut m, Key::Front4);
    assert!(m.state.progress.is_empty());
    assert!(m.state.round.is_none());
    assert_eq!((m.state.count, m.state.threshold), (10, 7));
    assert!(m.dirty);
    assert_eq!(m.mutation, mutation + 1);
}

#[test]
fn reset_check_wrong_answers_and_power_cancel_preserve_progress_and_restart_guard() {
    for cancel_at in 0..3 {
        for exit in [None, Some(Key::Home), Some(Key::Shutdown)] {
            let mut m = quiz();
            tap(&mut m, Key::Front1);
            let before = serde_json::to_vec(&m.state).unwrap();
            begin_reset(&mut m);
            for _ in 0..cancel_at {
                displayed(&mut m);
                let answer = reset_answer(&m.view());
                tap(&mut m, answer);
            }
            if let Some(key) = exit {
                // 物理刷新未完成时，电源取消和关机仍须立即响应。
                tap(&mut m, key);
                assert_eq!(m.page, Page::Home);
                assert_eq!(m.platform_requests & 1, u32::from(key == Key::Shutdown));
            } else {
                displayed(&mut m);
                let wrong = (reset_answer(&m.view()) as u32 + 1) % 4;
                tap(&mut m, Key::from_u32(wrong).unwrap());
                assert_eq!(m.page, Page::Settings);
            }
            assert_eq!(serde_json::to_vec(&m.state).unwrap(), before);
            begin_reset(&mut m);
            assert!(m.view().hint.starts_with("验证 1/3"));
            assert_eq!(m.view().screen, layout::SCREEN_QUIZ);
        }
    }
}

#[test]
fn reset_check_display_ack_release_and_reboot_block_accidental_confirmation() {
    let mut m = quiz();
    tap(&mut m, Key::Front1);
    begin_reset(&mut m);
    let first = m.view();
    let answer = reset_answer(&first);
    tap(&mut m, answer);
    assert_eq!(serde_json::to_vec(&m.view()).unwrap(), serde_json::to_vec(&first).unwrap(), "unshown question cannot accept answers");
    let pending = m.begin_render().unwrap();
    assert!(!m.complete(pending + 1));
    tap(&mut m, answer);
    assert_eq!(serde_json::to_vec(&m.view()).unwrap(), serde_json::to_vec(&first).unwrap());
    assert!(m.complete(pending));
    m.key(answer, true);
    assert!(m.view().hint.starts_with("验证 2/3"));
    displayed(&mut m);
    let second = m.view();
    m.key(answer, true);
    let next_answer = reset_answer(&second);
    tap(&mut m, next_answer);
    assert_eq!(serde_json::to_vec(&m.view()).unwrap(), serde_json::to_vec(&second).unwrap(), "old key must be released before answering again");
    m.key(answer, false);
    tap(&mut m, next_answer);
    assert!(m.view().hint.starts_with("验证 3/3"));
    let (_, _, record) = m.export().unwrap();
    let mut restored = Model::new();
    assert!(restored.load(&record, &[]));
    begin_reset(&mut restored);
    assert!(restored.view().hint.starts_with("验证 1/3"));
    let before = serde_json::to_vec(&restored.state).unwrap();
    // 即使误入最终确认页，没有本次会话的三题通过状态也不能清空。
    restored.page = Page::Reset;
    restored.selected = 1;
    displayed(&mut restored);
    tap(&mut restored, Key::Front4);
    assert_eq!(serde_json::to_vec(&restored.state).unwrap(), before);
    let mut direct = quiz();
    tap(&mut direct, Key::Front1);
    direct.page = Page::Reset;
    direct.selected = 1;
    displayed(&mut direct);
    tap(&mut direct, Key::Front4);
    assert_eq!(direct.state.progress.len(), 1);
}

#[test]
fn reset_check_questions_have_one_correct_choice_and_rotate_answer_keys() {
    let mut seen = [false; 4];
    let mut questions = alloc::collections::BTreeSet::new();
    for _ in 0..12 {
        let mut m = Model::new();
        m.state.serial = questions.len() as u32;
        begin_reset(&mut m);
        let mut previous = None;
        for step in 1..=3 {
            displayed(&mut m);
            let view = m.view();
            assert!(view.hint.starts_with(&format!("验证 {step}/3")));
            assert_eq!(view.options.iter().collect::<alloc::collections::BTreeSet<_>>().len(), 4);
            let answer = reset_answer(&view);
            assert_ne!(previous, Some(answer));
            previous = Some(answer);
            seen[answer as usize] = true;
            questions.insert(view.hero.clone());
            tap(&mut m, answer);
        }
    }
    assert!(seen.into_iter().all(|used| used));
    assert!(questions.len() > 3, "new attempts must vary the questions");
}
