"""Minimal reader of BioShock Infinite's Unreal Engine 3 packages (.xxx).

Cooked packages are LZO compressed (CompressionFlags = 2), split into chunks. Only what is asked
for is inflated; the tables (names, imports, exports) are in the first chunk.
Dependency: lzallright (requirements-dev.txt).

    from upk import Package
    pkg = Package(r"...\\S_twn_FairStreet_C1b_Geo.xxx")
    for exp in pkg.exports: print(pkg.path_of(exp), pkg.class_of(exp), pkg.archetype_of(exp))
"""

import struct
from dataclasses import dataclass

from lzallright import LZOCompressor

TAG = 0x9E2A83C1
COMPRESS_LZO = 2


@dataclass
class Import:
    class_package: str
    class_name: str
    outer: int
    name: str


@dataclass
class Export:
    class_index: int
    super_index: int
    outer: int
    name: str
    archetype: int
    serial_size: int
    serial_offset: int


class Package:
    def __init__(self, path: str):
        self.path = path
        with open(path, "rb") as f:
            self.raw = f.read()
        self._parse_summary()
        self._cache: dict[int, bytes] = {}
        self.names = self._read_names()
        self.imports = self._read_imports()
        self.exports = self._read_exports()

    def _parse_summary(self) -> None:
        d = self.raw
        tag, self.version, self.licensee = struct.unpack_from("<IHH", d, 0)
        if tag != TAG:
            raise ValueError("not a UE3 package")
        # +8 TotalHeaderSize, +12 BioShock-specific field, +16 FolderName (FString)
        folder_len = struct.unpack_from("<i", d, 16)[0]
        o = 20 + folder_len
        (
            self.package_flags,
            self.name_count,
            self.name_offset,
            self.export_count,
            self.export_offset,
            self.import_count,
            self.import_offset,
        ) = struct.unpack_from("<7I", d, o)
        # The chunk table follows BioShock-specific fields: it is recognized by its first chunk,
        # which starts exactly at NameOffset, preceded by the chunk count and the LZO flag.
        self.chunks = []
        for p in range(o, min(len(d), 0x1000) - 16):
            uoff, usize, coff, csize = struct.unpack_from("<4I", d, p)
            if uoff == self.name_offset and coff < len(d) and usize > 0:
                count, flags = struct.unpack_from("<I", d, p - 4)[0], struct.unpack_from("<I", d, p - 8)[0]
                if flags == COMPRESS_LZO and 0 < count < 4096:
                    self.chunks = [struct.unpack_from("<4I", d, p + 16 * i) for i in range(count)]
                    break
        self.compressed = bool(self.chunks)

    def _chunk_data(self, index: int) -> bytes:
        if index in self._cache:
            return self._cache[index]
        uoff, usize, coff, csize = self.chunks[index]
        d = self.raw
        tag, block_size, total_c, total_u = struct.unpack_from("<4I", d, coff)
        if tag != TAG:
            raise ValueError(f"invalid chunk {index}")
        blocks = []
        p = coff + 16
        remaining = total_u
        while remaining > 0:
            c, u = struct.unpack_from("<2I", d, p)
            blocks.append((c, u))
            p += 8
            remaining -= u
        out = bytearray()
        for c, u in blocks:
            out += LZOCompressor.decompress(d[p : p + c], output_size_hint=u)
            p += c
        self._cache[index] = bytes(out)
        return self._cache[index]

    def read(self, offset: int, size: int) -> bytes:
        """Bytes at `offset` in the INFLATED package."""
        if not self.compressed:
            return self.raw[offset : offset + size]
        out = bytearray()
        for i, (uoff, usize, _, _) in enumerate(self.chunks):
            if uoff + usize <= offset or uoff >= offset + size:
                continue
            data = self._chunk_data(i)
            start = max(offset, uoff) - uoff
            end = min(offset + size, uoff + usize) - uoff
            out += data[start:end]
        return bytes(out)

    def _read_names(self) -> list[str]:
        # Read generously from NameOffset: the table fits in the first chunks.
        end = self.import_offset if self.import_offset > self.name_offset else self.name_offset + 0x100000
        d = self.read(self.name_offset, end - self.name_offset)
        names, o = [], 0
        for _ in range(self.name_count):
            n = struct.unpack_from("<i", d, o)[0]
            o += 4
            if n >= 0:
                names.append(d[o : o + n - 1].decode("latin-1"))
                o += n
            else:
                names.append(d[o : o - 2 * n - 2].decode("utf-16le"))
                o += -2 * n
            o += 8  # entry flags
        return names

    def name(self, index: int, number: int = 0) -> str:
        base = self.names[index] if 0 <= index < len(self.names) else f"?{index}"
        return f"{base}_{number - 1}" if number > 0 else base

    def _read_imports(self) -> list[Import]:
        d = self.read(self.import_offset, 28 * self.import_count)
        out = []
        for i in range(self.import_count):
            cp, cpn, cn, cnn, outer, n, nn = struct.unpack_from("<iiiiiii", d, 28 * i)
            out.append(Import(self.name(cp, cpn), self.name(cn, cnn), outer, self.name(n, nn)))
        return out

    def _read_exports(self) -> list[Export]:
        d = self.read(self.export_offset, 0x200 * max(self.export_count, 1))
        out, o = [], 0
        for _ in range(self.export_count):
            cls, sup, outer, n, nn, arch = struct.unpack_from("<iiiiii", d, o)
            o += 24
            o += 8  # ObjectFlags (64 bits)
            size, offset = struct.unpack_from("<ii", d, o)
            o += 8
            o += 4  # ExportFlags
            gen_count = struct.unpack_from("<i", d, o)[0]
            o += 4 + 4 * gen_count
            if gen_count:
                # BioShock: only the entries with NetObjects (sub-packages) then carry an integer,
                # the PackageGuid and the PackageFlags.
                o += 4 + 16 + 4
            out.append(Export(cls, sup, outer, self.name(n, nn), arch, size, offset))
        return out

    def properties(self, exp: Export) -> dict:
        """Tagged properties of an export: name -> (type, value).
        Vector -> (x, y, z), Rotator -> (pitch, yaw, roll), Int/Float/Name/Object/Bool decoded, the
        rest as raw bytes. Array properties (ArrayIndex > 0) are ignored."""
        data = self.read(exp.serial_offset, exp.serial_size)

        def name_at(o):
            i, n = struct.unpack_from("<ii", data, o)
            return self.name(i, n) if 0 <= i < len(self.names) else None

        start = next(
            (
                o
                for o in range(0, 32, 4)
                if o + 16 <= len(data)
                and name_at(o)
                and (name_at(o) == "None" or (name_at(o + 8) or "").endswith("Property"))
            ),
            None,
        )
        props, o = {}, start if start is not None else len(data)
        while o + 8 <= len(data):
            nm = name_at(o)
            if nm is None or nm == "None":
                break
            ty = name_at(o + 8)
            size, index = struct.unpack_from("<ii", data, o + 16)
            o += 24
            extra = None
            if ty in ("StructProperty", "ByteProperty"):
                extra = name_at(o)
                o += 8
            elif ty == "BoolProperty":
                extra = data[o]
                o += 1
            raw = data[o : o + size]
            o += size
            if index:
                continue
            if ty == "StructProperty" and extra == "Vector" and size == 12:
                value = struct.unpack("<3f", raw)
            elif ty == "StructProperty" and extra == "Rotator" and size == 12:
                value = struct.unpack("<3i", raw)
            elif ty == "IntProperty":
                value = struct.unpack("<i", raw)[0]
            elif ty == "FloatProperty":
                value = struct.unpack("<f", raw)[0]
            elif ty == "NameProperty":
                value = self.name(*struct.unpack("<ii", raw))
            elif ty == "ObjectProperty":
                value = self.ref_name(struct.unpack("<i", raw)[0])
            elif ty == "BoolProperty":
                value = bool(extra)
            else:
                value = raw
            props[nm] = (ty, value)
        return props

    def ref_name(self, ref: int) -> str:
        """Name of an object reference: >0 export (1-based), <0 import, 0 = none."""
        if ref > 0:
            return self.exports[ref - 1].name
        if ref < 0:
            return self.imports[-ref - 1].name
        return "None"

    def ref_outer(self, ref: int) -> int:
        if ref > 0:
            return self.exports[ref - 1].outer
        if ref < 0:
            return self.imports[-ref - 1].outer
        return 0

    def class_of(self, exp: Export) -> str:
        return self.ref_name(exp.class_index) if exp.class_index else "Class"

    def archetype_of(self, exp: Export) -> str:
        return self.ref_name(exp.archetype)

    def path_of(self, exp: Export, package: str) -> str:
        parts = [exp.name]
        ref = exp.outer
        while ref:
            parts.append(self.ref_name(ref))
            ref = self.ref_outer(ref)
        parts.append(package)
        return ".".join(reversed(parts))
