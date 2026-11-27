//! "Report a problem" (GitHub #31): packs a game's newest playtest report (F10 in the game writes
//! one beside its log, games/common/report.h) with the launcher's settings and the game's saves
//! into one zip, shows it, and opens the playtest issue form. Paths, the user name and the computer
//! name are replaced in the text files the launcher adds; the game scrubs its own.

use crate::game::Game;
use std::fs;
use std::io;
use std::path::{Path, PathBuf};

/// The issue form (.github/ISSUE_TEMPLATE/playtest.yml).
const ISSUE_FORM: &str = "https://github.com/Funkatronics/GRAWBox/issues/new?template=playtest.yml";

/// This build's id (build.rs).
pub const BUILD: &str = env!("GRAWBOX_BUILD");

/// The game's report folders, beside its log.
fn reports_dir(game: &Game) -> PathBuf {
    game.log().parent().map(|p| p.join("reports")).unwrap_or_default()
}

/// The newest folder a game's F10 wrote, if any.
fn newest_report(game: &Game) -> Option<PathBuf> {
    fs::read_dir(reports_dir(game))
        .ok()?
        .filter_map(|e| e.ok())
        .filter(|e| e.path().is_dir())
        .max_by_key(|e| e.file_name())
        .map(|e| e.path())
}

/// Replaces the user's profile folder, user name and computer name, as the game does: the folder
/// wherever it appears, the names only as whole words (a user "dev" must not turn "device" into
/// "<user>ice").
pub fn scrub(text: &str) -> String {
    let mut out = text.to_string();
    if let Ok(profile) = std::env::var("USERPROFILE") {
        if profile.len() >= 2 {
            out = out.replace(&profile.replace('/', "\\"), "<profile>");
            out = out.replace(&profile.replace('\\', "/"), "<profile>");
        }
    }
    for (var, tag) in [("USERNAME", "<user>"), ("COMPUTERNAME", "<computer>")] {
        if let Ok(name) = std::env::var(var) {
            out = replace_word(&out, &name, tag);
        }
    }
    out
}

fn replace_word(text: &str, word: &str, with: &str) -> String {
    if word.len() < 2 {
        return text.to_string();
    }
    let is_word = |c: Option<char>| c.is_some_and(|c| c.is_alphanumeric() || c == '_');
    let mut out = String::with_capacity(text.len());
    let mut rest = text;
    while let Some(at) = rest.find(word) {
        let before = rest[..at].chars().next_back().or_else(|| out.chars().next_back());
        let after = rest[at + word.len()..].chars().next();
        out.push_str(&rest[..at]);
        if is_word(before) || is_word(after) {
            out.push_str(word);
        } else {
            out.push_str(with);
        }
        rest = &rest[at + word.len()..];
    }
    out.push_str(rest);
    out
}

/// Packs the report and returns the zip's path.
pub fn pack(game: &Game, settings_file: &Path) -> io::Result<PathBuf> {
    let mut zip = Zip::default();
    let stamp = chrono_stamp();
    match newest_report(game) {
        Some(dir) => {
            for entry in fs::read_dir(&dir)? {
                let path = entry?.path();
                if path.is_file() {
                    let name = path.file_name().unwrap().to_string_lossy().to_string();
                    zip.add(&name, &fs::read(&path)?);
                }
            }
        }
        None => {
            // No F10 report: the end of the log is still worth having.
            if let Ok(log) = fs::read(game.log()) {
                let tail = &log[log.len().saturating_sub(2 << 20)..];
                zip.add("log-tail.txt", scrub(&String::from_utf8_lossy(tail)).as_bytes());
            }
        }
    }
    zip.add(
        "launcher.txt",
        format!("GRAWBox launcher build: {BUILD}\nGame: {}\n", game.name).as_bytes(),
    );
    if let Ok(text) = fs::read_to_string(settings_file) {
        zip.add("settings.toml", scrub(&text).as_bytes());
    }
    let user = game.user_dir();
    for file in files_under(&user) {
        if let (Ok(bytes), Ok(rel)) = (fs::read(&file), file.strip_prefix(&user)) {
            let name = format!("saves/{}", rel.to_string_lossy().replace('\\', "/"));
            zip.add(&name, &bytes);
        }
    }
    let out = reports_dir(game).join(format!("report-{}-{stamp}.zip", game.id));
    fs::create_dir_all(reports_dir(game))?;
    fs::write(&out, zip.finish())?;
    Ok(out)
}

/// Shows the zip in Explorer and opens the issue form with the game and version filled in.
pub fn show(zip: &Path, game: &Game) {
    let _ = std::process::Command::new("explorer").arg(format!("/select,{}", zip.display())).spawn();
    let label = if game.id == "graw1" { "Advanced Warfighter (GRAW 1)" } else { "Advanced Warfighter 2 (GRAW 2)" };
    let url = format!("{ISSUE_FORM}&game={}&version={}", encode(label), encode(BUILD));
    let _ = std::process::Command::new("explorer").arg(url).spawn();
}

fn encode(s: &str) -> String {
    s.bytes()
        .map(|b| match b {
            b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9' | b'-' | b'.' | b'_' => (b as char).to_string(),
            _ => format!("%{b:02X}"),
        })
        .collect()
}

