"""Generate and verify release deltas from the checked portable archives."""

import argparse
import hashlib
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import zipfile


PACKAGES = {
    "windows-x64": ("Windows-x64", ".zip"),
    "linux-arm64": ("Linux-ARM64-Portable", ".tar.gz"),
}
UPDATER = Path(__file__).resolve().parents[1] / "tools" / "zhili_update.py"


def extract(package, destination):
    if package.suffix == ".zip":
        with zipfile.ZipFile(package, metadata_encoding="gbk") as archive:
            archive.extractall(destination)
    else:
        with tarfile.open(package) as archive:
            archive.extractall(destination, filter="data")
    return destination / "ZhiLi"


def release_updates(baseline, target, baseline_dir, dist):
    for platform, (label, extension) in PACKAGES.items():
        with tempfile.TemporaryDirectory() as temp:
            work = Path(temp)
            before = extract(baseline_dir / f"ZhiLi-{baseline}-{label}{extension}", work / "baseline")
            after = extract(dist / f"ZhiLi-{target}-{label}{extension}", work / "target")
            package = dist / f"ZhiLi-{baseline}-to-{target}-{label}.update.zip"
            common = ["--platform", platform, "--from-version", baseline, "--base", str(before)]
            command = [sys.executable, str(UPDATER)]
            subprocess.run(command + ["create", *common, "--to-version", target, "--target",
                                      str(after), "--output", str(package)], check=True)
            with package.open("rb") as archive:
                checksum = hashlib.file_digest(archive, "sha256").hexdigest()
            subprocess.run(command + ["apply", *common, "--package", str(package), "--sha256",
                                      checksum, "--output", str(work / "verified")], check=True)
            package.with_name(package.name + ".sha256").write_text(
                f"{checksum}  {package.name}\n", encoding="ascii")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline")
    parser.add_argument("target")
    parser.add_argument("baseline_dir", type=Path)
    parser.add_argument("dist", type=Path)
    args = parser.parse_args()
    release_updates(args.baseline, args.target, args.baseline_dir, args.dist)
