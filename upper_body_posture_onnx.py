"""
基于单个 YOLO26-Pose ONNX 模型，检测：
1. 斜肩
2. 左手撑头 / 右手撑头
3. 双手撑头

支持摄像头、视频和图片。
模型推理仅加载一个 ONNX 文件；Ultralytics 负责图像预处理和结果解析。

安装:
    pip install -r requirements.txt

示例:
    # 摄像头
    python upper_body_posture_onnx.py --model yolo26n-pose.onnx --source 0 --device cpu

    # NVIDIA GPU（需安装 onnxruntime-gpu）
    python upper_body_posture_onnx.py --model yolo26n-pose.onnx --source 0 --device 0

    # 视频
    python upper_body_posture_onnx.py --model yolo26n-pose.onnx --source input.mp4 --output result.mp4
"""
from __future__ import annotations

import argparse
import math
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Optional, Tuple, Union

import cv2
import numpy as np
from ultralytics import YOLO


# COCO 17关键点索引
NOSE = 0
LEFT_EYE = 1
RIGHT_EYE = 2
LEFT_EAR = 3
RIGHT_EAR = 4
LEFT_SHOULDER = 5
RIGHT_SHOULDER = 6
LEFT_ELBOW = 7
RIGHT_ELBOW = 8
LEFT_WRIST = 9
RIGHT_WRIST = 10

UPPER_BODY_EDGES = (
    (LEFT_EAR, LEFT_EYE),
    (LEFT_EYE, NOSE),
    (NOSE, RIGHT_EYE),
    (RIGHT_EYE, RIGHT_EAR),
    (LEFT_SHOULDER, RIGHT_SHOULDER),
    (LEFT_SHOULDER, LEFT_ELBOW),
    (LEFT_ELBOW, LEFT_WRIST),
    (RIGHT_SHOULDER, RIGHT_ELBOW),
    (RIGHT_ELBOW, RIGHT_WRIST),
)


@dataclass
class DetectionConfig:
    person_conf: float = 0.35
    keypoint_conf: float = 0.40

    # 肩线相对水平线的绝对角度超过该阈值，认为存在斜肩
    shoulder_angle_deg: float = 7.0

    # 手腕到面部关键点的距离阈值，单位为“肩宽”
    wrist_head_distance_ratio: float = 0.48

    # 肘关节夹角过大时通常不是撑头动作
    max_elbow_angle_deg: float = 140.0

    # 事件至少连续满足多少帧后才显示，减少抖动误报
    hold_frames: int = 6

    # 摄像机只拍上半身时，建议保留较高分辨率
    image_size: int = 640


class TemporalLatch:
    """简单连续帧防抖。"""

    def __init__(self, hold_frames: int) -> None:
        self.hold_frames = max(1, hold_frames)
        self.counts: Dict[str, int] = {
            "sloped": 0,
            "left_support": 0,
            "right_support": 0,
        }

    def update(self, raw: Dict[str, bool]) -> Dict[str, bool]:
        stable: Dict[str, bool] = {}
        for name in self.counts:
            if raw.get(name, False):
                self.counts[name] = min(self.hold_frames, self.counts[name] + 1)
            else:
                # 缓慢衰减比立即清零更适合视频抖动
                self.counts[name] = max(0, self.counts[name] - 1)
            stable[name] = self.counts[name] >= self.hold_frames
        return stable


def point_valid(kpts: np.ndarray, idx: int, threshold: float) -> bool:
    return bool(kpts[idx, 2] >= threshold and np.isfinite(kpts[idx, :2]).all())


def distance(a: np.ndarray, b: np.ndarray) -> float:
    return float(np.linalg.norm(a[:2] - b[:2]))


def joint_angle(a: np.ndarray, b: np.ndarray, c: np.ndarray) -> float:
    """计算 ∠ABC，返回0~180度。"""
    ba = a[:2] - b[:2]
    bc = c[:2] - b[:2]
    denom = float(np.linalg.norm(ba) * np.linalg.norm(bc))
    if denom < 1e-6:
        return 180.0
    cosine = float(np.clip(np.dot(ba, bc) / denom, -1.0, 1.0))
    return math.degrees(math.acos(cosine))


