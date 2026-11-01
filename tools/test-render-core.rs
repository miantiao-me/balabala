#[test]
fn actual_props_pages_render_and_report_font_budget() {
    let mut m = Model::new();
    let mut report = String::new();
    let mut max = 0;
    let pages = [
        Page::Home,
        Page::Deck,
        Page::Stats,
        Page::Settings,
        Page::Reset,
        Page::Network,
        Page::Error,
    ];
    m.error = "卡组格式损坏或数量超限".into();
    for page in pages {
        m.page = page;
        let name = format!("{page:?}").to_lowercase();
        let stats = pbm(&name, &m.view());
        max = max.max(stats.resident_cmap_bytes);
        report.push_str(&format!(
            "{name}: 24px={} 48px={} resident-cmap-heap={}\n",
            stats.normal, stats.large, stats.resident_cmap_bytes
        ));
    }
    for s in 0..4 {
        m.start(s, Mode::Read, false);
        pbm(&format!("{s}-read-front"), &m.view());
        m.state.round.as_mut().unwrap().flipped = true;
        pbm(&format!("{s}-read-back"), &m.view());
        m.start(s, Mode::Quiz, false);
        let correct = answer_position(&m);
        let stats = pbm(&format!("{s}-quiz"), &m.view());
        max = max.max(stats.resident_cmap_bytes);
        displayed(&mut m);
        tap(&mut m, Key::from_u32(correct as u32).unwrap());
        assert!(m.view().success);
        pbm(&format!("{s}-correct"), &m.view());
        tap(&mut m, Key::Down);
        displayed(&mut m);
        let wrong = (answer_position(&m) + 1) % 4;
        tap(&mut m, Key::from_u32(wrong as u32).unwrap());
        pbm(&format!("{s}-incorrect"), &m.view());
    }
    m.page = Page::Done;
    pbm("done", &m.view());
    let mut measured_fonts = 0;
    let mut measured_render = 0;
    let mut measured_render_case = String::new();
    // 遍历内置卡的正反面、题面、对/错反馈及全部平台状态，避免只取每科首卡低估峰值。
    let mut views = Vec::new();
    for page in pages {
        m.page = page;
        views.push(m.view());
    }
    m.page = Page::Home;
    m.selected = 6;
    views.push(m.view());
    for status in 1..=6 {
        m.page = Page::Network;
        m.network = status;
        views.push(m.view());
    }
    let platform_views = views.len();
    for subject in 0..4 {
        for card in &m.decks[subject].cards.clone() {
            m.start(subject, Mode::Read, false);
            let r = m.state.round.as_mut().unwrap();
            r.ids = vec![card.id.clone()];
            r.answers = vec![-1];
            r.cursor = 0;
            views.push(m.view());
            m.state.round.as_mut().unwrap().flipped = true;
            views.push(m.view());
            m.state.round.as_mut().unwrap().mode = Mode::Quiz;
            views.push(m.view());
            m.state.round.as_mut().unwrap().answers[0] = 1;
            views.push(m.view());
            m.state.round.as_mut().unwrap().answers[0] = 0;
            views.push(m.view());
        }
    }
    for (index, view) in views.iter().enumerate() {
        let mut ui = Ui::new();
        let (stats, font_peak) = heap_meter::measure(|| fonts::load(&mut ui, view).unwrap());
        assert!(font_peak <= stats.logical_construction_budget);
        assert_eq!(
            fonts::construction_budget(view).unwrap(),
            stats.logical_construction_budget
        );
        assert!(font_peak <= fonts::MAX_PAGE_FONT_BYTES);
        measured_fonts = measured_fonts.max(font_peak);
        let mut framebuffer = vec![0; FRAMEBUFFER_BYTES];
        let mut stripe = vec![0; STRIPE_PIXELS];
        let (_, render_peak) =
            heap_meter::measure(|| render(view, &mut framebuffer, &mut stripe).unwrap());
        assert!(framebuffer.iter().any(|b| *b != 0xff), "blank model page");
        if render_peak > measured_render {
            measured_render = render_peak;
            measured_render_case = if index >= platform_views {
                let card_index = (index - platform_views) / 5;
                let (deck, card) = m
                    .decks
                    .iter()
                    .flat_map(|d| d.cards.iter().map(move |c| (d, c)))
                    .nth(card_index)
                    .unwrap();
                format!(
                    "{}/{} state={} (0=front,1=back,2=quiz,3=correct,4=incorrect)",
                    deck.subject,
                    card.id,
                    (index - platform_views) % 5
                )
            } else {
                format!("platform view index={index} hero={}", view.hero)
            };
        }
        assert!(fonts::width(&view.title, fonts::REGULAR_24) <= 424);
        assert!(view.hero.lines().all(|line| fonts::width(
            line,
            if view.question_size == fonts::REGULAR_24 {
                fonts::REGULAR_24
            } else {
                fonts::BOLD_48
            }
        ) <= 432));
        for line in view
            .lines
            .iter()
            .chain([&view.hint, &view.keys])
            .flat_map(|s| s.lines())
        {
            assert!(
                fonts::width(line, fonts::REGULAR_24) <= 432,
                "overflow: {line}"
            );
        }
    }
    let mut largest = 0;
    let mut largest_view = None;
    // 只保留当前页和最大页；补全标题后实测这个最大值，不额外构造全部状态的Atlas。
    for subject in 0..4 {
        for card in &m.decks[subject].cards.clone() {
            m.start(subject, Mode::Read, false);
            for total in 1..=10 {
                for cursor in 0..total {
                    let mut ids: Vec<String> = m.decks[subject]
                        .cards
                        .iter()
                        .filter(|c| c.id != card.id)
                        .take(total - 1)
                        .map(|c| c.id.clone())
                        .collect();
                    ids.insert(cursor, card.id.clone());
                    {
                        let r = m.state.round.as_mut().unwrap();
                        r.ids = ids;
                        r.answers = vec![-1; total];
                        r.cursor = cursor;
                    }
                    for (mode, flipped, answer) in [
                        (Mode::Read, false, -1),
                        (Mode::Read, true, -1),
                        (Mode::Quiz, false, -1),
                        (Mode::Quiz, false, 0),
                        (Mode::Quiz, false, 1),
                    ] {
                        let r = m.state.round.as_mut().unwrap();
                        r.mode = mode;
                        r.flipped = flipped;
                        r.answers[cursor] = answer;
                        let view = m.view();
                        let budget = fonts::construction_budget(&view).unwrap();
                        if budget > largest {
                            largest = budget;
                            largest_view = Some(view);
                        }
                    }
                }
            }
        }
    }
    assert!(largest <= fonts::MAX_PAGE_FONT_BYTES);
    let largest_view = largest_view.unwrap();
    let mut ui = Ui::new();
    let (_, font_peak) = heap_meter::measure(|| fonts::load(&mut ui, &largest_view).unwrap());
    assert!(font_peak < largest);
    let mut framebuffer = vec![0; FRAMEBUFFER_BYTES];
    let mut stripe = vec![0; STRIPE_PIXELS];
    let (_, render_peak) =
        heap_meter::measure(|| render(&largest_view, &mut framebuffer, &mut stripe).unwrap());
    pbm("font-budget-maximum", &largest_view);
    let card_count: usize = m.decks.iter().map(|d| d.cards.len()).sum();
    report.push_str(&format!("Complete built-in learning states={} ({card_count} cards * 55 position/length combinations * 5 states), plus 4 deck views; estimated maximum={largest}, actual font request peak={font_peak}, actual render request peak={render_peak}; title={}\n", card_count * 55 * 5, largest_view.title));
    report.push_str(&format!("Sample resident cmap heap peak={max}; exhaustive views={}\nHost requested-byte peak (chars capacity + final resident cmap; bitmap borrows Flash)={measured_fonts}\nHost full mount/draw/render heap request peak={measured_render} (64-bit host, excludes model/framebuffer/stripe; realloc conservatively counts old+new)\nframebuffer=48000 stripe=7680; allocator metadata/alignment and C3 runtime heap remain unmeasured\n",views.len()));
    report.push_str(&format!("Render peak case={measured_render_case}; meter begins immediately before render(view, framebuffer, stripe), ends after generated App/UI/Atlas drop. View and Model already exist outside meter; framebuffer/stripe are also excluded. Mount/font construction, UI draw/layout and render temporaries are included.\n"));
    let (_, builtin_lifecycle_peak) = heap_meter::measure(|| {
        let mut model = Model::new();
        model.start(2, Mode::Quiz, false);
        model.state.round.as_mut().unwrap().answers[0] = 1;
        let view = model.view();
        render(&view, &mut framebuffer, &mut stripe).unwrap();
    });
    report.push_str(&format!("Built-in english/english-01 correct-feedback lifecycle peak={builtin_lifecycle_peak}; includes Model/content/state + View + generated UI/Atlas/render heap; excludes framebuffer/stripe, Model struct on stack, actual C3 App Box allocation, allocator metadata and platform.\n"));
    std::fs::write(
        std::path::Path::new(env!("BALABALA_GENERATED")).join("../font-memory.txt"),
        report,
    )
    .unwrap();
    println!("font construction peak={measured_fonts}; host render peak={measured_render}");
    assert!(measured_fonts < 100000);
}
#[test]
fn abi_bad_slots_render_complete_and_front_exit() {
    unsafe {
        let app = balabala_app_create();
        let mut fb = vec![0; FRAMEBUFFER_BYTES];
        let mut stripe = vec![0; STRIPE_PIXELS];
        let mut rev = 0;
        let bad = b"broken save";
        for empty in [true, false] {
            let (ptr, len) = if empty {
                (core::ptr::null(), 0)
            } else {
                (bad.as_ptr(), bad.len())
            };
            assert_eq!(balabala_app_load(app, ptr, len, ptr, len), 1);
            assert_eq!((*app).model.page, Page::Error);
            assert_eq!((*app).model.error, "存档不可用，已保留当前进度");
            assert_eq!(
                balabala_app_render(
                    app,
                    fb.as_mut_ptr(),
                    fb.len(),
                    stripe.as_mut_ptr(),
                    stripe.len(),
                    &mut rev
                ),
                0
            );
            assert!(fb.iter().any(|b| *b != 0xff), "error page must render");
            std::fs::create_dir_all(
                std::path::Path::new(env!("BALABALA_GENERATED")).join("../pages"),
            )
            .unwrap();
            if !empty {
                let mut pbm = format!("P4\n{WIDTH} {HEIGHT}\n").into_bytes();
                pbm.extend(fb.iter().map(|b| !b));
                std::fs::write(
                    std::path::Path::new(env!("BALABALA_GENERATED"))
                        .join("../pages/save-error.pbm"),
                    pbm,
                )
                .unwrap();
            }
            assert_eq!(balabala_app_key(app, Key::Front4 as u32, 1), 0);
            assert_eq!(balabala_app_key(app, Key::Front4 as u32, 0), 0);
            assert_eq!((*app).model.page, Page::Error);
            assert_eq!(balabala_app_display_complete(app, rev), 0);
            assert_eq!(balabala_app_key(app, Key::Front4 as u32, 1), 0);
            assert_eq!(balabala_app_key(app, Key::Front4 as u32, 0), 0);
            assert_eq!((*app).model.page, Page::Home);
        }
        balabala_app_destroy(app);
    }
}
#[test]
fn eight_row_stripes_equal_full_small_render() {
    const W: usize = 96;
    const H: usize = 128;
    let mut view = View::new();
    view.title = "山".into();
    view.hero = "A".into();
    let (app, _) = mount(&view, W, H).unwrap();
    let ui = app.ui().core();
    let mut full = vec![0xffff; W * H];
    raster::render_scaled_rgb565_over(ui, &ui.current_draw_list().words, &mut full, 1);
    assert!(full.iter().any(|p| *p != 0xffff));
    let mut striped = vec![0; W * H];
    for (i, strip) in striped.chunks_mut(W * STRIPE_ROWS).enumerate() {
        let y = i * STRIPE_ROWS;
        render_window(
            ui,
            strip,
            DamageRect::new(0, y as i32, W as i32, (y + STRIPE_ROWS) as i32),
        );
    }
    assert_eq!(striped, full);
}
#[test]
fn monochrome_threshold_and_msb_order_are_defined() {
    let mut out = [0];
    pack_monochrome(
        &[0xffff, 0, 0xf800, 0x07e0, 0x001f, 0x8410, 0x7bef, 0xffff],
        &mut out,
    );
    assert_eq!(out, [0b1001_0101]);
}
#[test]
fn monochrome_quantization_matches_weighted_luminance_for_all_rgb565_colors() {
    let pixels: Vec<u16> = (0..=u16::MAX).collect();
    let mut output = vec![0; pixels.len() / 8];
    pack_monochrome(&pixels, &mut output);
    for (i, &pixel) in pixels.iter().enumerate() {
        let r = u32::from((pixel >> 11) & 31) * 255 / 31;
        let g = u32::from((pixel >> 5) & 63) * 255 / 63;
        let b = u32::from(pixel & 31) * 255 / 31;
        assert_eq!(output[i / 8] & (0x80 >> (i % 8)) != 0, r * 299 + g * 587 + b * 114 >= 128_000, "RGB565 {pixel:04x}");
    }
    let mut extremes = [0; 2];
    pack_monochrome(&[0xffff; 16], &mut extremes);
    assert_eq!(extremes, [0xff; 2]);
    pack_monochrome(&[0; 16], &mut extremes);
    assert_eq!(extremes, [0; 2]);
}

