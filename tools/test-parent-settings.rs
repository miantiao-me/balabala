fn open_parent_settings(m: &mut Model) {
    tap(m, Key::Home);
    if !m.ready() { displayed(m); }
    m.key(Key::Front3, true);
    tap(m, Key::Parent);
    m.key(Key::Front3, false);
    assert_eq!((m.page, m.selected), (Page::Settings, 0));
}

#[test]
fn clear_interval_cycles_saves_and_requires_ack_and_release() {
    let mut m = Model::new();
    open_parent_settings(&mut m);
    displayed(&mut m);
    tap(&mut m, Key::Front4);
    for _ in 0..3 { tap(&mut m, Key::Down); }
    assert_eq!((m.page, m.selected), (Page::LearningSettings, 3));
    for expected in [16, 32, 64, 128, 8] {
        let old = m.state.clear_interval;
        tap(&mut m, Key::Front4);
        assert_eq!(m.state.clear_interval, old);
        displayed(&mut m);
        m.key(Key::Front4, true);
        assert_eq!(m.state.clear_interval, expected);
        assert!(m.dirty);
        assert!(m.view().options[3].contains(&format!("{expected} 次")));
        displayed(&mut m);
        m.key(Key::Front4, true);
        assert_eq!(m.state.clear_interval, expected);
        m.key(Key::Front4, false);
        let (_, generation, record) = m.export().unwrap();
        assert!(record.len() <= model::MAX_SAVE);
        let mut restored = Model::new();
        assert!(restored.load(&record, &[]));
        assert_eq!(restored.state.clear_interval, expected);
        m.save_ack(generation, true);
        // 下一次选择仍须等待自己的显示回执。
        tap(&mut m, Key::Up);
        tap(&mut m, Key::Down);
    }
    displayed(&mut m);
    tap(&mut m, Key::Down);
    assert_eq!(m.selected, 4);
    displayed(&mut m);
    tap(&mut m, Key::Front4);
    assert_eq!(m.page, Page::Settings);
}

fn rewrite_clear_interval(record: &[u8], value: Option<serde_json::Value>) -> Vec<u8> {
    let version2 = record[4] == 2;
    let start = if version2 { 28 } else { 24 };
    let end = if version2 { start + u32::from_le_bytes(record[24..28].try_into().unwrap()) as usize } else { record.len() };
    let mut state: serde_json::Value = serde_json::from_slice(&record[start..end]).unwrap();
    state.as_object_mut().unwrap().remove("clear_interval");
    if let Some(value) = value { state["clear_interval"] = value; }
    let json = serde_json::to_vec(&state).unwrap();
    let mut changed = record[..24].to_vec();
    if version2 { changed.extend_from_slice(&(json.len() as u32).to_le_bytes()); }
    changed.extend_from_slice(&json);
    if version2 { changed.extend_from_slice(&record[end..]); }
    let length = changed.len() - 24;
    changed[16..20].copy_from_slice(&(length as u32).to_le_bytes());
    let crc = model::crc32(&[&changed[..20], &changed[24..]]);
    changed[20..24].copy_from_slice(&crc.to_le_bytes());
    changed
}

#[test]
fn clear_interval_defaults_for_legacy_records_and_rejects_invalid_values() {
    let mut source = Model::new();
    let (_, _, v2) = source.export().unwrap();
    let v1 = padded_save_record(&source.state, 1);
    for record in [&v1, &v2] {
        let old = rewrite_clear_interval(record, None);
        let mut restored = Model::new();
        assert!(restored.load(&old, &[]));
        assert_eq!(restored.state.clear_interval, 8);
        for value in [serde_json::Value::Null, 0.into(), 7.into(), 9.into(), 129.into(), 256.into(), (-1).into(), "16".into()] {
            let invalid = rewrite_clear_interval(record, Some(value));
            let mut restored = Model::new();
            assert!(!restored.load(&invalid, &[]));
        }
    }
}

#[test]
fn parent_sources_scroll_without_moving_network_or_download_entries() {
    let mut m = Model::new();
    m.dirty = false;
    open_parent_settings(&mut m);
    for _ in 0..3 { tap(&mut m, Key::Down); }
    assert_eq!(m.view().options[3], "蓝牙配网");
    tap(&mut m, Key::Down);
    assert_eq!(m.view().lines[4], "刷机模式");
    tap(&mut m, Key::Down);
    assert_eq!((m.selected, m.view().focus), (5, 4));
    assert_eq!(m.view().options[0], "学习情况");
    assert_eq!(m.view().lines[4], "课程来源");
    displayed(&mut m);
    tap(&mut m, Key::Front4);
    assert_eq!(m.page, Page::Sources);
    for expected in ["Unicode Unihan 18.0.0", "NGSL 1.2 (2023)", "数学：运算生成"] {
        assert!(m.view().options.iter().any(|s| s == expected));
        assert_eq!(m.view().focus, 255);
        tap(&mut m, Key::Down);
    }
    assert_eq!(m.selected, 0);
    tap(&mut m, Key::Up);
    assert_eq!(m.selected, 2);
    displayed(&mut m);
    tap(&mut m, Key::Front3);
    assert_eq!(m.page, Page::Settings);
    assert!(!m.dirty);
    open_parent_settings(&mut m);
    tap(&mut m, Key::Up);
    displayed(&mut m);
    tap(&mut m, Key::Front4);
    displayed(&mut m);
    tap(&mut m, Key::Front4);
    assert_eq!(m.page, Page::Settings);
}
