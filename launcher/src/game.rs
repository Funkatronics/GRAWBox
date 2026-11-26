//! The two games: what their files are, whether they are in place, and starting them.

use crate::extract::{sha1_of, Source};
use crate::settings::Settings;
use std::path::{Path, PathBuf};
use std::process::{Child, Command};
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::{Arc, Mutex};

pub struct Game {
    pub id: &'static str,
    pub name: &'static str,
    pub entry: &'static str,
    /// The Xbox 360 title ID: names the game's folders in its save data.
    pub title_id: &'static str,
    /// Files the port is made from, and their SHA-1. The updates come from the title update.
    disc: &'static [(&'static str, &'static str)],
    update: &'static [(&'static str, &'static str)],
}

pub const GAMES: [Game; 2] = [
    Game {
        id: "graw1",
        name: "Advanced Warfighter",
        entry: "Yeti_Final_Xenon.exe",
        title_id: "555307D5",
        disc: &[("Yeti_Final_Xenon.exe", "5243428702092fa5a99a9ee5e1542ad71b003245")],
        update: &[("Yeti_Final_Xenon.exep", "6a4013a5c5424e1f0f1491554cc2b1fbca8a33c5")],
    },
    Game {
        id: "graw2",
        name: "Advanced Warfighter 2",
        entry: "Yeti_Final_Xenon.xex",
        title_id: "555307F0",
        disc: &[
            ("Yeti_Final_Xenon.xex", "67a3d13a9d599a7cc4f069124a4f46f359ea5a96"),
            ("AI_X360_F.xex", "11d1a4e5cb921fd98aa6eb5f517ba73ed52b5b60"),
        ],
        update: &[
            ("Yeti_Final_Xenon.xexp", "cae14b2961fc822f1da54a5fa6d8605d261466cc"),
            ("AI_X360_F.xexp", "c66f3d1f95ed06b5a5e6a58db7e4baf882c0cb7b"),
        ],
    },
];

#[derive(Clone, Copy, PartialEq, Debug)]
pub enum Ready {
    NoProgram,
    NoDisc,
    NoUpdate,
    Yes,
}

pub fn home() -> PathBuf {
    std::env::current_exe()
        .ok()
        .and_then(|p| p.parent().map(Path::to_path_buf))
        .unwrap_or_else(|| PathBuf::from("."))
}

impl Game {
    pub fn program_dir(&self) -> PathBuf {
        home().join("programs").join(self.id)
    }

    fn program(&self) -> PathBuf {
        self.program_dir().join(format!("{}_campaign.exe", self.id))
    }

    fn data(&self) -> PathBuf {
        home().join("games").join(self.id)
    }

    /// The game's files: the player's own folder if one was chosen.
    pub fn game_dir(&self, s: &Settings) -> PathBuf {
        match s.folders.get(self.id) {
            Some(dir) if !dir.is_empty() => PathBuf::from(dir),
            _ => self.data().join("game"),
        }
    }

    /// The game's save data (ReXGlue's user folder).
    pub fn user_dir(&self) -> PathBuf {
        self.data().join("user")
    }

    /// Saves replaced by an import, newest last.
    pub fn backup_dir(&self) -> PathBuf {
        self.data().join("save backups")
    }

    pub fn log(&self) -> PathBuf {
        self.data().join("game.log")
    }

    /// By the names only: quick enough for every frame.
    pub fn ready(&self, s: &Settings) -> Ready {
        let dir = self.game_dir(s);
        let has = |files: &[(&str, &str)]| files.iter().all(|(n, _)| dir.join(n).is_file());
        if !self.program().is_file() {
            Ready::NoProgram
        } else if !has(self.disc) || !dir.join("Yeti.big").is_file() {
            Ready::NoDisc
        } else if !has(self.update) {
            Ready::NoUpdate
        } else {
            Ready::Yes
        }
    }

    /// Whether the files are the ones the port is made from.
    pub fn verify(&self, s: &Settings) -> Result<(), String> {
        let dir = self.game_dir(s);
        for (name, hash) in self.disc.iter().chain(self.update) {
            if sha1_of(&dir.join(name)).map_err(|e| format!("{name}: {e}"))? != *hash {
                return Err(format!(
                    "{name} is another version than this port is made for. Use the disc and title update it names."
                ));
            }
        }
        Ok(())
    }

