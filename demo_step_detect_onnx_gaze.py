import time
from dataclasses import dataclass

import cv2
import numpy as np
import onnxruntime as ort

# 使用 old.py 中的 uniface Landmark106 封装来做关键点前后处理
try:
    from uniface import constants as uniface_constants
    from uniface.landmark import Landmark106 as UniFaceLandmark106
except Exception as e:
    uniface_constants = None
    UniFaceLandmark106 = None
    _UNIFACE_IMPORT_ERROR = e

# =========================================================
# 路径配置
# =========================================================
SCRFD_ONNX = "scrfd_500m_640x640_fp32_op11.onnx"
LANDMARK_ONNX = "2d106det_192x192_fp32_op12.onnx"
GAZE_ONNX = "gaze_estimation.onnx"
# ONNX 版本的 gaze y 轴与 OpenCV 图像坐标/原可视化方向相反；
# 只翻转 gaze 向量的第 2 个分量，左右方向和其他模型逻辑保持不变。
GAZE_FLIP_Y = True

# =========================================================
# 底层推理类：SCRFD 与 Landmark106
# =========================================================
def nms(boxes, scores, nms_thresh=0.4):
    if len(boxes) == 0: return []
    x1, y1, x2, y2 = boxes[:, 0], boxes[:, 1], boxes[:, 2], boxes[:, 3]
    areas = np.maximum(0.0, x2 - x1 + 1.0) * np.maximum(0.0, y2 - y1 + 1.0)
    order = scores.argsort()[::-1]
    keep = []
    while order.size > 0:
        i = order[0]
        keep.append(i)
        xx1 = np.maximum(x1[i], x1[order[1:]])
        yy1 = np.maximum(y1[i], y1[order[1:]])
        xx2 = np.minimum(x2[i], x2[order[1:]])
        yy2 = np.minimum(y2[i], y2[order[1:]])
        w = np.maximum(0.0, xx2 - xx1 + 1.0)
        h = np.maximum(0.0, yy2 - yy1 + 1.0)
        inter = w * h
        iou = inter / (areas[i] + areas[order[1:]] - inter + 1e-6)
        inds = np.where(iou <= nms_thresh)[0]
        order = order[inds + 1]
    return keep

def distance2bbox(points, distance):
    x1 = points[:, 0] - distance[:, 0]
    y1 = points[:, 1] - distance[:, 1]
    x2 = points[:, 0] + distance[:, 2]
    y2 = points[:, 1] + distance[:, 3]
    return np.stack([x1, y1, x2, y2], axis=-1)

