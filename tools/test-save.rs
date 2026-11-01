#[test]
fn crc32_preserves_ieee_vectors_chunking_and_existing_records() {
    fn bitwise(bytes: &[u8]) -> u32 {
        let mut crc = !0u32;
        for &byte in bytes {
            crc ^= u32::from(byte);
            for _ in 0..8 {
                crc = (crc >> 1) ^ (0xedb88320u32.wrapping_mul(crc & 1));
            }
        }
        !crc
    }
    assert_eq!(model::crc32(&[]), 0);
    assert_eq!(model::crc32(&[b"123456789"]), 0xcbf43926);
    let bytes: Vec<u8> = (0..model::MAX_SAVE).map(|i| (i * 73 + 19) as u8).collect();
    for len in [0, 1, 2, 7, 8, 31, 32, 255, 4095, model::MAX_SAVE] {
        let expected = bitwise(&bytes[..len]);
        for split in [0, len / 2, len] {
            assert_eq!(model::crc32(&[&bytes[..split], &[], &bytes[split..len]]), expected, "len={len}, split={split}");
        }
    }
    let mut source = quiz();
    let (_, _, mut record) = source.export().unwrap();
    let mut legacy_payload = record[..20].to_vec();
    legacy_payload.extend_from_slice(&record[24..]);
    record[20..24].copy_from_slice(&bitwise(&legacy_payload).to_le_bytes());
    let mut restored = Model::new();
    assert!(restored.load(&record, &[]));
    assert_eq!(restored.state.round.unwrap().ids, source.state.round.unwrap().ids);
}

