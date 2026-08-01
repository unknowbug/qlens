"""QLens QC 质量检测引擎（纯算法，零模型依赖，图片不出本机）。

检测项：
  模糊         — Laplacian 方差（边缘锐度）
  曝光过度      — 亮部像素占比
  曝光不足      — 暗部像素占比
  色偏         — RGB 通道均值偏差 + 灰色参考
  红眼         — 人脸区域红通道异常（需检测到人脸）
  闭眼         — 眼睛区域检测不到瞳孔/睁眼特征（需检测到人脸）

用法：
  detect_qc(image_path) -> {"模糊": conf, "曝光过度": conf, ...}  置信度 0~1
"""
import os

import cv2
import numpy as np

# 检测阈值（经验值，后续可调）
_BLUR_THRESHOLD = 100.0        # Laplacian 方差低于此 → 模糊
_OVEREXPOSE_RATIO = 0.15       # 亮度 >245 的像素占比超过此 → 曝光过度
_UNDEREXPOSE_RATIO = 0.25      # 亮度 <10 的像素占比超过此 → 曝光不足
_COLOR_CAST_DIST = 30.0        # 通道均值到灰色的距离超过此 → 色偏
_RED_EYE_RED_RATIO = 0.55      # 眼睛区域红色占比超过此 → 红眼
_CLOSED_EYE_VARIANCE = 8.0     # 眼睛区域像素方差低于此 → 闭眼

# 眼睛检测缓存：Haar 级联加载较慢，全局只加载一次
_FACE_CASCADE = None
_EYE_CASCADE = None


def _load_cascades():
    """加载人脸/眼睛 Haar 级联（OpenCV 自带，首次调用加载）。"""
    global _FACE_CASCADE, _EYE_CASCADE
    if _FACE_CASCADE is None:
        cv2_dir = os.path.dirname(cv2.__file__)
        _FACE_CASCADE = cv2.CascadeClassifier(
            os.path.join(cv2_dir, "data", "haarcascade_frontalface_default.xml"))
        _EYE_CASCADE = cv2.CascadeClassifier(
            os.path.join(cv2_dir, "data", "haarcascade_eye.xml"))
    return _FACE_CASCADE, _EYE_CASCADE


def _sigmoid(x, center, width):
    """单边 sigmoid：x 超过 center 越多，置信度越高（0~1）。
    用于曝光检测（亮部多→过度，暗部多→不足）。"""
    return 1.0 / (1.0 + np.exp(-(x - center) / max(width, 1e-6) * 4.0))


def _distance_conf(dist, center, width):
    """距离型 sigmoid：dist 偏离 center 越远，置信度越高（对称）。
    用于色偏等"偏离中性值"的检测。"""
    return 1.0 / (1.0 + np.exp(-(abs(dist - center) - width) / max(width, 1e-6) * 4.0))


def _detect_blur(gray):
    """Laplacian 方差：值越低越模糊。"""
    var = cv2.Laplacian(gray, cv2.CV_64F).var()
    # 方差 < 阈值 → 模糊；方差越接近 0 置信度越高
    if var < _BLUR_THRESHOLD:
        return 1.0 - var / _BLUR_THRESHOLD
    return 0.0


def _detect_exposure(gray):
    """曝光过度 + 曝光不足：直方图两端占比（单边判定）。"""
    hist = cv2.calcHist([gray], [0], None, [256], [0, 256]).flatten()
    total = float(gray.size)
    bright = hist[245:].sum() / total   # >245
    dark = hist[:10].sum() / total      # <10
    over = _sigmoid(bright, _OVEREXPOSE_RATIO, 0.05)
    under = _sigmoid(dark, _UNDEREXPOSE_RATIO, 0.05)
    return over, under


def _detect_color_cast(bgr):
    """色偏：RGB 通道均值到灰色的距离（对称判定）。"""
    b, g, r = bgr[..., 0].mean(), bgr[..., 1].mean(), bgr[..., 2].mean()
    gray_level = (r + g + b) / 3.0
    dist = abs(r - gray_level) + abs(g - gray_level) + abs(b - gray_level)
    return _distance_conf(dist, _COLOR_CAST_DIST, 15.0)


