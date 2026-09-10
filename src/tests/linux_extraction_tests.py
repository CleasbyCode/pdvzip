#!/usr/bin/env python3
"""Run Linux extraction regressions against a built pdvzip executable.

Usage: python3 tests/linux_extraction_tests.py /path/to/pdvzip
All generated archives, programs, and output files live in temporary directories.
"""

import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import zipfile
import zlib


def png_chunk(name, data):
    return (struct.pack(">I", len(data)) + name + data
            + struct.pack(">I", zlib.crc32(name + data)))


def cover_png():
    # More than 256 colors keeps this fixture in truecolor mode.
    pixels = b"".join(
        b"\0" + b"".join(bytes((x, y, (x + y) % 256)) for x in range(100))
        for y in range(100)
    )
    return (b"\x89PNG\r\n\x1a\n"
            + png_chunk(b"IHDR", struct.pack(">IIBBBBB", 100, 100, 8, 2, 0, 0, 0))
            + png_chunk(b"IDAT", zlib.compress(pixels))
            + png_chunk(b"IEND", b""))


def indexed_cover_png(width, height):
    # Keep an explicit 8-bit indexed IHDR to exercise its binary shell prefix.
    pixels = (b"\0" * (width + 1)) * height
    return (b"\x89PNG\r\n\x1a\n"
            + png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 3, 0, 0, 0))
            + png_chunk(b"PLTE", b"\0\0\0\xff\xff\xff")
            + png_chunk(b"IDAT", zlib.compress(pixels))
            + png_chunk(b"IEND", b""))


def command_stub(directory, name, text):
    path = directory / name
    path.write_text("#!/bin/sh\n" + text)
    path.chmod(0o700)


def extraction_directory(filename):
    stem = filename.rsplit(".", 1)[0] if "." in filename else filename
    return filename + "_files" if stem in ("", ".", "..", filename) else stem


