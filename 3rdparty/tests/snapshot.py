#!/usr/bin/env python3
"""Record dependency contents and mtimes before/after a CUBRID clean rebuild."""
import hashlib
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1]).resolve()
result = {}
for path in sorted(root.rglob("*")):
    if path.is_file() or path.is_symlink():
        stat = path.lstat()
        result[str(path.relative_to(root))] = {
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            "mtime_ns": stat.st_mtime_ns,
            "symlink": str(path.readlink()) if path.is_symlink() else None,
        }
json.dump(result, sys.stdout, indent=2, sort_keys=True)
print()
