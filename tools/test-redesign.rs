use super::*;

#[test]
fn home_subjects_and_disabled_review_follow_visible_focus() {
    let mut m = Model::new();
    displayed(&mut m);
    let revision = m.revision;
    tap(&mut m, Key::Front3);
    assert_eq!((m.page, m.revision), (Page::Home, revision));
    for subject in 0..4 {
        tap(&mut m, Key::Down);
        assert_eq!(m.view().focus, (subject + 1) as u8);
    }
    tap(&mut m, Key::Down);
    assert_eq!(m.selected, 0);
    tap(&mut m, Key::Up);
    assert_eq!(m.selected, 4);
    displayed(&mut m);
    tap(&mut m, Key::Front4);
    assert_eq!((m.page, m.subject), (Page::Deck, 3));
    assert_ne!(m.view().muted & 4, 0);
    tap(&mut m, Key::Down);
    assert_eq!(m.selected, 1);
    tap(&mut m, Key::Down);
    assert_eq!(m.selected, 0);
    m.start(3, Mode::Quiz, false);
    displayed(&mut m);
    let wrong = (m.card().unwrap().correct + 1) % 4;
    tap(&mut m, [Key::Front1, Key::Front2, Key::Front3, Key::Front4][wrong as usize]);
    tap(&mut m, Key::Home);
    m.selected = 4;
    displayed(&mut m);
    tap(&mut m, Key::Front4);
    tap(&mut m, Key::Up);
    assert_eq!(m.selected, 2);
    assert_eq!(m.view().muted, 0);
    displayed(&mut m);
    tap(&mut m, Key::Front4);
    assert_eq!(m.page, Page::Learn);
    assert_eq!(m.state.round.as_ref().unwrap().ids.len(), 1);
}

#[test]
fn parent_hold_requires_home_origin_and_release_before_confirm() {
    let mut m = Model::new();
    displayed(&mut m);
    tap(&mut m, Key::Parent);
    assert_eq!(m.page, Page::Home);
    m.key(Key::Front3, true);
    tap(&mut m, Key::Parent);
    assert_eq!(m.page, Page::Settings);
    displayed(&mut m);
    tap(&mut m, Key::Front4);
    assert_eq!(m.state.count, 5);
    m.key(Key::Front3, false);
    tap(&mut m, Key::Front4);
    assert_eq!(m.page, Page::LearningSettings);
    displayed(&mut m);
    tap(&mut m, Key::Front4);
    assert_eq!(m.state.count, 10);
    displayed(&mut m);
    m.key(Key::Front3, true);
    assert_eq!(m.page, Page::Home);
    displayed(&mut m);
    tap(&mut m, Key::Parent);
    assert_eq!(m.page, Page::Home);
    m.key(Key::Front3, false);
    m.key(Key::Front3, true);
    tap(&mut m, Key::Parent);
    assert_eq!(m.page, Page::Settings);
    m.key(Key::Front3, false);
}

#[test]
fn sleep_screen_requires_saved_released_state_and_full_refresh() {
    let mut m = Model::new();
    assert!(m.sleep_ready().is_none());
    m.dirty = false;
    m.storage_status(2);
    assert!(m.sleep_ready().is_none());
    m.storage_status(0);
    m.network_owned = true;
    assert!(m.sleep_ready().is_none());
    m.network_owned = false;
    let r = m.begin_render().unwrap();
    assert!(m.sleep_ready().is_none());
    m.complete(r);
    let r = m.sleep_ready().unwrap();
    assert_eq!(m.page, Page::Sleep);
    assert_eq!(m.view().caps, [0; 4]);
    assert_eq!(m.begin_render(), Some(r));
    assert!(!m.refresh_partial());
    m.complete(r);
    for k in [Key::Home, Key::Shutdown, Key::Front3, Key::Parent, Key::Down] {
        tap(&mut m, k);
    }
    assert_eq!((m.page, m.revision), (Page::Sleep, r));
}

