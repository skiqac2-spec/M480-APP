#!/usr/bin/env python3
"""downloads/files.in -> downloads/files.txt (파일 크기 자동 기입)"""
import os, sys
root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "downloads")
out = []
for line in open(os.path.join(root, "files.in"), encoding="utf-8"):
    line = line.rstrip("\r\n")
    if not line or line.startswith("#") or line.startswith("@"):
        out.append(line)
        continue
    f = line.split("\t")
    path = os.path.normpath(os.path.join(root, f[0]))
    if not os.path.exists(path):
        sys.exit("missing: " + f[0])
    f[1] = str(os.path.getsize(path))
    out.append("\t".join(f))
open(os.path.join(root, "files.txt"), "w", encoding="utf-8", newline="\n").write("\n".join(out) + "\n")
print("files.txt updated")
