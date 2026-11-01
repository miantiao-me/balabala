#[test]
fn resident_content_limit_rejects_valid_low_glyph_growth() {
    let mut v = low_glyph_deck();
    let original_version = v["version"].as_u64().unwrap();
    v["version"] = (original_version + 1).into();
    let bytes = serde_json::to_vec(&v).unwrap();
    assert!(bytes.len() <= model::MAX_PACKAGE);
    model::parse_deck(&bytes).unwrap();
    let mut m = quiz();
    tap(&mut m, Key::Front1);
    let before = serde_json::to_vec(&m.state).unwrap();
    println!(
        "builtin active-content charge={}; limit={}",
        m.content_bytes(),
        model::MAX_ACTIVE_CONTENT_BYTES
    );
    assert!(m.content_bytes() <= model::MAX_ACTIVE_CONTENT_BYTES);
    assert_eq!(m.import(&bytes), Err("全部卡组超出常驻内存上限"));
    assert_eq!(u64::from(m.decks[0].version), original_version);
    assert_eq!(serde_json::to_vec(&m.state).unwrap(), before);
    // 专项审查样本：低字形增长卡组的近常驻上限、满输入及满双槽。
    let (near, near_charge) = largest_content_fixture(v.clone());
    let mut probe = Model::new();
    probe.import(&near).unwrap();
    assert_eq!(probe.content_bytes(), near_charge);
    let (_, import_peak) = heap_meter::measure(|| {
        let mut model = Model::new();
        let input = near.clone();
        model.import(&input).unwrap();
    });
    let mut rejected = bytes.clone();
    rejected.resize(model::MAX_PACKAGE, b' ');
    let (_, rejected_peak) = heap_meter::measure(|| {
        let mut model = Model::new();
        let input = rejected.clone();
        assert_eq!(model.import(&input), Err("全部卡组超出常驻内存上限"));
    });
    let progress: Vec<_> = (0..160)
        .map(|i| model::Progress {
            subject: i / 40,
            id: format!("p{i:031}"),
            status: 2,
            ..Default::default()
        })
        .collect();
    let snapshot = model::Snapshot {
        offsets: [0; 4],
        count: 10,
        round: None,
        progress,
        ..Default::default()
    };
    let record = padded_save_record(&snapshot, 1);
    let (_, restore_peak) = heap_meter::measure(|| {
        let mut model = Model::new();
        let input = near.clone();
        model.import(&input).unwrap();
        drop(input);
        let a = record.clone();
        let b = record.clone();
        assert!(!model.load(&a, &b));
        assert!(model.export().is_err());
    });
    let mut framebuffer = vec![0; FRAMEBUFFER_BYTES];
    let mut stripe = vec![0; STRIPE_PIXELS];
    let (_, render_peak) = heap_meter::measure(|| {
        let mut model = Model::new();
        let input = near.clone();
        model.import(&input).unwrap();
        drop(input);
        model.start(0, Mode::Quiz, false);
        model.state.round.as_mut().unwrap().answers[0] = 0;
        let view = model.view();
        render(&view, &mut framebuffer, &mut stripe).unwrap();
    });
    let report = format!("64-bit host conservative requested-byte lifecycle samples; realloc counts old+new; includes Model/content/state plus all allocations after meter starts. No allocator headers/alignment/C FILE/FatFS/SPI/IDF tasks/task stack/framebuffer/stripe/BLE. Not whole allowed-domain upper bounds.\nnear-cap C3 content charge={near_charge}; package bytes={}\nboot -> near-cap import peak={import_peak}\nboot -> rejected 65536-byte valid JSON/40-card candidate import peak={rejected_peak}\nboot -> near-cap import -> drop input -> restore two 32768-byte CRC-valid records with 160 progress entries each peak={restore_peak}\nboot -> near-cap import -> drop input -> wrong-feedback view/render peak={render_peak}\nrestore inputs are cloned Rust Vec allocations standing in for equal-size C file buffers; header/CRC selection precedes one candidate decode at a time; unknown valid IDs in subjects without a verified import now protect saving after selection. Render sample keeps Model+View live; generated UI/Atlas are dropped before View/Model.\n",near.len());
    std::fs::write(
        std::path::Path::new(env!("BALABALA_GENERATED")).join("../platform2-memory-scenarios.txt"),
        &report,
    )
    .unwrap();
    println!("{report}");
}
#[test]
fn memory_round2_same_scenarios_report_peak_and_largest_request() {
    let mut report = Vec::new();
    macro_rules! sample { ($name:expr,$body:expr) => {{
        let (_,peak)=heap_meter::measure($body);
        let trace=heap_meter::trace();assert!(!trace.overflow);
        let stages:Vec<_>=trace.stages[..trace.count].iter().map(|s|serde_json::json!({"end":s.name,"live":s.live,"peak":s.peak,"largestRequest":s.largest})).collect();
        let growth:Vec<_>=trace.events[..trace.events_len].iter().map(|g|serde_json::json!({"stage":trace.stages[g.stage].name,"old":g.old,"new":g.new,"overlappingLive":g.overlapping_live})).collect();
        report.push(serde_json::json!({"case":$name,"peak":peak,"largestRequest":heap_meter::largest_request(),"stages":stages,"reallocationsOver8KiB":growth}));
    }}; }
    let mut frame = vec![0; FRAMEBUFFER_BYTES];
    let mut stripe = vec![0; STRIPE_PIXELS];
    sample!("builtin-correct-model-view-render", || {
        let mut m = Model::new();
        m.start(2, Mode::Quiz, false);
        m.state.round.as_mut().unwrap().answers[0] = 1;
        render(&m.view(), &mut frame, &mut stripe).unwrap();
    });
    for status in 1..=6 {
        sample!(format!("network-{status}-model-view-render"), || {
            let mut m = Model::new();
            m.page = Page::Network;
            m.network = status;
            render(&m.view(), &mut frame, &mut stripe).unwrap();
        });
    }
    let near = memory_round1_fixture();
    let mut high: serde_json::Value = serde_json::from_slice(&near).unwrap();
    high["cards"][0]["question"] = "MNOP".into();
    high["cards"][0]["options"] = serde_json::json!(["ABC", "DEF", "GHI", "JKL"]);
    let high = serde_json::to_vec(&high).unwrap();
    sample!("near-cap-high-glyph-model-view-render", || {
        let mut m = Model::new();
        let input = high.clone();
        m.import(&input).unwrap();
        drop(input);
        m.start(0, Mode::Quiz, false);
        render(&m.view(), &mut frame, &mut stripe).unwrap();
    });
    // 同时接近内容与完整页面字体门禁；该组合在新公式下合法，单列不冒充旧基线。
    let (dense_input, dense_font, dense_charge) = near_dual_limit_fixture();
    sample!(
        "near-content-and-page-font-limits-model-view-render",
        || {
            let mut m = Model::new();
            let input = dense_input.clone();
            m.import(&input).unwrap();
            drop(input);
            m.start(0, Mode::Quiz, false);
            render(&m.view(), &mut frame, &mut stripe).unwrap();
        }
    );
    let mut dense_model = Model::new();
    dense_model.import(&dense_input).unwrap();
    dense_model.start(0, Mode::Quiz, false);
    let exhaustive = model::page_font_budget(&dense_model.decks[0]).unwrap();
    pbm("near-content-and-font-limits", &dense_model.view());
    drop(dense_model);
    println!("Combined near limits: content={dense_charge}/32768; current-page font={dense_font}/83807; full-deck font={exhaustive}/83807");
    let snapshot = model::Snapshot {
        count: 10,
        offsets: [0; 4],
        round: Some(model::Round {
            subject: 0,
            ids: (0..10).map(|i| format!("h{i:031}")).collect(),
            answers: vec![1; 10],
            cursor: 9,
            mode: Mode::Quiz,
            flipped: false,
            permutations: vec![],
        }),
        progress: (0..160)
            .map(|i| model::Progress {
                subject: i / 40,
                id: format!("p{i:031}"),
                status: 2,
                ..Default::default()
            })
            .collect(),
        ..Default::default()
    };
    let a = padded_save_record(&snapshot, 1);
    let b = padded_save_record(&snapshot, 2);
    sample!("near-cap-two-max-slots-unexplained-ids-protected", || {
        let mut m = Model::new();
        let input = near.clone();
        m.import(&input).unwrap();
        drop(input);
        let a = a.clone();
        let b = b.clone();
        assert!(!m.load(&a, &b));
        assert!(m.export().is_err());
    });
    sample!("max-progress-round-save-real-abi", || unsafe {
        let app = balabala_app_create();
        (*app).model.state = snapshot.clone();
        let mut out = vec![0; model::MAX_SAVE];
        let (mut slot, mut generation) = (0, 0);
        assert!(
            balabala_app_save_export(app, out.as_mut_ptr(), out.len(), &mut slot, &mut generation)
                > 24
        );
        balabala_app_destroy(app);
    });
    let mut oversized: serde_json::Value =
        serde_json::from_slice(include_bytes!("../content/hanzi.json")).unwrap();
    oversized["cards"][0]["front"] = "a".repeat(55000).into();
    let mut large: serde_json::Value = serde_json::from_slice(&near).unwrap();
    let mut cards = Vec::new();
    for i in 0..40 {
        let mut card = large["cards"][0].clone();
        card["id"] = format!("h{i:031}").into();
        cards.push(card);
    }
    large["cards"] = cards.into();
    let mut large = serde_json::to_vec(&large).unwrap();
    large.resize(model::MAX_PACKAGE, b' ');
    sample!("max-package-incremental-budget-rejection", || {
        let mut m = Model::new();
        let input = large.clone();
        assert_eq!(m.import(&input), Err("全部卡组超出常驻内存上限"));
    });
    let mut invalid = snapshot.clone();
    invalid.count = 7;
    let invalid = padded_save_record(&invalid, 3);
    sample!(
        "new-semantic-bad-slot-unexplained-fallback-protected",
        || {
            let mut m = Model::new();
            let a = a.clone();
            let invalid = invalid.clone();
            assert!(!m.load(&a, &invalid));
            assert_eq!(m.generation, 0);
            assert!(m.export().is_err());
        }
    );
    for (name, input) in [
        (
            "rejected-large-known-string",
            serde_json::to_vec(&oversized).unwrap(),
        ),
        (
            "rejected-escaped-unknown-field",
            format!("{{\"{}\":0}}", "\\u0061".repeat(10000)).into_bytes(),
        ),
    ] {
        sample!(name, || {
            let mut m = Model::new();
            let input = input.clone();
            assert!(m.import(&input).is_err());
        });
    }
    let out =
        std::path::Path::new(env!("BALABALA_GENERATED")).join("../memory-round2-scenarios.json");
    std::fs::write(out, serde_json::to_vec_pretty(&report).unwrap()).unwrap();
}