fn files_under(dir: &Path) -> Vec<PathBuf> {
    let mut out = Vec::new();
    let mut stack = vec![dir.to_path_buf()];
    while let Some(d) = stack.pop() {
        if let Ok(entries) = fs::read_dir(&d) {
            for e in entries.flatten() {
                let p = e.path();
                if p.is_dir() { stack.push(p) } else { out.push(p) }
            }
        }
    }
    out
}

/// Local time as YYYYMMDD-HHMMSS, without a date library: from the system clock and the time
/// zone offset Windows reports.
fn chrono_stamp() -> String {
    #[repr(C)]
    #[derive(Default)]
    struct SystemTime { year: u16, month: u16, weekday: u16, day: u16, hour: u16, minute: u16, second: u16, ms: u16 }
    #[link(name = "kernel32")]
    extern "system" {
        fn GetLocalTime(t: *mut SystemTime);
    }
    let mut t = SystemTime::default();
    unsafe { GetLocalTime(&mut t) };
    format!("{:04}{:02}{:02}-{:02}{:02}{:02}", t.year, t.month, t.day, t.hour, t.minute, t.second)
}

/// A minimal zip writer: files stored without compression (no extra library), CRC-32 per file.
#[derive(Default)]
struct Zip {
    data: Vec<u8>,
    central: Vec<u8>,
    count: u16,
}

impl Zip {
    fn add(&mut self, name: &str, bytes: &[u8]) {
        let crc = crc32(bytes);
        let offset = self.data.len() as u32;
        let size = bytes.len() as u32;
        let name_bytes = name.as_bytes();
        // Local header: version 20, UTF-8 names (bit 11), stored.
        let mut local = Vec::new();
        local.extend(0x04034b50u32.to_le_bytes());
        local.extend(20u16.to_le_bytes());
        local.extend(0x0800u16.to_le_bytes());
        local.extend(0u16.to_le_bytes()); // stored
        local.extend(0u32.to_le_bytes()); // time and date
        local.extend(crc.to_le_bytes());
        local.extend(size.to_le_bytes());
        local.extend(size.to_le_bytes());
        local.extend((name_bytes.len() as u16).to_le_bytes());
        local.extend(0u16.to_le_bytes());
        local.extend(name_bytes);
        self.data.extend(&local);
        self.data.extend(bytes);
        let c = &mut self.central;
        c.extend(0x02014b50u32.to_le_bytes());
        c.extend(20u16.to_le_bytes());
        c.extend(20u16.to_le_bytes());
        c.extend(0x0800u16.to_le_bytes());
        c.extend(0u16.to_le_bytes());
        c.extend(0u32.to_le_bytes());
        c.extend(crc.to_le_bytes());
        c.extend(size.to_le_bytes());
        c.extend(size.to_le_bytes());
        c.extend((name_bytes.len() as u16).to_le_bytes());
        c.extend([0u8; 12]); // extra, comment, disk, internal attributes
        c.extend(0u32.to_le_bytes()); // external attributes
        c.extend(offset.to_le_bytes());
        c.extend(name_bytes);
        self.count += 1;
    }

    fn finish(mut self) -> Vec<u8> {
        let start = self.data.len() as u32;
        let size = self.central.len() as u32;
        self.data.extend(&self.central);
        self.data.extend(0x06054b50u32.to_le_bytes());
        self.data.extend([0u8; 4]); // disk numbers
        self.data.extend(self.count.to_le_bytes());
        self.data.extend(self.count.to_le_bytes());
        self.data.extend(size.to_le_bytes());
        self.data.extend(start.to_le_bytes());
        self.data.extend(0u16.to_le_bytes());
        self.data
    }
}

fn crc32(bytes: &[u8]) -> u32 {
    let mut crc = !0u32;
    for &b in bytes {
        crc ^= b as u32;
        for _ in 0..8 {
            crc = if crc & 1 != 0 { (crc >> 1) ^ 0xEDB8_8320 } else { crc >> 1 };
        }
    }
    !crc
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn crc_known_answer() {
        assert_eq!(crc32(b"123456789"), 0xCBF4_3926);
    }

    #[test]
    fn zip_layout() {
        let mut z = Zip::default();
        z.add("a.txt", b"hello");
        z.add("saves/b.bin", &[1, 2, 3]);
        let data = z.finish();
        assert_eq!(&data[0..4], &0x04034b50u32.to_le_bytes());
        // End of central directory: two entries.
        let end = data.len() - 22;
        assert_eq!(&data[end..end + 4], &0x06054b50u32.to_le_bytes());
        assert_eq!(u16::from_le_bytes([data[end + 10], data[end + 11]]), 2);
        // The first file's bytes follow its 30-byte header and name.
        assert_eq!(&data[30 + 5..30 + 10], b"hello");
    }

    #[test]
    fn whole_words_only() {
        assert_eq!(replace_word("x/dev/y and dev, device", "dev", "<user>"),
                   "x/<user>/y and <user>, device");
        assert_eq!(replace_word("devdev dev", "dev", "<user>"), "devdev <user>");
    }
}
