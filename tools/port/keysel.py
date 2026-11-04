"""Probe-window selection shared by gen_data.py and pp_assets.py.

A block is located in the ROM by two 8-byte windows at 4-aligned offsets.
Windows with few distinct bytes (zero runs, padding) occur everywhere in a
ROM, so the most distinctive windows are chosen. A block whose content is one
repeated byte (apart from holes) is not searched at all: it is filled.
Returns (key1, off1, key2, off2, fill): key = 8 bytes, off = -1 if none,
fill = the byte value if the block is constant, else None.
"""


def select(data, free=None):
    n = len(data)
    best = []
    seen = set()
    for q in range(0, n - 7, 4):
        if free is not None and not all(free[q:q + 8]):
            continue
        w = bytes(data[q:q + 8])
        s = len(set(w))
        if s < 2:
            continue
        best.append((s, q, w))
    if not best:
        vals = set(data[i] for i in range(n) if free is None or free[i])
        fill = vals.pop() if len(vals) == 1 else 0
        return (None, -1, None, -1, fill)
    best.sort(key=lambda t: (-t[0], t[1]))
    s1, off1, key1 = best[0]
    key2, off2 = None, -1
    for s, q, w in best:
        if abs(q - off1) >= 8:
            key2, off2 = w, q
            break
    return (key1, off1, key2, off2, None)
