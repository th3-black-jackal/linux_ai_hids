#!/usr/bin/env python3
"""Write a schema-correct model.bin with known (seeded) weights and compute the
reference predictions in NumPy, so the C++ hids_replay forward pass can be
diffed against an independent implementation.

This does NOT produce a trained model -- weights are random. Its only job is to
verify that Model::predict in C++ matches the math. Real accuracy comes from the
model.bin your export_model.py writes.

  python3 make_ref_model.py <out.bin> <sample.csv> <ref_preds.txt> \
          [--n-in 31 --h1 128 --h2 128 --n-out 9 --seed 0]

.bin layout (little-endian), identical to Generated_model_schema:
  int32  n_in, h1, h2, n_out
  f32[n_in]      feature_min      (0.0 here -> normalize is identity on [0,1])
  f32[n_in]      feature_max      (1.0 here)
  f32[h1*n_in]   W1  f32[h1]   b1   (W row-major [out][in])
  f32[h2*h1]     W2  f32[h2]   b2
  f32[n_out*h2]  W3  f32[n_out] b3
"""
import argparse
import struct
import sys

import numpy as np

# FEATURE_ORDER must match model_meta.hpp exactly.
FEATURE_ORDER = [
    "madvise", "setgroups32", "statfs64", "seconds_RES_data", "getsockname",
    "connect", "block:block_unplug_KERN_data", "ext4:ext4_ext_rm_leaf", "socket",
    "jbd2:jbd2_handle_extend", "util", "write_kbs", "rename", "prlimit64",
    "write_merge", "dup2", "recv", "ext4:ext4_da_update_reserve_space_RES_data",
    "iowrite", "fchmod", "setitimer", "mkdir", "inotify_add_watch",
    "ext4:ext4_ext_remove_space_done", "pipe2",
    "writeback:sb_clear_inode_writeback_KERN_data", "brk", "shutdown",
    "getegid32", "iowritetime", "geteuid32",
]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("out_bin")
    ap.add_argument("sample_csv")
    ap.add_argument("ref_preds")
    ap.add_argument("--n-in", type=int, default=31)
    ap.add_argument("--h1", type=int, default=128)
    ap.add_argument("--h2", type=int, default=128)
    ap.add_argument("--n-out", type=int, default=9)
    ap.add_argument("--seed", type=int, default=0)
    a = ap.parse_args()

    rng = np.random.default_rng(a.seed)
    # float32 throughout so the reference matches the C++ f32 arithmetic closely.
    W1 = rng.standard_normal((a.h1, a.n_in), dtype=np.float32) * np.float32(0.1)
    b1 = rng.standard_normal(a.h1, dtype=np.float32) * np.float32(0.1)
    W2 = rng.standard_normal((a.h2, a.h1), dtype=np.float32) * np.float32(0.1)
    b2 = rng.standard_normal(a.h2, dtype=np.float32) * np.float32(0.1)
    W3 = rng.standard_normal((a.n_out, a.h2), dtype=np.float32) * np.float32(0.1)
    b3 = rng.standard_normal(a.n_out, dtype=np.float32) * np.float32(0.1)

    fmin = np.zeros(a.n_in, dtype=np.float32)
    fmax = np.ones(a.n_in, dtype=np.float32)

    with open(a.out_bin, "wb") as fh:
        fh.write(struct.pack("<4i", a.n_in, a.h1, a.h2, a.n_out))
        fh.write(fmin.tobytes())
        fh.write(fmax.tobytes())
        fh.write(W1.tobytes()); fh.write(b1.tobytes())
        fh.write(W2.tobytes()); fh.write(b2.tobytes())
        fh.write(W3.tobytes()); fh.write(b3.tobytes())

    # Read the sample, reorder columns into FEATURE_ORDER by name (same contract
    # the C++ tool enforces).
    with open(a.sample_csv) as fh:
        header = fh.readline().strip().split(",")
        idx = {name: i for i, name in enumerate(header)}
        missing = [f for f in FEATURE_ORDER[: a.n_in] if f not in idx]
        if missing:
            print(f"sample missing columns: {missing}", file=sys.stderr)
            return 1
        rows = []
        for line in fh:
            line = line.strip()
            if not line:
                continue
            rows.append(line.split(","))

    def relu(v):
        return np.maximum(v, np.float32(0))

    with open(a.ref_preds, "w") as out:
        for r, cells in enumerate(rows, start=1):
            x = np.array([np.float32(cells[idx[f]]) for f in FEATURE_ORDER[: a.n_in]],
                         dtype=np.float32)
            h = relu(W1 @ x + b1)
            h = relu(W2 @ h + b2)
            z = W3 @ h + b3               # log_softmax is monotonic -> argmax(z)
            pred = int(np.argmax(z))
            y = int(cells[idx["label"]]) if "label" in idx else -1
            out.write(f"{r} {y} {pred}\n")

    print(f"wrote {a.out_bin} and reference predictions -> {a.ref_preds}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
