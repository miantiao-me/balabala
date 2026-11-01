fn open_download_model(m: &mut Model) {
    displayed(m);
    tap(m, Key::Home);
    if !m.ready() { displayed(m); }
    m.key(Key::Front3, true);
    m.key(Key::Parent, true);
    m.key(Key::Parent, false);
    m.key(Key::Front3, false);
    displayed(m);
    for _ in 0..4 {
        tap(m, Key::Down);
    }
    displayed(m);
    tap(m, Key::Front4);
    displayed(m);
}
#[test]
fn download_entry_default_cancel_release_single_request_and_volatile_restore() {
    let mut m = Model::new();
    open_download_model(&mut m);
    assert_eq!(m.page, Page::DownloadConfirm);
    assert_eq!(m.selected, 0);
    pbm("download-confirm-cancel", &m.view());
    tap(&mut m, Key::Front4);
    assert_eq!(m.page, Page::Settings);
    assert_eq!(m.platform_requests, 0);
    // index3（第四项）仍为BLE，不能由原兜底分支吞掉下载项。
    m.selected = 3;
    displayed(&mut m);
    tap(&mut m, Key::Front4);
    assert_eq!(m.page, Page::Network);
    assert_eq!(m.platform_requests, 2);
    tap(&mut m, Key::Home);
    assert_eq!(m.page, Page::Network);
    m.network_phase(1, 5, 0, 2);
    m.platform_requests = 0;
    open_download_model(&mut m);
    tap(&mut m, Key::Down);
    displayed(&mut m);
    pbm("download-confirm-enter", &m.view());
    m.key(Key::Front4, true);
    assert_eq!(m.page, Page::DownloadPreparing);
    assert_eq!(m.platform_requests, 8);
    m.platform_requests = 0;
    m.key(Key::Front4, true);
    assert_eq!(m.platform_requests, 0);
    m.key(Key::Front4, false);
    pbm("download-preparing", &m.view());
    let before = serde_json::to_vec(&m.state).unwrap();
    for key in [Key::Up, Key::Down, Key::Front1, Key::Front2, Key::Front4] {
        tap(&mut m, key);
    }
    assert_eq!(serde_json::to_vec(&m.state).unwrap(), before);
    assert_eq!(m.page, Page::DownloadPreparing);
    let (_, generation, bytes) = m.export().unwrap();
    assert!(m.save_ack(generation, true));
    let revision = m.download_ready().unwrap();
    assert_eq!(m.revision, revision);
    assert!(m.download_ready().is_none());
    m.storage_status(1);
    let view = m.view();
    assert_eq!(view.hero, "Download Mode");
    assert_eq!(view.lines[0], "Keep USB connected");
    assert!(view.download && !view.success);
    assert!(view.title.is_empty() && view.hint.is_empty() && view.keys.is_empty());
    assert!(view.lines[1..].iter().all(String::is_empty));
    let mut restored = Model::new();
    assert!(restored.load(&bytes, &[]));
    assert_eq!(restored.page, Page::Home);
    assert_eq!(restored.platform_requests, 0);
    tap(&mut m, Key::Front3); // 无须等提示页完成，也能取消。
    assert_eq!(m.page, Page::Home);
    assert_eq!(m.platform_requests, 16);
}
#[test]
fn download_confirm_must_display_and_release_before_second_confirmation() {
    let mut m = Model::new();
    displayed(&mut m);
    m.page = Page::Settings;
    m.selected = 4;
    m.revision += 1;
    displayed(&mut m);
    m.key(Key::Front4, true);
    assert_eq!(m.page, Page::DownloadConfirm);
    tap(&mut m, Key::Down);
    displayed(&mut m);
    m.key(Key::Front4, true);
    assert_eq!(m.page, Page::DownloadConfirm);
    m.key(Key::Front4, false);
    tap(&mut m, Key::Front4);
    assert_eq!(m.page, Page::DownloadPreparing);
    tap(&mut m, Key::Shutdown);
    assert_eq!(m.page, Page::Home);
    assert_eq!(m.platform_requests, 17); // 清除尚未消费的进入请求，保留取消和关机。
}
#[test]
fn download_prompt_pbm_has_only_two_centered_ink_bands_within_font_budget() {
    let mut m = Model::new();
    m.page = Page::DownloadPreparing;
    m.dirty = false;
    m.download_ready().unwrap();
    let view = m.view();
    let stats = pbm("download-mode", &view);
    assert!(stats.logical_construction_budget <= fonts::MAX_PAGE_FONT_BYTES);
    let mut frame = vec![0; FRAMEBUFFER_BYTES];
    let mut stripe = vec![0; STRIPE_PIXELS];
    render(&view, &mut frame, &mut stripe).unwrap();
    let mut bands = Vec::<(usize, usize, usize, usize)>::new();
    for y in 0..HEIGHT {
        let xs: Vec<_> = (0..WIDTH)
            .filter(|x| frame[y * WIDTH / 8 + x / 8] & (0x80 >> (x % 8)) == 0)
            .collect();
        if xs.is_empty() {
            continue;
        }
        if bands.last().is_none_or(|b| b.1 + 1 < y) {
            bands.push((y, y, xs[0], *xs.last().unwrap()));
        } else {
            let b = bands.last_mut().unwrap();
            b.1 = y;
            b.2 = b.2.min(xs[0]);
            b.3 = b.3.max(*xs.last().unwrap());
        }
    }
    assert_eq!(bands.len(), 2, "ink bands {bands:?}");
    for band in &bands {
        assert!(
            (band.2 + band.3).abs_diff(WIDTH - 1) <= 4,
            "horizontal ink bounds {bands:?}"
        );
    }
    assert!(
        (bands[0].0 + bands[1].1).abs_diff(HEIGHT - 1) <= 8,
        "vertical ink bounds {bands:?}"
    );
    println!(
        "Download prompt ink bounds: {bands:?}; font construction {} B",
        stats.logical_construction_budget
    );
}

