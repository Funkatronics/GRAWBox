"""Read a player's own Xbox 360 disc image (XDVDFS) or title update (STFS LIVE/PIRS/CON).

    python -I tools/x360_extract.py list <image or package>
    python -I tools/x360_extract.py extract <image or package> <output folder>

The input is only ever read. Output must go to an untracked folder.
"""

import hashlib
import os
import struct
import sys
from pathlib import Path

SECTOR = 2048
BLOCK = 4096
XDVDFS_MAGIC = b"MICROSOFT*XBOX*MEDIA"
XDVDFS_BASES = (0x0, 0x0FD90000, 0x02080000, 0x18300000)
STFS_MAGICS = (b"LIVE", b"PIRS", b"CON ")
HASHES_PER_TABLE = 170


class FormatError(Exception):
    pass


def safe_target(root, relative):
    """Resolve a name from the image under root, refusing anything that would leave it."""
    root = Path(root).resolve()
    parts = relative.split("/")
    for part in parts:
        if part in ("", ".", "..") or any(c in part for c in '\\:*?"<>|\0'):
            raise FormatError(f"unsafe name in image: {relative!r}")
    target = root.joinpath(*parts).resolve()
    if root not in target.parents:
        raise FormatError(f"name leaves the output folder: {relative!r}")
    return target


class Xdvdfs:
    def __init__(self, stream):
        self.stream = stream
        for base in XDVDFS_BASES:
            stream.seek(base + 32 * SECTOR)
            header = stream.read(28)
            if header[:20] == XDVDFS_MAGIC:
                self.base = base
                self.root = struct.unpack_from("<II", header, 20)
                return
        raise FormatError("no XDVDFS partition")

    def files(self):
        found = {}
        self._directory(self.root, "", found, set())
        return found

    def _directory(self, extent, prefix, found, visited):
        sector, size = extent
        if size == 0 or sector in visited:
            return
        visited.add(sector)
        self.stream.seek(self.base + sector * SECTOR)
        table = self.stream.read(size)
        pending, seen = [0], set()
        while pending:
            at = pending.pop()
            if at in seen or at + 14 > len(table):
                continue
            seen.add(at)
            left, right, start, length, attributes, name_length = struct.unpack_from(
                "<HHIIBB", table, at)
            if left == 0xFFFF:
                continue
            name = prefix + table[at + 14:at + 14 + name_length].decode("latin-1")
            if attributes & 0x10:
                self._directory((start, length), name + "/", found, visited)
            else:
                found[name] = (start, length)
            pending += [child * 4 for child in (left, right) if child]

    def chunks(self, entry):
        start, length = entry
        self.stream.seek(self.base + start * SECTOR)
        while length:
            data = self.stream.read(min(length, 1 << 24))
            if not data:
                raise FormatError("image is truncated")
            length -= len(data)
            yield data


class Stfs:
    def __init__(self, stream):
        self.stream = stream
        stream.seek(0)
        header = stream.read(0x400)
        if header[:4] not in STFS_MAGICS:
            raise FormatError("not an STFS package")
        header_size = struct.unpack_from(">I", header, 0x340)[0]
        self.data_start = (header_size + BLOCK - 1) // BLOCK * BLOCK
        # Read-only packages keep one copy of each hash table, others two.
        self.tables = 1 if header[0x37B] & 1 else 2
        self.table_blocks = struct.unpack_from("<H", header, 0x37C)[0]
        self.table_start = int.from_bytes(header[0x37E:0x381], "little")
        if self.tables != 1:
            raise FormatError("only read-only (title update) packages are supported")

    def _offset(self, block):
        physical, span = block, HASHES_PER_TABLE
        for _ in range(3):
            physical += (block + span) // span * self.tables
            if block < span:
                break
            span *= HASHES_PER_TABLE
        return self.data_start + physical * BLOCK

    def _read_block(self, block):
        """Return (data, next block), checking the block against its SHA-1."""
        first = block - block % HASHES_PER_TABLE
        self.stream.seek(self._offset(first) - BLOCK + (block - first) * 24)
        record = self.stream.read(24)
        self.stream.seek(self._offset(block))
        data = self.stream.read(BLOCK)
        if hashlib.sha1(data).digest() != record[:20]:
            raise FormatError(f"block {block} does not match its hash")
        return data, int.from_bytes(record[21:24], "big")

    def _chain(self, block, count):
        for _ in range(count):
            data, following = self._read_block(block)
            yield data
            block = following

    def files(self):
        table = b"".join(self._chain(self.table_start, self.table_blocks))
        entries = []
        for at in range(0, len(table), 64):
            record = table[at:at + 64]
            name_length = record[0x28] & 0x3F
            if not name_length:
                break
            entries.append((
                record[:name_length].decode("latin-1"),
                bool(record[0x28] & 0x80),
                int.from_bytes(record[0x2F:0x32], "little"),
                struct.unpack_from(">h", record, 0x32)[0],
                struct.unpack_from(">I", record, 0x34)[0],
            ))
        found = {}
        for name, is_directory, start, parent, length in entries:
            if is_directory:
                continue
            depth = 0
            while parent != -1:
                if not 0 <= parent < len(entries) or depth > len(entries):
                    raise FormatError("bad parent in file table")
                name = entries[parent][0] + "/" + name
                parent = entries[parent][3]
                depth += 1
            found[name] = (start, length)
        return found

    def chunks(self, entry):
        start, length = entry
        for data in self._chain(start, (length + BLOCK - 1) // BLOCK):
            yield data[:length]
            length -= min(length, BLOCK)


def open_source(stream):
    stream.seek(0)
    return Stfs(stream) if stream.read(4) in STFS_MAGICS else Xdvdfs(stream)


def main(arguments):
    if len(arguments) not in (2, 3) or arguments[0] != ("list", "extract")[len(arguments) - 2]:
        print(__doc__)
        return 2
    with open(arguments[1], "rb") as stream:
        source = open_source(stream)
        files = source.files()
        total = sum(length for _, length in files.values())
        if arguments[0] == "list":
            for name, (_, length) in sorted(files.items()):
                print(f"{length:>12}  {name}")
        else:
            for name, entry in sorted(files.items(), key=lambda item: item[1][0]):
                target = safe_target(arguments[2], name)
                target.parent.mkdir(parents=True, exist_ok=True)
                partial = target.with_name(target.name + ".partial")
                with open(partial, "wb") as output:
                    for data in source.chunks(entry):
                        output.write(data)
                if partial.stat().st_size != entry[1]:
                    raise FormatError(f"short file: {name}")
                os.replace(partial, target)
        print(f"{len(files)} files, {total} bytes")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except (FormatError, OSError) as error:
        sys.exit(f"error: {error}")
