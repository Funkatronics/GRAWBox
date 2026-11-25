"""Refuse commits that would publish game files, generated code, secrets or personal details.

    python -I tools/check_commit.py              check what is staged
    python -I tools/check_commit.py --all        check every tracked file
    python -I tools/check_commit.py --message F  check a commit message file
    python -I tools/check_commit.py --install    install the Git hooks for this clone

Private words (names, host names) go one per line in .git/private-words, which Git never tracks.
"""

import os
import re
import subprocess
import sys
from pathlib import Path

MAX_BYTES = 512 * 1024
ALLOWED_SUFFIXES = {
    ".md", ".txt", ".py", ".ps1", ".sh", ".bat", ".toml", ".json", ".cmake", ".yml", ".yaml",
    ".cpp", ".cc", ".c", ".h", ".hpp", ".inl", ".java", ".kt", ".kts", ".gradle", ".xml",
    ".hlsl", ".glsl", ".rc", ".in", ".diff", ".patch", ".svg", ".properties",
}
ALLOWED_NAMES = {"LICENSE", "NOTICE", ".gitignore", ".gitattributes", "CMakeLists.txt"}
BLOCKED_PARTS = {"private", "generated", "assets", "game", "out", "build", ".tools", "captures",
                 "title-updates", "logs"}
BLOCKED_MAGIC = {
    b"XEX1": "Xbox 360 executable", b"XEX2": "Xbox 360 executable", b"XBEH": "Xbox executable",
    b"LIVE": "Xbox 360 package", b"PIRS": "Xbox 360 package", b"CON ": "Xbox 360 package",
    b"MZ": "Windows program", b"\x7fELF": "Linux or Android program", b"BIK": "game video",
    b"RIFF": "media file", b"PK\x03\x04": "archive", b"7z\xbc\xaf": "archive",
}
PATTERNS = [
    (re.compile(r"\b[A-Za-z]:[\\/](?!/)[A-Za-z0-9_.$ -]"), "absolute Windows path"),
    (re.compile(r"(?<![\w.])/(?:home|Users)/[A-Za-z0-9_.-]+"), "home folder path"),
    (re.compile(r"[A-Za-z0-9._%+-]+@(?!users\.noreply\.github\.com)[A-Za-z0-9-]+\.[A-Za-z.]{2,}"),
     "e-mail address"),
    (re.compile(r"\b(?:gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,})"), "GitHub token"),
    (re.compile(r"-----BEGIN [A-Z ]*PRIVATE KEY-----"), "private key"),
    (re.compile(r"\b(?:DEFINE_REX_FUNC|REX_LOAD_U32|REX_STORE_U32)\("), "translated game code"),
    (re.compile(r"(?:0x[0-9A-Fa-f]{2},\s*){64}"), "large block of raw bytes"),
]
# Guest device names such as D:\Dummy are game facts, not paths on anyone's computer.
ALLOWED_TEXT = re.compile(r"D:\\Dummy|[dg]ame:\\|update:\\", re.IGNORECASE)
# This file holds the patterns it searches for.
SELF = "tools/check_commit.py"


def git(*arguments, binary=False):
    result = subprocess.run(["git", *arguments], capture_output=True, check=True)
    return result.stdout if binary else result.stdout.decode("utf-8", "replace")


def private_words():
    path = Path(git("rev-parse", "--git-path", "private-words").strip())
    if not path.is_file():
        sys.exit("check_commit: .git/private-words is missing; add one private word per line")
    words = [line.strip() for line in path.read_text("utf-8").splitlines() if line.strip()]
    if not words:
        sys.exit("check_commit: .git/private-words is empty")
    return [re.compile(r"(?<![A-Za-z0-9])" + re.escape(w) + r"(?![A-Za-z0-9])", re.IGNORECASE)
            for w in words]


def check_text(text, words, where, problems, skip_patterns=False):
    for number, line in enumerate(text.splitlines(), 1):
        if not skip_patterns:
            cleaned = ALLOWED_TEXT.sub("", line)
            for pattern, name in PATTERNS:
                if pattern.search(cleaned):
                    problems.append(f"{where}:{number}: {name}")
        # The word itself is never printed.
        if any(word.search(line) for word in words):
            problems.append(f"{where}:{number}: private word")


def check_file(path, data, words, problems):
    parts = path.split("/")
    blocked = BLOCKED_PARTS.intersection(part.lower() for part in parts[:-1])
    if blocked:
        problems.append(f"{path}: folder '{sorted(blocked)[0]}' must not be committed")
    name = parts[-1]
    if name not in ALLOWED_NAMES and os.path.splitext(name)[1].lower() not in ALLOWED_SUFFIXES:
        problems.append(f"{path}: file type is not on the allowed list")
    if len(data) > MAX_BYTES:
        problems.append(f"{path}: larger than {MAX_BYTES // 1024} KB")
    for magic, kind in BLOCKED_MAGIC.items():
        if data.startswith(magic) and (len(magic) > 2 or b"\0" in data[:512]):
            problems.append(f"{path}: {kind}")
    if b"\0" in data:
        problems.append(f"{path}: binary file")
        return
    check_text(path, words, path, problems, True)
    check_text(data.decode("utf-8", "replace"), words, path, problems, path == SELF)


def check_files(everything):
    words, problems = private_words(), []
    if everything:
        names = git("ls-files", "-z")
    else:
        names = git("diff", "--cached", "--name-only", "--diff-filter=ACMRT", "-z")
        for kind in ("GIT_AUTHOR_IDENT", "GIT_COMMITTER_IDENT"):
            result = subprocess.run(["git", "var", kind], capture_output=True)
            identity = result.stdout.decode("utf-8", "replace")
            if not re.search(r"<[^>]+@users\.noreply\.github\.com>", identity):
                problems.append(f"{kind}: e-mail is not a GitHub no-reply address")
    for path in filter(None, names.split("\0")):
        mode = git("ls-files", "-s", "--", path).split(" ", 1)[0]
        if mode not in ("100644", "100755"):
            problems.append(f"{path}: links and submodules are not allowed")
            continue
        check_file(path, git("show", f":{path}", binary=True), words, problems)
    return problems


def check_message(path):
    words, problems = private_words(), []
    text = Path(path).read_text("utf-8")
    lines = [line for line in text.splitlines() if line.strip() and not line.startswith("#")]
    if len(lines) != 1:
        problems.append("message: must be one line")
    if lines and len(lines[0]) > 72:
        problems.append("message: longer than 72 characters")
    if re.search(r"co-authored-by|generated with|claude|anthropic", text, re.IGNORECASE):
        problems.append("message: no attribution lines")
    check_text("\n".join(lines), words, "message", problems)
    return problems


def install():
    hooks = Path(git("rev-parse", "--git-path", "hooks").strip())
    hooks.mkdir(parents=True, exist_ok=True)
    python = Path(sys.executable).as_posix()
    for hook, arguments in (("pre-commit", ""), ("commit-msg", ' --message "$1"')):
        target = hooks / hook
        target.write_text(f'#!/bin/sh\nexec "{python}" -I tools/check_commit.py{arguments}\n',
                          "utf-8", newline="\n")
        target.chmod(0o755)
    print("hooks installed")


def main(arguments):
    if arguments == ["--install"]:
        install()
        return 0
    if len(arguments) == 2 and arguments[0] == "--message":
        problems = check_message(arguments[1])
    elif arguments in ([], ["--all"]):
        problems = check_files(bool(arguments))
    else:
        print(__doc__)
        return 2
    for problem in problems:
        print("BLOCKED", problem, file=sys.stderr)
    if not problems:
        print("check_commit: ok")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
