use pocketjs_core::{resources::RenderResources, text::{Atlas, Fonts}};
const FLASH_LARGE: &[u8] = include_bytes!(concat!(env!("BALABALA_GENERATED"), "/font-14.bin"));
const fn tiny_font() -> [u8; 52] {
    let mut b = [0; 52];
    let magic = pocketjs_core::spec::font_atlas::MAGIC.to_le_bytes();
    let mut i = 0;
    while i < 4 { b[i] = magic[i]; i += 1; }
    b[4] = 2; b[6] = 3;
    b[8] = 2; b[9] = 2; b[10] = 1; b[11] = 3;
    b[16] = b'A'; b[20] = 2; b[22] = 3; b[23] = 1;
    b[24] = b'B'; b[28] = 0; b[30] = 4;
    b[32] = b'C'; b[36] = 1; b[38] = 5;
    i = 40;
    while i < 52 { b[i] = ((i - 40) / 4 + 1) as u8 * 85; i += 1; }
    b
}
static TINY_FONT: [u8; 52] = tiny_font();
static TINY_REPLACEMENT: [u8; 52] = { let mut b = tiny_font(); b[22] = 7; b[48] = 17; b };
#[test]
fn font_flash_owned_borrowed_metrics_fallback_and_validation() {
    let owned = Atlas::subset(&TINY_FONT, &['A', 'C']).unwrap();
    let borrowed = Atlas::subset_static(&TINY_FONT, &['A', 'C']).unwrap();
    assert_eq!(owned.glyph_count, 2);
    assert_eq!(borrowed.glyph_count, 3); // source ID range, including unselected B/gid 0
    assert!(borrowed.bitmap.is_empty());
    assert_eq!(borrowed.bitmap().as_ptr(), TINY_FONT[40..].as_ptr());
    for ch in ['A', 'C'] {
        let o = owned.lookup_entry(ch as u32).unwrap();
        let b = borrowed.lookup_entry(ch as u32).unwrap();
        assert_eq!((o.advance, o.xoff), (b.advance, b.xoff));
        assert_eq!(owned.glyph_rows(o.gid), borrowed.glyph_rows(b.gid));
    }
    assert!(borrowed.lookup('B' as u32).is_none());
    assert_eq!(borrowed.logical_coverage(3, 0, 0), 0);
    for bytes in [&TINY_FONT[..0], &TINY_FONT[..15], &TINY_FONT[..51]] {
        assert!(Atlas::subset_static(bytes, &['A']).is_none());
        assert!(Atlas::subset(bytes, &['A']).is_none());
    }
    for chars in [&[][..], &['C','A'][..], &['A','A'][..], &['Z'][..]] {
        assert!(Atlas::subset_static(&TINY_FONT, chars).is_none());
        assert!(Atlas::subset(&TINY_FONT, chars).is_none());
    }
    static BAD_GID: [u8; 52] = { let mut b = tiny_font(); b[28] = 3; b };
    static BAD_ORDER: [u8; 52] = { let mut b = tiny_font(); b[24] = b'A'; b };
    static BAD_SLOT: [u8; 52] = { let mut b = tiny_font(); b[12] = 255; b };
    for bytes in [&BAD_GID, &BAD_ORDER, &BAD_SLOT] {
        assert!(Atlas::subset_static(bytes, &['A']).is_none());
        assert!(Atlas::subset(bytes, &['A']).is_none());
    }
    // Compare actual pixels/placement for a miss, including after replacement.
    let mut o = Fonts::default(); let mut b = Fonts::default();
    for source in [&TINY_FONT, &TINY_REPLACEMENT] {
        assert!(o.load_subset(source, &['A','C']));
        assert!(b.load_subset_static(source, &['A','C']));
        assert_eq!(o.measure_run("AZC",0,0.0,f32::NAN), b.measure_run("AZC",0,0.0,f32::NAN));
        let mut op = Vec::new(); let mut bp = Vec::new();
        o.layout_run("AZC",0,0.0,f32::NAN,0,40.0,&mut op);
        b.layout_run("AZC",0,0.0,f32::NAN,0,40.0,&mut bp);
        assert_eq!(op.len(),3); assert_eq!(bp.len(),op.len());
        for (og,bg) in op.iter().zip(&bp) {
            assert_eq!((og.x,og.y,og.codepoint),(bg.x,bg.y,bg.codepoint));
            assert_eq!(o.atlas(0).unwrap().glyph_rows(og.gid),b.atlas(0).unwrap().glyph_rows(bg.gid));
        }
        assert_eq!(bp[1].gid, 2); // owned fallback gid 0 maps to selected A/source gid 2
    }
    static DENSE: [u8;88] = {
        let tiny = tiny_font(); let mut b = [0;88]; let mut i = 0;
        while i < 40 { b[i] = tiny[i]; i += 1; }
        b[4] = pocketjs_core::spec::font_atlas::VERSION as u8; b[14] = 2;
        while i < 88 { b[i] = ((i-40)/16 + 1) as u8 * 85; i += 1; }
        b
    };
    let o = Atlas::subset(&DENSE,&['A','C']).unwrap();
    let b = Atlas::subset_static(&DENSE,&['A','C']).unwrap();
    assert_eq!(b.raster_density,2);
    for ch in ['A','C'] {
        let og = o.lookup(ch as u32).unwrap().0; let bg = b.lookup(ch as u32).unwrap().0;
        assert_eq!(o.glyph_rows(og),b.glyph_rows(bg));
        assert_eq!(o.logical_coverage(og,1,1),b.logical_coverage(bg,1,1));
    }
    // Owned loading remains independent of the source lifetime and mutations.
    let mut bytes = TINY_FONT.to_vec();
    let copied = Atlas::subset(&bytes, &['A']).unwrap(); bytes.fill(0); drop(bytes);
    assert_eq!(copied.glyph_rows(0), &[255;4]);
}
#[test]
fn font_flash_replacement_resources_pages_and_streaming() {
    let mut ui = Ui::new(); ui.set_viewport(100.0,100.0);
    ui.set_prop(pocketjs_core::spec::ROOT_ID,pocketjs_core::spec::prop::ALIGN,0.0);
    assert!(ui.load_font_subset_static(&TINY_FONT, &['A','C']));
    let text = ui.create_node(1); ui.insert_before(pocketjs_core::spec::ROOT_ID,text,0);
    ui.set_prop(text,pocketjs_core::spec::prop::FONT_SLOT,0.0); ui.set_text(text,"A");
    ui.tick(); ui.draw();
    let width = ui.layout_of(text).unwrap().2;
    let revision = ui.font_atlas_revision(0); let raster = ui.raster_revision();
    let resource = RenderResources::font_atlas(&ui,0).unwrap();
    assert_eq!(resource.bitmap.as_ptr(), TINY_FONT[40..].as_ptr());
    assert_eq!(resource.glyph_rows(2), &[255;4]);
    assert!(ui.prepare_glyph_page(0,3).is_none());
    assert!(ui.prepare_glyph_page(0,2).is_some());
    let mut pages = pocketjs_core::font_pages::FontPages::default();
    let (page, fresh) = pages.get(ui.font_atlas(0).unwrap(),revision,2).unwrap();
    assert!(fresh && page.contains(2));
    pages.retire_frame();
    assert!(!ui.load_font_subset_static(&TINY_FONT[..51], &['A']));
    assert_eq!((ui.font_atlas_revision(0),ui.raster_revision()),(revision,raster));
    assert!(ui.load_font_subset_static(&TINY_REPLACEMENT, &['A','C']));
    assert_ne!(ui.font_atlas_revision(0),revision); assert_ne!(ui.raster_revision(),raster);
    ui.tick(); ui.draw(); assert!(ui.layout_of(text).unwrap().2 > width);
    assert!(pages.get(ui.font_atlas(0).unwrap(),ui.font_atlas_revision(0),2).unwrap().1);
    let mut config = [0u8;20];
    config[..4].copy_from_slice(&pocketjs_core::font_stream::CONFIG_MAGIC.to_le_bytes());
    // Detaching an archive that never streamed must retain its borrowed coverage.
    assert!(ui.font_stream_configure(&config));
    assert_eq!(ui.font_atlas(0).unwrap().bitmap().as_ptr(),TINY_REPLACEMENT[40..].as_ptr());
    config[4] = 1; config[9..15].copy_from_slice(&[2,2,1,3,2,1]); config[16] = 2;
    assert!(ui.font_stream_configure(&config));
    assert!(!ui.font_atlas(0).unwrap().bitmap.is_empty());
    assert_eq!(ui.font_atlas(0).unwrap().glyph_rows(2), &TINY_REPLACEMENT[48..52]);
    let mut lease = [0u8;20];
    lease[..4].copy_from_slice(&pocketjs_core::font_stream::BATCH_MAGIC.to_le_bytes());
    lease[4] = 1; lease[12] = 1; lease[16] = b'Z';
    assert_eq!(ui.font_stream_batch(&lease),0);
    let mut batch = [0u8;21];
    batch[..4].copy_from_slice(&pocketjs_core::font_stream::GLYPH_MAGIC.to_le_bytes());
    batch[4] = 1; batch[9..12].copy_from_slice(&[1,2,2]); batch[12] = b'Z';
    batch[16] = 2; batch[18] = 1; batch[20] = 255;
    assert_eq!(ui.font_stream_commit(&batch),1);
    let gid = ui.font_atlas(0).unwrap().lookup('Z' as u32).unwrap().0;
    assert_eq!(ui.font_atlas(0).unwrap().glyph_rows(gid), &[255;4]);
    config[4] = 0; config[16] = 0;
    assert!(ui.font_stream_configure(&config));
    assert!(ui.font_atlas(0).unwrap().lookup('Z' as u32).is_none());
    assert_eq!(ui.font_atlas(0).unwrap().glyph_rows(2), &TINY_REPLACEMENT[48..52]);
    assert!(ui.load_font_subset(&TINY_FONT,&['A']));
    assert_eq!(RenderResources::font_atlas(&ui,0).unwrap().glyph_rows(0), &[255;4]);
}
fn render_owned_page(view: &View) -> Vec<u8> {
    let mut core = if view.success { Ui::new_with_layout_capacity(projected_nodes(view)) } else { Ui::new() };
    core.set_viewport(WIDTH as f32,HEIGHT as f32);
    fonts::load_owned(&mut core, view);
    let mut host = microts::Ui::from_core(core);
    assert!(host.load_styles(include_bytes!(concat!(env!("BALABALA_GENERATED"),"/styles.bin"))));
    let mut app = generated::AppApp::new(host,view.props(),ViewModel);
    app.frame(&microts::Input::default()); app.ui_mut().core_mut().draw();
    let mut frame = vec![0;FRAMEBUFFER_BYTES]; let mut stripe = vec![0;STRIPE_PIXELS];
    for y in (0..HEIGHT).step_by(STRIPE_ROWS) {
        render_window(app.ui().core(),&mut stripe,DamageRect::new(0,y as i32,WIDTH as i32,(y+STRIPE_ROWS) as i32));
        let start = y*WIDTH/8; pack_monochrome(&stripe,&mut frame[start..start+STRIPE_PIXELS/8]);
    }
    overlay::draw(view, &mut frame);
    frame
}
#[test]
fn font_flash_current_and_frozen_page_pixels_equal_owned() {
    let home = Model::new().view();
    let stats = fonts::load(&mut Ui::new(),&home).unwrap();
    assert!(stats.resident_cmap_bytes > 0 && stats.logical_construction_budget <= fonts::MAX_PAGE_FONT_BYTES);
    let base = flash_frozen_pages();
    let mut inputs = Vec::new();
    if let Some(base) = base.as_ref() {
        for entry in std::fs::read_dir(base).unwrap() {
            let path = entry.unwrap().path();
            if path.to_string_lossy().ends_with(".view.json") { inputs.push(path); }
        }
        assert!(inputs.len() >= 40,"frozen download-final page evidence missing");
    }
    inputs.sort();
    let mut frame = vec![0;FRAMEBUFFER_BYTES]; let mut stripe = vec![0;STRIPE_PIXELS];
    for input in &inputs {
        let view: View = serde_json::from_slice(&std::fs::read(input).unwrap()).unwrap();
        let owned = render_owned_page(&view);
        render(&view,&mut frame,&mut stripe).unwrap();
        assert_eq!(owned,frame,"owned/Flash pixels for {}",input.display());
        let frozen = std::fs::read(input.with_file_name(input.file_name().unwrap().to_str().unwrap().replace(".view.json",".pbm"))).unwrap();
        let pixels: Vec<_> = frame.iter().map(|b| !b).collect();
        assert!(frozen.ends_with(&pixels),"frozen PBM pixels for {}",input.display());
    }
    for (name,view) in flash_test_views() {
        let owned = render_owned_page(&view);
        render(&view,&mut frame,&mut stripe).unwrap();
        assert_eq!(owned,frame,"owned/Flash pixels for {name}");
    }
    println!("font-flash: {} frozen Views: owned/borrowed/PBM equal; current home resident cmap {} B",inputs.len(),stats.resident_cmap_bytes);
}
#[test]
fn font_flash_fragmented_child() {
    let Some(mode) = std::env::var_os("BALABALA_FONT_ARENA_CHILD") else { return; };
    // Preserve the historical 16800 B alpha allocation as an explicit legacy
    // fixture; the real mono owned subset now fits this fragmented arena.
    let legacy = if mode == "legacy-owned" {
        let mono = Atlas::parse(FLASH_LARGE).unwrap();
        let count = mono.glyph_count as usize;
        let offset = 16 + count * 8;
        let mut alpha = FLASH_LARGE[..offset].to_vec();
        alpha[13] &= !pocketjs_core::spec::font_atlas::FLAG_MONO1;
        for gid in 0..mono.glyph_count {
            for y in 0..mono.cell_h { for x in 0..mono.cell_w {
                alpha.push(mono.logical_coverage(gid, x, y));
            }}
        }
        Some(alpha)
    } else { None };
    let stats = font_arena::run(&[15_872,15_872,11_680],|| {
        let chars = ['一','发','现','起','\u{fffd}'];
        let atlas = if mode == "legacy-owned" { Atlas::subset(legacy.as_ref().unwrap(),&chars) }
            else if mode == "owned" { Atlas::subset(FLASH_LARGE,&chars) }
            else { Atlas::subset_static(FLASH_LARGE,&chars) }.unwrap();
        assert_eq!(atlas.lookup('现' as u32).unwrap().1,48);
        drop(atlas);
    });
    assert_eq!(stats.restored,43_424); assert!(stats.largest < 15_872);
    println!("synthetic fragmentation only; not C3 replay: {stats:?}");
}
#[test]
fn font_flash_fragmented_43424_total_15872_max_rejects_old_16800() {
    let exe = std::env::current_exe().unwrap();
    for mode in ["legacy-owned","owned","borrowed"] {
        let output = std::process::Command::new(&exe).args(["--exact","tests::font_flash_fragmented_child","--nocapture","--test-threads=1"])
            .env("BALABALA_FONT_ARENA_CHILD",mode).current_dir(std::env::temp_dir()).output().unwrap();
        if mode == "legacy-owned" {
            assert!(!output.status.success());
            assert!(String::from_utf8_lossy(&output.stderr).contains("memory allocation of 16800 bytes failed"),"{}",String::from_utf8_lossy(&output.stderr));
        } else { assert!(output.status.success(),"{}",String::from_utf8_lossy(&output.stderr)); }
        println!("fragment child {mode}: status={} stdout={} stderr={}",output.status,String::from_utf8_lossy(&output.stdout),String::from_utf8_lossy(&output.stderr));
    }
}
fn flash_frozen_pages() -> Option<std::path::PathBuf> {
    if let Some(path) = std::env::var_os("BALABALA_COMPARE_PAGES") { return Some(path.into()); }
    let path = std::path::PathBuf::from(env!("BALABALA_GENERATED")).join("../download-final/pages");
    path.is_dir().then_some(path)
}
fn flash_test_views() -> Vec<(&'static str,View)> {
    let mut model = Model::new();
    let home = model.view();
    let (near,_,_) = near_dual_limit_fixture();
    model.import(&near).unwrap(); model.start(0,Mode::Quiz,false);
    let near = model.view();
    model = Model::new(); model.start(2,Mode::Quiz,false);
    model.state.round.as_mut().unwrap().answers[0] = 1;
    let correct = model.view();
    let mut maximum = View::new();
    maximum.title = "英语小伙伴  2 / 10".into(); maximum.hero = "orange".into();
    maximum.lines[0] = "我们一起看看".into(); maximum.lines[1] = "答案：orange".into();
    maximum.lines[2] = "orange可以表示橙子。".into();
    maximum.hint = "侧面下或确认：下一张".into(); maximum.keys = "左    右    返回    确认".into();
    assert_eq!(fonts::construction_budget(&maximum).unwrap(),60_072);
    model.page = Page::DownloadPrompt;
    let download = model.view();
    vec![("home",home),("near-content-and-font-limits",near),("font-budget-maximum",maximum),("2-correct",correct),("download-mode",download)]
}
#[test]
fn font_flash_bounded_complete_page_lifecycles_restore_all_regions() {
    let (near_input,_,_) = near_dual_limit_fixture();
    let mut report = Vec::new();
    for (name,view) in flash_test_views() {
        let bytes = if let Some(base) = flash_frozen_pages() { std::fs::read(base.join(format!("{name}.view.json"))).unwrap() }
            else { serde_json::to_vec(&view).unwrap() };
        let mut peaks = [0;3];
        for peak in &mut peaks {
            let stats = font_arena::run(&[65_536;4],|| {
                let mut model = Model::new();
                if name == "near-content-and-font-limits" {
                    let input = near_input.clone(); model.import(&input).unwrap(); drop(input);
                    model.start(0,Mode::Quiz,false);
                }
                if name == "2-correct" { model.start(2,Mode::Quiz,false); model.state.round.as_mut().unwrap().answers[0]=1; }
                // Keep Model, frozen View, mounted App, framebuffer and stripe in this bounded arena.
                let view: View = if name == "home" || name == "near-content-and-font-limits" { model.view() } else { serde_json::from_slice(&bytes).unwrap() };
                let mut frame = vec![0;FRAMEBUFFER_BYTES]; let mut stripe = vec![0;STRIPE_PIXELS];
                render(&view,&mut frame,&mut stripe).unwrap();
            });
            assert_eq!(stats.restored,262_144);
            assert_eq!(stats.largest,FRAMEBUFFER_BYTES);
            *peak = stats.peak;
        }
        assert_eq!(peaks,[peaks[0];3]);
        println!("bounded full lifecycle {name}: peak={} largest=48000 restored=262144 on all three iterations; synthetic host arena, not C3 replay",peaks[0]);
        report.push(serde_json::json!({"case":name,"arenaCapacity":262144,"regions":[65536,65536,65536,65536],"iterationPeaks":peaks,"peak":peaks[0],"largestRequest":48000,"restoredPerIteration":262144}));
    }
    let path = std::path::Path::new(env!("BALABALA_GENERATED")).join("../font-flash-lifecycles.json");
    std::fs::write(path,serde_json::to_vec_pretty(&report).unwrap()).unwrap();
}

