//! MicroTS strict AOT 学习页与单线程平台数据桥。
#![cfg_attr(target_os = "none", no_std)]
extern crate alloc;
pub mod art;
pub mod fit;
mod fonts;
mod overlay;
pub mod layout {
    include!(concat!(env!("BALABALA_GENERATED"), "/layout.rs"));
}
mod abi;
#[cfg(target_os = "none")]
mod allocator;
pub mod model;
mod render;
mod view;
pub use abi::*;
#[cfg(test)]
use pocketjs_core::{damage::DamageRect, raster, Ui};
#[cfg(test)]
use render::*;
pub use view::View;

mod generated {
    include!(concat!(env!("BALABALA_GENERATED"), "/mod.rs"));
}
include!(concat!(env!("BALABALA_GENERATED"), "/content.rs"));
include!(concat!(env!("BALABALA_GENERATED"), "/projection.rs"));
include!(concat!(env!("BALABALA_GENERATED"), "/provisioning.rs"));
#[inline]
fn memory_checkpoint(stage: u32) {
    #[cfg(target_os = "none")]
    unsafe {
        extern "C" {
            fn balabala_memory_checkpoint(stage: u32);
        }
        balabala_memory_checkpoint(stage);
    }
    #[cfg(not(target_os = "none"))]
    let _ = stage;
}
pub const WIDTH: usize = 480;
pub const HEIGHT: usize = 800;
pub const FRAMEBUFFER_BYTES: usize = WIDTH * HEIGHT / 8;
pub const STRIPE_ROWS: usize = 8;
pub const STRIPE_PIXELS: usize = WIDTH * STRIPE_ROWS;

pub struct App {
    pub model: model::Model,
    panel_cached: bool,
    panel_generation: u32,
    panel_identity_generation: u32,
}
#[cfg(feature = "autotest")]
mod autotest;
#[cfg(feature = "autotest")]
pub use autotest::balabala_app_autotest_snapshot;
#[cfg(test)]
#[path = "../../tools/test-native-render.rs"]
mod tests;
