"""Build and apply offline file-level updates for ZhiLi portable releases."""

import argparse
import hashlib
import json
import ntpath
import os
from pathlib import Path, PureWindowsPath
import re
import shutil
import stat
import tempfile
import zipfile


FORMAT = "zhili-update-v1"
MANIFEST = "manifest.json"
PLATFORMS = ("windows-x64", "linux-arm64")
VERSION = re.compile(r"v\d+\.\d+\.\d+(?:-[A-Za-z0-9]+(?:[.-][A-Za-z0-9]+)*)?")
SHA256 = re.compile(r"[0-9a-f]{64}")
BLOCK_SIZE = 1024 * 1024


def fingerprint(source, destination=None):
    digest = hashlib.sha256()
    size = 0
    for block in iter(lambda: source.read(BLOCK_SIZE), b""):
        digest.update(block)
        size += len(block)
        if destination is not None:
            destination.write(block)
    return {"sha256": digest.hexdigest(), "size": size}


def checked_path(root, relative):
    path = root
    for name in relative.split("/"):
        path = path / name
        info = path.lstat()
        if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT:
            raise ValueError(f"Linked files and directories are unsupported: {relative}")
    if not stat.S_ISREG(info.st_mode):
        raise ValueError(f"Expected a regular file: {relative}")
    return path


def valid_path(relative):
    return all(part not in ("", ".", "..") and not part.endswith((" ", "."))
               and not reserved_name(part)
               and not any(ord(char) < 32 or char in '\\:*?"<>|' for char in part)
               for part in relative.split("/"))


def reserved_name(name):
    if hasattr(ntpath, "isreserved"):
        return ntpath.isreserved(name)
    return PureWindowsPath(name).is_reserved()


def is_executable(path, platform):
    return platform != "windows-x64" and bool(path.stat().st_mode & 0o111)


def validate_inventory(files):
    if not isinstance(files, dict) or not files:
        raise ValueError("An update inventory must contain files.")
    if len({name.casefold() for name in files}) != len(files):
        raise ValueError("An update inventory contains conflicting paths.")
    for name, record in files.items():
        if (not valid_path(name) or not isinstance(record, dict)
                or set(record) != {"sha256", "size", "executable"}
                or not isinstance(record["sha256"], str) or not SHA256.fullmatch(record["sha256"])
                or type(record["size"]) is not int or record["size"] < 0
                or type(record["executable"]) is not bool):
            raise ValueError(f"Invalid inventory entry: {name}")