#[test]
fn font_flash_reports_heap_separately_from_logical_coverage() {
    let mut report = Vec::new();
    let mut frame = vec![0;FRAMEBUFFER_BYTES]; let mut stripe = vec![0;STRIPE_PIXELS];
    for (name,view) in flash_test_views() {
        let mut ui = Ui::new();
        let (stats,peak) = heap_meter::measure(|| fonts::load(&mut ui,&view).unwrap());
        let largest = heap_meter::largest_request();
        for slot in [5,6,14,15] {
            let Some(atlas) = ui.font_atlas(slot) else { continue; };
            let source = fonts::font(slot);
            let count = u16::from_le_bytes([source[6],source[7]]);
            assert!(atlas.bitmap.is_empty());
            assert_eq!(atlas.glyph_count,count);
            assert_eq!(atlas.bitmap(),&source[16+count as usize*8..]);
        }
        drop(ui);
        let mut ui = Ui::new();
        let (_,owned_peak) = heap_meter::measure(|| {
            fonts::load_owned(&mut ui, &view);
        });
        let owned_largest = heap_meter::largest_request(); drop(ui);
        assert!(owned_peak > peak);
        let (_,render_peak) = heap_meter::measure(|| render(&view,&mut frame,&mut stripe).unwrap());
        let render_largest = heap_meter::largest_request();
        report.push(serde_json::json!({"case":name,"logicalSerializedNormalBytes":stats.normal,"logicalSerializedLargeBytes":stats.large,"logicalConstructionBudget":stats.logical_construction_budget,"residentCmapBytes":stats.resident_cmap_bytes,"copiedBitmapBytes":0,"fontHeapPeak":peak,"fontLargestRequest":largest,"ownedFontHeapPeak":owned_peak,"ownedFontLargestRequest":owned_largest,"renderHeapPeak":render_peak,"renderLargestRequest":render_largest}));
    }
    let path = std::path::Path::new(env!("BALABALA_GENERATED")).join("../font-flash-metrics.json");
    std::fs::write(path,serde_json::to_vec_pretty(&report).unwrap()).unwrap();
}