    pub fn start(&self, s: &Settings) -> Result<Child, String> {
        let data = self.data();
        for sub in ["user", "cache"] {
            std::fs::create_dir_all(data.join(sub)).map_err(|e| e.to_string())?;
        }
        let keys = self.program_dir().join("grawbox-keys.txt");
        std::fs::write(&keys, s.keys_file()).map_err(|e| e.to_string())?;
        let mut args = vec![
            format!("--title.game_root={}", self.game_dir(s).display()),
            format!("--title.entry={}", self.entry),
            format!("--app.user_root={}", self.user_dir().display()),
            format!("--app.cache_root={}", data.join("cache").display()),
            format!("--log.file_name={}", self.log().display()),
        ];
        args.extend(s.arguments());
        Command::new(self.program())
            .args(args)
            .current_dir(&data)
            .spawn()
            .map_err(|e| format!("{}: {e}", self.program().display()))
    }
}

/// Which game's files a disc or title update holds.
fn recognize(source: &Source) -> Option<&'static Game> {
    GAMES.iter().find(|g| {
        let wanted = if source.is_disc() { g.disc } else { g.update };
        wanted.iter().all(|(n, _)| source.files().contains_key(*n))
            // Both discs have the first game's names in other forms: the entry decides.
            && (!source.is_disc() || source.files().contains_key(g.entry))
    })
}

/// A copy from a disc image or title update, on a thread of its own.
pub struct Import {
    pub game: &'static Game,
    pub done: Arc<AtomicU64>,
    pub total: u64,
    pub stop: Arc<AtomicBool>,
    pub result: Arc<Mutex<Option<Result<String, String>>>>,
}

impl Import {
    pub fn start(path: &Path, s: &Settings) -> Result<Import, String> {
        let mut source = Source::open(path)?;
        let game = recognize(&source)
            .ok_or("this is not a disc or title update of either game's campaign")?;
        let disc = source.is_disc();
        let names: Vec<String> = if disc {
            source.files().keys().cloned().collect()
        } else {
            game.update.iter().map(|(n, _)| n.to_string()).collect()
        };
        let total = names.iter().map(|n| source.files()[n].length).sum();
        let root = game.game_dir(s);
        if let Some(space) = free_space(&root) {
            if space < total + (1 << 28) {
                return Err(format!(
                    "not enough free space: {:.1} GB are needed",
                    (total + (1 << 28)) as f64 / 1e9
                ));
            }
        }
        let import = Import {
            game,
            done: Arc::default(),
            total,
            stop: Arc::default(),
            result: Arc::default(),
        };
        let (done, stop, result) = (import.done.clone(), import.stop.clone(), import.result.clone());
        std::thread::spawn(move || {
            let mut progress = |n: u64| {
                done.fetch_add(n, Ordering::Relaxed);
                !stop.load(Ordering::Relaxed)
            };
            let outcome = names.iter().try_for_each(|n| {
                source.write(n, &root, &mut progress).map_err(|e| format!("{n}: {e}"))
            });
            let kind = if disc { "Disc" } else { "Title update" };
            *result.lock().unwrap() = Some(outcome.map(|_| format!("{kind} of {} copied", game.name)));
        });
        Ok(import)
    }
}

#[cfg(windows)]
fn free_space(path: &Path) -> Option<u64> {
    use std::os::windows::ffi::OsStrExt;
    #[link(name = "kernel32")]
    extern "system" {
        fn GetDiskFreeSpaceExW(dir: *const u16, free: *mut u64, total: *mut u64, all: *mut u64) -> i32;
    }
    let existing = path.ancestors().find(|p| p.is_dir())?;
    let wide: Vec<u16> = existing.as_os_str().encode_wide().chain([0]).collect();
    let mut free = 0;
    let null = std::ptr::null_mut();
    // SAFETY: `wide` ends in a zero and `free` outlives the call.
    (unsafe { GetDiskFreeSpaceExW(wide.as_ptr(), &mut free, null, null) } != 0).then_some(free)
}

#[cfg(not(windows))]
fn free_space(_: &Path) -> Option<u64> {
    None
}
