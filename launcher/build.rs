// GRAWBOX_BUILD: `git describe --tags --always --dirty` of the source (for example
// v0.2.0-12-gabc1234), shown in the launcher and put in playtest reports, as the games log theirs
// (games/common/build_id.cmake).
use std::process::Command;

fn main() {
    let id = Command::new("git")
        .args(["describe", "--tags", "--always", "--dirty"])
        .output()
        .ok()
        .filter(|o| o.status.success())
        .and_then(|o| String::from_utf8(o.stdout).ok())
        .map(|s| s.trim().to_string())
        .filter(|s| !s.is_empty())
        .unwrap_or_else(|| "unknown".into());
    println!("cargo:rustc-env=GRAWBOX_BUILD={id}");
    // Again when the checked-out commit or the index (a new commit, a change staged) changes.
    println!("cargo:rerun-if-changed=../.git/HEAD");
    println!("cargo:rerun-if-changed=../.git/index");
    println!("cargo:rerun-if-changed=../.git/refs");
}
