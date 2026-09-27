#!/usr/bin/env python3
"""Assemble an Arduino IDE library from the canonical shared C sources."""

from pathlib import Path
import argparse
import re
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
ADAPTER = SHARED / "adapters" / "arduino"


GITHUB_RAW = "https://raw.githubusercontent.com/Weyla/MECS"


def copy_files(source_dir: Path, repository_dir: str, names: list[str],
               destination: Path, github_ref: str | None) -> None:
    for name in names:
        target = destination / name
        if github_ref is None:
            shutil.copy2(source_dir / name, target)
        else:
            url = (f"{GITHUB_RAW}/{quote(github_ref, safe='/')}/"
                   f"{repository_dir}/{name}")
            with urlopen(url, timeout=30) as response, target.open("wb") as out:
                shutil.copyfileobj(response, out)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output_directory",
                        help="folder where the MECSClient library is created")
    parser.add_argument("--github-ref",
                        help="fetch canonical files from this GitHub branch, tag, or commit")
    parser.add_argument("--version", default="0.2.0",
                        help="Arduino library version to write to library.properties (default: 0.2.0)")
    args = parser.parse_args()
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", args.version):
        parser.error("--version must use MAJOR.MINOR.PATCH, for example 0.2.0")

    output = Path(args.output_directory).expanduser().resolve() / "MECSClient"
    if output.exists():
        print(f"refusing to overwrite existing directory: {output}",
              file=sys.stderr)
        return 2

    source = output / "src"
    source.mkdir(parents=True)
    copy_files(CORE, "SHARED/components/mecs_core",
               ["mecs_protocol.c", "mecs_io.c", "mecs_io_wire.c"],
               source, args.github_ref)
    copy_files(CORE / "include", "SHARED/components/mecs_core/include",
               ["mecs_protocol.h", "mecs_io.h"], source,
               args.github_ref)
    copy_files(CLIENT, "SHARED/components/mecs_client", ["mecs_client.c"],
               source, args.github_ref)
    copy_files(CLIENT / "include", "SHARED/components/mecs_client/include",
               ["mecs_client.h"], source, args.github_ref)
    copy_files(ADAPTER, "SHARED/adapters/arduino",
               ["MECSTwaiTransport.h", "MECSTwaiTransport.cpp",
                "MECSClient.h", "MECSClient.cpp"], source,
               args.github_ref)

    copy_files(SHARED / "components" / "mecs_client", "SHARED/components/mecs_client",
               ["MECS.cpp"], source, args.github_ref)
    copy_files(SHARED / "components" / "mecs_client" / "include",
               "SHARED/components/mecs_client/include", ["MECS.h"], source, args.github_ref)

    (output / "library.properties").write_text(
        "name=MECSClient\n"
        f"version={args.version}\n"
        "author=MECS Project\n"
        "maintainer=MECS Project\n"
        "sentence=Framework-friendly client for modular CAN I/O nodes.\n"
        "paragraph=Discover modules and use remote I/O without building CAN frames.\n"
        "category=Communication\n"
        "url=https://github.com/Weyla/MECS\n"
        "architectures=esp32\n"
        "includes=MECSClient.h\n",
        encoding="utf-8",
    )
    copy_files(ADAPTER, "SHARED/adapters/arduino", ["README.md"], output,
               args.github_ref)
    examples = output / "examples" / "BasicMaster"
    examples.mkdir(parents=True)
    copy_files(ROOT / "examples" / "arduino" / "BasicMaster",
               "examples/arduino/BasicMaster", ["BasicMaster.ino"],
               examples, args.github_ref)
    for folder, repository_dir, filename in [
        ("ServoRecovery", "examples/arduino/ServoRecovery", "ServoRecovery.ino"),
        ("DO4", "DO4/examples/arduino", "do4.ino"),
        ("DI4", "DI4/examples/arduino", "di4.ino"),
    ]:
        destination = output / "examples" / folder
        destination.mkdir(parents=True)
        copy_files(ROOT / repository_dir, repository_dir, [filename],
                   destination, args.github_ref)
        if filename != folder + ".ino":
            (destination / filename).rename(destination / (folder + ".ino"))
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