#[test]
fn rewards_vary_across_rounds_and_survive_revisit_and_restore() {
    for course in [false, true] {
        for count in [5, 10] {
            let new_model = || if course { super::curriculum::curriculum() } else { Model::new() };
            let mut m = new_model();
            m.state.count = count;
            m.set_threshold(9);
            let mut seen = std::collections::BTreeSet::new();
            for round in 0..12 {
                m.start(0, Mode::Quiz, false);
                let mut arts = Vec::new();
                for cursor in 0..m.state.round.as_ref().unwrap().ids.len() {
                    displayed(&mut m);
                    let correct = answer_position(&m);
                    tap(&mut m, Key::from_u32(u32::from(correct)).unwrap());
                    let view = m.view();
                    assert!(view.success);
                    assert!(!arts.contains(&view.art), "repeated reward in a {count}-card round");
                    arts.push(view.art);
                    seen.insert(view.art);
                    assert_eq!(m.view().art, view.art);
                    if cursor == 0 && round == 0 {
                        let (_, _, record) = m.export().unwrap();
                        let mut restored = new_model();
                        assert!(restored.load(&record, &[]));
                        displayed(&mut restored);
                        tap(&mut restored, Key::Front4);
                        assert_eq!(restored.page, Page::Learn);
                        assert_eq!(restored.view().art, view.art);
                    }
                    if cursor > 0 {
                        tap(&mut m, Key::Up);
                        assert_eq!(m.view().art, arts[cursor - 1]);
                        tap(&mut m, Key::Down);
                        assert_eq!(m.view().art, view.art);
                    }
                    tap(&mut m, Key::Down);
                }
                assert_eq!(m.page, Page::Done);
            }
            assert_eq!(seen.len(), 12, "the full reward library must be reachable");
        }
    }
}