def shoulder_angle(kpts: np.ndarray, cfg: DetectionConfig) -> Optional[float]:
    if not (
        point_valid(kpts, LEFT_SHOULDER, cfg.keypoint_conf)
        and point_valid(kpts, RIGHT_SHOULDER, cfg.keypoint_conf)
    ):
        return None

    left = kpts[LEFT_SHOULDER, :2]
    right = kpts[RIGHT_SHOULDER, :2]
    dx = float(right[0] - left[0])
    dy = float(right[1] - left[1])

    if abs(dx) < 1e-6:
        return 90.0

    angle = math.degrees(math.atan2(dy, dx))
    # 映射到[-90, 90]，避免左右顺序或人体转向造成180度跳变
    if angle > 90:
        angle -= 180
    elif angle < -90:
        angle += 180
    return angle


def hand_supports_head(
    kpts: np.ndarray,
    side: str,
    shoulder_width: float,
    cfg: DetectionConfig,
) -> Tuple[bool, Dict[str, float]]:
    """
    判定某只手是否撑头。

    规则同时考虑：
    - 手腕靠近鼻、眼、耳等面部关键点；
    - 手腕位于肩部以上或附近；
    - 肘部可见时，肘关节处于弯曲状态，且手腕不明显低于肘部。
    """
    if side == "left":
        wrist_idx, elbow_idx, shoulder_idx = LEFT_WRIST, LEFT_ELBOW, LEFT_SHOULDER
        head_indices = (NOSE, LEFT_EYE, LEFT_EAR, RIGHT_EYE)
    elif side == "right":
        wrist_idx, elbow_idx, shoulder_idx = RIGHT_WRIST, RIGHT_ELBOW, RIGHT_SHOULDER
        head_indices = (NOSE, RIGHT_EYE, RIGHT_EAR, LEFT_EYE)
    else:
        raise ValueError("side 必须是 'left' 或 'right'")

    debug = {
        "distance_ratio": float("inf"),
        "elbow_angle": 180.0,
    }

    if shoulder_width < 20 or not point_valid(kpts, wrist_idx, cfg.keypoint_conf):
        return False, debug

    valid_head = [
        kpts[i]
        for i in head_indices
        if point_valid(kpts, i, cfg.keypoint_conf)
    ]
    if len(valid_head) < 2:
        return False, debug

    wrist = kpts[wrist_idx]
    min_head_distance = min(distance(wrist, p) for p in valid_head)
    dist_ratio = min_head_distance / shoulder_width
    debug["distance_ratio"] = dist_ratio

    shoulder_mid_y = float(
        (kpts[LEFT_SHOULDER, 1] + kpts[RIGHT_SHOULDER, 1]) / 2.0
    )
    near_head = dist_ratio <= cfg.wrist_head_distance_ratio
    wrist_high_enough = float(wrist[1]) <= shoulder_mid_y + 0.20 * shoulder_width

    # 肘部若可靠可见，增加几何约束；若被画面裁切，则允许仅按手腕-面部关系判断。
    elbow_geometry_ok = True
    if (
        point_valid(kpts, elbow_idx, cfg.keypoint_conf)
        and point_valid(kpts, shoulder_idx, cfg.keypoint_conf)
    ):
        elbow = kpts[elbow_idx]
        shoulder = kpts[shoulder_idx]
        angle = joint_angle(shoulder, elbow, wrist)
        debug["elbow_angle"] = angle
        elbow_geometry_ok = (
            angle <= cfg.max_elbow_angle_deg
            and float(wrist[1]) <= float(elbow[1]) + 0.20 * shoulder_width
        )

    return bool(near_head and wrist_high_enough and elbow_geometry_ok), debug