def check_case(executable, *, filename="bundle.png", collision=None,
               invocation="relative", handler="python", failure=None, status=0,
               cover_image=None, linux_arguments="", expected_arguments=None,
               member_path=None):
    with tempfile.TemporaryDirectory(prefix="pdvzip-linux-extract-") as temporary:
        root = Path(temporary)
        source_dir = root / "source"
        source_dir.mkdir()
        run_dir = source_dir
        if invocation in ("absolute", "relative-remote"):
            run_dir = root / "run"
            run_dir.mkdir()

        cover = root / "cover.png"
        cover.write_bytes(cover_png() if cover_image is None else cover_image)
        archive = root / "input.zip"
        contents = {}
        if handler in ("python", "executable"):
            argument_capture = (
                "import json, sys\n"
                "Path('arguments.json').write_text(json.dumps(sys.argv[1:]))\n"
                if expected_arguments is not None else ""
            )
            program_name = member_path or ("main.py" if handler == "python" else "runner")
            contents[program_name] = (
                ("#!/usr/bin/env python3\n" if handler == "executable" else "")
                + "from pathlib import Path\n"
                "Path('launched.txt').write_text('launched')\n"
                + argument_capture
                + f"raise SystemExit({status})\n"
            ).encode()
        elif handler == "folder":
            contents["docs/"] = b""
            contents["docs/readme.txt"] = b"folder fixture\n"
        elif handler == "media":
            contents["movie.mp4"] = b"media handler fixture\n"
        elif handler == "windows":
            contents["program.exe"] = b"extraction-only fixture on Linux\n"
        else:
            raise AssertionError("unknown test handler")

        member_bytes = b"ordinary archive member, not the source polyglot\n"
        if collision == "file":
            contents[filename] = member_bytes
        elif collision == "directory":
            contents[filename + "/"] = b""
            contents[filename + "/child.txt"] = member_bytes
        contents["later.txt"] = b"entry following any filename collision\n"
        with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_STORED) as zipped:
            for name, data in contents.items():
                zipped.writestr(name, data)

        built = subprocess.run(
            [str(executable), str(cover), str(archive)], cwd=source_dir,
            input=(linux_arguments + "\n\n").encode(), capture_output=True, timeout=20,
        )
        assert built.returncode == 0, f"creation failed: {built.stderr!r}"
        generated = list(source_dir.glob("pzip_*.png"))
        assert len(generated) == 1, "expected one generated polyglot"
        original = generated[0].read_bytes()
        source = source_dir / filename
        generated[0].rename(source)
        destination = run_dir / extraction_directory(filename)

        mocks = root / "commands"
        mocks.mkdir()
        command_stub(mocks, "clear", "exit 0\n")
        for name in ("xdg-open", "mpv"):
            command_stub(mocks, name, "printf '%s\\n' \"$@\" > opened.txt\n")
        if failure == "unzip":
            command_stub(mocks, "unzip", "exit 9\n")
        elif failure == "move":
            command_stub(mocks, "mv", "exit 13\n")
        elif failure == "existing-directory":
            destination.mkdir()
            (destination / "keep.txt").write_bytes(b"pre-existing file\n")

        environment = os.environ.copy()
        environment["PATH"] = str(mocks) + os.pathsep + os.defpath
        environment["TERM"] = "dumb"
        for variable in ("UNZIP", "UNZIPOPT", "BASH_ENV", "ENV"):
            environment.pop(variable, None)
        if invocation == "absolute":
            script_path = str(source)
        elif invocation == "relative-remote":
            script_path = os.path.relpath(source, run_dir)
        elif invocation == "bare":
            script_path = filename
        else:
            script_path = "./" + filename
        result = subprocess.run(
            ["bash", "--", script_path], cwd=run_dir, env=environment,
            capture_output=True, timeout=10,
        )

        if failure:
            assert result.returncode != 0, "failed preparation must not launch the payload"
            assert source.exists() and source.read_bytes() == original, "failure lost the original polyglot"
            assert not (destination / "launched.txt").exists(), "payload ran after preparation failed"
            if failure == "existing-directory":
                assert list(destination.iterdir()) == [destination / "keep.txt"]
                assert (destination / "keep.txt").read_bytes() == b"pre-existing file\n"
            return

        expected_status = status if handler in ("python", "executable") else 0
        assert result.returncode == expected_status, f"unexpected execution status: {result.stderr!r}"
        if cover_image is not None:
            assert b"#" not in original[16:24] + original[29:33], "comment byte survived in IHDR"
            width, height = struct.unpack(">II", original[16:24])
            input_width, input_height = struct.unpack(">II", cover_image[16:24])
            assert 68 <= width <= input_width and 68 <= height <= input_height, "invalid resized dimensions"
            assert original[24:29] == cover_image[24:29], "indexed PNG format changed"
            assert struct.unpack(">I", original[29:33])[0] == zlib.crc32(original[12:29]), "invalid IHDR CRC"
        for name, data in contents.items():
            extracted = destination / name
            if name.endswith("/"):
                assert extracted.is_dir(), f"missing extracted directory: {name}"
            else:
                assert extracted.read_bytes() == data, f"archive member changed: {name}"
        if collision:
            assert source.exists(), "colliding member removed the original polyglot"
            assert source.read_bytes() == original, "colliding member changed the original polyglot"
        else:
            assert not source.exists(), "normal extraction should move the source into its folder"
            assert (destination / filename).read_bytes() == original, "moved polyglot changed"
        if handler in ("python", "executable"):
            assert (destination / "launched.txt").read_text() == "launched", "program payload did not run"
            assert not (destination / "opened.txt").exists(), "program was opened instead of executed"
            if expected_arguments is not None:
                actual_arguments = json.loads((destination / "arguments.json").read_text())
                assert actual_arguments == expected_arguments, (
                    f"expected arguments {expected_arguments!r}, got {actual_arguments!r}"
                )
                assert not list(root.rglob("ARGUMENT_SENTINEL")), "argument text executed as a shell command"
        elif handler in ("folder", "media"):
            expected_item = "./docs/" if handler == "folder" else "./movie.mp4"
            assert expected_item in (destination / "opened.txt").read_text().splitlines(), "wrong item launched"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=lambda value: Path(value).resolve(strict=True))
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument("--collision-only", action="store_true", help="run the original regression alone")
    selection.add_argument("--header-only", action="store_true", help="run PNG-header comment regressions alone")
    selection.add_argument("--arguments-only", action="store_true", help="run Linux argument regressions alone")
    selection.add_argument("--classification-only", action="store_true", help="run extensionless program regressions alone")
    args = parser.parse_args()
    run_all = not (args.collision_only or args.header_only or args.arguments_only or args.classification_only)
    cases = [("file collision", {"collision": "file"})] if run_all or args.collision_only else []
    if run_all:
        cases += [
            ("directory collision", {"collision": "directory"}),
            ("normal extraction", {}),
            ("payload exit status", {"collision": "file", "status": 23}),
            ("absolute source", {"invocation": "absolute", "collision": "file"}),
            ("relative source from another directory", {"invocation": "relative-remote"}),
            ("spaces in source name", {"filename": "bundle with spaces.png", "collision": "file"}),
            ("leading dash in source name", {"filename": "-bundle.png", "invocation": "bare", "collision": "file"}),
            ("extensionless source", {"filename": "bundle", "collision": "file"}),
            ("dotfile source", {"filename": ".png", "collision": "file"}),
            ("folder handler", {"handler": "folder", "collision": "file"}),
            ("media handler", {"handler": "media", "collision": "file"}),
            ("extraction-only handler", {"handler": "windows", "collision": "file"}),
            ("unzip failure", {"failure": "unzip"}),
            ("move failure", {"failure": "move"}),
            ("existing extraction directory", {"failure": "existing-directory"}),
        ]
    if run_all or args.header_only:
        # Each fixture has newline/# in the named field and no metacharacter
        # caught by the old check, so Bash previously skipped the Linux branch.
        cases += [
            ("comment byte in PNG width", {"cover_image": indexed_cover_png(2595, 69)}),
            ("comment byte in PNG height", {"cover_image": indexed_cover_png(69, 2595)}),
            ("comment byte in PNG CRC", {"cover_image": indexed_cover_png(102, 518)}),
        ]
    if run_all or args.arguments_only:
        cases += [
            ("double-quoted Windows path", {
                "linux_arguments": r'"C:\Users\nick"',
                "expected_arguments": [r"C:\Users\nick"],
            }),
            ("double-quoted regular expression", {
                "linux_arguments": r'"\d+\.txt"',
                "expected_arguments": [r"\d+\.txt"],
            }),
            ("ordinary backslashes inside double quotes", {
                "linux_arguments": '"a\\ b" "c\\\'d"',
                "expected_arguments": ["a\\ b", "c\\'d"],
            }),
            ("special escapes inside double quotes", {
                "linux_arguments": r'"\$" "\`" "\"" "\\"',
                "expected_arguments": ["$", "`", '"', "\\"],
            }),
            ("empty arguments and mixed quoted segments", {
                "linux_arguments": '"" \'\' pre"C:\\Users\\nick"\' tail\'',
                "expected_arguments": ["", "", "preC:\\Users\\nick tail"],
            }),
            ("single-quoted backslash control", {
                "linux_arguments": "'C:\\Users\\nick' '\\$\\`\\\"\\\\'",
                "expected_arguments": [r"C:\Users\nick", '\\$\\`\\"\\\\'],
            }),
            ("unquoted escape control", {
                "linux_arguments": r'one\ two \$ \` \" \\ \q',
                "expected_arguments": ["one two", "$", "`", '"', "\\", "q"],
            }),
            ("quoted shell syntax stays literal", {
                "linux_arguments": '"$(touch ARGUMENT_SENTINEL)" "`touch ARGUMENT_SENTINEL`" '
                                   '"$HOME" "; touch ARGUMENT_SENTINEL" "a|b&c>d<e#f*?"',
                "expected_arguments": ["$(touch ARGUMENT_SENTINEL)", "`touch ARGUMENT_SENTINEL`",
                                       "$HOME", "; touch ARGUMENT_SENTINEL", "a|b&c>d<e#f*?"],
            }),
        ]
    if run_all or args.classification_only:
        cases += [
            ("extensionless program at archive root", {
                "handler": "executable", "member_path": "runner",
            }),
            ("extensionless program in undotted directory", {
                "handler": "executable", "member_path": "pkg/runner",
            }),
            ("extensionless program in dotted directory", {
                "handler": "executable", "member_path": "pkg.v1/runner", "status": 23,
                "linux_arguments": "--flag 'two words'", "expected_arguments": ["--flag", "two words"],
            }),
            ("extensionless program in nested dotted directories with spaces", {
                "handler": "executable", "member_path": "release.v2/pkg.v1 with spaces/runner",
                "linux_arguments": "--flag 'two words'", "expected_arguments": ["--flag", "two words"],
            }),
        ]
    failures = 0
    for label, options in cases:
        try:
            check_case(args.executable, **options)
            print("PASS:", label)
        except (AssertionError, OSError, subprocess.SubprocessError) as error:
            failures += 1
            print("FAIL:", label, "-", error)
    print(f"Linux extraction tests: {len(cases)} cases, {failures} failures.")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