fn pages() -> Vec<(String, View)> {
    let mut pages = Vec::new();
    let mut m = Model::new();
    for i in 0..5 {
        m.selected = i;
        pages.push((format!("ui-home-{i}"), m.view()));
    }
    for s in 0..4 {
        m.subject = s;
        m.page = Page::Deck;
        for i in 0..2 {
            m.selected = i;
            pages.push((format!("ui-deck-{s}-{i}"), m.view()));
        }
        for c in 0..m.decks[s].cards.len() {
            for mode in [Mode::Read, Mode::Quiz] {
                m.start(s, mode, false);
                let id = m.decks[s].cards[c].id.clone();
                let r = m.state.round.as_mut().unwrap();
                r.ids[0] = id;
                if mode == Mode::Read {
                    pages.push((format!("ui-read-{s}-{c:02}-front"), m.view()));
                    m.state.round.as_mut().unwrap().flipped = true;
                    pages.push((format!("ui-read-{s}-{c:02}-back"), m.view()));
                } else {
                    pages.push((format!("ui-quiz-{s}-{c:02}"), m.view()));
                    m.state.round.as_mut().unwrap().answers[0] = 0;
                    pages.push((format!("ui-wrong-{s}-{c:02}"), m.view()));
                    m.state.round.as_mut().unwrap().answers[0] = 1;
                    pages.push((format!("ui-correct-{s}-{c:02}"), m.view()));
                }
            }
        }
    }
    m.start(0, Mode::Quiz, false);
    m.state.round.as_mut().unwrap().answers[0] = 1;
    for art in overlay::CHEERS {
        let mut view = m.view();
        view.art = art;
        pages.push((format!("ui-reward-{art:02}"), view));
    }
    let mut limits = View::new();
    limits.screen = layout::SCREEN_QUIZ;
    limits.title = "字".into();
    (limits.question_size, limits.hero) = fit::question("选一选").unwrap();
    (limits.option_size, limits.options) = fit::options(&["W".repeat(16), "m".repeat(16), "@".repeat(16), "山".repeat(15)]).unwrap();
    pages.push(("ui-option-limits".into(), limits));
    m.start(0, Mode::Quiz, false);
    m.page = Page::Done;
    pages.push(("ui-done-empty".into(), m.view()));
    m.state.round.as_mut().unwrap().answers.fill(1);
    pages.push(("ui-done-correct".into(), m.view()));
    m.state.round.as_mut().unwrap().answers[0] = 0;
    m.state.progress.push(model::Progress { subject: 0, id: m.state.round.as_ref().unwrap().ids[0].clone(), status: 1, ..Default::default() });
    pages.push(("ui-done-review".into(), m.view()));
    m.subject = 0; m.selected = 2; m.page = Page::Deck;
    pages.push(("ui-deck-review".into(), m.view()));
    for i in 0..5 {
        m.page = Page::Settings; m.selected = i;
        pages.push((format!("ui-settings-{i}"), m.view()));
    }
    m.page = Page::Stats;
    pages.push(("ui-stats".into(), m.view()));
    let mut check = Model::new();
    begin_reset(&mut check);
    for step in 1..=3 {
        let view = check.view();
        pages.push((format!("ui-reset-check-{step}"), view.clone()));
        displayed(&mut check);
        tap(&mut check, reset_answer(&view));
    }
    for page in [Page::Reset, Page::DownloadConfirm, Page::DownloadPreparing, Page::Sleep] {
        m.page = page;
        for i in 0..if matches!(page, Page::Reset | Page::DownloadConfirm) { 2 } else { 1 } {
            m.selected = i;
            pages.push((format!("ui-{page:?}-{i}"), m.view()));
        }
    }
    for selected in 0..6 {
        m.page = Page::Settings;
        m.selected = selected;
        pages.push((format!("ui-parent-{selected}"), m.view()));
    }
    for selected in 0..3 {
        m.page = Page::Sources;
        m.selected = selected;
        pages.push((format!("ui-sources-{selected}"), m.view()));
    }
    for interval in model::CLEAR_INTERVALS {
        m.page = Page::LearningSettings;
        m.selected = 3;
        m.state.clear_interval = interval;
        pages.push((format!("ui-clear-interval-{interval}"), m.view()));
    }
    m.show_error("存档不可用，已保留当前进度");
    pages.push(("ui-error".into(), m.view()));
    m.storage_status(2); m.page = Page::Home;
    pages.push(("ui-save-failed".into(), m.view()));
    let mut course = super::curriculum::curriculum();
    for (page, count) in [(Page::LearningSettings,5),(Page::StartPoint,4),(Page::StartConfirm,2),(Page::CourseDone,1)] {
        course.page = page;
        for selected in 0..count {
            course.selected = selected;
            pages.push((format!("ui-course-{page:?}-{selected}"),course.view()));
        }
    }
    course.page = Page::UnitChoice;
    for subject in 0..4 {
        course.subject = subject;
        let last = model::course::COURSE_UNITS.iter().filter(|u|usize::from(u.subject)==subject).count()-1;
        for selected in 0..=last {
            course.selected = selected;
            pages.push((format!("ui-course-units-{subject}-{selected}"),course.view()));
        }
    }
    course.state.course.fill(model::course::SEEN|model::course::MASTERED|3);
    course.page = Page::Stats;
    pages.push(("ui-course-stats-full".into(),course.view()));
    course.state.course.fill(0);
    course.state.course[..1000].fill(model::course::SEEN|model::course::MASTERED|3);
    course.state.course[1000..2000].fill(model::course::SEEN|1);
    pages.push(("ui-course-stats-mixed".into(),course.view()));
    pages
}

