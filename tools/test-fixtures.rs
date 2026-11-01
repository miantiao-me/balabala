fn low_glyph_deck() -> serde_json::Value {
    let mut value: serde_json::Value =
        serde_json::from_slice(include_bytes!("../content/hanzi.json")).unwrap();
    let card = value["cards"][0].clone();
    value["cards"] = (0..40)
        .map(|i| {
            let mut c = card.clone();
            c["id"] = format!("h{i:031}").into();
            c["front"] = "山".repeat(8).into();
            c["question"] = "山".repeat(17).into();
            c["back"] = "山".repeat(51).into();
            c["explanation"] = "山".repeat(51).into();
            c["options"] = (0..4)
                .map(|n| format!("{}{n}", "山".repeat(14)))
                .collect::<Vec<_>>()
                .into();
            c
        })
        .collect::<Vec<_>>()
        .into();
    value
}
fn largest_content_fixture(mut value: serde_json::Value) -> (Vec<u8>, usize) {
    let cards = value["cards"].as_array().unwrap().clone();
    let (mut input, mut charge) = (Vec::new(), 0);
    for count in 1..=cards.len() {
        value["cards"] = cards[..count].to_vec().into();
        let bytes = serde_json::to_vec(&value).unwrap();
        let mut model = Model::new();
        model.decks[0] = serde_json::from_slice(&bytes).unwrap();
        if model.content_bytes() <= model::MAX_ACTIVE_CONTENT_BYTES {
            charge = model.content_bytes();
            input = bytes;
        }
    }
    assert!(!input.is_empty());
    (input, charge)
}
fn font_counterexample() -> serde_json::Value {
    let mut v: serde_json::Value =
        serde_json::from_slice(include_bytes!("../content/hanzi.json")).unwrap();
    v["version"] = 2.into();
    v["title"] = "|',.:;IijlÌÍìíĪī".into();
    v["cards"][0]["question"] = r#"Ǐǐ!()-[]f{}\rt"*?szcx#$+02346789"#.into();
    v["cards"][0]["options"] = serde_json::json!([
        "<=>FJLY^_ekvy~èé",
        "êēěESTVXZagÈÉàáā",
        "ĒĚǎPR`bdhnopquÀÁ",
        "òóùúüĀōūǍǒǔǖǘǚǜ&"
    ]);
    v
}
fn memory_round1_fixture() -> Vec<u8> {
    let mut value = low_glyph_deck();
    value["version"] = 2.into();
    largest_content_fixture(value).0
}
fn padded_save_record(snapshot: &model::Snapshot, generation: u64) -> Vec<u8> {
    let payload = serde_json::to_vec(snapshot).unwrap();
    let mut bytes = vec![b' '; model::MAX_SAVE];
    bytes[..8].copy_from_slice(b"BALA\x01\0\0\0");
    bytes[8..16].copy_from_slice(&generation.to_le_bytes());
    bytes[16..20].copy_from_slice(&((model::MAX_SAVE - 24) as u32).to_le_bytes());
    bytes[24..24 + payload.len()].copy_from_slice(&payload);
    let crc = model::crc32(&[&bytes[..20], &bytes[24..]]);
    bytes[20..24].copy_from_slice(&crc.to_le_bytes());
    bytes
}
fn near_dual_limit_fixture() -> (Vec<u8>, usize, usize) {
    let near = memory_round1_fixture();
    let full = font_counterexample();
    let mut dense = serde_json::from_slice::<serde_json::Value>(&near).unwrap();
    let mut dense_input = Vec::new();
    let mut dense_font = 0;
    let mut dense_charge = 0;
    for n in 1..=16 {
        dense["title"] = full["title"]
            .as_str()
            .unwrap()
            .chars()
            .take(n)
            .collect::<String>()
            .into();
        dense["cards"][0]["question"] = full["cards"][0]["question"]
            .as_str()
            .unwrap()
            .chars()
            .take(n)
            .collect::<String>()
            .into();
        for i in 0..4 {
            dense["cards"][0]["options"][i] = full["cards"][0]["options"][i]
                .as_str()
                .unwrap()
                .chars()
                .take(n)
                .collect::<String>()
                .into();
        }
        let bytes = serde_json::to_vec(&dense).unwrap();
        let mut m = Model::new();
        m.decks[0] = serde_json::from_slice(&bytes).unwrap();
        let charge = m.content_bytes();
        m.start(0, Mode::Quiz, false);
        let font = fonts::construction_budget(&m.view()).unwrap();
        if font <= fonts::MAX_PAGE_FONT_BYTES - 3000 && charge <= model::MAX_ACTIVE_CONTENT_BYTES {
            dense_input = bytes;
            dense_font = font;
            dense_charge = charge;
        }
    }
    let builtin_max = Model::new()
        .decks
        .iter()
        .map(|deck| model::page_font_budget(deck).unwrap())
        .max()
        .unwrap();
    assert!(
        dense_font > builtin_max,
        "dense packed budget={dense_font}; builtin maximum={builtin_max}"
    );
    assert!(dense_charge > 32000);
    (dense_input, dense_font, dense_charge)
}
fn fully_escaped_json(value: &serde_json::Value) -> Vec<u8> {
    fn strings(v: &serde_json::Value, out: &mut Vec<String>) {
        match v {
            serde_json::Value::String(s) => out.push(s.clone()),
            serde_json::Value::Array(a) => {
                for v in a {
                    strings(v, out)
                }
            }
            serde_json::Value::Object(m) => {
                for (k, v) in m {
                    out.push(k.clone());
                    strings(v, out)
                }
            }
            _ => {}
        }
    }
    let mut texts = Vec::new();
    strings(value, &mut texts);
    texts.sort();
    texts.dedup();
    let mut json = serde_json::to_string(value).unwrap();
    for text in texts {
        let escaped = format!(
            "\"{}\"",
            text.encode_utf16()
                .map(|u| format!("\\u{u:04x}"))
                .collect::<String>()
        );
        json = json.replace(&serde_json::to_string(&text).unwrap(), &escaped);
    }
    json.into_bytes()
}
