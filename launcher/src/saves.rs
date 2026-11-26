//! Save data: each game's user folder (games/<game>/user), as ReXGlue keeps it: the profile under
//! <title id>/profile and the saves under B13EBABEBABEBABE/<title id>. Exported and imported as plain folders.

use std::fs;
use std::io;
use std::path::{Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};

/// The folder of the content (saves) of all profiles.
const CONTENT: &str = "B13EBABEBABEBABE";

/// Whether `dir` holds save data of the game with `title_id`: at its top, or one folder down (an
/// export copied into another folder).
pub fn find_save(dir: &Path, title_id: &str) -> Option<PathBuf> {
    let holds = |d: &Path| d.join(title_id).is_dir() || d.join(CONTENT).join(title_id).is_dir();
    if holds(dir) {
        return Some(dir.to_path_buf());
    }
    fs::read_dir(dir).ok()?.flatten().map(|e| e.path()).find(|p| p.is_dir() && holds(p))
}

/// The newest change in the save folder, for display; None without save data.
pub fn last_saved(user: &Path, title_id: &str) -> Option<SystemTime> {
    find_save(user, title_id)?;
    newest(&user.join(CONTENT).join(title_id)).or_else(|| newest(user))
}

fn newest(dir: &Path) -> Option<SystemTime> {
    let mut best = None;
    for entry in fs::read_dir(dir).ok()?.flatten() {
        let path = entry.path();
        let time = if path.is_dir() { newest(&path) } else { entry.metadata().ok()?.modified().ok() };
        best = best.max(time);
    }
    best
}

fn copy_tree(from: &Path, to: &Path) -> io::Result<()> {
    fs::create_dir_all(to)?;
    for entry in fs::read_dir(from)? {
        let entry = entry?;
        let target = to.join(entry.file_name());
        if entry.file_type()?.is_dir() {
            copy_tree(&entry.path(), &target)?;
        } else {
            fs::copy(entry.path(), &target)?;
        }
    }
    Ok(())
}

/// UTC, "2026-11-25 1930": sorts by time and is valid in a file name.
pub fn stamp(time: SystemTime) -> String {
    let secs = time.duration_since(UNIX_EPOCH).map_or(0, |d| d.as_secs());
    let (days, rest) = ((secs / 86400) as i64, secs % 86400);
    // Civil date from days since 1970-01-01 (Howard Hinnant's algorithm).
    let z = days + 719468;
    let era = z.div_euclid(146097);
    let doe = z - era * 146097;
    let yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let day = doy - (153 * mp + 2) / 5 + 1;
    let month = if mp < 10 { mp + 3 } else { mp - 9 };
    let year = yoe + era * 400 + i64::from(month <= 2);
    format!("{year:04}-{month:02}-{day:02} {:02}{:02}", rest / 3600, rest % 3600 / 60)
}

/// Copies the save to a new folder in `into`; returns it.
pub fn export(user: &Path, title_id: &str, game: &str, into: &Path) -> Result<PathBuf, String> {
    if find_save(user, title_id).is_none() {
        return Err(format!("{game} has no save yet."));
    }
    let base = format!("GRAWBox {game} save {}", stamp(SystemTime::now()));
    let mut target = into.join(&base);
    let mut n = 2;
    while target.exists() {
        target = into.join(format!("{base} ({n})"));
        n += 1;
    }
    copy_tree(user, &target).map_err(|e| e.to_string())?;
    Ok(target)
}

/// Replaces the save with the one in `from`, after moving the current one to
/// `backups/<stamp>`. Returns the backup, if there was a save to back up.
pub fn import(user: &Path, title_id: &str, game: &str, from: &Path, backups: &Path) -> Result<Option<PathBuf>, String> {
    let source = find_save(from, title_id).ok_or_else(|| format!("That folder holds no {game} save."))?;
    if source.starts_with(user) || user.starts_with(&source) {
        return Err("That is the save in use.".into());
    }
    let staged = user.with_extension("importing");
    let _ = fs::remove_dir_all(&staged);
    copy_tree(&source, &staged).map_err(|e| e.to_string())?;
    let mut backup = None;
    if user.exists() {
        fs::create_dir_all(backups).map_err(|e| e.to_string())?;
        let mut target = backups.join(stamp(SystemTime::now()));
        let mut n = 2;
        while target.exists() {
            target = backups.join(format!("{} ({n})", stamp(SystemTime::now())));
            n += 1;
        }
        fs::rename(user, &target).map_err(|e| e.to_string())?;
        backup = Some(target);
    }
    if let Err(e) = fs::rename(&staged, user) {
        // Put the old save back.
        if let Some(b) = &backup {
            let _ = fs::rename(b, user);
        }
        return Err(e.to_string());
    }
    Ok(backup)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn temp(name: &str) -> PathBuf {
        let dir = std::env::temp_dir().join(format!("grawbox-saves-{name}-{}", std::process::id()));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        dir
    }

    fn save(dir: &Path, text: &str) {
        let d = dir.join(CONTENT).join("555307D5").join("00000001").join("GRAW");
        fs::create_dir_all(&d).unwrap();
        fs::write(d.join("GRAW.sav"), text).unwrap();
    }

    fn read(dir: &Path) -> String {
        fs::read_to_string(dir.join(CONTENT).join("555307D5/00000001/GRAW/GRAW.sav")).unwrap()
    }

    #[test]
    fn stamps() {
        assert_eq!(stamp(UNIX_EPOCH), "1970-01-01 0000");
        assert_eq!(stamp(UNIX_EPOCH + std::time::Duration::from_secs(1_772_000_000)), "2026-02-25 0613");
    }

    #[test]
    fn export_then_import() {
        let root = temp("roundtrip");
        let user = root.join("user");
        save(&user, "first");
        let out = root.join("out");
        fs::create_dir_all(&out).unwrap();
        let exported = export(&user, "555307D5", "GRAW", &out).unwrap();
        assert_eq!(read(&exported), "first");
        assert!(last_saved(&user, "555307D5").is_some());

        save(&user, "second");
        // An export found one folder down, as a player might pick the folder it is in.
        let backup = import(&user, "555307D5", "GRAW", &out, &root.join("backups")).unwrap().unwrap();
        assert_eq!(read(&user), "first");
        assert_eq!(read(&backup), "second");
        assert!(!user.with_extension("importing").exists());
        let _ = fs::remove_dir_all(&root);
    }

    #[test]
    fn refuses_other_folders() {
        let root = temp("refuse");
        let user = root.join("user");
        save(&user, "kept");
        let other = root.join("other");
        fs::create_dir_all(other.join("555307F0")).unwrap();
        assert!(import(&user, "555307D5", "GRAW", &other, &root.join("b")).is_err());
        assert!(import(&user, "555307D5", "GRAW", &user, &root.join("b")).is_err());
        assert!(export(&root.join("none"), "555307D5", "GRAW", &root).is_err());
        assert_eq!(read(&user), "kept");
        let _ = fs::remove_dir_all(&root);
    }
}
