#!/usr/bin/env python3
"""Enforce SaberStage's private-logger ELF boundary without external tools."""

from __future__ import annotations

import pathlib
import struct
import sys


def fail(message: str) -> None:
    raise RuntimeError(message)


def c_string(table: bytes, offset: int) -> str:
    if offset < 0 or offset >= len(table):
        fail(f"ELF string offset {offset} is outside its string table")
    end = table.find(b"\0", offset)
    if end < 0:
        fail("ELF string table entry is not terminated")
    return table[offset:end].decode("utf-8", errors="replace")


def inspect_elf(path: pathlib.Path) -> tuple[set[str], set[str]]:
    data = path.read_bytes()
    if data[:6] != b"\x7fELF\x02\x01":
        fail(f"{path} is not a little-endian ELF64 library")
    if len(data) < 64 or struct.unpack_from("<H", data, 18)[0] != 183:
        fail(f"{path} is not an AArch64 ELF library")

    section_offset = struct.unpack_from("<Q", data, 40)[0]
    section_size = struct.unpack_from("<H", data, 58)[0]
    section_count = struct.unpack_from("<H", data, 60)[0]
    string_section_index = struct.unpack_from("<H", data, 62)[0]
    if section_size < 64 or string_section_index >= section_count:
        fail("ELF section table is malformed")

    sections: list[dict[str, int]] = []
    for index in range(section_count):
        offset = section_offset + index * section_size
        if offset + 64 > len(data):
            fail("ELF section table extends beyond the file")
        sections.append(
            {
                "name": struct.unpack_from("<I", data, offset)[0],
                "type": struct.unpack_from("<I", data, offset + 4)[0],
                "offset": struct.unpack_from("<Q", data, offset + 24)[0],
                "size": struct.unpack_from("<Q", data, offset + 32)[0],
                "link": struct.unpack_from("<I", data, offset + 40)[0],
                "entry_size": struct.unpack_from("<Q", data, offset + 56)[0],
            }
        )

    def contents(section: dict[str, int]) -> bytes:
        start = section["offset"]
        end = start + section["size"]
        if end > len(data):
            fail("ELF section extends beyond the file")
        return data[start:end]

    section_names = contents(sections[string_section_index])
    by_name = {c_string(section_names, section["name"]): section for section in sections}
    dynamic = by_name.get(".dynamic")
    dynsym = by_name.get(".dynsym")
    if not dynamic or not dynsym:
        fail("ELF is missing .dynamic or .dynsym")

    def linked_strings(section: dict[str, int]) -> bytes:
        if section["link"] >= len(sections):
            fail("ELF section has an invalid linked string table")
        return contents(sections[section["link"]])

    dynamic_strings = linked_strings(dynamic)
    needed: set[str] = set()
    dynamic_data = contents(dynamic)
    for offset in range(0, len(dynamic_data) - 15, 16):
        tag, value = struct.unpack_from("<qQ", dynamic_data, offset)
        if tag == 0:
            break
        if tag == 1:
            needed.add(c_string(dynamic_strings, value))

    symbol_strings = linked_strings(dynsym)
    entry_size = dynsym["entry_size"] or 24
    if entry_size < 24:
        fail("ELF dynamic symbol entry size is invalid")
    symbols: set[str] = set()
    symbol_data = contents(dynsym)
    for offset in range(0, len(symbol_data) - entry_size + 1, entry_size):
        name_offset = struct.unpack_from("<I", symbol_data, offset)[0]
        if name_offset:
            symbols.add(c_string(symbol_strings, name_offset))
    return needed, symbols


def verify(path: pathlib.Path) -> None:
    needed, symbols = inspect_elf(path)
    paper_libraries = sorted(name for name in needed if "paper2" in name.lower())
    if paper_libraries:
        fail(f"SaberStage still declares Paper2 in DT_NEEDED: {paper_libraries}")
    leaked = sorted(
        name
        for name in symbols
        if name.startswith("paper2_") or name.startswith("__wrap_paper2_") or name.startswith("__real_paper2_")
    )
    if leaked:
        fail(f"SaberStage exposes private Paper2 bridge symbols: {leaked}")
    print(f"Verified private logger ELF boundary: {path}")


if __name__ == "__main__":
    try:
        target = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path("build/libsaberstage.so")
        verify(target.resolve())
    except (OSError, RuntimeError, struct.error) as exception:
        print(f"NATIVE LIBRARY VERIFICATION FAILED: {exception}", file=sys.stderr)
        raise SystemExit(1)
