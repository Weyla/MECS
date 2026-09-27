#!/usr/bin/env python3
"""Assemble an Arduino IDE library from the canonical shared C sources."""

from pathlib import Path
import argparse
import shutil
from urllib.request import urlopen
from urllib.parse import quote


SCRIPT_PATH = Path(__file__).resolve()
ROOT = next((parent for parent in SCRIPT_PATH.parents
             if (parent / "SHARED" / "components").is_dir()), Path.cwd())
SHARED = ROOT / "SHARED"
CORE = SHARED / "components" / "mio_core"
CLIENT = SHARED / "components" / "mio_client"
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
                        help="folder where the MIOClient library is created")
    parser.add_argument("--github-ref",
                        help="fetch canonical files from this GitHub branch, tag, or commit")
    args = parser.parse_args()
    output = Path(args.output_directory).expanduser().resolve() / "MIOClient"
    if output.exists():
        print(f"refusing to overwrite existing directory: {output}",
              file=sys.stderr)
        return 2

    source = output / "src"
    source.mkdir(parents=True)
    copy_files(CORE, "SHARED/components/mio_core",
               ["mio.c", "mio_io.c", "mio_io_wire.c", "mio_command.c"],
               source, args.github_ref)
    copy_files(CORE / "include", "SHARED/components/mio_core/include",
               ["mio.h", "mio_io.h", "mio_command.h"], source,
               args.github_ref)
    copy_files(CLIENT, "SHARED/components/mio_client", ["mio_client.c"],
               source, args.github_ref)
    copy_files(CLIENT / "include", "SHARED/components/mio_client/include",
               ["mio_client.h"], source, args.github_ref)
    copy_files(ADAPTER, "SHARED/adapters/arduino",
               ["MIOArduinoTwai.h", "MIOArduinoTwai.cpp",
                "MIOClient.h", "MIOClient.cpp"], source,
               args.github_ref)

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
    copy_files(ADAPTER, "SHARED/adapters/arduino", ["README.md"], output,
               args.github_ref)
    examples = output / "examples" / "BasicMaster"
    examples.mkdir(parents=True)
    copy_files(ADAPTER, "SHARED/adapters/arduino", ["BasicMaster.ino"],
               examples, args.github_ref)
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