#[test]
fn download_all_fixed_errors_and_legacy_large_ui_characters_keep_font_coverage() {
    let legacy: serde_json::Value =
        serde_json::from_str(include_str!("fixtures/legacy-large-glyphs.json")).unwrap();
    let legacy_chars: Vec<char> = legacy["characters"].as_str().unwrap().chars().collect();
    assert_eq!(legacy_chars.len(), 282);
    assert!(legacy_chars.windows(2).all(|w| w[0] < w[1]));
    for ch in legacy_chars {
        assert!(
            fonts::advance(ch, fonts::REGULAR_48).is_some(),
            "original 282 U+{:04X}",
            ch as u32
        );
    }
    let source: serde_json::Value =
        serde_json::from_str(include_str!("../native/font-sources.json")).unwrap();
    for ch in source["largeUiChars"].as_str().unwrap().chars() {
        assert!(
            fonts::advance(ch, fonts::REGULAR_48).is_some(),
            "legacy/new UI hero U+{:04X}",
            ch as u32
        );
    }
    for reason in 1..=13 {
        let mut m = Model::new();
        m.page = Page::DownloadPreparing;
        m.download_failed(reason);
        assert_eq!(m.page, Page::Error);
        let view = m.view();
        assert!(fonts::construction_budget(&view).unwrap() <= fonts::MAX_PAGE_FONT_BYTES);
        let mut frame = vec![0; FRAMEBUFFER_BYTES];
        let mut stripe = vec![0; STRIPE_PIXELS];
        render(&view, &mut frame, &mut stripe).unwrap();
        if reason == 6 {
            assert_eq!(m.error, "安全下载受限，无法使用普通USB下载");
            pbm("download-security-error", &view);
        }
        if reason == 8 {
            assert_eq!(m.error, "存储卡不可用，无法进入刷机模式");
            pbm("download-no-storage", &view);
        }
    }
}