class SCRFD:
    def __init__(self, model_path, input_size=(640, 640), conf_thresh=0.5, nms_thresh=0.4):
        self.input_size = input_size
        self.conf_thresh = conf_thresh
        self.nms_thresh = nms_thresh
        self.strides = [8, 16, 32]
        self.session = ort.InferenceSession(model_path, providers=["CPUExecutionProvider"])
        self.input_name = self.session.get_inputs()[0].name
        self.output_names = [o.name for o in self.session.get_outputs()]

    def detect(self, img):
        h, w = img.shape[:2]
        input_w, input_h = self.input_size
        ratio = min(input_w / w, input_h / h)
        new_w, new_h = int(round(w * ratio)), int(round(h * ratio))
        resized = cv2.resize(img, (new_w, new_h), interpolation=cv2.INTER_LINEAR)
        det_img = np.zeros((input_h, input_w, 3), dtype=np.uint8)
        det_img[:new_h, :new_w, :] = resized
        blob = cv2.dnn.blobFromImage(det_img, 1.0/128.0, (input_w, input_h), (127.5, 127.5, 127.5), swapRB=True).astype(np.float32)
        
        outputs = self.session.run(self.output_names, {self.input_name: blob})
        score_outputs, bbox_outputs = [], []
        for out in outputs:
            arr = np.squeeze(np.asarray(out))
            if arr.ndim == 0: continue
            if arr.ndim == 1: arr = arr.reshape(-1, 1)
            if arr.shape[-1] == 1: score_outputs.append(arr.astype(np.float32))
            elif arr.shape[-1] == 4: bbox_outputs.append(arr.astype(np.float32))

        score_outputs = sorted(score_outputs, key=lambda x: x.shape[0], reverse=True)
        bbox_outputs = sorted(bbox_outputs, key=lambda x: x.shape[0], reverse=True)

        all_boxes, all_scores = [], []
        for idx, stride in enumerate(self.strides):
            scores = score_outputs[idx].reshape(-1)
            bbox_preds = bbox_outputs[idx]
            feat_h, feat_w = input_h // stride, input_w // stride
            num_anchors = scores.shape[0] // (feat_h * feat_w)
            if num_anchors <= 0: continue
            
            shift_x, shift_y = np.arange(0, feat_w) * stride, np.arange(0, feat_h) * stride
            xv, yv = np.meshgrid(shift_x, shift_y)
            centers = np.stack((xv, yv), axis=-1).reshape(-1, 2)
            if num_anchors > 1: centers = np.stack([centers] * num_anchors, axis=1).reshape(-1, 2)
            anchor_centers = centers.astype(np.float32)
            
            pos_inds = np.where(scores >= self.conf_thresh)[0]
            if len(pos_inds) == 0: continue
            boxes = distance2bbox(anchor_centers[pos_inds], bbox_preds[pos_inds] * stride) / ratio
            all_boxes.append(boxes)
            all_scores.append(scores[pos_inds])

        if len(all_boxes) == 0: return np.empty((0, 4), dtype=np.float32), np.empty((0,), dtype=np.float32)
        boxes, scores = np.concatenate(all_boxes, axis=0), np.concatenate(all_scores, axis=0)
        boxes[:, [0, 2]] = np.clip(boxes[:, [0, 2]], 0, img.shape[1] - 1)
        boxes[:, [1, 3]] = np.clip(boxes[:, [1, 3]], 0, img.shape[0] - 1)
        keep = nms(boxes, scores, self.nms_thresh)
        return boxes[keep], scores[keep]

class Landmark106:
    def __init__(self, model_path, input_size=192):
        self.input_size = input_size
        self.session = ort.InferenceSession(model_path, providers=["CPUExecutionProvider"])
        self.input_name = self.session.get_inputs()[0].name
        self.output_name = self.session.get_outputs()[0].name

    def infer(self, img, bbox, scale_factor=1.5):
        x1, y1, x2, y2 = bbox.astype(np.float32)
        cx, cy = (x1 + x2) * 0.5, (y1 + y2) * 0.5
        size = max(x2 - x1, y2 - y1) * scale_factor
        src = np.array([[cx - size*0.5, cy - size*0.5], [cx + size*0.5, cy - size*0.5], [cx - size*0.5, cy + size*0.5]], dtype=np.float32)
        dst = np.array([[0, 0], [self.input_size-1, 0], [0, self.input_size-1]], dtype=np.float32)
        M = cv2.getAffineTransform(src, dst)
        crop = cv2.warpAffine(img, M, (self.input_size, self.input_size), flags=cv2.INTER_LINEAR)
        
        blob = cv2.dnn.blobFromImage(crop, 1.0/128.0, (self.input_size, self.input_size), (127.5, 127.5, 127.5), swapRB=True).astype(np.float32)
        pred = self.session.run([self.output_name], {self.input_name: blob})[0].reshape(-1, 2).astype(np.float32)
        if np.max(np.abs(pred)) <= 2.5: pred = (pred + 1.0) * (self.input_size * 0.5)
        return np.concatenate([pred, np.ones((pred.shape[0], 1), dtype=np.float32)], axis=1) @ cv2.invertAffineTransform(M).T


