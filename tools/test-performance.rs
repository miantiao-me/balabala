use std::{hint::black_box, time::Instant};

fn performance_sample(mut operation: impl FnMut(), iterations: usize) -> u128 {
    operation();
    let mut samples = [0; 5];
    for sample in &mut samples {
        let start = Instant::now();
        for _ in 0..iterations {
            operation();
        }
        *sample = start.elapsed().as_nanos() / iterations as u128;
    }
    samples.sort_unstable();
    samples[2]
}

#[test]
#[ignore = "显式运行宿主性能基准；耗时不作为 CI 门槛"]
fn performance_render_import_and_save() {
    let mut model = Model::new();
    let mut views = vec![("home", model.view())];
    model.start(0, Mode::Read, false);
    views.push(("read-front", model.view()));
    displayed(&mut model);
    tap(&mut model, Key::Front4);
    views.push(("read-back", model.view()));
    model.start(2, Mode::Quiz, false);
    views.push(("quiz", model.view()));
    displayed(&mut model);
    let wrong = (model.card().unwrap().correct + 1) % 4;
    tap(&mut model, Key::from_u32(u32::from(wrong)).unwrap());
    views.push(("feedback", model.view()));
    model.page = Page::Stats;
    views.push(("stats", model.view()));
    let mut frame = vec![0xff; FRAMEBUFFER_BYTES];
    let mut stripe = vec![0; STRIPE_PIXELS];
    let mut results = Vec::new();
    for (name, view) in &views {
        let full = performance_sample(|| { black_box(render(view, &mut frame, &mut stripe).unwrap()); }, 12);
        let text = performance_sample(|| { black_box(raster(view, &mut frame, &mut stripe).unwrap()); }, 12);
        let base = frame.clone();
        let overlay = performance_sample(|| {
            frame.copy_from_slice(&base);
            overlay::draw(view, &mut frame);
            black_box(&frame);
        }, 48);
        let (_, peak) = heap_meter::measure(|| { render(view, &mut frame, &mut stripe).unwrap(); });
        let trace = heap_meter::trace();
        assert!(!trace.overflow);
        let requests: usize = trace.requests[..trace.requests_len].iter().map(|r| r.count).sum();
        results.push(serde_json::json!({"case":name,"renderNs":full,"textNs":text,"overlayNs":overlay,"renderHeapPeak":peak,"renderAllocationRequests":requests}));
    }
    for (name, bytes) in [
        ("import-hanzi", include_bytes!("../content/hanzi.json").as_slice()),
        ("import-english", include_bytes!("../content/english.json").as_slice()),
    ] {
        let ns = performance_sample(|| { black_box(model::parse_deck(black_box(bytes)).unwrap()); }, 3);
        let (_, peak) = heap_meter::measure(|| { model::parse_deck(bytes).unwrap(); });
        let trace = heap_meter::trace();
        assert!(!trace.overflow);
        let requests: usize = trace.requests[..trace.requests_len].iter().map(|r| r.count).sum();
        results.push(serde_json::json!({"case":name,"timeNs":ns,"heapPeak":peak,"allocationRequests":requests}));
    }
    for indexed in [false, true] {
        let ns = performance_sample(|| {
            for card in black_box(model::course::COURSE_CARDS) {
                let subject = black_box(usize::from(card.subject));
                let id = black_box(card.id);
                let found = if indexed {
                    model::course::index_for_id(subject, id)
                } else {
                    model::course::COURSE_CARDS.iter().position(|c| usize::from(c.subject) == subject && c.id == id)
                };
                black_box(found);
            }
        }, 3);
        let name = if indexed { "course-lookup-4000-indexed" } else { "course-lookup-4000-linear" };
        results.push(serde_json::json!({"case":name,"timeNs":ns}));
    }
    let input = vec![0xa5; model::MAX_SAVE];
    let crc_ns = performance_sample(|| { black_box(model::crc32(&[black_box(&input)])); }, 64);
    results.push(serde_json::json!({"case":"crc32-32768","timeNs":crc_ns}));
    let mut save_buffer = vec![0; model::MAX_SAVE];
    let save_ns = performance_sample(|| { black_box(model.export_into(black_box(&mut save_buffer)).unwrap()); }, 32);
    results.push(serde_json::json!({"case":"save-export","timeNs":save_ns}));
    let output = std::env::var_os("BALABALA_PERFORMANCE_OUT").map(std::path::PathBuf::from)
        .unwrap_or_else(|| std::path::Path::new(env!("BALABALA_GENERATED")).join("../performance.json"));
    std::fs::create_dir_all(output.parent().unwrap()).unwrap();
    std::fs::write(&output, serde_json::to_vec_pretty(&results).unwrap()).unwrap();
    println!("Host performance medians (5 batches; ns/op; overlay includes framebuffer reset; not C3): {}", output.display());
    println!("{}", serde_json::to_string_pretty(&results).unwrap());
}
