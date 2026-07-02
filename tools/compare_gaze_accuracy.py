#!/usr/bin/env python3
"""Compare dumped gaze OM inputs/outputs with the original ONNX model.

The board code writes files like:
  gaze_input_00_0.bin
  gaze_input_00_0.nchw.f32
  gaze_input_00_1.bin
  gaze_raw_00_0.txt

Run this on the PC after copying /tmp/gaze_* from the board.
"""

from __future__ import annotations

import argparse
import math
import re
from pathlib import Path

import numpy as np


DEFAULT_ONNX = (
    r"D:\PycharmProjects\zen_desk\models"
    r"\gaze-estimation-adas-0002_60x60_fp32_op12.onnx"
)


def _prod(shape: list[int | str | None]) -> int | None:
    total = 1
    for dim in shape:
        if not isinstance(dim, int) or dim <= 0:
            return None
        total *= dim
    return total


def _resolve_path(value: str) -> Path:
    path = Path(value)
    if path.exists():
        return path

    match = re.match(r"^([A-Za-z]):[\\/](.*)$", value)
    if match:
        drive = match.group(1).lower()
        rest = match.group(2).replace("\\", "/")
        wsl_path = Path(f"/mnt/{drive}/{rest}")
        if wsl_path.exists():
            return wsl_path
    return path


def _read_txt_floats(path: Path) -> np.ndarray:
    values: list[float] = []
    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        parts = line.strip().split()
        if not parts:
            continue
        values.append(float(parts[-1]))
    return np.asarray(values, dtype=np.float32)


def _read_raw_floats(path: Path) -> np.ndarray:
    data = path.read_bytes()
    if len(data) % 4 != 0:
        raise ValueError(f"{path} size {len(data)} is not float32 aligned")
    return np.frombuffer(data, dtype=np.float32).copy()


def _find_frames(dump_dir: Path) -> list[int]:
    frames = set()
    pattern = re.compile(r"gaze_raw_(\d+)_\d+\.(?:txt|bin)$")
    for path in dump_dir.glob("gaze_raw_*_*.*"):
        match = pattern.match(path.name)
        if match:
            frames.add(int(match.group(1)))
    return sorted(frames)


def _load_om_output(dump_dir: Path, frame: int) -> np.ndarray | None:
    txt = dump_dir / f"gaze_raw_{frame:02d}_0.txt"
    bin_path = dump_dir / f"gaze_raw_{frame:02d}_0.bin"
    if txt.exists():
        return _read_txt_floats(txt)
    if bin_path.exists():
        return _read_raw_floats(bin_path)
    return None


def _load_input_for_onnx(
    dump_dir: Path,
    frame: int,
    input_idx: int,
    expected_shape: list[int | str | None],
) -> tuple[np.ndarray, str]:
    expected_count = _prod(expected_shape)
    bin_path = dump_dir / f"gaze_input_{frame:02d}_{input_idx}.bin"

    if not bin_path.exists():
        if expected_count is None:
            raise RuntimeError(
                f"missing input {input_idx} for frame {frame}, and ONNX shape is dynamic"
            )
        return np.zeros(expected_count, dtype=np.float32).reshape(expected_shape), "missing->zeros"

    raw = bin_path.read_bytes()
    if expected_count is not None and len(raw) >= expected_count * 4 and len(raw) % 4 == 0:
        arr = np.frombuffer(raw, dtype=np.float32).copy()[:expected_count].reshape(expected_shape)
        source = "dump" if len(raw) == expected_count * 4 else f"dump_padded({len(raw)}B)"
        return arr, source

    if expected_count is not None and len(raw) == expected_count:
        arr = np.frombuffer(raw, dtype=np.uint8).astype(np.float32).reshape(expected_shape)
        return arr, "dump_u8"

    raise RuntimeError(
        f"{bin_path.name} size={len(raw)} bytes does not match ONNX input "
        f"{input_idx} shape={expected_shape} ({expected_count} floats)"
    )


