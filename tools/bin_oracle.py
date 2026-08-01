#!/usr/bin/env python3
"""Independent NumPy reference for the C++ hids_replay forward pass.

Loads an EXISTING model.bin (the one your export_model.py wrote) and dumps
predictions over the same CSV files, in the same deterministic order as the C++
tool, so the two prediction streams can be diffed row-for-row. ~100% agreement
means the C++ service reproduces the model exactly; that is the real port test,
independent of train/test split or model quality.

  python3 bin_oracle.py <model.bin> <csv-or-dir> [<csv-or-dir> ...] \
          [--raw] [--dump]

  --raw   apply the min/max scaler before inference (match C++ --raw). Omit for
          the released, already-normalized dataset.
  --dump  print "<index> <true> <pred>" per row (true = -1 if unlabeled).

Without --dump it prints accuracy + macro-F1, mirroring the C++ report.
"""
import argparse
import os
import struct
import sys

import numpy as np

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
CLASS_NAMES = ["normal", "ransomware", "thetick", "bashlite", "httpbackdoor",
               "beurk", "backdoor", "bdvl", "xmrig"]


def load_bin(path):
    with open(path, "rb") as fh:
        n_in, h1, h2, n_out = struct.unpack("<4i", fh.read(16))

        def f32(n):
            return np.frombuffer(fh.read(4 * n), dtype="<f4").astype(np.float32)

        fmin = f32(n_in)
        fmax = f32(n_in)
        W1 = f32(h1 * n_in).reshape(h1, n_in); b1 = f32(h1)
        W2 = f32(h2 * h1).reshape(h2, h1);     b2 = f32(h2)
        W3 = f32(n_out * h2).reshape(n_out, h2); b3 = f32(n_out)
        if fh.read(1):
            print("warning: trailing bytes in model.bin", file=sys.stderr)
    return dict(n_in=n_in, h1=h1, h2=h2, n_out=n_out, fmin=fmin, fmax=fmax,
                W1=W1, b1=b1, W2=W2, b2=b2, W3=W3, b3=b3)


def gather_csvs(inputs):
    files = []
    for p in inputs:
        if os.path.isdir(p):
            for root, _, names in os.walk(p):
                files += [os.path.join(root, n) for n in names if n.endswith(".csv")]
        else:
            files.append(p)
    return sorted(files)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("model_bin")
    ap.add_argument("inputs", nargs="+")
    ap.add_argument("--raw", action="store_true")
    ap.add_argument("--dump", action="store_true")
    a = ap.parse_args()

    m = load_bin(a.model_bin)
    n_in, K = m["n_in"], m["n_out"]
    if n_in != len(FEATURE_ORDER):
        print(f"warning: model n_in={n_in} != {len(FEATURE_ORDER)} FEATURE_ORDER",
              file=sys.stderr)
    denom = np.where(m["fmax"] - m["fmin"] > 0, m["fmax"] - m["fmin"], np.float32(1))

    def predict(x):
        if a.raw:
            x = np.clip((x - m["fmin"]) / denom, 0, 1).astype(np.float32)
        h = np.maximum(m["W1"] @ x + m["b1"], np.float32(0))
        h = np.maximum(m["W2"] @ h + m["b2"], np.float32(0))
        z = m["W3"] @ h + m["b3"]
        return int(np.argmax(z))

    files = gather_csvs(a.inputs)
    if not files:
        print("error: no CSV inputs found", file=sys.stderr)
        return 1
    if not a.dump:
        print(f"reading {len(files)} file(s)...", file=sys.stderr)

    cm = np.zeros((K, K), dtype=np.int64)
    total = correct = unlabeled = bad = gidx = 0
    for path in files:
        with open(path) as fh:
            header = fh.readline().strip().split(",")
            idx = {n: i for i, n in enumerate(header)}
            miss = [f for f in FEATURE_ORDER[:n_in] if f not in idx]
            if miss:
                print(f"error: {path} missing {miss}", file=sys.stderr)
                return 1
            lcol = idx.get("label")
            for line in fh:
                line = line.strip()
                if not line:
                    continue
                v = line.split(",")
                try:
                    x = np.array([np.float32(v[idx[f]]) for f in FEATURE_ORDER[:n_in]],
                                 dtype=np.float32)
                    pred = predict(x)
                    y = int(v[lcol]) if lcol is not None else -1
                    if y >= K or y < -1:
                        y = -2
                except (ValueError, IndexError):
                    bad += 1
                    continue
                gidx += 1
                if a.dump:
                    print(f"{gidx} {y} {pred}")
                if y == -1:
                    unlabeled += 1
                elif y == -2:
                    bad += 1
                else:
                    cm[y][pred] += 1
                    total += 1
                    correct += pred == y

    if a.dump:
        return 0
    print(f"\nfiles: {len(files)}   labeled rows: {total}   "
          f"unlabeled: {unlabeled}   unparseable: {bad}")
    print(f"scaler: {'applied' if a.raw else 'skipped (pre-normalized)'}")
    if total == 0:
        print("(no labels -> agreement-only run)")
        return 0
    print(f"\noverall accuracy: {correct / total:.4f}\n")
    f1s = []
    print(f"{'class':<14}{'support':>8}{'prec':>8}{'recall':>8}{'f1':>8}")
    for c in range(K):
        tp = cm[c][c]
        fp = cm[:, c].sum() - tp
        fn = cm[c, :].sum() - tp
        support = cm[c, :].sum()
        prec = tp / (tp + fp) if tp + fp else 0.0
        rec = tp / (tp + fn) if tp + fn else 0.0
        f1 = 2 * prec * rec / (prec + rec) if prec + rec else 0.0
        f1s.append(f1)
        name = CLASS_NAMES[c] if c < len(CLASS_NAMES) else "?"
        print(f"{name:<14}{support:>8}{prec:>8.3f}{rec:>8.3f}{f1:>8.3f}")
    print(f"\nmacro F1: {sum(f1s) / K:.4f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
