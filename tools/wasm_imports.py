#!/usr/bin/env python3
"""List wasm module imports (module, name, kind) - minimal section parser."""
import sys

def uleb(b, i):
    r = 0; s = 0
    while True:
        x = b[i]; i += 1
        r |= (x & 0x7F) << s
        if not (x & 0x80):
            return r, i
        s += 7

def main(path):
    b = open(path, "rb").read()
    assert b[:4] == b"\x00asm", "not wasm"
    i = 8
    while i < len(b):
        sid = b[i]; i += 1
        size, i = uleb(b, i)
        end = i + size
        if sid == 2:  # import
            j = i
            cnt, j = uleb(b, j)
            for _ in range(cnt):
                ml, j = uleb(b, j); mod = b[j:j+ml].decode(); j += ml
                nl, j = uleb(b, j); nm = b[j:j+nl].decode(); j += nl
                kind = b[j]; j += 1
                if kind == 0: _, j = uleb(b, j)          # func typeidx
                elif kind == 1: j += 2                     # table
                elif kind == 2: j += 1 + (1 if b[j+1] else 0) + 1  # mem lim
                elif kind == 3: j += 2                     # global
                print(f"import \"{mod}\" \"{nm}\" kind={kind}")
        i = end

if __name__ == "__main__":
    for p in sys.argv[1:]:
        print(f"== {p} ==")
        main(p)