class UniFaceLandmarkWrapper:
    """old.py 同款 Landmark106 前后处理封装。"""

    def __init__(self, model_path, providers=None):
        if UniFaceLandmark106 is None:
            raise ImportError(
                "未找到 uniface。这个版本需要与你的 old.py 一样安装 uniface。"
            ) from _UNIFACE_IMPORT_ERROR

        providers = providers or ["CPUExecutionProvider"]
        self.model = UniFaceLandmark106(
            providers=providers,
            model_name=uniface_constants.LandmarkWeights.DEFAULT,
        )
        self.model.session = ort.InferenceSession(model_path, providers=providers)

    def infer(self, img, bbox):
        landmarks = self.model.get_landmarks(img, bbox)
        if landmarks is None:
            return None
        return np.asarray(landmarks, dtype=np.float32)


# =========================================================
# ONNX Runtime 视线模型：替代 gaze-estimation-adas-0002.xml + .bin
# =========================================================
class ONNXGaze:
    """
    只替换原 OpenVINO gaze-estimation-adas-0002 的推理后端。

    兼容同构 ONNX gaze 模型：
      - 左眼输入:  [1, 3, 60, 60]
      - 右眼输入:  [1, 3, 60, 60]
      - 头姿输入:  [1, 3]，顺序为 yaw, pitch, roll
      - 输出:      3 维 gaze vector，可能是 [1,3] 或 [1,1,1,3]

    注意：当前主流程没有 head-pose 模型，保持原脚本行为，默认传入 (0,0,0)。
    """

    def __init__(self, onnx_path, providers=None, flip_y=False):
        providers = providers or ["CPUExecutionProvider"]
        self.flip_y = bool(flip_y)
        self.session = ort.InferenceSession(onnx_path, providers=providers)
        self.inputs = self.session.get_inputs()
        self.outputs = self.session.get_outputs()
        self.output_name = self.outputs[0].name

        self.input_names = [i.name for i in self.inputs]
        self.input_meta = {i.name: i for i in self.inputs}

        # 优先按名字找；若转换后的 ONNX 名字丢失，则按形状兜底。
        self.head_name = self._find_head_input()
        self.left_name, self.right_name = self._find_eye_inputs()

    @staticmethod
    def _shape_without_dynamic(shape):
        return [int(x) if isinstance(x, (int, np.integer)) else -1 for x in shape]

    @staticmethod
    def _onnx_type_to_dtype(type_str):
        t = str(type_str).lower()
        if "uint8" in t:
            return np.uint8
        if "float16" in t:
            return np.float16
        if "float" in t:
            return np.float32
        # gaze 模型正常只会用 uint8/float/float16；未知类型按 float32 兜底。
        return np.float32

    def _find_head_input(self):
        for inp in self.inputs:
            name = inp.name.lower()
            if any(k in name for k in ["head", "angle", "pose"]):
                return inp.name

        for inp in self.inputs:
            shape = self._shape_without_dynamic(inp.shape)
            if len(shape) == 2 and shape[-1] == 3:
                return inp.name

        raise RuntimeError(
            f"无法在 ONNX 模型中找到 head_pose_angles 输入；当前输入为: "
            f"{[(i.name, i.shape, i.type) for i in self.inputs]}"
        )

    def _find_eye_inputs(self):
        left = right = None
        for inp in self.inputs:
            name = inp.name.lower()
            if "left" in name:
                left = inp.name
            elif "right" in name:
                right = inp.name

        if left is not None and right is not None:
            return left, right

        eye_inputs = []
        for inp in self.inputs:
            if inp.name == self.head_name:
                continue
            shape = self._shape_without_dynamic(inp.shape)
            if len(shape) == 4 and shape[1] == 3 and shape[2] == 60 and shape[3] == 60:
                eye_inputs.append(inp.name)

        if len(eye_inputs) >= 2:
            # ADAS gaze ONNX 通常顺序为 left_eye_image, right_eye_image, head_pose_angles。
            return eye_inputs[0], eye_inputs[1]

        raise RuntimeError(
            f"无法在 ONNX 模型中找到左右眼输入；当前输入为: "
            f"{[(i.name, i.shape, i.type) for i in self.inputs]}"
        )

    def _make_eye_blob(self, eye_bgr, input_name):
        if eye_bgr is None or eye_bgr.size == 0:
            raise ValueError("eye crop is empty")
        if eye_bgr.shape[:2] != (60, 60):
            eye_bgr = cv2.resize(eye_bgr, (60, 60), interpolation=cv2.INTER_LINEAR)

        blob = eye_bgr.transpose(2, 0, 1)[None, :, :, :]
        dtype = self._onnx_type_to_dtype(self.input_meta[input_name].type)
        if dtype == np.uint8:
            return np.clip(blob, 0, 255).astype(np.uint8)
        return blob.astype(dtype)

    def _make_head_blob(self, head_pose):
        dtype = self._onnx_type_to_dtype(self.input_meta[self.head_name].type)
        return np.array(head_pose, dtype=dtype).reshape(1, 3)

    def infer(self, left_eye_bgr, right_eye_bgr, head_pose=(0.0, 0.0, 0.0)):
        feed = {
            self.left_name: self._make_eye_blob(left_eye_bgr, self.left_name),
            self.right_name: self._make_eye_blob(right_eye_bgr, self.right_name),
            self.head_name: self._make_head_blob(head_pose),
        }
        out = self.session.run([self.output_name], feed)[0]
        gaze_vec = np.asarray(out, dtype=np.float32).reshape(-1)[:3].copy()

        # 坐标系修正：该 ONNX 输出的 y 分量在你的摄像头显示中表现为上下反向。
        # 翻转 y 后，向上看箭头向上，向下看箭头向下；x 左右方向不变。
        if self.flip_y and gaze_vec.size >= 2:
            gaze_vec[1] *= -1.0

        return gaze_vec