fn regions(v: &View) -> Vec<([i32; 4], &str)> {
    use layout::*;
    let mut r = vec![(HEAD_STATUS, v.hint.as_str())];
    match v.screen {
        SCREEN_HOME => r.extend(text_regions(v)),
        SCREEN_DECK => {
            r.extend([(HEAD_CHIP, v.title.as_str()), (HEAD_TITLE, v.hero.as_str())]);
            r.extend(DECK_NAMES.into_iter().zip(["认一认", "选一选", "再看看"]));
            r.extend(DECK_NOTES.into_iter().zip(["看卡片，翻一翻", "四个里面选一个", &v.lines[0]]));
        }
        SCREEN_READ => r.extend([(HEAD_CHIP, v.title.as_str()), (READ_FRONT, &v.hero), (READ_BACK_FRONT, &v.lines[0]), (READ_BACK, &v.lines[1])]),
        SCREEN_QUIZ => {
            r.extend(text_regions(v));
            r.extend([(QUIZ_FEEDBACK_TITLE, v.lines[0].as_str()), (QUIZ_FEEDBACK_BODY, &v.lines[1])]);
        }
        SCREEN_SETTINGS => {
            r.push((SETTINGS_TITLE, v.hero.as_str()));
            r.extend(SETTINGS_LABELS.into_iter().zip(v.options.iter().map(String::as_str).chain(core::iter::once(v.lines[4].as_str()))));
            r.push((SETTINGS_VALUE, &v.lines[0]));
            r.push((SETTINGS_NOTE, &v.lines[5]));
        }
        SCREEN_STATS => {
            r.push((STATS_TITLE, "学习情况"));
            r.extend(STATS_COUNTS.into_iter().zip(v.lines.iter().take(4).map(String::as_str)));
        }
        SCREEN_SLEEP => r.extend([(SLEEP_TITLE, v.hero.as_str()), (SLEEP_BODY, &v.lines[0])]),
        SCREEN_MESSAGE => {
            r.extend([(MESSAGE_TITLE, v.hero.as_str()), (MESSAGE_BODY, &v.lines[0])]);
            r.extend(MESSAGE_BUTTONS.into_iter().zip(v.options.iter().take(2).map(String::as_str)));
        }
        _ => panic!("unexpected screen {}", v.screen),
    }
    r
}

#[test]
fn all_redesigned_pages_have_bounded_text_and_heap() {
    let pages = pages();
    let mut frame = vec![0; FRAMEBUFFER_BYTES];
    let mut again = vec![0; FRAMEBUFFER_BYTES];
    let mut stripe = vec![0; STRIPE_PIXELS];
    let (mut peak, mut worst) = (0, String::new());
    for (name, v) in &pages {
        let (_, p) = heap_meter::measure(|| render(v, &mut frame, &mut stripe).unwrap());
        if p > peak { (peak, worst) = (p, name.clone()); }
        render(v, &mut again, &mut stripe).unwrap();
        assert_eq!(frame, again, "{name}: repeated render differs");
        raster(v, &mut again, &mut stripe).unwrap();
        let regions = regions(v);
        for y in 0..HEIGHT {
            for x in 0..WIDTH {
                if black(&again, x, y) {
                    assert!(regions.iter().any(|(r, _)| within(x, y, *r)), "{name}: text outside its region at ({x},{y})");
                }
            }
        }
        for (r, text) in regions.iter().filter(|(_, t)| !t.is_empty()) {
            assert!((0..HEIGHT).any(|y| (0..WIDTH).any(|x| within(x, y, *r) && black(&again, x, y))), "{name}: no ink for {text:?} in {r:?}");
        }
        if v.screen == layout::SCREEN_QUIZ && overlay::CHEERS.contains(&v.art) {
            let ink = (0..HEIGHT).flat_map(|y| (0..WIDTH).map(move |x| (x, y)))
                .filter(|&(x, y)| within(x, y, layout::QUIZ_ART) && black(&frame, x, y)).count();
            assert!(ink > 500, "{name}: reward illustration is missing");
        }
        pbm(name, v);
    }
    assert!(peak <= 51_166, "render peak {peak} in {worst} exceeds measured old template maximum");
    println!("all redesigned pages: {} views; host render peak {peak} B ({worst}); limit 51166 B", pages.len());
}
