#!/usr/bin/env python3
"""Assemble an ESPHome external component from canonical shared C sources."""

from pathlib import Path
import argparse
import shutil
import sys
from urllib.request import urlopen
from urllib.parse import quote


SCRIPT_PATH = Path(__file__).resolve()
ROOT = next((parent for parent in SCRIPT_PATH.parents
             if (parent / "SHARED" / "components").is_dir()), Path.cwd())
SHARED = ROOT / "SHARED"
CORE = SHARED / "components" / "mecs_core"
CLIENT = SHARED / "components" / "mecs_client"
COMPONENT = SHARED / "adapters" / "esphome" / "mecs_expansion"
GITHUB_RAW = "https://raw.githubusercontent.com/Weyla/MECS"


def copy_names(source: Path, names: list[str], destination: Path) -> None:
    for name in names:
        shutil.copy2(source / name, destination / name)


def copy_github(repository_path: str, names: list[str], destination: Path,
                github_ref: str) -> None:
    for name in names:
        url = (f"{GITHUB_RAW}/{quote(github_ref, safe='/')}/"
               f"{repository_path}/{name}")
        with urlopen(url, timeout=30) as response, (destination / name).open("wb") as out:
            shutil.copyfileobj(response, out)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output_directory",
                        help="folder where the mecs_components package is created")
    parser.add_argument("--github-ref",
                        help="fetch canonical files from this GitHub branch, tag, or commit")
    args = parser.parse_args()
    output = Path(args.output_directory).expanduser().resolve() / "mecs_components"
    if output.exists():
        print(f"refusing to overwrite existing directory: {output}",
              file=sys.stderr)
        return 2
    component = output / "mecs_expansion"
    sources = output / "mecs_sources"
    component.mkdir(parents=True)
    sources.mkdir(parents=True)
    if args.github_ref is None:
        for path in COMPONENT.iterdir():
            if path.is_file():
                shutil.copy2(path, component / path.name)
    else:
        copy_github("SHARED/adapters/esphome/mecs_expansion",
                    ["__init__.py", "mecs_expansion.cpp", "mecs_expansion.h"],
                    component, args.github_ref)
    core_sources = ["mecs_protocol.c", "mecs_io.c", "mecs_io_wire.c"]
    core_headers = ["mecs_protocol.h", "mecs_io.h"]
    client_headers = ["mecs_client.h"]
    if args.github_ref is None:
        copy_names(CORE, core_sources, sources)
        copy_names(CORE / "include", core_headers, sources)
        copy_names(CLIENT, ["mecs_client.c"], sources)
        copy_names(CLIENT / "include", client_headers, sources)
    else:
        copy_github("SHARED/components/mecs_core", core_sources, sources,
                    args.github_ref)
        copy_github("SHARED/components/mecs_core/include", core_headers,
                    sources, args.github_ref)
        copy_github("SHARED/components/mecs_client", ["mecs_client.c"],
                    sources, args.github_ref)
        copy_github("SHARED/components/mecs_client/include", client_headers,
                    sources, args.github_ref)
    # The external component's C++ files include the public headers locally.
    if args.github_ref is None:
        copy_names(CORE / "include", core_headers, component)
        copy_names(CLIENT / "include", client_headers, component)
    else:
        copy_github("SHARED/components/mecs_core/include", core_headers,
                    component, args.github_ref)
        copy_github("SHARED/components/mecs_client/include", client_headers,
                    component, args.github_ref)
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
