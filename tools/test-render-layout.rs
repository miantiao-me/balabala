#[test]
fn giant_fronts_fit_and_center_in_read_area() {
    let dir = std::path::Path::new(env!("BALABALA_GENERATED")).join("../pages");
    let mut tiers = std::collections::BTreeMap::new();
    for deck in builtin_decks() {
        for (i, card) in deck.cards.iter().enumerate() {
            let tier = match fit::front(&card.front).unwrap() {
                fit::Front::Giant(tier) => tier,
                fit::Front::Bold(text) => {
                    assert!(!text.is_empty());
                    *tiers.entry("bold48").or_insert(0) += 1;
                    continue;
                }
            };
            *tiers.entry(["176", "112"][tier]).or_insert(0) += 1;
            let mut frame = vec![0xff; FRAMEBUFFER_BYTES];
            overlay::giant_text(&mut frame, layout::READ_FRONT, &card.front, tier);
            let ink = |x: usize, y: usize| frame[y * WIDTH / 8 + x / 8] & (0x80 >> (x % 8)) == 0;
            let (mut l, mut r, mut t, mut b) = (WIDTH, 0, HEIGHT, 0);
            for y in 0..HEIGHT {
                for x in 0..WIDTH {
                    if ink(x, y) {
                        (l, r, t, b) = (l.min(x), r.max(x + 1), t.min(y), b.max(y + 1));
                    }
                }
            }
            let [fx, fy, fw, fh] = layout::READ_FRONT.map(|v| v as usize);
            assert!(
                l >= fx && r <= fx + fw && t >= fy && b <= fy + fh,
                "{} outside front area",
                card.front
            );
            assert!(
                (l - fx).abs_diff(fx + fw - r) <= 1 && (t - fy).abs_diff(fy + fh - b) <= 1,
                "{} not centered",
                card.front
            );
            let mut pbm = format!("P4\n{WIDTH} {HEIGHT}\n").into_bytes();
            pbm.extend(frame.iter().map(|b| !b));
            std::fs::write(
                dir.join(format!("probe-front-{}-{i:02}.pbm", deck.subject)),
                pbm,
            )
            .unwrap();
        }
    }
    println!("front tiers: {tiers:?}");
}
fn redesign_views() -> Vec<(String, View)> {
    let chip = |s: &str| match s {
        "hanzi" => "字",
        "pinyin" => "ā",
        "english" => "Aa",
        _ => "123",
    };
    let mut views = Vec::new();
    for focus in 0..5u8 {
        let mut v = View::new();
        v.screen = layout::SCREEN_HOME;
        v.focus = focus;
        v.title = "字".into();
        v.hero = "继续".into();
        views.push((format!("probe-home-{focus}"), v));
    }
    for deck in builtin_decks() {
        for (i, card) in deck.cards.iter().enumerate() {
            let mut v = View::new();
            v.screen = layout::SCREEN_QUIZ;
            v.title = chip(&deck.subject).into();
            (v.question_size, v.hero) = fit::question(&card.question).unwrap();
            (v.option_size, v.options) = fit::options(&card.options).unwrap();
            views.push((format!("probe-quiz-{}-{i:02}", deck.subject), v));
        }
    }
    views
}
/// 新模板的文字区域：(区域, 该区域文字)。叠加前的文字墨迹只能落在这些区域内，且有字的区域必须有墨迹。
fn text_regions(view: &View) -> Vec<([i32; 4], &str)> {
    match view.screen {
        layout::SCREEN_HOME => {
            let mut r = vec![
                (layout::HOME_HERO, view.hero.as_str()),
                (layout::HOME_CHIP, view.title.as_str()),
            ];
            r.extend(
                layout::HOME_TILES
                    .iter()
                    .zip(["汉字", "拼音", "英语", "数学"])
                    .map(|(t, s)| (*t, s)),
            );
            r
        }
        layout::SCREEN_QUIZ => {
            let mut r = vec![
                (layout::HEAD_CHIP, view.title.as_str()),
                (layout::QUIZ_QUESTION, view.hero.as_str()),
            ];
            r.extend(
                layout::QUIZ_OPTIONS
                    .iter()
                    .zip(&view.options)
                    .map(|(o, s)| (*o, s.as_str())),
            );
            r
        }
        _ => unreachable!(),
    }
}
fn black(frame: &[u8], x: usize, y: usize) -> bool {
    frame[y * WIDTH / 8 + x / 8] & (0x80 >> (x % 8)) == 0
}
fn within(x: usize, y: usize, [rx, ry, rw, rh]: [i32; 4]) -> bool {
    (x as i32) >= rx && (x as i32) < rx + rw && (y as i32) >= ry && (y as i32) < ry + rh
}
#[test]
fn redesign_templates_gate() {
    let views = redesign_views();
    let mut framebuffer = vec![0; FRAMEBUFFER_BYTES];
    let mut again = vec![0; FRAMEBUFFER_BYTES];
    let mut stripe = vec![0; STRIPE_PIXELS];
    let (mut peak, mut case, mut nodes) = (0, String::new(), 0);
    for (name, view) in &views {
        let (_, p) = heap_meter::measure(|| render(view, &mut framebuffer, &mut stripe).unwrap());
        if p > peak {
            (peak, case) = (p, name.clone());
        }
        nodes = nodes.max(projected_nodes(view));
        render(view, &mut again, &mut stripe).unwrap();
        assert!(framebuffer == again, "{name}: repeated render differs");
        raster(view, &mut again, &mut stripe).unwrap();
        let regions = text_regions(view);
        let mut inked = vec![false; regions.len()];
        for y in 0..HEIGHT {
            for x in 0..WIDTH {
                if black(&again, x, y) {
                    let hit = regions.iter().position(|(r, _)| within(x, y, *r));
                    assert!(
                        hit.is_some(),
                        "{name}: text ink at ({x},{y}) outside template regions"
                    );
                    inked[hit.unwrap()] = true;
                }
            }
        }
        for ((rect, text), inked) in regions.iter().zip(inked) {
            assert_eq!(
                inked,
                !text.is_empty(),
                "{name}: region {rect:?} text {text:?}"
            );
        }
        pbm(name, view);
    }
    // 叠加层常量文字也必须有字形，否则 overlay 会静默跳过。
    for (text, slot) in [
        ("字ā", fonts::BOLD_48),
        ("Aa123", fonts::BOLD_48),
        ("ABCD", fonts::REGULAR_24),
    ] {
        assert!(
            text.chars().all(|c| fonts::advance(c, slot).is_some()),
            "{text}"
        );
    }
    // 焦点切换只改焦点所在块，切回后逐字节复原。
    let frame = |v: &View| {
        let mut f = vec![0; FRAMEBUFFER_BYTES];
        render(v, &mut f, &mut vec![0; STRIPE_PIXELS]).unwrap();
        f
    };
    let (home0, home1) = (frame(&views[0].1), frame(&views[1].1));
    for y in 0..HEIGHT {
        for x in 0..WIDTH {
            if black(&home0, x, y) != black(&home1, x, y) {
                assert!(
                    within(x, y, layout::HOME_CARD) || within(x, y, layout::HOME_TILES[0]),
                    "focus change at ({x},{y})"
                );
            }
        }
    }
    assert!(home1 != home0 && frame(&views[0].1) == home0);
    // 新模板不得高于重写验收时冻结的旧模板宿主 render 峰值；真机 heap 另行测量。
    // 历史 View 生成物不纳入版本控制，门禁只保留其已测量上限，确保干净检出可重放。
    const LEGACY_RENDER_PEAK: usize = 51_166;
    assert!(
        peak <= LEGACY_RENDER_PEAK,
        "new template peak {peak} ({case}) > measured legacy maximum {LEGACY_RENDER_PEAK}"
    );
    let time = |view: &View, framebuffer: &mut [u8], stripe: &mut [u16]| {
        let start = std::time::Instant::now();
        for _ in 0..20 {
            render(view, framebuffer, stripe).unwrap();
        }
        start.elapsed().as_micros() / 20
    };
    println!(
        "redesign templates: {} views, max projected nodes={nodes}, host render peak={peak} ({case}); host render us new quiz={} home={}",
        views.len(),
        time(&views[5].1, &mut framebuffer, &mut stripe),
        time(&views[0].1, &mut framebuffer, &mut stripe),
    );
}
#[test]
fn compare_frozen_page_inputs_when_supplied() {
    let Some(input) = std::env::var_os("BALABALA_COMPARE_PAGES") else {
        println!("Frozen PBM comparison not configured");
        return;
    };
    let input = std::path::PathBuf::from(input);
    let mut count = 0;
    for entry in std::fs::read_dir(&input).unwrap() {
        let path = entry.unwrap().path();
        let Some(name) = path
            .file_name()
            .unwrap()
            .to_str()
            .unwrap()
            .strip_suffix(".view.json")
        else {
            continue;
        };
        let view: View = serde_json::from_slice(&std::fs::read(&path).unwrap()).unwrap();
        let mut frame = vec![0; FRAMEBUFFER_BYTES];
        let mut stripe = vec![0; STRIPE_PIXELS];
        render(&view, &mut frame, &mut stripe).unwrap();
        let mut pbm = format!("P4\n{WIDTH} {HEIGHT}\n").into_bytes();
        pbm.extend(frame.iter().map(|b| !b));
        assert_eq!(
            pbm,
            std::fs::read(input.join(format!("{name}.pbm"))).unwrap(),
            "page {name}"
        );
        count += 1;
    }
    // 原基线此页由真实ABI直接写PBM；提示字符串和其余View输入未改变。
    let mut m = Model::new();
    m.show_error("存档不可用，已保留当前进度");
    let mut frame = vec![0; FRAMEBUFFER_BYTES];
    let mut stripe = vec![0; STRIPE_PIXELS];
    render(&m.view(), &mut frame, &mut stripe).unwrap();
    let mut pbm = format!("P4\n{WIDTH} {HEIGHT}\n").into_bytes();
    pbm.extend(frame.iter().map(|b| !b));
    assert_eq!(pbm, std::fs::read(input.join("save-error.pbm")).unwrap());
    count += 1;
    let expected = std::fs::read_dir(input)
        .unwrap()
        .filter(|e| {
            e.as_ref()
                .unwrap()
                .path()
                .extension()
                .is_some_and(|s| s == "pbm")
        })
        .count();
    assert_eq!(count, expected);
    println!("Frozen exact-input PBMs compared: {count}; all byte-identical");
}

