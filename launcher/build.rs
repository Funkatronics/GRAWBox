// GRAWBOX_BUILD: `git describe --tags --always --dirty` of the source (for example
// v0.2.0-12-gabc1234), shown in the launcher and put in playtest reports, as the games log theirs
// (games/common/build_id.cmake). GRAWBOX_REPO: the GitHub repository the source was cloned from
// (its `origin`), for the playtest issue form; empty if that isn't a GitHub URL.
use std::process::Command;

fn git(args: &[&str]) -> Option<String> {
    Command::new("git")
        .args(args)
        .output()
        .ok()
        .filter(|o| o.status.success())
        .and_then(|o| String::from_utf8(o.stdout).ok())
        .map(|s| s.trim().to_string())
        .filter(|s| !s.is_empty())
}

fn main() {
    let id = git(&["describe", "--tags", "--always", "--dirty"]).unwrap_or_else(|| "unknown".into());
    println!("cargo:rustc-env=GRAWBOX_BUILD={id}");
    // Any GitHub remote form: https:// or http:// (with or without a user), ssh://, git://, or the
    // scp-like "<user> at github.com:<owner>/<repo>". The part after the host is the path.
    let repo = git(&["remote", "get-url", "origin"])
        .and_then(|url| {
            let after_scheme = url.split_once("://").map_or(url.as_str(), |(_, rest)| rest);
            let host_and_path = after_scheme.rsplit_once('@').map_or(after_scheme, |(_, rest)| rest);
            let path = host_and_path
                .strip_prefix("github.com/")
                .or_else(|| host_and_path.strip_prefix("github.com:"))?;
            Some(format!("https://github.com/{}", path.trim_end_matches(".git").trim_end_matches('/')))
        })
        .unwrap_or_default();
    println!("cargo:rustc-env=GRAWBOX_REPO={repo}");
    // Again when the commit, the index or the sources change (a "-dirty" build id).
    println!("cargo:rerun-if-changed=../.git/HEAD");
    println!("cargo:rerun-if-changed=../.git/index");
    println!("cargo:rerun-if-changed=../.git/refs");
    println!("cargo:rerun-if-changed=../.git/config");  // a changed remote
    println!("cargo:rerun-if-changed=src");
}
