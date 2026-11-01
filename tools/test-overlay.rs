use super::*;

fn patterned_frame() -> alloc::vec::Vec<u8> {
    (0..crate::FRAMEBUFFER_BYTES).map(|i| (i * 37) as u8).collect()
}

#[test]
fn packed_bitmap_preserves_alignment_clipping_and_padding() {
    for width in [1usize, 7, 8, 9, 15, 16, 17, 31, 32, 33, 95, 112, 480] {
        let stride = width.div_ceil(8);
        let bits: alloc::vec::Vec<u8> = (0..stride * 5).map(|i| (i * 71 + 255) as u8).collect();
        for x in [-17, -9, -8, -7, -1, 0, 1, 2, 3, 4, 5, 6, 7, WIDTH as i32 - 1, WIDTH as i32, WIDTH as i32 + 7] {
            for y in [-3, 0, HEIGHT as i32 - 2] {
                let mut expected = patterned_frame();
                let mut actual = expected.clone();
                for row in 0..5 {
                    for col in 0..width {
                        if bits[row * stride + col / 8] & (0x80 >> (col % 8)) != 0 {
                            plot(&mut expected, x + col as i32, y + row as i32, Ink::Black);
                        }
                    }
                }
                blit_rows(&mut actual, &bits, stride, width, x, y);
                assert_eq!(actual, expected, "bitmap width={width}, x={x}, y={y}");
            }
        }
    }
}

#[test]
fn packed_fill_and_inversion_preserve_rounded_pixel_geometry() {
    for [w, h, r] in [[0, 5, 0], [1, 1, 0], [8, 5, 0], [9, 7, 3], [16, 16, 8], [33, 19, 9], [104, 264, 16], [432, 160, 16]] {
        for x in [-8, -1, 0, 1, 7, WIDTH as i32 - 17, WIDTH as i32 - 1, WIDTH as i32] {
            for y in [-8, 0, HEIGHT as i32 - 4] {
                for ink in [Ink::Black, Ink::White, Ink::Invert] {
                    let rect = [x, y, w, h];
                    let mut expected = patterned_frame();
                    let mut actual = expected.clone();
                    for py in y..y + h {
                        for px in x..x + w {
                            if inside(px, py, rect, r) {
                                plot(&mut expected, px, py, ink);
                            }
                        }
                    }
                    fill(&mut actual, rect, r, ink);
                    assert_eq!(actual, expected, "fill {rect:?}, radius={r}");
                }
            }
        }
    }
}

#[test]
fn panel_rect_matches_pixel_reference_at_byte_and_screen_boundaries() {
    for [x, y, w, h] in [
        [1, 2, 3, 4], [6, 2, 5, 3], [8, 2, 16, 3], [3, 2, 31, 3],
        [0, 0, 0, 5], [0, 0, 5, 0], [0, 0, WIDTH, HEIGHT],
        [WIDTH - 1, HEIGHT - 1, 1, 1], [WIDTH - 3, HEIGHT - 2, 8, 5],
        [WIDTH, HEIGHT, 4, 4],
    ] {
        for white in [false, true] {
            let mut expected = patterned_frame();
            let mut actual = expected.clone();
            for row in y..(y + h).min(HEIGHT) {
                for col in x..(x + w).min(WIDTH) {
                    let byte = &mut expected[row * WIDTH / 8 + col / 8];
                    let mask = 0x80 >> (col % 8);
                    if white { *byte |= mask; } else { *byte &= !mask; }
                }
            }
            panel_rect(&mut actual, [x, y, w, h], white);
            assert_eq!(actual, expected, "rect={:?}, white={white}", [x, y, w, h]);
        }
    }
}
