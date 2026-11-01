use super::content::valid_id;
use super::{course::*, decode::*, Mode, Model, Page};
use alloc::{string::String, vec::Vec};
use serde::{Deserialize, Serialize};

pub const MAX_SAVE: usize = 32768;

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Round {
    pub subject: usize,
    #[serde(deserialize_with = "bounded_ids")]
    pub ids: Vec<String>,
    #[serde(deserialize_with = "bounded_vec::<_,i8,10>")]
    pub answers: Vec<i8>,
    pub cursor: usize,
    #[serde(deserialize_with = "bounded_mode")]
    pub mode: Mode,
    pub flipped: bool,
    #[serde(default, deserialize_with = "bounded_vec::<_,[u8;4],10>")]
    pub permutations: Vec<[u8; 4]>,
}
impl Round {
    pub fn option_order(&self, cursor: usize) -> [u8; 4] {
        self.permutations.get(cursor).copied().unwrap_or([0,1,2,3])
    }
}
#[derive(Clone, Debug, Default, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Progress {
    pub subject: usize,
    #[serde(deserialize_with = "bounded_id")]
    pub id: String,
    pub status: u8,
    #[serde(default)]
    pub correct_count: u8,
    #[serde(default)]
    pub mastered: bool,
}
fn default_threshold() -> u8 { 3 }
fn default_clear_interval() -> u8 { 8 }
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Snapshot {
    pub offsets: [usize; 4],
    pub count: usize,
    pub round: Option<Round>,
    #[serde(deserialize_with = "bounded_vec::<_,Progress,160>")]
    pub progress: Vec<Progress>,
    #[serde(default = "default_threshold")]
    pub threshold: u8,
    #[serde(default = "default_clear_interval")]
    pub clear_interval: u8,
    #[serde(default)]
    pub unlocked: [u16; 4],
    #[serde(default)]
    pub starts: [u16; 4],
    #[serde(default)]
    pub serial: u32,
    #[serde(default)]
    pub course_id: u32,
    #[serde(default)]
    pub course_version: u32,
    #[serde(default)]
    pub course_crc: u32,
    #[serde(skip)]
    pub course: Vec<u8>,
    #[serde(skip)]
    pub(crate) unknown_course: bool,
}
impl Default for Snapshot {
    fn default() -> Self {
        Self { offsets:[0;4], count:5, round:None, progress:Vec::new(), threshold:3, clear_interval:8,
            unlocked:[0;4], starts:[0;4], serial:0, course_id:0, course_version:0, course_crc:0,
            course:Vec::new(), unknown_course:false }
    }
}
// 64B只读表把每字节8轮位运算缩成两次查表，不占运行时heap。
const CRC32_NIBBLES: [u32; 16] = {
    let mut table = [0; 16];
    let mut i = 0;
    while i < table.len() {
        let mut crc = i as u32;
        let mut bit = 0;
        while bit < 4 {
            crc = (crc >> 1) ^ (0xedb88320u32.wrapping_mul(crc & 1));
            bit += 1;
        }
        table[i] = crc;
        i += 1;
    }
    table
};
pub fn crc32(parts: &[&[u8]]) -> u32 {
    let mut crc = !0u32;
    for part in parts {
        for b in *part {
            crc ^= u32::from(*b);
            crc = (crc >> 4) ^ CRC32_NIBBLES[(crc & 15) as usize];
            crc = (crc >> 4) ^ CRC32_NIBBLES[(crc & 15) as usize];
        }
    }
    !crc
}
fn record_generation(bytes: &[u8]) -> Option<u64> {
    if bytes.len() < 24 || bytes.len() > MAX_SAVE || &bytes[..4] != b"BALA"
        || !matches!(bytes[4..8], [1,0,0,0] | [2,0,0,0]) { return None; }
    let generation = u64::from_le_bytes(bytes[8..16].try_into().ok()?);
    let len = u32::from_le_bytes(bytes[16..20].try_into().ok()?) as usize;
    let crc = u32::from_le_bytes(bytes[20..24].try_into().ok()?);
    if generation == 0 || len != bytes.len() - 24 || crc != crc32(&[&bytes[..20], &bytes[24..]]) {
        return None;
    }
    Some(generation)
}
fn decode(bytes: &[u8]) -> Option<Snapshot> {
    let legacy = bytes[4] == 1;
    let (json, records) = if legacy { (&bytes[24..], &[][..]) } else {
        let len = u32::from_le_bytes(bytes.get(24..28)?.try_into().ok()?) as usize;
        (bytes.get(28..28usize.checked_add(len)?)?, bytes.get(28usize.checked_add(len)?..)?)
    };
    if records.len() % 5 != 0 || records.len() / 5 > MAX_COURSE_CARDS { return None; }
    let mut state: Snapshot = serde_json::from_slice(json).ok()?;
    if ![5,10].contains(&state.count) || ![1,3,5,7,9].contains(&state.threshold)
        || !super::CLEAR_INTERVALS.contains(&state.clear_interval)
        || state.progress.len() > 160 || state.offsets.iter().any(|n| *n >= if legacy {40} else {MAX_COURSE_CARDS})
        || state.unlocked.iter().chain(&state.starts).any(|n| usize::from(*n) >= MAX_COURSE_CARDS)
        || state.course_id == 0 && (!records.is_empty() || state.course_version != 0 || state.course_crc != 0) { return None; }
    for i in 0..state.progress.len() {
        let p = &state.progress[i];
        if p.subject >= 4 || !valid_id(&p.id) || !(1..=2).contains(&p.status) || p.correct_count > 9
            || p.mastered && p.correct_count == 0
            || state.progress[..i].iter().any(|o| o.id == p.id && o.subject == p.subject) { return None; }
        if legacy && p.status == 2 { state.progress[i].correct_count = state.progress[i].correct_count.max(1); }
    }
    if let Some(r) = &state.round {
        if r.subject >= 4 || r.ids.is_empty() || r.ids.len() > 10 || r.cursor >= r.ids.len()
            || r.answers.len() != r.ids.len() || r.answers.iter().any(|a| !(-1..=1).contains(a))
            || r.ids.iter().enumerate().any(|(i,id)| !valid_id(id) || r.ids[..i].contains(id))
            || !r.permutations.is_empty() && r.permutations.len() != r.ids.len()
            || r.permutations.iter().any(|p| { let mut sorted = *p; sorted.sort_unstable(); sorted != [0,1,2,3] }) { return None; }
    }
    if state.course_id != 0 {
        state.course.resize(COURSE_CARDS.len(), 0);
        let mut previous = None;
        for record in records.chunks_exact(5) {
            let key = u32::from_le_bytes(record[..4].try_into().ok()?);
            let flags = record[4];
            if previous.is_some_and(|p| key <= p) || flags == 0 || !valid_flags(flags) { return None; }
            previous = Some(key);
            if let Some(index) = index_for_key(key) { state.course[index] = flags; }
            else { state.unknown_course = true; }
        }
    }
    Some(state)
}
impl Model {
    pub fn save_protect(&mut self) {
        self.save_protected = true;
        self.pending_save = None;
    }
    pub fn load(&mut self, a: &[u8], b: &[u8]) -> bool {
        let ga = record_generation(a);
        let gb = record_generation(b);
        let candidates = if gb > ga { [(1,b,gb),(0,a,ga)] } else { [(0,a,ga),(1,b,gb)] };
        let mut best = None;
        for (slot, bytes, generation) in candidates {
            if let Some(generation) = generation {
                if let Some(state) = decode(bytes) { best = Some((slot,generation,state)); break; }
            }
        }
        let Some((slot,generation,mut state)) = best else { self.save_protect(); return false; };
        let has_course = state.course_id != 0;
        if has_course && (!self.course_active() || state.course_id != COURSE_ID || state.course_version > COURSE_VERSION
            || state.unknown_course || state.course_version == COURSE_VERSION && state.course_crc != COURSE_CRC) {
            self.save_protect(); return false;
        }
        let known = |subject: usize, id: &str| {
            self.decks[subject].cards.iter().any(|c| c.id == id)
                || self.course_active() && index_for_id(subject,id).is_some()
        };
        let unexplained = |subject: usize, id: &str| self.imported_subjects & (1 << subject) == 0 && !known(subject,id);
        // 最高有效档出现无法解释的ID时保护原档；不能回退更旧槽掩盖它。
        if state.progress.iter().any(|p| unexplained(p.subject,&p.id))
            || state.round.as_ref().is_some_and(|r| r.ids.iter().any(|id| unexplained(r.subject,id))) {
            self.save_protect(); return false;
        }
        state.progress.retain(|p| known(p.subject,&p.id));
        if state.round.as_ref().is_some_and(|r| r.ids.iter().any(|id| !known(r.subject,id))) { state.round = None; }
        if self.course_active() {
            if state.course.is_empty() { state.course.resize(COURSE_CARDS.len(),0); }
            for p in &state.progress {
                if let Some(i) = index_for_id(p.subject,&p.id) {
                    state.course[i] = SEEN | p.correct_count | if p.mastered {MASTERED} else if p.status == 1 {WRONG} else {0};
                }
            }
            state.progress.retain(|p| index_for_id(p.subject,&p.id).is_none());
            if state.round.as_ref().is_some_and(|r|r.ids.iter().any(|id| index_for_id(r.subject,id).is_none())) { state.round = None; }
            for subject in 0..4 {
                if unit_for(subject,state.unlocked[subject]).is_none() || state.starts[subject] > state.unlocked[subject] {
                    self.save_protect(); return false;
                }
            }
            state.course_id = COURSE_ID;
            state.course_version = COURSE_VERSION;
            state.course_crc = COURSE_CRC;
        }
        let cards = if self.course_active() {
            match self.read_course_round(state.round.as_ref()) {
                Ok(cards) => cards,
                Err(error) => { self.save_protect(); self.show_error(error); return false; }
            }
        } else { Vec::new() };
        self.state = state;
        if self.course_active() { for subject in 0..4 { self.unlock_units(subject); } }
        self.course_cards = cards;
        self.generation = generation;
        self.slot = slot;
        self.save_protected = false;
        self.pending_save = None;
        self.saved_change();
        if self.page == Page::Error { self.changed(); } else { self.go(Page::Home); }
        true
    }
    pub fn export_into(&mut self, out: &mut [u8]) -> Result<(u32,u64,usize), &'static str> {
        if self.save_protected { return Err("存档不可用，已保留当前进度"); }
        let (slot,generation) = if let Some((generation,mutation,slot)) = self.pending_save {
            if mutation != self.mutation { return Err("等待上次存档写入完成"); }
            (slot,generation)
        } else { (1-self.slot,self.generation.checked_add(1).ok_or("存档代数超限")?) };
        let payload = serde_json::to_vec(&self.state).map_err(|_| "存档编码失败")?;
        let touched = self.state.course.iter().filter(|p| **p != 0).count();
        let len = 28 + payload.len() + touched * 5;
        if len > MAX_SAVE || len > out.len() { return Err("存档超过大小上限"); }
        out[..8].copy_from_slice(b"BALA\x02\0\0\0");
        out[8..16].copy_from_slice(&generation.to_le_bytes());
        out[16..20].copy_from_slice(&((len-24) as u32).to_le_bytes());
        out[24..28].copy_from_slice(&(payload.len() as u32).to_le_bytes());
        out[28..28+payload.len()].copy_from_slice(&payload);
        let mut cursor = 28+payload.len();
        for &index in COURSE_KEYS {
            if let Some(&flags) = self.state.course.get(usize::from(index)).filter(|p| **p != 0) {
                out[cursor..cursor+4].copy_from_slice(&COURSE_CARDS[usize::from(index)].key.to_le_bytes());
                out[cursor+4] = flags;
                cursor += 5;
            }
        }
        let crc = crc32(&[&out[..20],&out[24..len]]);
        out[20..24].copy_from_slice(&crc.to_le_bytes());
        self.pending_save = Some((generation,self.mutation,slot));
        Ok((slot,generation,len))
    }
    #[cfg(test)]
    pub fn export(&mut self) -> Result<(u32,u64,Vec<u8>), &'static str> {
        let mut out = alloc::vec![0;MAX_SAVE];
        let (slot,generation,len) = self.export_into(&mut out)?;
        out.truncate(len);
        Ok((slot,generation,out))
    }
    pub fn save_ack(&mut self, generation: u64, success: bool) -> bool {
        let Some((pending,mutation,slot)) = self.pending_save else { return false; };
        if pending != generation { return false; }
        if success {
            self.generation = generation;
            self.slot = slot;
            if mutation == self.mutation { self.dirty = false; }
        }
        self.pending_save = None;
        true
    }
}
