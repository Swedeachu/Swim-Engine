"""Format first-party C/C++ changes without touching dependencies or archives."""

import argparse
import pathlib
import re
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
EXTENSIONS = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"}


def git_paths(*arguments: str) -> list[str]:
    result = subprocess.run(
        ["git", "-C", str(ROOT), *arguments], check=True, capture_output=True
    )
    return [name for name in result.stdout.decode("utf-8").split("\0") if name]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Check without changing files")
    parser.add_argument("--all", action="store_true", help="Select all maintained C/C++ source")
    parser.add_argument("--base-ref", default="HEAD", help="Compare tracked files to this Git revision")
    parser.add_argument("--clang-format", default="clang-format", help="clang-format 22+ executable")
    args = parser.parse_args()

    executable = shutil.which(args.clang_format)
    if not executable:
        parser.error("clang-format 22 or newer is required for the repository style")

    version = subprocess.run([executable, "--version"], check=True, capture_output=True, text=True)
    match = re.search(r"version (\d+)", version.stdout)
    if not match or int(match[1]) < 22:
        parser.error("clang-format 22 or newer is required for the repository style")

    if args.all:
        names = git_paths("ls-files", "-z", "--cached", "--others", "--exclude-standard", "--", "Source")
    else:
        # Resolve the revision before passing it to diff; names beginning with '-'
        # cannot become options, and deleted files are omitted below.
        revision = subprocess.run(
            ["git", "-C", str(ROOT), "rev-parse", "--verify", "--end-of-options", args.base_ref + "^{commit}"],
            check=True, capture_output=True, text=True,
        ).stdout.strip()
        names = git_paths("diff", "--name-only", "-z", revision, "--", "Source")
        names += git_paths("ls-files", "-z", "--others", "--exclude-standard", "--", "Source")

    files = []
    for name in sorted(set(names)):
        path = (ROOT / name).resolve()
        if path.is_relative_to(ROOT / "Source") and path.is_file() and path.suffix.lower() in EXTENSIONS:
            files.append(path)

    failed = False
    for path in files:
        options = ["--dry-run", "--Werror"] if args.check else ["-i"]
        result = subprocess.run([executable, "--style=file", *options, str(path)])
        failed |= result.returncode != 0

    action = "Checked" if args.check else "Formatted"
    print(f"{action} {len(files)} first-party C/C++ file(s).", flush=True)
    return 1 if failed else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except subprocess.CalledProcessError as error:
        print(error.stderr or str(error), file=sys.stderr)
        sys.exit(error.returncode)
