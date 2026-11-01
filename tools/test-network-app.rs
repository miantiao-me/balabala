#[test]
fn network_platform_states_cancel_and_late_status_are_explicit() {
    let mut m = quiz();
    tap(&mut m, Key::Front2);
    let state = serde_json::to_vec(&m.state).unwrap();
    m.page = Page::Settings;
    m.selected = 3;
    displayed(&mut m);
    tap(&mut m, Key::Front4);
    assert_eq!(m.page, Page::Network);
    assert_eq!(m.network, 7);
    assert_eq!(m.platform_requests & 2, 2);
    m.platform_requests = 0;
    let revision = m.network_phase(1, 7, 1, 3);
    assert!(!m.network_ready(1, 1, revision));
    displayed(&mut m);
    assert!(m.network_ready(1, 1, revision));
    for (generation, phase, rev) in [(2, 1, revision), (1, 2, revision), (1, 1, revision + 1)] {
        assert!(!m.network_ready(generation, phase, rev));
    }
    let revision = m.network_phase(1, 7, 2, 1);
    assert!(!m.network_ready(1, 2, revision));
    // 未ready也能取消；held去重，清理结束前保持原页面和学习状态。
    m.key(Key::Front4, true);
    assert_eq!(m.network, 8);
    assert_eq!(m.platform_requests, 4);
    m.platform_requests = 0;
    m.key(Key::Front4, true);
    assert_eq!(m.platform_requests, 0);
    m.key(Key::Front4, false);
    assert!(!m.network_ready(1, 2, revision));
    assert_eq!(m.page, Page::Network);
    assert!(!m.network_status(3));
    m.network_phase(1, 5, 0, 2);
    assert_eq!(m.page, Page::Settings);
    assert_eq!(serde_json::to_vec(&m.state).unwrap(), state);
    assert!(!m.network_status(3));
}
#[test]
fn network_identity_abi_pages_and_exit_are_bounded_and_render_only_changes() {
    unsafe {
        let app = balabala_app_create();
        (*app).model.page = Page::Network;
        (*app).model.network = 1;
        let name = b"PROV_A1B2C3";
        let pop = b"012345"; // 固定测试数据，不是真机PoP。
        for boundary in [b"000000", b"999999"] {
            assert_eq!(
                balabala_app_network_info(app, name.as_ptr(), 11, boundary.as_ptr(), 6),
                0
            );
            assert_eq!((*app).model.view().lines[1].as_bytes(), boundary);
        }
        assert_eq!(
            balabala_app_network_info(app, name.as_ptr(), 11, pop.as_ptr(), 6),
            0
        );
        let revision = (*app).model.revision;
        assert_eq!(
            balabala_app_network_info(app, name.as_ptr(), 11, pop.as_ptr(), 6),
            0
        );
        assert_eq!((*app).model.revision, revision);
        for (name_len, pop_len) in [
            (10, 6),
            (12, 6),
            (11, 5),
            (11, 7),
            (11, 16),
            (11, 0),
            (0, 6),
        ] {
            assert_eq!(
                balabala_app_network_info(app, name.as_ptr(), name_len, pop.as_ptr(), pop_len),
                -1
            );
        }
        assert_eq!(
            balabala_app_network_info(app, core::ptr::null(), 11, pop.as_ptr(), 6),
            -1
        );
        assert_eq!(
            balabala_app_network_info(app, b"PROV_abcdef".as_ptr(), 11, pop.as_ptr(), 6),
            -1
        );
        assert_eq!(
            balabala_app_network_info(app, name.as_ptr(), 11, core::ptr::null(), 6),
            -1
        );
        for invalid in [
            b"01234a".as_slice(),
            b"12345 ",
            b"12345\0",
            "１２".as_bytes(),
        ] {
            assert_eq!(
                balabala_app_network_info(app, name.as_ptr(), 11, invalid.as_ptr(), 6),
                -1
            );
        }
        assert_eq!((*app).model.revision, revision);
        assert_eq!(balabala_app_network_status(app, 2), 0);
        let revision = (*app).model.revision;
        assert_eq!(balabala_app_network_status(app, 2), 0);
        assert_eq!((*app).model.revision, revision);
        let view = (*app).model.view();
        assert_eq!(view.lines[0], "PROV_A1B2C3");
        assert_eq!(view.lines[1], "012345");
        assert!(fonts::construction_budget(&view).unwrap() <= fonts::MAX_PAGE_FONT_BYTES);
        let stats = pbm("ble-connecting", &view);
        assert!(stats.logical_construction_budget <= fonts::MAX_PAGE_FONT_BYTES);
        drop(view);
        assert_eq!(
            balabala_app_network_info(app, core::ptr::null(), 0, core::ptr::null(), 0),
            0
        );
        let cleared = (*app).model.view();
        assert!(cleared.lines[0].is_empty(), "device name must be cleared");
        assert!(cleared.lines[1].is_empty(), "PoP must be cleared");
        assert_eq!(
            balabala_app_network_info(app, name.as_ptr(), 11, pop.as_ptr(), 6),
            0
        );
        assert_eq!(balabala_app_network_status(app, 3), 0);
        let view = (*app).model.view();
        assert!(!view.lines.iter().any(|s| s.contains("012345")));
        pbm("ble-success", &view);
        drop(view);
        displayed(&mut (*app).model);
        tap(&mut (*app).model, Key::Front3);
        assert_ne!(balabala_app_platform_requests(app) & 4, 0);
        assert_eq!(
            balabala_app_network_info(app, name.as_ptr(), 11, pop.as_ptr(), 6),
            -1
        );
        balabala_app_destroy(app);
    }
    for status in [1, 4, 5, 6] {
        let mut m = Model::new();
        m.page = Page::Network;
        m.network = status;
        pbm(&format!("ble-status-{status}"), &m.view());
    }
    for key in [Key::Home, Key::Shutdown, Key::Front3, Key::Front4] {
        let mut m = Model::new();
        m.page = Page::Network;
        m.network = 3;
        displayed(&mut m);
        tap(&mut m, key);
        assert_ne!(m.platform_requests & 4, 0);
        assert_ne!(m.page, Page::Network);
    }
}
#[test]
fn network_cleanup_error_abi_accepts_cancel_timeout_but_rejects_late_success_and_left_page() {
    unsafe {
        for terminal in [3, 5, 6] {
            let app = balabala_app_create();
            (*app).model.page = Page::Network;
            (*app).model.network = 2;
            assert_eq!(balabala_app_network_status(app, terminal), 0);
            assert_eq!(balabala_app_network_status(app, 3), -1);
            assert_eq!(balabala_app_network_status(app, 4), 0);
            assert_eq!((*app).model.network, 4);
            assert!((*app).model.view().provisioning);
            assert_eq!(balabala_app_network_status(app, 3), -1);
            displayed(&mut (*app).model);
            assert_eq!(balabala_app_key(app, Key::Front4 as u32, 1), 0);
            assert_eq!(balabala_app_key(app, Key::Front4 as u32, 0), 0);
            assert_eq!((*app).model.page, Page::Settings);
            assert_ne!(balabala_app_platform_requests(app) & 4, 0);
            assert_eq!(balabala_app_network_status(app, 4), -1);
            balabala_app_destroy(app);
        }
    }
}
#[test]
fn network_shutdown_blocked_keeps_owned_panel_cancels_shutdown_and_never_replays_it() {
    unsafe {
        assert_eq!(
            balabala_app_network_shutdown_blocked(core::ptr::null_mut()),
            -1
        );
        let app = balabala_app_create();
        (*app).model.page = Page::Network;
        (*app).model.network = 2;
        (*app).model.network_owned = true;
        assert_eq!(
            balabala_app_network_info(app, b"PROV_A1B2C3".as_ptr(), 11, b"012345".as_ptr(), 6),
            0
        );
        tap(&mut (*app).model, Key::Shutdown);
        assert_eq!((*app).model.page, Page::Network);
        assert_eq!(balabala_app_platform_requests(app), 5);
        assert_eq!(balabala_app_network_shutdown_blocked(app), 0);
        assert_eq!((*app).model.network, 9);
        assert!((*app).model.view().lines.iter().all(String::is_empty));
        assert_eq!(balabala_app_platform_requests(app), 0);
        assert_eq!(balabala_app_network_status(app, 3), -1);
        assert_eq!(
            balabala_app_network_info(app, b"PROV_A1B2C3".as_ptr(), 11, b"012345".as_ptr(), 6),
            -1
        );
        for key in [Key::Home, Key::Front3, Key::Front4, Key::Shutdown] {
            tap(&mut (*app).model, key);
            assert_eq!((*app).model.page, Page::Network);
            assert_eq!((*app).model.network, 9);
            assert_eq!(balabala_app_platform_requests(app) & 4, 4);
            balabala_app_network_phase(app, 1, 9, 0, 5);
        }
        // 人工恢复释放资源后显示错误，不能重放原关机。
        balabala_app_network_phase(app, 1, 4, 0, 6);
        assert_eq!((*app).model.page, Page::Error);
        assert_eq!(balabala_app_platform_requests(app) & 1, 0);
        balabala_app_destroy(app);
    }
}
