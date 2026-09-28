#!/usr/bin/env python3
"""Check large indexed PNG processing under a 64 MiB address-space limit.

Usage: python3 tests/indexed_image_memory_test.py /path/to/pdvzip
Pass --unlimited to measure elapsed time and peak child RSS without the limit.
The fixture and generated output stay in a temporary directory.
"""

import argparse
from pathlib import Path
import resource
import struct
import subprocess
import tempfile
import time
import zipfile
import zlib


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
ADDRESS_SPACE_LIMIT = 64 * 1024 * 1024


def png_chunk(name, data):
    return (struct.pack(">I", len(data)) + name + data
            + struct.pack(">I", zlib.crc32(name + data)))


def indexed_cover():
    # These dimensions and format have a launcher-safe IHDR CRC (c86db654),
    # so this test measures the path that preserves an existing indexed image.
    header = struct.pack(">IIBBBBB", 4096, 4096, 1, 3, 0, 0, 0)
    assert zlib.crc32(b"IHDR" + header) == 0xC86DB654
    palette = b"\0\0\0\xff\xff\xff"
    scanlines = (b"\0" + b"\x55" * 512) * 4096
    compressed = zlib.compress(scanlines)
    png = (PNG_SIGNATURE + png_chunk(b"IHDR", header)
           + png_chunk(b"PLTE", palette) + png_chunk(b"IDAT", compressed)
           + png_chunk(b"IEND", b""))
    return png, header, palette, compressed


def png_chunks(data):
    assert data.startswith(PNG_SIGNATURE), "output lost its PNG signature"
    chunks = []
    cursor = len(PNG_SIGNATURE)
    while cursor < len(data):
        assert len(data) - cursor >= 12, "truncated output chunk"
        length = struct.unpack_from(">I", data, cursor)[0]
        end = cursor + 12 + length
        assert end <= len(data), "output chunk exceeds file size"
        name = data[cursor + 4:cursor + 8]
        payload = data[cursor + 8:end - 4]
        checksum = struct.unpack_from(">I", data, end - 4)[0]
        assert checksum == zlib.crc32(name + payload), f"invalid {name!r} CRC"
        chunks.append((name, payload))
        cursor = end
        if name == b"IEND":
            assert payload == b"" and cursor == len(data), "invalid output ending"
            return chunks
    raise AssertionError("output PNG lacks IEND")


def limit_address_space():
    resource.setrlimit(resource.RLIMIT_AS, (ADDRESS_SPACE_LIMIT, ADDRESS_SPACE_LIMIT))


def check_memory(executable, unlimited):
    with tempfile.TemporaryDirectory(prefix="pdvzip-indexed-memory-") as temporary:
        root = Path(temporary)
        png, header, palette, compressed = indexed_cover()
        cover = root / "cover.png"
        cover.write_bytes(png)
        archive = root / "input.zip"
        program = b"pass\n"
        with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_STORED) as zipped:
            zipped.writestr("main.py", program)

        started = time.perf_counter()
        result = subprocess.run(
            [str(executable), str(cover), str(archive)], cwd=root,
            input=b"\n\n", capture_output=True, timeout=30,
            preexec_fn=None if unlimited else limit_address_space,
        )
        elapsed = time.perf_counter() - started
        assert result.returncode == 0, (
            f"creation failed with status {result.returncode}: {result.stderr!r}")
        generated = list(root.glob("pzip_*.png"))
        assert len(generated) == 1, "expected one generated polyglot"
        chunks = png_chunks(generated[0].read_bytes())
        assert [data for name, data in chunks if name == b"IHDR"] == [header], "IHDR changed"
        assert [data for name, data in chunks if name == b"PLTE"] == [palette], "palette changed"
        idat = [data for name, data in chunks if name == b"IDAT"]
        assert len(idat) == 2 and idat[0] == compressed, "native image IDAT changed"
        with zipfile.ZipFile(generated[0]) as zipped:
            assert zipped.namelist() == ["main.py"], "embedded ZIP members changed"
            assert zipped.read("main.py") == program, "embedded program changed"
            assert zipped.testzip() is None, "embedded ZIP checksum failure"

        mode = "without a memory limit" if unlimited else "under 64 MiB RLIMIT_AS"
        print(f"PASS: 4096 x 4096, 1-bit indexed PNG {mode}")
        if unlimited:
            peak_kib = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
            print(f"Elapsed: {elapsed:.6f} seconds; peak child RSS: {peak_kib} KiB (Linux)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=lambda value: Path(value).resolve(strict=True))
    parser.add_argument("--unlimited", action="store_true", help="omit RLIMIT_AS and report measurements")
    args = parser.parse_args()
    try:
        check_memory(args.executable, args.unlimited)
    except (AssertionError, OSError, subprocess.SubprocessError, zipfile.BadZipFile) as error:
        print("FAIL:", error)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