#[test]
fn aot_projection_hint_matches_live_nodes_and_small_hint_still_grows() {
    let mut m = Model::new();
    m.start(2, Mode::Quiz, false);
    let mut views = vec![m.view()];
    let mut download = Model::new();
    download.page = Page::DownloadPreparing;
    download.dirty = false;
    assert!(download.download_ready().is_some());
    views.push(download.view());
    m.state.round.as_mut().unwrap().answers[0] = 0;
    views.push(m.view());
    m.state.round.as_mut().unwrap().answers[0] = 1;
    views.push(m.view());
    let mut empty_lines = m.view();
    empty_lines.lines = core::array::from_fn(|_| String::new());
    empty_lines.success = false;
    views.push(empty_lines);
    let mut full = m.view();
    full.lines = core::array::from_fn(|_| "答案".into());
    views.push(full);
    let mut rows = Vec::new();
    for view in views {
        let count = projected_nodes(&view);
        let (app, _) = mount(&view, WIDTH, HEIGHT).unwrap();
        assert_eq!(app.ui().core().layout_node_count(), count);
        let expected = app.ui().core().current_draw_list().words.clone();
        drop(app);
        // 用真实生成App和同一props验证提示不足仍正常扩容，不裁掉投影节点。
        let mut core = Ui::new_with_layout_capacity(1);
        core.set_viewport(WIDTH as f32, HEIGHT as f32);
        fonts::load(&mut core, &view).unwrap();
        let mut host = microts::Ui::from_core(core);
        assert!(host.load_styles(include_bytes!(concat!(
            env!("BALABALA_GENERATED"),
            "/styles.bin"
        ))));
        let mut app = generated::AppApp::new(host, view.props(), ViewModel);
        app.frame(&microts::Input::default());
        app.ui_mut().core_mut().draw();
        assert_eq!(app.ui().core().layout_node_count(), count);
        assert_eq!(app.ui().core().current_draw_list().words, expected);
        rows.push(serde_json::json!({"success":view.success,"nonEmptyTexts":view.lines.iter().filter(|s|!s.is_empty()).count()+[&view.title,&view.hero,&view.hint,&view.keys].iter().filter(|s|!s.is_empty()).count(),"projectedNodes":count,"hintEnabled":true,"underHint":1}));
    }
    std::fs::write(
        std::path::Path::new(env!("BALABALA_GENERATED"))
            .join("../memory-round2-projection-checks.json"),
        serde_json::to_vec_pretty(&rows).unwrap(),
    )
    .unwrap();
}