def _detect_red_eye_and_closed(bgr, gray):
    """红眼/闭眼：基于人脸 + 眼睛区域检测。
    检测不到人脸时两者都返回 0（不做误判）。"""
    face_cascade, eye_cascade = _load_cascades()
    faces = face_cascade.detectMultiScale(gray, 1.1, 4, minSize=(32, 32))
    if len(faces) == 0:
        return 0.0, 0.0

    red_eye_conf = 0.0
    closed_eye_conf = 0.0
    for (fx, fy, fw, fh) in faces:
        # 眼睛通常在上 2/3 的脸部区域
        eye_roi_gray = gray[fy:fy + int(fh * 0.6), fx:fx + fw]
        eye_roi_color = bgr[fy:fy + int(fh * 0.6), fx:fx + fw]
        if eye_roi_gray.size == 0:
            continue
        eyes = eye_cascade.detectMultiScale(eye_roi_gray, 1.1, 4, minSize=(8, 8))
        if len(eyes) == 0:
            # 检测不到眼睛 → 疑似闭眼
            closed_eye_conf = max(closed_eye_conf, 0.6)
            continue
        for (ex, ey, ew, eh) in eyes:
            region = eye_roi_color[ey:ey + eh, ex:ex + ew]
            if region.size == 0:
                continue
            hsv = cv2.cvtColor(region, cv2.COLOR_BGR2HSV)
            # 红色检测：HSV 色相在红区 + 较高饱和度
            h, s, v = hsv[..., 0], hsv[..., 1], hsv[..., 2]
            red_mask = ((h < 10) | (h > 170)) & (s > 80) & (v > 60)
            red_ratio = red_mask.mean()
            if red_ratio > _RED_EYE_RED_RATIO:
                red_eye_conf = max(red_eye_conf, min(1.0, (red_ratio - _RED_EYE_RED_RATIO) * 2))
            # 闭眼：眼睛区域方差低（无明显瞳孔/虹膜结构）
            region_var = region.astype(np.float32).var()
            if region_var < _CLOSED_EYE_VARIANCE:
                closed_eye_conf = max(closed_eye_conf, 0.7)

    return red_eye_conf, closed_eye_conf


def detect_qc(image_path):
    """对单张图片做 QC 检测，返回标签 → 置信度映射。"""
    # 统一用 BGR 读图（OpenCV 原生），失败返回空
    img = cv2.imread(image_path)
    if img is None:
        return {}
    # 超清图降采样加速（QC 不需要原始分辨率，但模糊检测例外 —— 见下）
    h, w = img.shape[:2]
    max_dim = 1600
    if max(h, w) > max_dim:
        scale = max_dim / max(h, w)
        img = cv2.resize(img, (int(w * scale), int(h * scale)), interpolation=cv2.INTER_AREA)
    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)

    result = {}
    blur = _detect_blur(gray)
    if blur > 0.35:
        result["模糊"] = blur

    over, under = _detect_exposure(gray)
    if over > 0.5:
        result["曝光过度"] = over
    if under > 0.5:
        result["曝光不足"] = under

    cast = _detect_color_cast(img)
    if cast > 0.55:
        result["色偏"] = cast

    red_eye, closed = _detect_red_eye_and_closed(img, gray)
    if red_eye > 0.5:
        result["红眼"] = red_eye
    if closed > 0.5:
        result["闭眼"] = closed

    return result


def analyze_folder(folder, recursive=False):
    """批量检测文件夹内图片，返回 [{path, qc: {tag: conf}}]。"""
    from qlens_lib import list_folder
    results = []
    for item in list_folder(folder, recursive=recursive):
        qc = detect_qc(item["path"])
        if qc:
            results.append({"path": item["path"], "qc": qc})
    return results
