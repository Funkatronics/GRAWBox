//! Reads the player's own disc image (XDVDFS) or title update (STFS). As tools/x360_extract.py.
//! The input is only ever read.

use crate::sha1::Sha1;
use std::collections::{BTreeMap, HashSet};
use std::fs::{self, File};
use std::io::{self, Read, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};

const SECTOR: u64 = 2048;
const BLOCK: u64 = 4096;
const XDVDFS_BASES: [u64; 4] = [0, 0x0FD9_0000, 0x0208_0000, 0x1830_0000];
const HASHES_PER_TABLE: u64 = 170;

type Result<T> = std::result::Result<T, String>;

/// Where a file is and how long it is.
#[derive(Clone, Copy)]
pub struct Entry {
    start: u64,
    pub length: u64,
}

pub enum Source {
    Disc { file: File, base: u64, files: BTreeMap<String, Entry> },
    Update { file: File, layout: Stfs, files: BTreeMap<String, Entry> },
}

#[derive(Clone)]
pub struct Stfs {
    data_start: u64,
    table_blocks: u64,
    table_start: u64,
}

fn read_at(file: &mut File, at: u64, size: usize) -> Result<Vec<u8>> {
    let mut out = vec![0; size];
    file.seek(SeekFrom::Start(at)).map_err(text)?;
    file.read_exact(&mut out).map_err(text)?;
    Ok(out)
}

fn text(e: io::Error) -> String {
    e.to_string()
}

fn le16(b: &[u8], at: usize) -> u64 {
    u16::from_le_bytes([b[at], b[at + 1]]) as u64
}

fn le32(b: &[u8], at: usize) -> u64 {
    u32::from_le_bytes([b[at], b[at + 1], b[at + 2], b[at + 3]]) as u64
}

fn le24(b: &[u8], at: usize) -> u64 {
    u32::from_le_bytes([b[at], b[at + 1], b[at + 2], 0]) as u64
}

fn be32(b: &[u8], at: usize) -> u64 {
    u32::from_be_bytes([b[at], b[at + 1], b[at + 2], b[at + 3]]) as u64
}

fn latin1(b: &[u8]) -> String {
    b.iter().map(|&c| c as char).collect()
}

impl Source {
    pub fn open(path: &Path) -> Result<Source> {
        let mut file = File::open(path).map_err(text)?;
        let magic = read_at(&mut file, 0, 4).map_err(|_| "the file is empty".to_string())?;
        if [&b"LIVE"[..], b"PIRS", b"CON "].contains(&&magic[..]) {
            let layout = Stfs::open(&mut file)?;
            let files = layout.files(&mut file)?;
            return Ok(Source::Update { file, layout, files });
        }
        for base in XDVDFS_BASES {
            let Ok(header) = read_at(&mut file, base + 32 * SECTOR, 28) else { continue };
            if &header[..20] != b"MICROSOFT*XBOX*MEDIA" {
                continue;
            }
            let mut files = BTreeMap::new();
            let mut seen = HashSet::new();
            directory(&mut file, base, le32(&header, 20), le32(&header, 24), "", &mut files, &mut seen)?;
            return Ok(Source::Disc { file, base, files });
        }
        Err("not an Xbox 360 disc image or title update".into())
    }

    pub fn is_disc(&self) -> bool {
        matches!(self, Source::Disc { .. })
    }

    pub fn files(&self) -> &BTreeMap<String, Entry> {
        match self {
            Source::Disc { files, .. } | Source::Update { files, .. } => files,
        }
    }