def classify_gaze(gaze, x_thr=0.18, y_thr=0.18):
    g = np.asarray(gaze, dtype=np.float32).reshape(-1) / (np.linalg.norm(gaze) + 1e-6)
    x, y = float(g[0]), float(g[1])
    if abs(x) < x_thr and abs(y) < y_thr: return "front"
    if abs(x) >= abs(y): return "left" if x < -x_thr else "right"
    return "up" if y < -y_thr else "down"

def crop_square(img, center, size, out_size=60):
    cx, cy, half = int(round(center[0])), int(round(center[1])), int(round(size * 0.5))
    x1, y1, x2, y2 = cx - half, cy - half, cx + half, cy + half
    pad_left, pad_top = max(0, -x1), max(0, -y1)
    pad_right, pad_bottom = max(0, x2 - img.shape[1]), max(0, y2 - img.shape[0])
    if pad_left or pad_top or pad_right or pad_bottom:
        img = cv2.copyMakeBorder(img, pad_top, pad_bottom, pad_left, pad_right, cv2.BORDER_CONSTANT, value=(0, 0, 0))
        x1 += pad_left; x2 += pad_left; y1 += pad_top; y2 += pad_top
    crop = img[y1:y2, x1:x2]
    return cv2.resize(crop, (out_size, out_size), interpolation=cv2.INTER_LINEAR) if crop.size > 0 else None


def bbox_area(bbox):
    x1, y1, x2, y2 = bbox
    return max(0.0, float(x2 - x1)) * max(0.0, float(y2 - y1))


def bbox_iou(a, b):
    if a is None or b is None:
        return 0.0
    ax1, ay1, ax2, ay2 = map(float, a)
    bx1, by1, bx2, by2 = map(float, b)
    ix1, iy1 = max(ax1, bx1), max(ay1, by1)
    ix2, iy2 = min(ax2, bx2), min(ay2, by2)
    iw, ih = max(0.0, ix2 - ix1), max(0.0, iy2 - iy1)
    inter = iw * ih
    return inter / (bbox_area(a) + bbox_area(b) - inter + 1e-6)