#[test]
fn c_abi_rejects_invalid_buffers_and_bridges_render_input_save_and_import() {
    unsafe {
        let app = balabala_app_create();
        let mut fb = vec![0xa5; FRAMEBUFFER_BYTES];
        let mut stripe = vec![0x1234; STRIPE_PIXELS];
        let mut rev = 0;
        assert_eq!(
            balabala_app_render(
                app,
                fb.as_mut_ptr(),
                FRAMEBUFFER_BYTES - 1,
                stripe.as_mut_ptr(),
                stripe.len(),
                &mut rev
            ),
            -1
        );
        assert!(fb.iter().all(|b| *b == 0xa5));
        assert_eq!(balabala_app_key(app, 99, 1), -1);
        assert_eq!(balabala_app_key(app, 0, 2), -1);
        assert_eq!(
            balabala_app_render(
                app,
                fb.as_mut_ptr(),
                fb.len(),
                stripe.as_mut_ptr(),
                stripe.len(),
                &mut rev
            ),
            0
        );
        assert_eq!(balabala_app_display_complete(app, rev), 0);
        assert_eq!(balabala_app_display_complete(app, rev), -1);
        let mut out = vec![0; model::MAX_SAVE];
        let mut slot = 9;
        let mut generation = 0;
        let length =
            balabala_app_save_export(app, out.as_mut_ptr(), out.len(), &mut slot, &mut generation);
        assert!(length > 24);
        assert_eq!(balabala_app_save_ack(app, generation, 1), 0);
        assert_eq!(
            balabala_app_load(app, out.as_ptr(), length as usize, core::ptr::null(), 0),
            0
        );
        assert_eq!(balabala_app_import_deck(app, core::ptr::null(), 0), -2);
        assert_eq!((*app).model.page, Page::Error);
        balabala_app_key(app, 6, 1);
        assert_eq!((*app).model.page, Page::Home);
        balabala_app_key(app, 7, 1);
        assert_eq!(balabala_app_platform_requests(app) & 1, 1);
        assert_eq!(balabala_app_platform_requests(app), 0);
        balabala_app_destroy(app);
    }
}