def inventory(root, platform):
    def scan_error(error):
        raise error

    files = {}
    for folder, dirs, names in os.walk(root, onerror=scan_error):
        for name in dirs:
            path = Path(folder, name)
            if path.is_symlink() or getattr(path.lstat(), "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT:
                raise ValueError(f"Linked directories are unsupported: {path}")
        for name in names:
            relative = Path(folder, name).relative_to(root).as_posix()
            path = checked_path(root, relative)
            with path.open("rb") as source:
                files[relative] = fingerprint(source)
            files[relative]["executable"] = is_executable(path, platform)
    validate_inventory(files)
    return files


def changed_files(base, target):
    return {name for name, record in target.items() if record != base.get(name)}


def require_match(actual, expected, path):
    if actual["size"] != expected["size"] or actual["sha256"] != expected["sha256"]:
        raise ValueError(f"File does not match the update manifest: {path}")


def publish(staged, output):
    lock = output.with_name("." + output.name + ".update-lock")
    with lock.open("xb"):
        pass
    try:
        if output.exists() or output.is_symlink():
            raise FileExistsError(f"Output already exists: {output}")
        staged.rename(output)
    finally:
        lock.unlink()


def create(args, output):
    base = args.base.resolve(strict=True)
    target = args.target.resolve(strict=True)
    if base == target or base in target.parents or target in base.parents:
        raise ValueError("Use two separate portable release directories.")
    if args.from_version == args.to_version or any(root in output.parents for root in (base, target)):
        raise ValueError("Use distinct versions and place the update outside the source directories.")
    before = inventory(base, args.platform)
    after = inventory(target, args.platform)
    changed = changed_files(before, after)
    manifest = {"format": FORMAT, "platform": args.platform, "from": args.from_version,
                "to": args.to_version, "base": before, "target": after}
    with tempfile.TemporaryDirectory(dir=output.parent) as temp:
        staged = Path(temp, "update.zip")
        with zipfile.ZipFile(staged, "w", zipfile.ZIP_DEFLATED) as archive:
            archive.writestr(MANIFEST, json.dumps(manifest, ensure_ascii=False, sort_keys=True))
            for name in sorted(changed):
                with checked_path(target, name).open("rb") as source, \
                        archive.open("files/" + name, "w", force_zip64=True) as member:
                    require_match(fingerprint(source, member), after[name], name)
        with staged.open("rb") as package:
            checksum = fingerprint(package)["sha256"]
        publish(staged, output)
    print(f"{args.from_version} -> {args.to_version}: {len(changed)} changed, "
          f"{len(before.keys() - after.keys())} removed\n{checksum}  {output.name}")


def read_manifest(archive, args):
    if archive.getinfo(MANIFEST).file_size > 4 * 1024 * 1024:
        raise ValueError("Update manifest is too large.")
    manifest = json.loads(archive.read(MANIFEST))
    if (not isinstance(manifest, dict) or manifest.get("format") != FORMAT
            or manifest.get("from") != args.from_version or manifest.get("platform") != args.platform
            or not isinstance(manifest.get("to"), str) or not VERSION.fullmatch(manifest["to"])
            or manifest["to"] == manifest["from"]):
        raise ValueError("Update format, platform or baseline version does not match.")
    base, target = manifest["base"], manifest["target"]
    validate_inventory(base)
    validate_inventory(target)
    changed = changed_files(base, target)
    names = archive.namelist()
    if len(names) != len(changed) + 1 or set(names) != {MANIFEST, *("files/" + name for name in changed)}:
        raise ValueError("Update archive has missing or unexpected entries.")
    return manifest, changed


def apply(args, output):
    base = args.base.resolve(strict=True)
    if base in output.parents:
        raise ValueError("Place the upgraded directory outside the baseline.")
    with args.package.open("rb") as package:
        if fingerprint(package)["sha256"] != args.sha256:
            raise ValueError("Update package SHA-256 does not match.")
        package.seek(0)
        with zipfile.ZipFile(package) as archive, tempfile.TemporaryDirectory(dir=output.parent) as temp:
            manifest, changed = read_manifest(archive, args)
            for name, expected in manifest["base"].items():
                path = checked_path(base, name)
                if is_executable(path, args.platform) != expected["executable"]:
                    raise ValueError(f"Baseline executable permission does not match: {name}")
                with path.open("rb") as source:
                    require_match(fingerprint(source), expected, name)
            staged = Path(temp, "ZhiLi")
            staged.mkdir()
            for name, expected in manifest["target"].items():
                destination = staged / name
                destination.parent.mkdir(parents=True, exist_ok=True)
                if name in changed:
                    member = archive.getinfo("files/" + name)
                    if member.file_size != expected["size"]:
                        raise ValueError(f"Unexpected payload size: {name}")
                    source = archive.open(member)
                else:
                    source = checked_path(base, name).open("rb")
                with source, destination.open("xb") as result:
                    require_match(fingerprint(source, result), expected, name)
                if args.platform != "windows-x64":
                    destination.chmod(0o755 if expected["executable"] else 0o644)
            publish(staged, output)
    print(f"{manifest['to']} is ready at {output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for command in ("create", "apply"):
        sub = commands.add_parser(command)
        sub.add_argument("--base", required=True, type=Path)
        sub.add_argument("--output", required=True, type=Path)
        sub.add_argument("--from-version", required=True)
        sub.add_argument("--platform", required=True, choices=PLATFORMS)
    commands.choices["create"].add_argument("--target", required=True, type=Path)
    commands.choices["create"].add_argument("--to-version", required=True)
    commands.choices["apply"].add_argument("--package", required=True, type=Path)
    commands.choices["apply"].add_argument("--sha256", required=True, type=str.lower)
    args = parser.parse_args()
    try:
        if not VERSION.fullmatch(args.from_version):
            raise ValueError("Expected a baseline version such as v1.0.2.")
        if args.command == "create" and not VERSION.fullmatch(args.to_version):
            raise ValueError("Expected a target version such as v1.0.3.")
        if args.command == "apply" and not SHA256.fullmatch(args.sha256):
            raise ValueError("Expected the published 64-character SHA-256.")
        if args.platform == "linux-arm64" and os.name == "nt":
            raise ValueError("Use Linux for ARM64 portable packages to preserve executable permissions.")
        output = args.output.parent.resolve(strict=True) / args.output.name
        if output.exists() or output.is_symlink():
            raise FileExistsError(f"Output already exists: {output}")
        (create if args.command == "create" else apply)(args, output)
    except (OSError, ValueError, KeyError, zipfile.BadZipFile) as error:
        parser.exit(1, f"Update failed: {error}\n")


if __name__ == "__main__":
    main()