def select_target_bbox(boxes, scores, frame_shape, prev_bbox=None,
                       min_area_ratio=0.035, lock_iou_thresh=0.12):
    """多人脸场景下稳定选择主目标。"""
    if boxes is None or len(boxes) == 0:
        return None

    h, w = frame_shape[:2]
    frame_area = float(w * h)
    boxes = np.asarray(boxes, dtype=np.float32)
    scores = np.asarray(scores, dtype=np.float32) if scores is not None and len(scores) == len(boxes) else np.ones((len(boxes),), dtype=np.float32)

    valid = [i for i, b in enumerate(boxes) if bbox_area(b) / frame_area >= min_area_ratio]
    if not valid:
        return None

    if prev_bbox is not None:
        ious = np.array([bbox_iou(prev_bbox, boxes[i]) for i in valid], dtype=np.float32)
        best_local = int(np.argmax(ious))
        if ious[best_local] >= lock_iou_thresh:
            return boxes[valid[best_local]]

    fw, fh = float(w), float(h)
    diag = (fw * fw + fh * fh) ** 0.5
    weighted_scores = []
    for i in valid:
        b = boxes[i]
        area_score = bbox_area(b) / frame_area
        cx, cy = (b[0] + b[2]) * 0.5, (b[1] + b[3]) * 0.5
        center_dist = (((cx - fw * 0.5) ** 2 + (cy - fh * 0.5) ** 2) ** 0.5) / (diag + 1e-6)
        weighted_scores.append(area_score * 2.0 + float(scores[i]) * 0.5 - center_dist * 0.8)

    return boxes[valid[int(np.argmax(weighted_scores))]]


def draw_landmark_indices(frame, landmarks, indices, color, prefix="", font_scale=0.42):
    for idx in indices:
        pt = landmarks[idx]
        x, y = int(pt[0]), int(pt[1])
        cv2.circle(frame, (x, y), 3, color, -1)
        cv2.putText(frame, f"{prefix}{idx}", (x + 2, y - 2),
                    cv2.FONT_HERSHEY_SIMPLEX, font_scale, color, 1)

# =========================================================
# EAR 和 MAR：完全使用 old.py 的 106 点切片逻辑
# =========================================================
LEFT_EYE_SLICE = slice(33, 43)
RIGHT_EYE_SLICE = slice(87, 97)
MOUTH_SLICE = slice(52, 72)


def eye_aspect_ratio(eye_landmarks):
    """old.py 同款 EAR。eye_landmarks 应为 10 个眼部点。"""
    v1 = np.linalg.norm(eye_landmarks[8] - eye_landmarks[3])
    v2 = np.linalg.norm(eye_landmarks[7] - eye_landmarks[0])
    v3 = np.linalg.norm(eye_landmarks[9] - eye_landmarks[4])
    h = np.linalg.norm(eye_landmarks[2] - eye_landmarks[6])

    if h < 1e-6:
        return 0.0
    return (v1 + v2 + v3) / (3.0 * h)


def mouth_aspect_ratio(mouth_landmarks) -> float:
    """old.py 同款 MAR。mouth_landmarks 应为 20 个嘴部点。"""
    v1 = np.linalg.norm(mouth_landmarks[4] - mouth_landmarks[2])
    v2 = np.linalg.norm(mouth_landmarks[10] - mouth_landmarks[8])
    v3 = np.linalg.norm(mouth_landmarks[18] - mouth_landmarks[5])
    h = np.linalg.norm(mouth_landmarks[13] - mouth_landmarks[17])

    if h < 1e-6:
        return 0.0
    return (v1 + v2 + v3) / (3.0 * h)

# =========================================================
# 初始化
# =========================================================
detector = SCRFD(SCRFD_ONNX, input_size=(640, 640), conf_thresh=0.5)
landmarker = UniFaceLandmarkWrapper(LANDMARK_ONNX, providers=["CPUExecutionProvider"])
gaze_model = ONNXGaze(GAZE_ONNX, providers=["CPUExecutionProvider"], flip_y=GAZE_FLIP_Y)