def analyze_pose(
    kpts: np.ndarray, cfg: DetectionConfig
) -> Tuple[Dict[str, bool], Dict[str, float]]:
    raw = {
        "sloped": False,
        "left_support": False,
        "right_support": False,
    }
    metrics = {
        "shoulder_angle": float("nan"),
        "left_distance_ratio": float("inf"),
        "right_distance_ratio": float("inf"),
        "left_elbow_angle": float("nan"),
        "right_elbow_angle": float("nan"),
    }

    if not (
        point_valid(kpts, LEFT_SHOULDER, cfg.keypoint_conf)
        and point_valid(kpts, RIGHT_SHOULDER, cfg.keypoint_conf)
    ):
        return raw, metrics

    shoulder_width = distance(kpts[LEFT_SHOULDER], kpts[RIGHT_SHOULDER])
    if shoulder_width < 20:
        return raw, metrics

    angle = shoulder_angle(kpts, cfg)
    if angle is not None:
        metrics["shoulder_angle"] = angle
        raw["sloped"] = abs(angle) >= cfg.shoulder_angle_deg

    left_support, left_debug = hand_supports_head(
        kpts, "left", shoulder_width, cfg
    )
    right_support, right_debug = hand_supports_head(
        kpts, "right", shoulder_width, cfg
    )

    raw["left_support"] = left_support
    raw["right_support"] = right_support
    metrics["left_distance_ratio"] = left_debug["distance_ratio"]
    metrics["right_distance_ratio"] = right_debug["distance_ratio"]
    metrics["left_elbow_angle"] = left_debug["elbow_angle"]
    metrics["right_elbow_angle"] = right_debug["elbow_angle"]
    return raw, metrics


def choose_primary_person(result, min_conf: float) -> Optional[int]:
    if result.boxes is None or len(result.boxes) == 0:
        return None

    boxes = result.boxes.xyxy.detach().cpu().numpy()
    confs = result.boxes.conf.detach().cpu().numpy()
    valid = np.where(confs >= min_conf)[0]
    if valid.size == 0:
        return None

    areas = (boxes[:, 2] - boxes[:, 0]) * (boxes[:, 3] - boxes[:, 1])
    # 主要场景是单人近景，选择面积最大的可靠人体
    return int(valid[np.argmax(areas[valid])])


def draw_pose(
    frame: np.ndarray,
    kpts: np.ndarray,
    cfg: DetectionConfig,
) -> None:
    for a, b in UPPER_BODY_EDGES:
        if point_valid(kpts, a, cfg.keypoint_conf) and point_valid(
            kpts, b, cfg.keypoint_conf
        ):
            pa = tuple(np.round(kpts[a, :2]).astype(int))
            pb = tuple(np.round(kpts[b, :2]).astype(int))
            cv2.line(frame, pa, pb, (0, 255, 0), 2, cv2.LINE_AA)

    for idx in range(11):
        if point_valid(kpts, idx, cfg.keypoint_conf):
            p = tuple(np.round(kpts[idx, :2]).astype(int))
            cv2.circle(frame, p, 4, (0, 0, 255), -1, cv2.LINE_AA)


def event_labels(stable: Dict[str, bool]) -> Tuple[str, str]:
    left = stable["left_support"]
    right = stable["right_support"]

    cn_parts = []
    en_parts = []

    if left and right:
        cn_parts.append("双手撑头")
        en_parts.append("BOTH_HANDS_ON_HEAD")
    elif left:
        cn_parts.append("左手撑头")
        en_parts.append("LEFT_HAND_ON_HEAD")
    elif right:
        cn_parts.append("右手撑头")
        en_parts.append("RIGHT_HAND_ON_HEAD")

    if stable["sloped"]:
        cn_parts.append("斜肩")
        en_parts.append("SLOPED_SHOULDER")

    if not cn_parts:
        return "正常/未触发", "NORMAL"

    return " + ".join(cn_parts), " + ".join(en_parts)


def parse_source(source: str) -> Union[int, str]:
    return int(source) if source.isdigit() else source


def build_argparser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="使用单个YOLO26-Pose ONNX检测斜肩和撑头动作"
    )
    parser.add_argument("--model", default="yolo26n-pose.onnx")
    parser.add_argument("--source", default="0", help="摄像头编号、视频或图片路径")
    parser.add_argument("--output", default="", help="可选：输出视频路径")
    parser.add_argument("--device", default="cpu", help="cpu 或 0")
    parser.add_argument("--person-conf", type=float, default=0.35)
    parser.add_argument("--kpt-conf", type=float, default=0.40)
    parser.add_argument("--shoulder-angle", type=float, default=7.0)
    parser.add_argument("--wrist-head-ratio", type=float, default=0.48)
    parser.add_argument("--max-elbow-angle", type=float, default=140.0)
    parser.add_argument("--hold-frames", type=int, default=6)
    parser.add_argument("--imgsz", type=int, default=640)
    parser.add_argument("--no-display", action="store_true")
    return parser