    /// Writes one file under `root`. `progress` gets the bytes written and returns false to stop.
    pub fn write(
        &mut self,
        name: &str,
        root: &Path,
        progress: &mut dyn FnMut(u64) -> bool,
    ) -> Result<()> {
        let entry = *self.files().get(name).ok_or("no such file")?;
        let target = safe_target(root, name)?;
        fs::create_dir_all(target.parent().unwrap()).map_err(text)?;
        let partial = target.with_file_name(format!(
            "{}.partial",
            target.file_name().unwrap().to_string_lossy()
        ));
        let mut out = File::create(&partial).map_err(text)?;
        let mut emit = |data: &[u8]| -> Result<()> {
            out.write_all(data).map_err(text)?;
            if progress(data.len() as u64) {
                Ok(())
            } else {
                Err("stopped".into())
            }
        };
        let result = match self {
            Source::Disc { file, base, .. } => copy(file, *base + entry.start * SECTOR, entry.length, &mut emit),
            Source::Update { file, layout, .. } => {
                let mut left = entry.length;
                layout.chain(file, entry.start, entry.length.div_ceil(BLOCK), &mut |data| {
                    let n = left.min(BLOCK) as usize;
                    left -= n as u64;
                    emit(&data[..n])
                })
            }
        };
        drop(out);
        if let Err(e) = result {
            let _ = fs::remove_file(&partial);
            return Err(e);
        }
        fs::rename(&partial, &target).map_err(text)
    }
}

fn copy(file: &mut File, at: u64, length: u64, emit: &mut dyn FnMut(&[u8]) -> Result<()>) -> Result<()> {
    file.seek(SeekFrom::Start(at)).map_err(text)?;
    let mut buffer = vec![0; 1 << 20];
    let mut left = length;
    while left > 0 {
        let n = left.min(buffer.len() as u64) as usize;
        file.read_exact(&mut buffer[..n]).map_err(|_| "the image is cut short".to_string())?;
        emit(&buffer[..n])?;
        left -= n as u64;
    }
    Ok(())
}

/// A name from the image under `root`, refusing anything that would leave it.
fn safe_target(root: &Path, name: &str) -> Result<PathBuf> {
    let mut target = root.to_path_buf();
    for part in name.split('/') {
        let bad = part.is_empty()
            || part == "."
            || part == ".."
            || part.ends_with(['.', ' '])
            || part.chars().any(|c| "\\:*?\"<>|".contains(c) || c < ' ');
        if bad {
            return Err(format!("unsafe name in the image: {name:?}"));
        }
        target.push(part);
    }
    Ok(target)
}

fn directory(
    file: &mut File,
    base: u64,
    sector: u64,
    size: u64,
    prefix: &str,
    found: &mut BTreeMap<String, Entry>,
    visited: &mut HashSet<u64>,
) -> Result<()> {
    if size == 0 || !visited.insert(sector) {
        return Ok(());
    }
    if size > 1 << 24 {
        return Err("the image has a damaged folder".into());
    }
    let table = read_at(file, base + sector * SECTOR, size as usize)?;
    let mut pending = vec![0usize];
    let mut seen = HashSet::new();
    while let Some(at) = pending.pop() {
        if at + 14 > table.len() || !seen.insert(at) {
            continue;
        }
        let (left, right) = (le16(&table, at), le16(&table, at + 2));
        if left == 0xFFFF {
            continue;
        }
        let (start, length) = (le32(&table, at + 4), le32(&table, at + 8));
        let end = at + 14 + table[at + 13] as usize;
        if end > table.len() {
            return Err("the image has a damaged folder".into());
        }
        let name = format!("{prefix}{}", latin1(&table[at + 14..end]));
        if table[at + 12] & 0x10 != 0 {
            directory(file, base, start, length, &format!("{name}/"), found, visited)?;
        } else {
            found.insert(name, Entry { start, length });
        }
        pending.extend([left, right].iter().filter(|&&c| c != 0).map(|&c| c as usize * 4));
    }
    Ok(())
}

impl Stfs {
    fn open(file: &mut File) -> Result<Stfs> {
        let header = read_at(file, 0, 0x400)?;
        if header[0x37B] & 1 == 0 {
            return Err("only title update packages are supported".into());
        }
        Ok(Stfs {
            data_start: be32(&header, 0x340).div_ceil(BLOCK) * BLOCK,
            table_blocks: le16(&header, 0x37C),
            table_start: le24(&header, 0x37E),
        })
    }

