#!/usr/bin/env python3
"""Assemble an Arduino IDE library from the canonical shared C sources."""

from pathlib import Path
import shutil
import sys


ROOT = Path(__file__).resolve().parents[2]
SHARED = ROOT / "SHARED"
CORE = SHARED / "components" / "mio_core"
CLIENT = SHARED / "components" / "mio_client"
ADAPTER = SHARED / "adapters" / "arduino"


def copy_sources(source_dir: Path, names: list[str], destination: Path) -> None:
    for name in names:
        shutil.copy2(source_dir / name, destination / name)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: package_arduino.py OUTPUT_DIRECTORY", file=sys.stderr)
        return 2
    output = Path(sys.argv[1]).expanduser().resolve() / "MIOClient"
    if output.exists():
        print(f"refusing to overwrite existing directory: {output}",
              file=sys.stderr)
        return 2

    source = output / "src"
    source.mkdir(parents=True)
    copy_sources(CORE, ["mio.c", "mio_io.c", "mio_io_wire.c",
                        "mio_command.c"], source)
    copy_sources(CORE / "include", ["mio.h", "mio_io.h", "mio_command.h"],
                 source)
    copy_sources(CLIENT, ["mio_client.c"], source)
    copy_sources(CLIENT / "include", ["mio_client.h"], source)
    copy_sources(ADAPTER, ["MIOArduinoTwai.h", "MIOArduinoTwai.cpp",
                           "MIOClient.h", "MIOClient.cpp"], source)

    (output / "library.properties").write_text(
        "name=MIOClient\n"
        "version=0.1.0\n"
        "author=MIO Project\n"
        "maintainer=MIO Project\n"
        "sentence=Framework-friendly client for modular CAN I/O nodes.\n"
        "paragraph=Discover modules and use remote I/O without building CAN frames.\n"
        "category=Communication\n"
        "architectures=esp32\n"
        "includes=MIOClient.h\n",
        encoding="utf-8",
    )
    shutil.copy2(ADAPTER / "README.md", output / "README.md")
    examples = output / "examples" / "BasicMaster"
    examples.mkdir(parents=True)
    shutil.copy2(ADAPTER / "BasicMaster.ino", examples / "BasicMaster.ino")
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