def _angle_deg(a: np.ndarray, b: np.ndarray) -> float | None:
    a3 = np.asarray(a[:3], dtype=np.float64)
    b3 = np.asarray(b[:3], dtype=np.float64)
    na = np.linalg.norm(a3)
    nb = np.linalg.norm(b3)
    if na < 1e-12 or nb < 1e-12:
        return None
    cosv = float(np.dot(a3, b3) / (na * nb))
    cosv = max(-1.0, min(1.0, cosv))
    return math.degrees(math.acos(cosv))


def _fmt(values: np.ndarray, max_num: int = 8) -> str:
    shown = values[:max_num]
    return "[" + ", ".join(f"{float(v):.6g}" for v in shown) + (
        ", ..." if values.size > max_num else ""
    ) + "]"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dump-dir", required=True, help="Directory containing gaze_* dumps")
    parser.add_argument("--onnx", default=DEFAULT_ONNX, help="Original ONNX model path")
    parser.add_argument("--max-frames", type=int, default=30)
    args = parser.parse_args()

    dump_dir = _resolve_path(args.dump_dir)
    onnx_path = _resolve_path(args.onnx)
    if not dump_dir.exists():
        raise SystemExit(f"dump dir does not exist: {dump_dir}")
    if not onnx_path.exists():
        raise SystemExit(f"onnx does not exist: {onnx_path}")

    try:
        import onnxruntime as ort
    except ImportError as exc:
        raise SystemExit(
            "onnxruntime is not installed. Install it in your Python env first: "
            "pip install onnxruntime"
        ) from exc

    sess = ort.InferenceSession(str(onnx_path), providers=["CPUExecutionProvider"])
    inputs = sess.get_inputs()
    outputs = sess.get_outputs()
    print(f"ONNX: {onnx_path}")
    print("Inputs:")
    for idx, item in enumerate(inputs):
        print(f"  [{idx}] {item.name} shape={item.shape} type={item.type}")
    print("Outputs:")
    for idx, item in enumerate(outputs):
        print(f"  [{idx}] {item.name} shape={item.shape} type={item.type}")

    frames = _find_frames(dump_dir)[: args.max_frames]
    if not frames:
        raise SystemExit(f"no gaze_raw_XX_N dumps found in {dump_dir}")

    all_abs: list[np.ndarray] = []
    all_angles: list[float] = []

    for frame in frames:
        feed = {}
        input_notes = []
        try:
            for idx, item in enumerate(inputs):
                arr, source = _load_input_for_onnx(dump_dir, frame, idx, item.shape)
                feed[item.name] = arr.astype(np.float32, copy=False)
                input_notes.append(f"{idx}:{source}")
        except RuntimeError as err:
            print(f"\nframe {frame:02d}: cannot compare directly: {err}")
            continue

        onnx_outs = sess.run(None, feed)
        onnx_vec = np.asarray(onnx_outs[0], dtype=np.float32).reshape(-1)
        om_vec = _load_om_output(dump_dir, frame)
        if om_vec is None:
            print(f"\nframe {frame:02d}: missing OM output")
            continue

        n = min(onnx_vec.size, om_vec.size)
        diff = om_vec[:n] - onnx_vec[:n]
        abs_diff = np.abs(diff)
        all_abs.append(abs_diff)
        angle = _angle_deg(om_vec, onnx_vec)
        if angle is not None:
            all_angles.append(angle)

        print(f"\nframe {frame:02d} inputs=({', '.join(input_notes)}) compare_len={n}")
        print(f"  OM   {om_vec.size}: {_fmt(om_vec)}")
        print(f"  ONNX {onnx_vec.size}: {_fmt(onnx_vec)}")
        print(
            f"  abs: mae={float(abs_diff.mean()):.6g} "
            f"max={float(abs_diff.max()):.6g}"
            + (f" angle={angle:.3f}deg" if angle is not None else " angle=n/a")
        )

    if all_abs:
        merged = np.concatenate(all_abs)
        print("\nSummary:")
        print(f"  frames compared: {len(all_abs)}")
        print(f"  abs mae: {float(merged.mean()):.6g}")
        print(f"  abs max: {float(merged.max()):.6g}")
        if all_angles:
            print(f"  angle mean: {float(np.mean(all_angles)):.3f}deg")
            print(f"  angle max: {float(np.max(all_angles)):.3f}deg")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