// A nine-pixel cell exercises both byte boundaries and per-row padding. The
// cmap deliberately orders source glyph IDs differently from codepoints.
const fn mono_source_alpha() -> [u8; 94] {
    let mut b = [0; 94];
    let mut i = 0;
    let tiny = tiny_font();
    while i < 40 { b[i] = tiny[i]; i += 1; }
    b[4] = 3; b[8] = 9; b[13] = pocketjs_core::spec::font_atlas::FLAG_BOLD; b[14] = 1;
    let boundaries = [0, 127, 128, 255];
    while i < 94 { b[i] = boundaries[(i - 40) % 4]; i += 1; }
    b
}
static MONO_SOURCE_ALPHA: [u8; 94] = mono_source_alpha();
static MONO_SOURCE: [u8; 52] = {
    let alpha = mono_source_alpha();
    let mut b = [0; 52];
    let mut i = 0;
    while i < 40 { b[i] = alpha[i]; i += 1; }
    b[13] |= pocketjs_core::spec::font_atlas::FLAG_MONO1;
    let mut gid = 0;
    while gid < 3 {
        let mut y = 0;
        while y < 2 {
            let mut x = 0;
            while x < 9 {
                if alpha[40 + gid * 18 + y * 9 + x] >= 128 {
                    b[40 + gid * 4 + y * 2 + x / 8] |= 0x80 >> (x % 8);
                }
                x += 1;
            }
            y += 1;
        }
        gid += 1;
    }
    b
};
#[test]
fn mono_atlas_parse_subsets_stride_and_truncation() {
    let alpha = Atlas::parse(&MONO_SOURCE_ALPHA).unwrap();
    let full = Atlas::parse(&MONO_SOURCE).unwrap();
    let owned = Atlas::subset(&MONO_SOURCE, &['A', 'C']).unwrap();
    let borrowed = Atlas::subset_static(&MONO_SOURCE, &['A', 'C']).unwrap();
    assert_eq!(full.flags, pocketjs_core::spec::font_atlas::FLAG_BOLD | pocketjs_core::spec::font_atlas::FLAG_MONO1);
    assert_eq!(full.bytes_per_row(), 2);
    assert_eq!(full.bitmap().len(), 12);
    assert_eq!(owned.bitmap().len(), 8);
    assert_eq!(borrowed.bitmap().as_ptr(), MONO_SOURCE[40..].as_ptr());
    assert!(borrowed.bitmap.is_empty());
    for ch in ['A', 'C'] {
        let original = alpha.lookup_entry(ch as u32).unwrap();
        for atlas in [&full, &owned, &borrowed] {
            let selected = atlas.lookup_entry(ch as u32).unwrap();
            assert_eq!((original.advance, original.xoff), (selected.advance, selected.xoff));
            assert_eq!(atlas.glyph_rows(selected.gid).len(), 4);
            for y in 0..2 {
                for x in 0..9 {
                    assert_eq!(atlas.logical_coverage(selected.gid, x, y),
                        if alpha.logical_coverage(original.gid, x, y) >= 128 { 255 } else { 0 });
                }
            }
        }
    }
    for len in 0..MONO_SOURCE.len() {
        assert!(Atlas::parse(&MONO_SOURCE[..len]).is_none(), "parse truncated at {len}");
        assert!(Atlas::subset(&MONO_SOURCE[..len], &['A']).is_none(), "subset truncated at {len}");
        assert!(Atlas::subset_static(&MONO_SOURCE[..len], &['A']).is_none(), "static truncated at {len}");
    }
    static DENSE_MONO: [u8; 52] = { let mut b = MONO_SOURCE; b[14] = 2; b };
    assert!(Atlas::parse(&DENSE_MONO).is_none());
    assert!(Atlas::subset(&DENSE_MONO, &['A']).is_none());
    assert!(Atlas::subset_static(&DENSE_MONO, &['A']).is_none());
    assert_eq!(owned.logical_coverage(0, 9, 0), 0);
    assert_eq!(owned.logical_coverage(0, 0, 2), 0);
    assert_eq!(owned.logical_coverage(2, 0, 0), 0);
}
#[test]
fn mono_same_source_glyphs_equal_alpha_after_rgb565_monochrome_pack() {
    // Compare through the production RGB565 blend and the device's exact
    // monochrome conversion at all four threshold values.
    for (background, color) in [(0xffff, 0xff00_0000), (0, 0xffff_ffff)] {
        let mut expected = Vec::new();
        for (source, subset) in [(&MONO_SOURCE_ALPHA[..], 0), (&MONO_SOURCE[..], 0),
            (&MONO_SOURCE[..], 1), (&MONO_SOURCE[..], 2)] {
            let mut ui = Ui::new(); ui.set_viewport(16.0, 8.0);
            assert!(match subset {
                1 => ui.load_font_subset(source, &['A','C']),
                2 => ui.load_font_subset_static(&MONO_SOURCE, &['A','C']),
                _ => ui.load_font_atlas(source),
            });
            let view = RenderResources::font_atlas(&ui, 0).unwrap();
            assert_eq!(view.bytes_per_row(), if subset == 0 && source.len() == 94 { 9 } else { 2 });
            for ch in ['A', 'C'] {
                let gid = ui.font_atlas(0).unwrap().lookup(ch as u32).unwrap().0;
                let words = [pocketjs_core::spec::draw_op::GLYPH_RUN, 1 << 16, color,
                    1 | (2 << 16), gid as u32];
                let mut rgb = vec![background; 128];
                raster::render_scaled_rgb565_over(&ui, &words, &mut rgb, 1);
                let mut mono = vec![0; 16]; pack_monochrome(&rgb, &mut mono);
                if subset == 0 && source.len() == MONO_SOURCE_ALPHA.len() {
                    let alpha = ui.font_atlas(0).unwrap();
                    for y in 0..2 {
                        for x in 0..9 {
                            let index = (y + 2) * 16 + x + 1;
                            let white = mono[index / 8] & (0x80 >> (index % 8)) != 0;
                            let ink = alpha.logical_coverage(gid, x as u32, y as u32) >= 128;
                            assert_eq!(white, if background == 0 { ink } else { !ink });
                        }
                    }
                    expected.push(mono);
                } else { assert_eq!(mono, expected[if ch == 'A' { 0 } else { 1 }]); }
            }
        }
    }
}
#[test]
fn mono_streaming_rejects_dynamic_capacity_but_detach_keeps_flash() {
    let mut ui = Ui::new();
    assert!(ui.load_font_subset_static(&MONO_SOURCE, &['A','C']));
    let revision = ui.font_atlas_revision(0);
    let mut config = [0u8; 20];
    config[..4].copy_from_slice(&pocketjs_core::font_stream::CONFIG_MAGIC.to_le_bytes());
    config[4] = 1; config[9..15].copy_from_slice(&[9,2,1,3,9,1]); config[16] = 2;
    assert!(!ui.font_stream_configure(&config));
    assert_eq!(ui.font_atlas_revision(0), revision);
    assert_eq!(ui.font_atlas(0).unwrap().bitmap().as_ptr(), MONO_SOURCE[40..].as_ptr());
    config[4] = 0; config[16] = 0;
    assert!(ui.font_stream_configure(&config));
    assert_eq!(ui.font_atlas(0).unwrap().bitmap().as_ptr(), MONO_SOURCE[40..].as_ptr());
}