def main() -> None:
    args = build_argparser().parse_args()

    model_path = Path(args.model)
    if not model_path.exists():
        raise FileNotFoundError(
            f"找不到模型: {model_path}\n"
            "请先运行 export_yolo26_pose_onnx.py 生成 ONNX 文件。"
        )

    cfg = DetectionConfig(
        person_conf=args.person_conf,
        keypoint_conf=args.kpt_conf,
        shoulder_angle_deg=args.shoulder_angle,
        wrist_head_distance_ratio=args.wrist_head_ratio,
        max_elbow_angle_deg=args.max_elbow_angle,
        hold_frames=args.hold_frames,
        image_size=args.imgsz,
    )

    model = YOLO(str(model_path), task="pose")
    source = parse_source(args.source)
    cap = cv2.VideoCapture(source)

    if not cap.isOpened():
        raise RuntimeError(f"无法打开输入源: {args.source}")

    writer = None
    if args.output:
        fps = cap.get(cv2.CAP_PROP_FPS)
        if not np.isfinite(fps) or fps <= 1:
            fps = 25.0
        width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
        height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
        writer = cv2.VideoWriter(
            args.output,
            cv2.VideoWriter_fourcc(*"mp4v"),
            fps,
            (width, height),
        )
        if not writer.isOpened():
            raise RuntimeError(f"无法创建输出视频: {args.output}")

    latch = TemporalLatch(cfg.hold_frames)
    previous_cn = ""

    try:
        while True:
            ok, frame = cap.read()
            if not ok:
                break

            results = model.predict(
                source=frame,
                imgsz=cfg.image_size,
                conf=cfg.person_conf,
                device=args.device,
                verbose=False,
            )
            result = results[0]
            primary = choose_primary_person(result, cfg.person_conf)

            raw = {
                "sloped": False,
                "left_support": False,
                "right_support": False,
            }
            metrics = {"shoulder_angle": float("nan")}

            if (
                primary is not None
                and result.keypoints is not None
                and result.keypoints.data is not None
            ):
                all_kpts = result.keypoints.data.detach().cpu().numpy()
                if primary < len(all_kpts):
                    kpts = all_kpts[primary]
                    raw, metrics = analyze_pose(kpts, cfg)
                    draw_pose(frame, kpts, cfg)

                    box = (
                        result.boxes.xyxy[primary]
                        .detach()
                        .cpu()
                        .numpy()
                        .round()
                        .astype(int)
                    )
                    x1, y1, x2, y2 = box.tolist()
                    cv2.rectangle(frame, (x1, y1), (x2, y2), (255, 255, 0), 2)

            stable = latch.update(raw)
            label_cn, label_en = event_labels(stable)

            angle = metrics.get("shoulder_angle", float("nan"))
            angle_text = (
                f"shoulder angle: {angle:+.1f} deg"
                if np.isfinite(angle)
                else "shoulder angle: unavailable"
            )

            cv2.putText(
                frame,
                label_en,
                (20, 40),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.75,
                (0, 0, 255) if label_en != "NORMAL" else (0, 180, 0),
                2,
                cv2.LINE_AA,
            )
            cv2.putText(
                frame,
                angle_text,
                (20, 72),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.65,
                (255, 255, 255),
                2,
                cv2.LINE_AA,
            )

            if label_cn != previous_cn:
                print(f"当前状态: {label_cn}")
                previous_cn = label_cn

            if writer is not None:
                writer.write(frame)

            if not args.no_display:
                cv2.imshow("Upper-body posture detection", frame)
                key = cv2.waitKey(1) & 0xFF
                if key in (27, ord("q")):
                    break
    finally:
        cap.release()
        if writer is not None:
            writer.release()
        cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