    fn offset(&self, block: u64) -> u64 {
        let (mut physical, mut span) = (block, HASHES_PER_TABLE);
        for _ in 0..3 {
            physical += (block + span) / span;
            if block < span {
                break;
            }
            span *= HASHES_PER_TABLE;
        }
        self.data_start + physical * BLOCK
    }

    /// The blocks from `block` on, each checked against its hash.
    fn chain(
        &self,
        file: &mut File,
        mut block: u64,
        count: u64,
        emit: &mut dyn FnMut(&[u8]) -> Result<()>,
    ) -> Result<()> {
        for _ in 0..count {
            let first = block - block % HASHES_PER_TABLE;
            let record = read_at(file, self.offset(first) - BLOCK + (block - first) * 24, 24)?;
            let data = read_at(file, self.offset(block), BLOCK as usize)?;
            let mut hash = Sha1::new();
            hash.update(&data);
            if hash.finish()[..] != record[..20] {
                return Err("the title update is damaged".into());
            }
            emit(&data)?;
            block = be32(&[0, record[21], record[22], record[23]], 0);
        }
        Ok(())
    }

    fn files(&self, file: &mut File) -> Result<BTreeMap<String, Entry>> {
        let mut table = Vec::new();
        self.chain(file, self.table_start, self.table_blocks, &mut |d| {
            table.extend_from_slice(d);
            Ok(())
        })?;
        let mut entries = Vec::new();
        for r in table.chunks_exact(64) {
            let n = (r[0x28] & 0x3F) as usize;
            if n == 0 {
                break;
            }
            let parent = i16::from_be_bytes([r[0x32], r[0x33]]);
            entries.push((latin1(&r[..n]), r[0x28] & 0x80 != 0, le24(r, 0x2F), parent, be32(r, 0x34)));
        }
        let mut found = BTreeMap::new();
        for (name, is_directory, start, parent, length) in &entries {
            if *is_directory {
                continue;
            }
            let (mut name, mut parent, mut depth) = (name.clone(), *parent, 0);
            while parent != -1 {
                let above = entries.get(parent as usize).ok_or("the title update is damaged")?;
                depth += 1;
                if depth > entries.len() {
                    return Err("the title update is damaged".into());
                }
                name = format!("{}/{name}", above.0);
                parent = above.3;
            }
            found.insert(name, Entry { start: *start, length: *length });
        }
        Ok(found)
    }
}

pub fn sha1_of(path: &Path) -> Result<String> {
    let mut file = File::open(path).map_err(text)?;
    let mut hash = Sha1::new();
    let mut buffer = vec![0; 1 << 20];
    loop {
        let n = file.read(&mut buffer).map_err(text)?;
        if n == 0 {
            return Ok(crate::sha1::hex(&hash.finish()));
        }
        hash.update(&buffer[..n]);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn names_stay_inside() {
        let root = Path::new("root");
        assert!(safe_target(root, "data/a.big").is_ok());
        for bad in ["../a", "a/../b", "a\\b", "c:x", "a//b", "", "a/.", "nul\0"] {
            assert!(safe_target(root, bad).is_err(), "{bad:?}");
        }
    }
}

/// With GRAWBOX_TEST_SOURCE=<image or update>: its listing, and with GRAWBOX_TEST_OUT=<folder> also
/// every file written there. For comparing with tools/x360_extract.py.
#[cfg(test)]
#[test]
#[ignore]
fn player_files() {
    let source = std::env::var("GRAWBOX_TEST_SOURCE").expect("GRAWBOX_TEST_SOURCE");
    let mut s = Source::open(Path::new(&source)).unwrap();
    let names: Vec<String> = s.files().keys().cloned().collect();
    for n in &names {
        println!("{:>12}  {n}", s.files()[n].length);
    }
    if let Ok(out) = std::env::var("GRAWBOX_TEST_OUT") {
        for n in &names {
            s.write(n, Path::new(&out), &mut |_| true).unwrap();
        }
    }
}