#[test]
fn platform_storage_hint_preserves_errors_gates_input_and_fits_font_budget() {
    let mut m = quiz();
    let mutation = m.mutation;
    m.storage_status(1);
    assert_eq!(m.view().hint, "存储不可用");
    assert_eq!(m.mutation, mutation);
    let revision = m.revision;
    m.storage_status(1);
    assert_eq!(m.revision, revision);
    tap(&mut m, Key::Front1);
    assert_eq!(m.state.round.as_ref().unwrap().answers[0], -1);
    displayed(&mut m);
    tap(&mut m, Key::Front1);
    assert_eq!(m.state.round.as_ref().unwrap().answers[0], 1);
    pbm("storage-unavailable", &m.view());
    m.storage_status(2);
    pbm("storage-save-failed", &m.view());
    let archive = m.export().unwrap().2;
    m.show_error("存档损坏，请返回后重试");
    assert!(m.load(&archive, &[]));
    assert_eq!(m.page, Page::Error);
    assert_eq!(m.error, "存档损坏，请返回后重试");
    let old = m.view();
    let revision = m.revision;
    m.storage_status(1);
    assert_eq!(m.page, Page::Error);
    assert_eq!(m.revision, revision);
    assert_eq!(m.view().lines, old.lines);
    assert_eq!(m.view().hint, old.hint);
    for deck in &m.decks {
        let peak = model::page_font_budget(deck).unwrap();
        println!("platform storage-hint font budget {}={peak}", deck.subject);
        assert!(
            peak <= fonts::MAX_PAGE_FONT_BYTES,
            "{}={peak}",
            deck.subject
        );
    }
    unsafe {
        let app = balabala_app_create();
        assert_eq!(balabala_app_storage_status(app, 3), -1);
        assert_eq!(balabala_app_storage_status(app, 1), 0);
        assert_eq!((*app).model.view().hint, "存储不可用");
        assert_eq!(balabala_app_storage_status(core::ptr::null_mut(), 0), -1);
        let mut frame = vec![0; FRAMEBUFFER_BYTES];
        let mut stripe = vec![0; STRIPE_PIXELS];
        let mut revision = 0;
        assert_eq!(
            balabala_app_render(
                app,
                frame.as_mut_ptr(),
                frame.len(),
                stripe.as_mut_ptr(),
                stripe.len(),
                &mut revision
            ),
            0
        );
        assert_eq!(balabala_app_storage_error(app), 0);
        assert_eq!((*app).model.error, "卡组无法读取，请检查存储卡");
        assert_eq!(balabala_app_status(app) & 3, 3);
        assert_eq!(
            balabala_app_render(
                app,
                frame.as_mut_ptr(),
                frame.len(),
                stripe.as_mut_ptr(),
                stripe.len(),
                &mut revision
            ),
            1
        );
        assert_eq!(balabala_app_display_complete(app, revision), 0);
        assert_eq!(
            balabala_app_load(app, archive.as_ptr(), archive.len(), core::ptr::null(), 0),
            0
        );
        assert_eq!((*app).model.page, Page::Error);
        assert_eq!((*app).model.state.round.as_ref().unwrap().answers[0], 1);
        pbm("storage-deck-read-error", &(*app).model.view());
        displayed(&mut (*app).model);
        tap(&mut (*app).model, Key::Front4);
        displayed(&mut (*app).model);
        tap(&mut (*app).model, Key::Front4);
        assert_eq!((*app).model.page, Page::Learn);
        assert_eq!((*app).model.state.round.as_ref().unwrap().answers[0], 1);
        assert_eq!(balabala_app_import_deck(app, b"{bad".as_ptr(), 4), -2);
        let error = (*app).model.error.clone();
        let revision = (*app).model.revision;
        assert_eq!(balabala_app_storage_error(app), 0);
        assert_eq!((*app).model.revision, revision);
        assert_eq!((*app).model.error, error);
        assert_eq!(
            balabala_app_load(app, core::ptr::null(), 0, core::ptr::null(), 0),
            1
        );
        assert_eq!((*app).model.error, error);
        assert_eq!(balabala_app_storage_error(core::ptr::null_mut()), -1);
        balabala_app_destroy(app);
    }
}

