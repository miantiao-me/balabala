use super::*;

fn shown(model: &mut Model) {
    let revision = model.begin_render().unwrap();
    assert!(model.complete(revision));
}
fn tap(model: &mut Model, key: Key) {
    model.key(key, true);
    model.key(key, false);
}

#[test]
fn partial_allowed_truth_table() {
    let pages = [
        Page::Home,
        Page::Deck,
        Page::Learn,
        Page::Done,
        Page::Stats,
        Page::Settings,
        Page::Sources,
        Page::LearningSettings,
        Page::StartPoint,
        Page::UnitChoice,
        Page::StartConfirm,
        Page::CourseDone,
        Page::Reset,
        Page::Network,
        Page::DownloadConfirm,
        Page::DownloadPreparing,
        Page::DownloadPrompt,
        Page::Error,
        Page::Sleep,
    ];
    let ordinary = [
        Page::Home, Page::Deck, Page::Learn, Page::Done, Page::Stats, Page::Settings,
        Page::LearningSettings, Page::StartPoint, Page::UnitChoice, Page::StartConfirm,
        Page::CourseDone, Page::Reset, Page::DownloadConfirm, Page::Sources,
    ];
    for next in pages {
        assert!(!partial_allowed(None, next));
        for previous in pages {
            let allowed = ordinary.contains(&previous) && ordinary.contains(&next);
            assert_eq!(
                partial_allowed(Some(previous), next),
                allowed,
                "{previous:?} -> {next:?}"
            );
        }
    }
}

#[test]
fn first_frame_menu_movement_and_page_change() {
    let mut model = Model::new();
    assert!(!model.refresh_partial());
    let first = model.begin_render().unwrap();
    assert!(!model.refresh_partial());
    assert!(model.complete(first));
    assert!(!model.refresh_partial());
    tap(&mut model, Key::Down);
    let menu = model.begin_render().unwrap();
    assert!(model.refresh_partial());
    assert!(model.complete(menu));
    tap(&mut model, Key::Front4);
    assert_eq!(model.page, Page::Deck);
    model.begin_render().unwrap();
    assert!(model.refresh_partial());
}

#[test]
fn same_learning_page_flip_next_card_and_answer_feedback_allow_partial() {
    let mut model = Model::new();
    shown(&mut model);
    model.start(0, Mode::Read, false);
    let first = model.begin_render().unwrap();
    assert!(model.refresh_partial());
    assert!(model.complete(first));
    tap(&mut model, Key::Front4);
    assert!(model.state.round.as_ref().unwrap().flipped);
    let flip = model.begin_render().unwrap();
    assert!(model.refresh_partial());
    assert!(model.complete(flip));
    tap(&mut model, Key::Down);
    let next = model.begin_render().unwrap();
    assert!(model.refresh_partial());
    assert!(model.complete(next));
    model.start(0, Mode::Quiz, false);
    shown(&mut model);
    let answer = model.card().unwrap().correct;
    tap(&mut model, Key::from_u32(answer as u32).unwrap());
    assert!(model.view().success);
    model.begin_render().unwrap();
    assert!(model.refresh_partial());
}

#[test]
fn network_download_and_error_rerenders_require_full() {
    let mut model = Model::new();
    for page in [
        Page::Network,
        Page::DownloadPreparing,
        Page::DownloadPrompt,
        Page::Error,
        Page::Sleep,
    ] {
        model.go(page);
        shown(&mut model);
        model.changed();
        let revision = model.begin_render().unwrap();
        assert!(!model.refresh_partial(), "{page:?}");
        assert!(model.complete(revision));
        model.go(Page::Home);
        let exit = model.begin_render().unwrap();
        assert!(!model.refresh_partial(), "exiting {page:?}");
        assert!(model.complete(exit));
    }
}

#[test]
fn failed_ack_keeps_previous_page_and_success_records_rendered_page() {
    let mut model = Model::new();
    shown(&mut model);
    model.go(Page::Network);
    let pending = model.begin_render().unwrap();
    assert!(!model.complete(pending + 1));
    assert_eq!(model.displayed_page, Some(Page::Home));
    tap(&mut model, Key::Home);
    assert_eq!(model.page, Page::Home);
    assert!(!model.refresh_partial());
    assert!(model.complete(pending));
    assert_eq!(model.displayed_page, Some(Page::Network));
    model.begin_render().unwrap();
    assert!(!model.refresh_partial());
}

#[test]
fn refresh_partial_abi_and_failed_display_complete() {
    unsafe {
        use crate::{
            balabala_app_create, balabala_app_destroy, balabala_app_display_complete,
            balabala_app_key, balabala_app_refresh_partial, balabala_app_render,
        };
        let app = balabala_app_create();
        let mut frame = alloc::vec![0; crate::FRAMEBUFFER_BYTES];
        let mut stripe = alloc::vec![0; crate::STRIPE_PIXELS];
        let mut revision = 0;
        assert_eq!(balabala_app_refresh_partial(core::ptr::null()), 0);
        assert_eq!(balabala_app_refresh_partial(app), 0);
        let render = |revision: &mut u64, frame: &mut Vec<u8>, stripe: &mut Vec<u16>| {
            balabala_app_render(
                app,
                frame.as_mut_ptr(),
                frame.len(),
                stripe.as_mut_ptr(),
                stripe.len(),
                revision,
            )
        };
        assert_eq!(render(&mut revision, &mut frame, &mut stripe), 0);
        assert_eq!(balabala_app_refresh_partial(app), 0);
        assert_eq!(balabala_app_display_complete(app, revision + 1), -1);
        assert_eq!((*app).model.displayed_page, None);
        assert_eq!(balabala_app_display_complete(app, revision), 0);
        assert_eq!(balabala_app_key(app, Key::Down as u32, 1), 0);
        assert_eq!(balabala_app_key(app, Key::Down as u32, 0), 0);
        assert_eq!(render(&mut revision, &mut frame, &mut stripe), 0);
        assert_eq!(balabala_app_refresh_partial(app), 1);
        assert_eq!(balabala_app_display_complete(app, revision + 1), -1);
        assert_eq!(balabala_app_refresh_partial(app), 1);
        assert_eq!(balabala_app_display_complete(app, revision), 0);
        assert_eq!(balabala_app_refresh_partial(app), 0);
        balabala_app_destroy(app);
    }
}