#[test]
fn two_save_slots_survive_torn_writes_corruption_and_reboot_without_rescoring() {
    let mut m = quiz();
    tap(&mut m, Key::Front1);
    tap(&mut m, Key::Down);
    let (s1, g1, a) = m.export().unwrap();
    assert!(m.save_ack(g1, true));
    assert!(!m.dirty);
    displayed(&mut m);
    tap(&mut m, Key::Front2);
    let (s2, g2, b) = m.export().unwrap();
    assert_ne!(s1, s2);
    assert_eq!(g2, g1 + 1);
    assert!(m.save_ack(g2, false));
    assert!(m.dirty);
    for cut in 0..b.len() {
        let mut reboot = Model::new();
        assert!(reboot.load(&a, &b[..cut]));
        assert_eq!(reboot.generation, g1);
        assert_eq!(reboot.state.round.as_ref().unwrap().cursor, 1);
        assert_eq!(reboot.state.progress.len(), 1);
    }
    let mut reboot = Model::new();
    assert!(reboot.load(&a, &b));
    assert_eq!(reboot.generation, g2);
    assert_eq!(reboot.state.progress.len(), 2);
    tap(&mut reboot, Key::Home);
    displayed(&mut reboot);
    tap(&mut reboot, Key::Front4);
    displayed(&mut reboot);
    tap(&mut reboot, Key::Front1);
    assert_eq!(reboot.state.progress.len(), 2);
    assert_eq!(reboot.state.round.as_ref().unwrap().answers[1], 1);
    let mut corrupt = b.clone();
    corrupt[30] ^= 1;
    assert!(reboot.load(&a, &corrupt));
    assert_eq!(reboot.generation, g1);
    assert!(reboot.load(&[], &a));
    assert!(!reboot.load(&[], &corrupt));
}
#[test]
fn save_ack_does_not_clear_newer_mutations_or_accept_stale_generation() {
    let mut m = quiz();
    let (_, g, _) = m.export().unwrap();
    tap(&mut m, Key::Front1);
    assert!(m.export().is_err());
    assert!(!m.save_ack(g + 1, true));
    assert!(m.save_ack(g, true));
    assert!(m.dirty);
    let (_, next, _) = m.export().unwrap();
    assert_eq!(next, g + 1);
    assert!(m.save_ack(next, true));
    assert!(!m.dirty);
}
#[test]
fn storage_protection_failed_load_blocks_mutations_reset_and_late_ack_until_restore() {
    let mut m = quiz();
    tap(&mut m, Key::Front1);
    let (_, generation, record) = m.export().unwrap();
    let before = serde_json::to_vec(&m.state).unwrap();
    assert!(!m.load(b"unreadable", &[]));
    assert_eq!(serde_json::to_vec(&m.state).unwrap(), before);
    assert!(m.export().is_err(), "failed restore must block export");
    assert!(
        !m.save_ack(generation, true),
        "late ack cannot clear protected dirty state"
    );
    assert!(m.dirty);
    tap(&mut m, Key::Home);
    m.start(0, Mode::Read, false);
    tap(&mut m, Key::Front4);
    assert!(m.export().is_err());
    begin_reset(&mut m);
    pass_reset_check(&mut m);
    displayed(&mut m);
    tap(&mut m, Key::Down);
    displayed(&mut m);
    tap(&mut m, Key::Front4);
    assert!(m.state.progress.is_empty());
    assert!(
        m.export().is_err(),
        "reset must not bypass unknown old record"
    );
    assert!(m.load(&record, &[]));
    assert_eq!(serde_json::to_vec(&m.state).unwrap(), before);
    let (_, next, _) = m.export().unwrap();
    assert_eq!(next, generation + 1);
    assert!(!m.save_ack(generation, true));
    assert!(m.save_ack(next, true));
    assert!(!m.dirty);
}
#[test]
fn storage_protection_unknown_ids_require_successful_subject_import_without_older_fallback() {
    let mut source = Model::new();
    let (_, old_generation, older) = source.export().unwrap();
    assert!(source.save_ack(old_generation, true));
    let mut deck: serde_json::Value =
        serde_json::from_slice(include_bytes!("../content/hanzi.json")).unwrap();
    deck["cards"][0]["id"] = "custom-01".into();
    let bytes = serde_json::to_vec(&deck).unwrap();
    source.import(&bytes).unwrap();
    source.start(0, Mode::Quiz, false);
    displayed(&mut source);
    tap(&mut source, Key::Front1);
    let (_, _, newer) = source.export().unwrap();
    let saved = serde_json::to_vec(&source.state).unwrap();
    let mut fallback = Model::new();
    assert!(fallback.import(b"{broken").is_err());
    fallback
        .import(include_bytes!("../content/pinyin.json"))
        .unwrap();
    let before = serde_json::to_vec(&fallback.state).unwrap();
    assert!(
        !fallback.load(&older, &newer),
        "newest valid custom record cannot be explained by fallback builtins"
    );
    assert_eq!(serde_json::to_vec(&fallback.state).unwrap(), before);
    assert!(fallback.export().is_err());
    fallback.import(&bytes).unwrap();
    assert!(fallback.import(b"{broken").is_err());
    assert!(fallback.load(&older, &newer));
    assert_eq!(serde_json::to_vec(&fallback.state).unwrap(), saved);
    // An explicitly validated replacement may intentionally remove saved IDs.
    let mut replacement = Model::new();
    deck["version"] = 2.into();
    deck["cards"].as_array_mut().unwrap().remove(0);
    replacement
        .import(&serde_json::to_vec(&deck).unwrap())
        .unwrap();
    assert!(replacement.load(&older, &newer));
    assert!(replacement.state.progress.is_empty());
    assert!(replacement.state.round.is_none());
    assert!(replacement.export().is_ok());
    // Round IDs alone also require the matching subject, even without progress.
    source.state.progress.clear();
    source.pending_save = None;
    let (_, _, round_only) = source.export().unwrap();
    assert!(!Model::new().load(&round_only, &[]));
    source.state.round = None;
    source.state.progress.push(model::Progress {
        subject: 0,
        id: "custom-01".into(),
        status: 2,
        correct_count: 1,
        mastered: false,
    });
    source.pending_save = None;
    let (_, _, progress_only) = source.export().unwrap();
    assert!(!Model::new().load(&progress_only, &[]));
}
#[test]
fn storage_protection_abi_is_silent_clears_pending_and_rejects_null() {
    unsafe {
        assert_eq!(balabala_app_save_protect(core::ptr::null_mut()), -1);
        let app = balabala_app_create();
        let mut out = vec![0; model::MAX_SAVE];
        let (mut slot, mut generation) = (0, 0);
        let len =
            balabala_app_save_export(app, out.as_mut_ptr(), out.len(), &mut slot, &mut generation);
        assert!(len > 24);
        let revision = (*app).model.revision;
        let mutation = (*app).model.mutation;
        let status = balabala_app_status(app);
        assert_eq!(balabala_app_save_protect(app), 0);
        assert_eq!((*app).model.revision, revision);
        assert_eq!((*app).model.mutation, mutation);
        assert_eq!((*app).model.page, Page::Home);
        assert_eq!(balabala_app_status(app), status);
        assert_eq!(balabala_app_save_ack(app, generation, 1), -1);
        let mut protected = vec![0x55; model::MAX_SAVE];
        assert_eq!(
            balabala_app_save_export(
                app,
                protected.as_mut_ptr(),
                protected.len(),
                &mut slot,
                &mut generation
            ),
            -3
        );
        assert!(protected.iter().all(|b| *b == 0x55));
        assert_eq!(
            balabala_app_load(app, out.as_ptr(), len as usize, core::ptr::null(), 0),
            0
        );
        assert!(
            balabala_app_save_export(
                app,
                protected.as_mut_ptr(),
                protected.len(),
                &mut slot,
                &mut generation
            ) > 24
        );
        balabala_app_destroy(app);
    }
}
#[test]
fn startup_bad_then_good_import_restores_state_without_hiding_error() {
    let mut saved = quiz();
    tap(&mut saved, Key::Front1);
    tap(&mut saved, Key::Down);
    let archive = saved.export().unwrap().2;
    let mut m = Model::new();
    let old = m.begin_render().unwrap();
    m.show_error("卡组格式损坏或数量超限");
    let error = m.error.clone();
    let revision = m.revision;
    m.import(include_bytes!("../content/pinyin.json")).unwrap();
    assert_eq!(m.page, Page::Error);
    assert_eq!(m.error, error);
    assert!(m.revision > revision);
    assert!(m.load(&archive, &[]));
    assert_eq!(m.page, Page::Error);
    assert_eq!(m.error, error);
    assert_eq!(m.state.round.as_ref().unwrap().cursor, 1);
    assert_eq!(m.state.round.as_ref().unwrap().answers[0], 1);
    assert!(m.complete(old));
    assert!(!m.ready());
    tap(&mut m, Key::Front4);
    assert_eq!(m.page, Page::Error);
    displayed(&mut m);
    tap(&mut m, Key::Front4);
    assert_eq!(m.page, Page::Home);
    displayed(&mut m);
    tap(&mut m, Key::Front4);
    assert_eq!(m.page, Page::Learn);
    assert_eq!(m.state.round.as_ref().unwrap().cursor, 1);
}
#[test]
fn equal_generation_prefers_slot_zero_and_bad_new_semantics_falls_back() {
    let mut first = Model::new().state;
    first.count = 5;
    let mut second = first.clone();
    second.count = 10;
    let a = padded_save_record(&first, 5);
    let b = padded_save_record(&second, 5);
    let mut m = Model::new();
    assert!(m.load(&a, &b));
    assert_eq!((m.slot, m.state.count), (0, 5));
    second.count = 7;
    let bad = padded_save_record(&second, 6);
    assert!(m.load(&a, &bad));
    assert_eq!((m.slot, m.generation, m.state.count), (0, 5, 5));
    assert!(m.load(&bad, &a));
    assert_eq!((m.slot, m.generation, m.state.count), (1, 5, 5));
    let before = serde_json::to_vec(&m.state).unwrap();
    let revision = m.revision;
    assert!(!m.load(&bad, &bad));
    assert_eq!(serde_json::to_vec(&m.state).unwrap(), before);
    assert_eq!(m.revision, revision);
}
#[test]
fn export_into_keeps_format_and_installs_pending_only_after_success() {
    let mut m = Model::new();
    let mut tiny = [0x55; 24];
    assert!(m.export_into(&mut tiny).is_err());
    assert_eq!(tiny, [0x55; 24]);
    assert!(m.pending_save.is_none());
    let mut out = vec![0; model::MAX_SAVE];
    let (slot, generation, len) = m.export_into(&mut out).unwrap();
    assert_eq!(&out[..8], b"BALA\x02\0\0\0");
    let mut restored = Model::new();
    assert!(restored.load(&out[..len], &[]));
    assert_eq!(
        serde_json::to_vec(&restored.state).unwrap(),
        serde_json::to_vec(&m.state).unwrap()
    );
    assert_eq!((slot, generation), (0, 1));
    assert!(m.save_ack(generation, false));
    assert!(m.dirty);
}