# 状态记录
from dataclasses import dataclass, field
@dataclass
class FaceState:
    blink_count: int = 0
    closed_frames: int = 0
    eyes_closed: bool = False
    yawn_count: int = 0
    yawning: bool = False
    yawn_start_time: float | None = None
    yawn_counted: bool = False
    smooth_eye_open: float = 0.0
    smooth_mouth_open: float = 0.0
    smooth_gaze_vec: np.ndarray = field(default_factory=lambda: np.array([0.0, 0.0, 0.0]))
    gaze_label: str = "unknown"

state = FaceState()
cap = cv2.VideoCapture(0, cv2.CAP_DSHOW)
prev_time, fps, frame_id, best_bbox = time.time(), 0.0, 0, None
last_detect_boxes, last_detect_scores = [], []
lost_detect_frames = 0
debug_mode = False  # 默认关闭 Debug
show_all_points = False

print("\n操作提示：按 d 开关 Debug；按 a 切换只显示计算点/全部106点。当前关键点使用 old.py 的 uniface get_landmarks 前后处理。\n")

while True:
    ret, frame = cap.read()
    if not ret: break
    frame_id += 1
    h, w = frame.shape[:2]

    # 每3帧检测一次人脸，并锁定主目标
    if frame_id % 3 == 0:
        boxes, scores = detector.detect(frame)
        last_detect_boxes, last_detect_scores = boxes, scores
        new_bbox = select_target_bbox(boxes, scores, frame.shape, best_bbox)

        if new_bbox is not None:
            best_bbox = new_bbox
            lost_detect_frames = 0
        else:
            lost_detect_frames += 1
            if lost_detect_frames >= 6:
                best_bbox = None

    if debug_mode and last_detect_boxes is not None and len(last_detect_boxes) > 0:
        for i, b in enumerate(last_detect_boxes):
            x1d, y1d, x2d, y2d = map(int, b)
            score = float(last_detect_scores[i]) if last_detect_scores is not None and i < len(last_detect_scores) else 0.0
            cv2.rectangle(frame, (x1d, y1d), (x2d, y2d), (180, 180, 180), 1)
            cv2.putText(frame, f"cand {i}:{score:.2f}", (x1d, max(18, y1d - 4)),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.45, (180, 180, 180), 1)

    eye_text = "UNKNOWN"

    if best_bbox is not None:
        landmarks = landmarker.infer(frame, best_bbox)

        if landmarks is None or len(landmarks) != 106:
            continue

        # 1. Debug 显示
        if debug_mode:
            if show_all_points:
                for idx, pt in enumerate(landmarks):
                    cv2.circle(frame, (int(pt[0]), int(pt[1])), 1, (0, 0, 255), -1)
                    cv2.putText(frame, str(idx), (int(pt[0]), int(pt[1])),
                                cv2.FONT_HERSHEY_SIMPLEX, 0.32, (0, 255, 0), 1)
            else:
                draw_landmark_indices(frame, landmarks, range(LEFT_EYE_SLICE.start, LEFT_EYE_SLICE.stop), (255, 255, 0), "L")
                draw_landmark_indices(frame, landmarks, range(RIGHT_EYE_SLICE.start, RIGHT_EYE_SLICE.stop), (0, 255, 255), "R")
                draw_landmark_indices(frame, landmarks, range(MOUTH_SLICE.start, MOUTH_SLICE.stop), (0, 255, 0), "M")
                cv2.putText(frame, "Debug: uniface get_landmarks + old.py slices",
                            (10, h - 15), cv2.FONT_HERSHEY_SIMPLEX, 0.55, (0, 255, 255), 1)

        try:
            # 2. old.py 同款 EAR/MAR 切片
            left_eye = landmarks[LEFT_EYE_SLICE]
            right_eye = landmarks[RIGHT_EYE_SLICE]
            mouth = landmarks[MOUTH_SLICE]

            eye_open = (eye_aspect_ratio(left_eye) + eye_aspect_ratio(right_eye)) / 2.0
            mouth_open = mouth_aspect_ratio(mouth)

            state.smooth_eye_open = eye_open if state.smooth_eye_open == 0.0 else 0.65 * state.smooth_eye_open + 0.35 * eye_open
            state.smooth_mouth_open = mouth_open if state.smooth_mouth_open == 0.0 else 0.65 * state.smooth_mouth_open + 0.35 * mouth_open

            # 3. 视线裁剪也使用 old.py 对应的眼部切片中心
            left_center = np.mean(left_eye, axis=0)
            right_center = np.mean(right_eye, axis=0)

            face_w = float(best_bbox[2] - best_bbox[0])
            eye_size = int(max(32, face_w * 0.22))

            left_crop = crop_square(frame, left_center, eye_size)
            right_crop = crop_square(frame, right_center, eye_size)

            if left_crop is not None and right_crop is not None and frame_id % 2 == 0:
                raw_gaze = gaze_model.infer(left_crop, right_crop)
                state.smooth_gaze_vec = raw_gaze if np.sum(state.smooth_gaze_vec) == 0.0 else 0.7 * state.smooth_gaze_vec + 0.3 * raw_gaze
                state.gaze_label = classify_gaze(state.smooth_gaze_vec)

            norm = np.linalg.norm(state.smooth_gaze_vec) + 1e-6
            cx, cy = int((left_center[0] + right_center[0]) * 0.5), int((left_center[1] + right_center[1]) * 0.5)
            end_x = int(cx + (state.smooth_gaze_vec[0] / norm) * 90)
            end_y = int(cy + (state.smooth_gaze_vec[1] / norm) * 90)
            cv2.arrowedLine(frame, (cx, cy), (end_x, end_y), (0, 0, 255), 3, tipLength=0.25)
            cv2.circle(frame, (cx, cy), 4, (0, 255, 255), -1)

        except Exception as e:
            if debug_mode:
                print("[WARN] landmark/gaze calculation failed:", e)

        # 状态判定
        state.eyes_closed = state.smooth_eye_open < 0.19
        eye_text = "CLOSED" if state.eyes_closed else "OPEN"
        if state.eyes_closed:
            state.closed_frames += 1
        else:
            if 2 <= state.closed_frames <= 8: state.blink_count += 1
            state.closed_frames = 0

        now = time.time()
        if state.smooth_mouth_open > 0.28:
            if state.yawn_start_time is None: state.yawn_start_time = now
            if (now - state.yawn_start_time) >= 0.8:
                if not state.yawn_counted:
                    state.yawn_count += 1
                    state.yawn_counted = True
        else:
            state.yawn_start_time, state.yawn_counted = None, False

        cv2.rectangle(frame, (int(best_bbox[0]), int(best_bbox[1])), (int(best_bbox[2]), int(best_bbox[3])), (0, 255, 0), 2)
        cv2.putText(frame, f"TARGET  Gaze: {state.gaze_label.upper()}", (int(best_bbox[0]), max(20, int(best_bbox[1]) - 10)), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 0, 255), 2)

    # UI 渲染
    dt = time.time() - prev_time
    prev_time = time.time()
    if dt > 0: fps = 1.0 / dt if fps == 0 else 0.9 * fps + 0.1 * (1.0 / dt)

    for i, (text, color) in enumerate([
        (f"FPS: {fps:.1f}", (0, 255, 255)), (f"EAR: {state.smooth_eye_open:.3f}", (255, 255, 0)),
        (f"MAR: {state.smooth_mouth_open:.3f}", (0, 200, 255)), (f"Blink: {state.blink_count}", (0, 255, 0)),
        (f"Yawn: {state.yawn_count}", (0, 255, 0)), (f"Eyes: {eye_text}", (0, 0, 255) if state.eyes_closed else (255, 255, 255))
    ]):
        cv2.putText(frame, text, (10, 30 + 28 * i), cv2.FONT_HERSHEY_SIMPLEX, 0.7, color, 2)

    cv2.imshow("Study Assistant Vision", frame)
    key = cv2.waitKey(1) & 0xFF
    if key == 27: break
    elif key == ord('d'):
        debug_mode = not debug_mode
    elif key == ord('a'):
        show_all_points = not show_all_points

cap.release()
cv2.destroyAllWindows()