// 固定输入的优化前后对照；只记录请求字节/最大单块，不宣称C3上界。
#[test]
fn direct_font_subset_checks_format_indices_and_preserves_glyph_metrics() {
    use pocketjs_core::text::Atlas;
    let source = fonts::FONTS[0];
    let chars = ['A', '山', '\u{fffd}'];
    let all = Atlas::parse(source).unwrap();
    let subset = Atlas::subset(source, &chars).unwrap();
    for ch in chars {
        let a = all.lookup_entry(ch as u32).unwrap();
        let b = subset.lookup_entry(ch as u32).unwrap();
        assert_eq!((a.advance, a.xoff), (b.advance, b.xoff));
        assert_eq!(all.glyph_rows(a.gid), subset.glyph_rows(b.gid));
    }
    assert!(Atlas::subset(source, &[]).is_none());
    assert!(Atlas::subset(source, &['山', 'A']).is_none());
    assert!(Atlas::subset(source, &['A', 'A']).is_none());
    assert!(Atlas::subset(source, &['\u{10ffff}']).is_none());
    for len in [0, 4, 15, 16, source.len() - 1] {
        assert!(Atlas::subset(&source[..len], &chars).is_none());
    }
    for edit in 0..4 {
        let mut bad = source.to_vec();
        match edit {
            0 => bad[0] = 0,
            1 => bad[12] = 255,
            2 => bad[20..22].copy_from_slice(&u16::MAX.to_le_bytes()),
            _ => bad[24..28].copy_from_slice(&source[16..20]),
        }
        assert!(Atlas::subset(&bad, &chars).is_none());
    }
}
