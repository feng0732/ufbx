#!/usr/bin/env python3
# Helper (not delivered): splice the rewritten bake-quat tests into test_animation.h
import io

path = "/srv/fb2/feng0732/0161-ufbx-9-ebfc6621/test/test_animation.h"
new_tests_path = "/srv/fb2/feng0732/0161-ufbx-9-ebfc6621/build-local/probe/new_tests.inc"

with io.open(path, "r", encoding="utf-8") as f:
    lines = f.readlines()

# 1-based: keep through line 4095 (#endif of helper block) + blank line 4096,
# replace 4097..4404 (the four tests), keep trailing content from 4405.
head = lines[:4096]          # lines 1..4096
tail = lines[4404:]          # line 4405..

with io.open(new_tests_path, "r", encoding="utf-8") as f:
    new = f.read()

with io.open(path, "w", encoding="utf-8") as f:
    f.writelines(head)
    f.write(new)
    if not new.endswith("\n"):
        f.write("\n")
    f.writelines(tail)
print("spliced; head ends at line", len(head), "tail starts at", 4405)
