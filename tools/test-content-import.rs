#[test]
fn builtin_content_is_valid_and_all_128_cards_have_unique_options_and_glyphs() {
    for deck in &Model::new().decks {
        println!(
            "{}: complete page font budget={}",
            deck.subject,
            model::page_font_budget(deck).unwrap()
        );
    }
    for subject in model::SUBJECTS {
        let path = std::path::Path::new(env!("BALABALA_GENERATED"))
            .join(format!("../../content/{subject}.json"));
        let bytes = std::fs::read(path).unwrap();
        let deck = model::parse_deck(&bytes).unwrap();
        assert_eq!(deck.cards.len(), 32);
    }
    for c in "āáǎàēéěèīíǐìōóǒòūúǔùǖǘǚǜü\u{0304}\u{0301}\u{030c}\u{0300}\u{0308}".chars()
    {
        assert!(
            fonts::advance(c, fonts::REGULAR_24).is_some(),
            "missing {c}"
        );
    }
}
#[test]
fn platform_import_reports_input_old_new_and_validation_overlap() {
    let mut report = String::from("64-bit host requested allocation peaks; includes a live model, one file input, candidate deck and validation temporaries. Excludes C file streams, allocator metadata/alignment and C3 tasks/stack. Realloc counts old+new. Parser scratch and Vec reallocation remain outside the active-content charge; rejected-path samples are not total heap bounds.\n");
    for subject in model::SUBJECTS {
        let path = std::path::Path::new(env!("BALABALA_GENERATED"))
            .join(format!("../../content/{subject}.json"));
        let ((input_bytes, old_charge, new_charge), peak) = heap_meter::measure(|| {
            let mut m = Model::new();
            let old_charge = m.content_bytes();
            let bytes = std::fs::read(&path).unwrap();
            let input_bytes = bytes.len();
            m.import(&bytes).unwrap();
            let new_charge = m.content_bytes();
            assert!(new_charge <= model::MAX_ACTIVE_CONTENT_BYTES);
            (input_bytes, old_charge, new_charge)
        });
        report.push_str(&format!("{subject}: input={input_bytes}; C3 active-content charge old={old_charge} after={new_charge}; host heap request peak={peak}\n"));
    }
    let path =
        std::path::Path::new(env!("BALABALA_GENERATED")).join("../platform-import-memory.txt");
    std::fs::write(path, &report).unwrap();
    println!("{report}");
}
#[test]
fn import_is_bounded_atomic_and_stable_ids_keep_progress() {
    let bytes = include_bytes!("../content/hanzi.json");
    let mut m = quiz();
    tap(&mut m, Key::Front2);
    let mut value: serde_json::Value = serde_json::from_slice(bytes).unwrap();
    value["version"] = 2.into();
    value["cards"][0]["explanation"] = "山有高高的山峰。".into();
    let updated = serde_json::to_vec(&value).unwrap();
    m.import(&updated).unwrap();
    assert_eq!(m.decks[0].version, 2);
    assert_eq!(m.state.progress[0].status, 1);
    assert_eq!(m.state.round.as_ref().unwrap().answers[0], 0);
    for bad in [vec![], b"{".to_vec(), vec![b' '; model::MAX_PACKAGE + 1]] {
        assert!(m.import(&bad).is_err());
        assert_eq!(m.decks[0].version, 2);
    }
    let good = value.clone();
    for case in 0..7 {
        let mut v = good.clone();
        match case {
            0 => v["cards"][0]["options"][1] = v["cards"][0]["options"][0].clone(),
            1 => v["cards"][0]["correct"] = 4.into(),
            2 => v["cards"][0]["front"] = "龘".into(),
            3 => v["schema"] = 2.into(),
            4 => v["cards"][1]["id"] = v["cards"][0]["id"].clone(),
            5 => v["cards"][0]["question"] = "山".repeat(30).into(),
            _ => {
                let c = v["cards"][0].clone();
                v["cards"] = vec![c; 41].into();
            }
        }
        assert!(
            m.import(&serde_json::to_vec(&v).unwrap()).is_err(),
            "invalid case {case}"
        );
        assert_eq!(m.decks[0].version, 2);
    }
    let mut removed = good.clone();
    removed["cards"].as_array_mut().unwrap().remove(0);
    m.import(&serde_json::to_vec(&removed).unwrap()).unwrap();
    assert!(m.state.round.is_none());
    assert!(m.state.progress.is_empty());
}
#[test]
fn import_rejects_invisible_fields_and_visually_duplicate_options_atomically() {
    let mut base: serde_json::Value =
        serde_json::from_slice(include_bytes!("../content/hanzi.json")).unwrap();
    base["cards"].as_array_mut().unwrap().truncate(1);
    let mut m = quiz();
    let state = serde_json::to_vec(&m.state).unwrap();
    let revision = m.revision;
    let card_count = m.decks[0].cards.len();
    let mut invalid = Vec::new();
    for field in [
        "title",
        "front",
        "back",
        "question",
        "explanation",
        "option",
    ] {
        let mut value = base.clone();
        match field {
            "title" => value["title"] = "   ".into(),
            "option" => value["cards"][0]["options"][0] = "   ".into(),
            _ => value["cards"][0][field] = "   ".into(),
        }
        invalid.push(value);
    }
    for options in [
        ["a", " a", "a ", "b"],
        ["a a", "a  a", "b", "c"],
        ["a?", "a ?", "b", "c"],
    ] {
        let mut value = base.clone();
        value["cards"][0]["options"] = serde_json::json!(options);
        invalid.push(value);
    }
    for value in invalid {
        assert!(m.import(&serde_json::to_vec(&value).unwrap()).is_err());
        assert_eq!(serde_json::to_vec(&m.state).unwrap(), state);
        assert_eq!(m.revision, revision);
        assert_eq!(m.decks[0].cards.len(), card_count);
    }
    base["cards"][0]["options"] = serde_json::json!([" a ", "b", "c", "d"]);
    m.import(&serde_json::to_vec(&base).unwrap()).unwrap();
    m.start(0, Mode::Quiz, false);
    let position = m.state.round.as_ref().unwrap().option_order(0).iter().position(|p|*p == 0).unwrap();
    assert_eq!(m.view().options[position], "a");
}
#[test]
fn imported_explanation_uses_three_lines_even_when_whole_words_need_four() {
    let text = vec!["W".repeat(13); 4].join(" ");
    assert_eq!(model::wrap(&text, 424).len(), 3);
    let mut value: serde_json::Value =
        serde_json::from_slice(include_bytes!("../content/hanzi.json")).unwrap();
    value["cards"][0]["explanation"] = text.clone().into();
    let mut m = Model::new();
    m.import(&serde_json::to_vec(&value).unwrap()).unwrap();
    m.start(0, Mode::Quiz, false);
    displayed(&mut m);
    tap(&mut m, Key::Front1);
    let view = m.view();
    assert!(view.success);
    assert_eq!(view.lines[1], fit::explanation(&text).unwrap());
    assert_eq!(view.lines[1].lines().count(), 3);
    assert!(view.lines[1].lines().all(
        |line| fonts::width(line, fonts::REGULAR_24) <= layout::QUIZ_FEEDBACK_BODY[2] as usize
    ));
    pbm("feedback-split-word", &view);
}
#[test]
fn packed_page_font_budget_accepts_previous_alpha_counterexample_atomically() {
    let value = font_counterexample();
    let bytes = serde_json::to_vec(&value).unwrap();
    let deck = model::parse_deck(&bytes).unwrap();
    let (budget, validation_peak) = heap_meter::measure(|| model::page_font_budget(&deck).unwrap());
    assert!(budget < fonts::MAX_PAGE_FONT_BYTES);
    assert!(
        validation_peak < 16 * 1024,
        "validator allocations: {validation_peak}"
    );
    let mut m = quiz();
    tap(&mut m, Key::Front2);
    let snapshot = serde_json::to_vec(&m.state).unwrap();
    m.import(&bytes).unwrap();
    assert_eq!(m.decks[0].version, 2);
    assert_eq!(serde_json::to_vec(&m.state).unwrap(), snapshot);
    m.start(0, Mode::Quiz, false);
    let view = m.view();
    assert!(fonts::construction_budget(&view).unwrap() <= budget);
    pbm("packed-previous-alpha-counterexample", &view);
    let mut invalid = value;
    invalid["cards"][0]["options"][0] = "山".repeat(17).into();
    let before = serde_json::to_vec(&m.state).unwrap();
    let revision = m.revision;
    assert!(m.import(&serde_json::to_vec(&invalid).unwrap()).is_err());
    assert_eq!(serde_json::to_vec(&m.state).unwrap(), before);
    assert_eq!(m.revision, revision);
    assert_eq!(m.decks[0].version, 2);
    println!(
        "packed counterexample complete page budget={budget}; validator heap={validation_peak}"
    );
}
#[test]
fn escaped_strings_keep_legal_limits_and_rejections_keep_old_content() {
    let mut value: serde_json::Value =
        serde_json::from_slice(include_bytes!("../content/hanzi.json")).unwrap();
    value["cards"].as_array_mut().unwrap().truncate(1);
    value["id"] = "d".repeat(32).into();
    value["cards"][0]["id"] = "c".repeat(32).into();
    let mut m = Model::new();
    for text in [
        "i".repeat(64),
        format!("{}{}", "山".repeat(44), "i".repeat(20)),
    ] {
        value["cards"][0]["back"] = text.clone().into();
        value["cards"][0]["explanation"] = "i".repeat(64).into();
        let escaped = fully_escaped_json(&value);
        assert!(std::str::from_utf8(&escaped)
            .unwrap()
            .contains(&"\\u0063".repeat(32)));
        m.import(&escaped).unwrap();
    }
    assert_eq!(m.decks[0].cards[0].back.chars().count(), 64);
    let mut field_max = value.clone();
    field_max["cards"][0]["back"] = "山".repeat(64).into();
    let decoded: model::Deck = serde_json::from_slice(&fully_escaped_json(&field_max)).unwrap();
    assert_eq!(decoded.cards[0].back.len(), 192); // 字段最大UTF8长度；整页三行限制仍另外校验。
    assert!(m.import(&fully_escaped_json(&field_max)).is_err());
    assert_eq!(m.decks[0].cards[0].id, "c".repeat(32));
    let before = serde_json::to_vec(&m.state).unwrap();
    let charge = m.content_bytes();
    let revision = m.revision;
    for n in [65, 193] {
        value["cards"][0]["back"] = "i".repeat(n).into();
        assert!(m.import(&fully_escaped_json(&value)).is_err());
    }
    assert_eq!(m.content_bytes(), charge);
    assert_eq!(m.revision, revision);
    assert_eq!(serde_json::to_vec(&m.state).unwrap(), before);
}
#[test]
fn collections_reject_extra_cards_progress_round_ids_and_answers() {
    let mut value: serde_json::Value =
        serde_json::from_slice(include_bytes!("../content/hanzi.json")).unwrap();
    let mut cards = Vec::new();
    for i in 0..41 {
        let mut c = value["cards"][0].clone();
        c["id"] = format!("c-{i}").into();
        cards.push(c);
    }
    value["cards"] = cards.into();
    assert!(model::parse_deck(&serde_json::to_vec(&value).unwrap()).is_err());
    let mut m = Model::new();
    let before = serde_json::to_vec(&m.state).unwrap();
    for which in 0..3 {
        let mut state = m.state.clone();
        if which == 0 {
            state.progress = (0..161)
                .map(|i| model::Progress {
                    subject: 0,
                    id: format!("p-{i}"),
                    status: 1,
                    ..Default::default()
                })
                .collect();
        } else {
            state.round = Some(model::Round {
                subject: 0,
                ids: (0..if which == 1 { 11 } else { 10 })
                    .map(|i| format!("c-{i}"))
                    .collect(),
                answers: vec![-1; if which == 2 { 11 } else { 10 }],
                cursor: 0,
                mode: Mode::Read,
                flipped: false,
                permutations: vec![],
            });
        }
        let bad = padded_save_record(&state, 9);
        assert!(!m.load(&bad, &[]));
        assert_eq!(serde_json::to_vec(&m.state).unwrap(), before);
    }
}
// 第 2 阶段探针：新首页与四列选一选模板的节点数、宿主 heap 峰值和光栅耗时，对照旧模板同一内容。
