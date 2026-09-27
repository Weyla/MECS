#!/usr/bin/env python3
"""Assemble an ESPHome external component from canonical shared C sources."""

from pathlib import Path
import shutil
import sys


ROOT = Path(__file__).resolve().parents[2]
SHARED = ROOT / "SHARED"
CORE = SHARED / "components" / "mio_core"
CLIENT = SHARED / "components" / "mio_client"
COMPONENT = SHARED / "adapters" / "esphome" / "mio_expansion"


def copy_names(source: Path, names: list[str], destination: Path) -> None:
    for name in names:
        shutil.copy2(source / name, destination / name)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: package_esphome.py OUTPUT_DIRECTORY", file=sys.stderr)
        return 2
    output = Path(sys.argv[1]).expanduser().resolve() / "mio_components"
    if output.exists():
        print(f"refusing to overwrite existing directory: {output}",
              file=sys.stderr)
        return 2
    component = output / "mio_expansion"
    sources = output / "mio_sources"
    component.mkdir(parents=True)
    sources.mkdir(parents=True)
    for path in COMPONENT.iterdir():
        if path.is_file():
            shutil.copy2(path, component / path.name)
    core_sources = ["mio.c", "mio_io.c", "mio_io_wire.c", "mio_command.c"]
    core_headers = ["mio.h", "mio_io.h", "mio_command.h"]
    client_headers = ["mio_client.h"]
    copy_names(CORE, core_sources, sources)
    copy_names(CORE / "include", core_headers, sources)
    copy_names(CLIENT, ["mio_client.c"], sources)
    copy_names(CLIENT / "include", client_headers, sources)
    # The external component's C++ files include the public headers locally.
    copy_names(CORE / "include", core_headers, component)
    copy_names(CLIENT / "include", client_headers, component)
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