fn saved_and_shown(model: &mut Model) {
    let (_, generation, _) = model.export().unwrap();
    assert!(model.save_ack(generation, true));
    if !model.ready() { shown(model); }
    assert!(!model.dirty);
}
fn unchanged(model: &mut Model, key: Key) {
    saved_and_shown(model);
    let before = (model.revision, model.mutation);
    tap(model, key);
    assert_eq!((model.revision, model.mutation), before);
    assert!(!model.dirty);
    assert_eq!(model.begin_render(), None);
}

#[test]
fn network_exit_releases_only_its_home_or_shutdown_bit() {
    for owned in [false, true] {
        for key in [Key::Home, Key::Shutdown] {
            for other_held in [false, true] {
                let mut model = Model::new();
                model.start(0, Mode::Quiz, false);
                model.go(Page::Network);
                model.network_owned = owned;
                if other_held { model.key(Key::Front2, true); }
                model.key(key, true);
                if owned {
                    assert_eq!(model.page, Page::Network);
                    model.network_phase(1, 5, 0, 2);
                }
                assert_eq!(model.page, Page::Home);
                assert_eq!(model.platform_requests & 1, u32::from(key == Key::Shutdown));
                model.key(key, false);
                assert_eq!(model.held, if other_held { 1 << Key::Front2 as u8 } else { 0 });
                shown(&mut model);
                tap(&mut model, Key::Front4);
                if other_held {
                    assert_eq!(model.page, Page::Home);
                    // 仍按住的旧键也必须阻止新轮评分。
                    model.start(0, Mode::Quiz, false);
                    shown(&mut model);
                    tap(&mut model, Key::Front1);
                    assert!(model.state.progress.is_empty());
                    model.key(Key::Front2, false);
                    tap(&mut model, Key::Front1);
                    assert_eq!(model.state.progress.len(), 1);
                } else {
                    assert_eq!(model.page, Page::Learn);
                    shown(&mut model);
                    tap(&mut model, Key::Front1);
                    assert_eq!(model.state.progress.len(), 1);
                }
            }
        }
    }
}

#[test]
fn noop_navigation_does_not_dirty_or_request_render() {
    let mut model = Model::new();
    for mode in [Mode::Read, Mode::Quiz] {
        model.start(0, mode, false);
        unchanged(&mut model, Key::Up);
    }
    model.start(0, Mode::Read, false);
    shown(&mut model);
    tap(&mut model, Key::Front4);
    saved_and_shown(&mut model);
    let before = (model.revision, model.mutation);
    tap(&mut model, Key::Up);
    assert!(!model.state.round.as_ref().unwrap().flipped);
    assert_eq!((model.revision, model.mutation), (before.0 + 1, before.1 + 1));
    assert!(model.dirty);
    shown(&mut model);
    tap(&mut model, Key::Down);
    assert_eq!(model.state.round.as_ref().unwrap().cursor, 1);
    shown(&mut model);
    for page in [Page::CourseDone, Page::Done] {
        model.go(page);
        unchanged(&mut model, Key::Down);
        if page == Page::CourseDone { unchanged(&mut model, Key::Up); }
    }
    let revision = model.revision;
    tap(&mut model, Key::Up);
    assert_eq!(model.page, Page::Learn);
    assert_eq!(model.revision, revision + 1);
    shown(&mut model);
    model.go(Page::Home);
    saved_and_shown(&mut model);
    tap(&mut model, Key::Down);
    assert_eq!(model.selected, 1);
    shown(&mut model);
    let revision = model.revision;
    tap(&mut model, Key::Home);
    assert_eq!(model.selected, 0);
    assert_eq!(model.revision, revision + 1);
    shown(&mut model);
}

#[test]
fn default_home_clears_parent_arm_preserves_held_and_shutdown_request() {
    for key in [Key::Home, Key::Shutdown] {
        let mut model = Model::new();
        saved_and_shown(&mut model);
        model.key(Key::Front3, true);
        model.key(Key::Front2, true);
        assert!(model.parent_armed);
        let before = (model.revision, model.mutation);
        tap(&mut model, key);
        assert!(!model.parent_armed);
        assert_eq!(model.held, (1 << Key::Front3 as u8) | (1 << Key::Front2 as u8));
        assert_eq!(model.revision, before.0);
        assert_eq!(model.begin_render(), None);
        assert_eq!(model.mutation, before.1 + u64::from(key == Key::Shutdown));
        assert_eq!(model.dirty, key == Key::Shutdown);
        assert_eq!(model.platform_requests & 1, u32::from(key == Key::Shutdown));
        model.key(Key::Parent, true);
        tap(&mut model, Key::Front4);
        assert_eq!(model.page, Page::Home);
        model.key(Key::Front3, false);
        tap(&mut model, Key::Front4);
        assert_eq!(model.page, Page::Home);
        model.key(Key::Front2, false);
        tap(&mut model, Key::Front4);
        assert_eq!(model.page, Page::Deck);
    }
}
