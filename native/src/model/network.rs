use super::{Model, Page};

impl Model {
    pub(crate) fn clear_network_info(&mut self) {
        for byte in self
            .network_name
            .iter_mut()
            .chain(self.network_pop.iter_mut())
        {
            unsafe { core::ptr::write_volatile(byte, 0) };
        }
    }
    pub fn network_info(&mut self, name: &[u8], pop: &[u8]) -> bool {
        if self.page != Page::Network {
            return false;
        }
        if name.is_empty() && pop.is_empty() {
            if self.network_name[0] != 0 {
                self.clear_network_info();
                self.changed();
            }
            return true;
        }
        if !matches!(self.network, 1 | 2 | 7)
            || name.len() != 11
            || pop.len() != 6
            || &name[..5] != b"PROV_"
            || !name[5..]
                .iter()
                .all(|b| b.is_ascii_hexdigit() && !b.is_ascii_lowercase())
            || !pop.iter().all(u8::is_ascii_digit)
        {
            return false;
        }
        if self.network_name != name || self.network_pop != pop {
            self.network_name.copy_from_slice(name);
            self.network_pop.copy_from_slice(pop);
            self.changed();
        }
        true
    }
    pub(super) fn network_exit(&mut self, intent: u8) {
        self.network_exit = self.network_exit.max(intent);
        self.platform_requests |= 4;
        if intent == 3 {
            self.platform_requests |= 1;
        }
        if self.network_owned {
            self.clear_network_info();
            if self.network != 9 {
                self.network = 8;
            }
            self.changed();
        } else {
            self.finish_network_exit();
        }
    }
    fn finish_network_exit(&mut self) {
        let intent = core::mem::take(&mut self.network_exit);
        if intent != 0 {
            self.go(if intent == 1 {
                Page::Settings
            } else {
                Page::Home
            });
        }
    }
    pub fn network_shutdown_blocked(&mut self) {
        self.network_exit = 0;
        self.platform_requests &= !1;
        self.clear_network_info();
        self.network = 9;
        self.changed();
    }
    pub fn network_identity_visible(&self) -> bool {
        self.network_name[0] != 0 && matches!(self.network, 1 | 2 | 7)
    }
    pub fn network_status(&mut self, status: u32) -> bool {
        if !(1..=9).contains(&status) || self.page != Page::Network {
            return false;
        }
        if matches!(self.network, 3..=6 | 8 | 9) && !matches!(status, 4 | 8 | 9) {
            return false;
        }
        self.set_network_status(status);
        true
    }
    fn set_network_status(&mut self, status: u32) {
        if self.network != status {
            if !matches!(status, 1 | 2 | 7) {
                self.clear_network_info();
            }
            self.network = status;
            self.changed();
        }
    }
    pub fn network_phase(&mut self, generation: u32, status: u32, phase: u32, flags: u32) -> u64 {
        if self.page != Page::Network || phase > 2 || !(1..=9).contains(&status) {
            return 0;
        }
        if generation != self.network_generation || phase != self.network_phase {
            self.network_generation = generation;
            self.network_phase = phase;
            self.changed();
        }
        self.network_owned = flags & 1 != 0 || flags & 2 == 0;
        self.set_network_status(status);
        if flags & 4 != 0 {
            self.network_exit = 0;
            self.platform_requests &= !1;
            if !self.network_owned {
                self.show_error("网络恢复失败，本次开机无法配网");
            }
        } else if !self.network_owned {
            self.finish_network_exit();
        }
        self.revision
    }
    pub fn network_ready(&self, generation: u32, phase: u32, revision: u64) -> bool {
        self.page == Page::Network
            && self.network == 7
            && self.network_owned
            && self.network_generation == generation
            && self.network_phase == phase
            && matches!(phase, 1 | 2)
            && self.network_exit == 0
            && self.platform_requests == 0
            && self.revision == revision
            && self.ready()
    }
}
