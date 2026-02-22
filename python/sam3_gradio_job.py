import argparse
import errno
from csv import writer
import gc
import traceback
import inspect
import math
import json
import os
import shutil
import subprocess
import sys
import threading
import time
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Any, Dict, Iterator, List, Mapping, Optional, Sequence, Tuple

import cv2
import gradio as gr
import imageio_ffmpeg
import numpy as np
import torch
from PIL import Image, ImageDraw
from transformers import Sam3TrackerVideoModel, Sam3TrackerVideoProcessor, Sam3Model, Sam3Processor

# -------------------------
# Job file I/O
# -------------------------

# job-level defaults (set after reading request.json)
_JOB_PROMPT_MODE = "click"
_JOB_OUTPUT_MODE = "fgmask"
_JOB_BG_MODE_DEFAULT = "GB"
_JOB_INSERT_MODE_DEFAULT = "transparent"
_JOB_GBBB_CODEC = "mp4v"
_JOB_AUDIO_MUX_DEFAULT = True
_JOB_DEVICE = "cuda" if torch.cuda.is_available() else ("mps" if getattr(torch.backends, "mps", None) and torch.backends.mps.is_available() else "cpu")
_JOB_DTYPE = torch.bfloat16

def _select_device(pref: str) -> str:
    p = (pref or "").strip().lower()
    if p.startswith("cpu"):
        return "cpu"
    if p.startswith("mps"):
        if getattr(torch.backends, "mps", None) and torch.backends.mps.is_available():
            return "mps"
        return "cuda" if torch.cuda.is_available() else "cpu"
    # default cuda
    return "cuda" if torch.cuda.is_available() else ("mps" if getattr(torch.backends, "mps", None) and torch.backends.mps.is_available() else "cpu")

def _select_dtype(device: str, pref: str = "bf16") -> torch.dtype:
    d = (device or "cpu").lower()
    p = (pref or "bf16").lower()
    if d == "cpu":
        return torch.float32
    if d in ("cuda", "mps"):
        if d == "cuda" and p in ("bf16", "bfloat16"):
            try:
                if hasattr(torch.cuda, "is_bf16_supported") and torch.cuda.is_bf16_supported():
                    return torch.bfloat16
            except Exception:
                pass
        # fallback
        return torch.float16
    return torch.float32

def _apply_request_defaults(req: dict) -> None:
    """Stage1: request.json に完全追従する job-level defaults を確定する"""
    global _JOB_PROMPT_MODE, _JOB_OUTPUT_MODE, _JOB_BG_MODE_DEFAULT, _JOB_GBBB_CODEC, _JOB_AUDIO_MUX_DEFAULT
    global _JOB_DEVICE, _JOB_DTYPE, _JOB_INSERT_MODE_DEFAULT

    _JOB_PROMPT_MODE = str((req.get("prompt", {}) or {}).get("mode", "click") or "click")
    _JOB_OUTPUT_MODE = str((req.get("output", {}) or {}).get("mode", "fgmask") or "fgmask")
    out = req.get("output", {}) or {}
    _JOB_BG_MODE_DEFAULT = str(out.get("bg_mode", "GB") or "GB").upper()
    _JOB_INSERT_MODE_DEFAULT = str(out.get("insert_mode", "transparent") or "transparent")

    gbbb = out.get("gbbb", {}) or {}
    _JOB_GBBB_CODEC = str(gbbb.get("codec", "mp4v") or "mp4v")
    _JOB_AUDIO_MUX_DEFAULT = bool(gbbb.get("audio_mux", True))
    opts = req.get("options", {}) or {}
    _JOB_DEVICE = _select_device(str(opts.get("device_preference", "cuda") or "cuda"))
    _JOB_DTYPE = _select_dtype(_JOB_DEVICE, str(opts.get("dtype_preference", "bf16") or "bf16"))

def jst_now() -> str:
    return datetime.now().strftime("%Y%m%d_%H%M%S")

def _is_retryable_replace_error(exc: BaseException) -> bool:
    if isinstance(exc, PermissionError):
        return True
    if isinstance(exc, OSError):
        winerror = int(getattr(exc, "winerror", 0) or 0)
        if winerror in (5, 32):
            return True
        err_no = int(getattr(exc, "errno", 0) or 0)
        if err_no in (errno.EACCES, errno.EPERM, errno.EBUSY):
            return True
    return False

def write_atomic(path: str, text: str, retries: int = 30, retry_sleep: float = 0.02) -> None:
    # ステータス確認が頻繁に行われる場合は一時ファイルで
    tmp = f"{path}.tmp.{os.getpid()}.{threading.get_ident()}"
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
        try:
            f.flush()
            os.fsync(f.fileno())
        except Exception:
            pass

    last_exc: Optional[BaseException] = None
    for i in range(max(1, int(retries))):
        try:
            os.replace(tmp, path)
            return
        except Exception as e:
            last_exc = e
            if (not _is_retryable_replace_error(e)) or (i + 1 >= max(1, int(retries))):
                break
            time.sleep(float(retry_sleep))

    # フォールバック、ジョブがロックで失敗しないようにする
    try:
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        return
    except Exception:
        if last_exc is not None:
            raise last_exc
        raise
    finally:
        try:
            if os.path.exists(tmp):
                os.remove(tmp)
        except Exception:
            pass

def write_status(job_dir: str, **kwargs) -> None:
    payload = {
        "state": kwargs.get("state", "running"),
        "prompt_mode": kwargs.get("prompt_mode", _JOB_PROMPT_MODE),
        "output_mode": kwargs.get("output_mode", _JOB_OUTPUT_MODE),
        "phase": kwargs.get("phase", ""),
        "progress": float(kwargs.get("progress", 0.0)),
        "message": kwargs.get("message", ""),
        "gradio_url": kwargs.get("gradio_url", ""),
        "updated_at_jst": jst_now(),
    }
    try:
        write_atomic(os.path.join(job_dir, "status.json"), json.dumps(payload, ensure_ascii=False, indent=2))
    except Exception as e:
        print(f"[WARN] write_status failed: {e}")

def write_result(
    job_dir: str,
    success: bool,
    mask_video_path: str = "",
    output_video_path: str = "",
    fg_video_path: str = "",
    composited_video_path: str = "",
    bg_mode: str = "",
    insert_mode: str = "",
    error_message: str = "",
    stats: Optional[dict] = None,
    prompt_mode: Optional[str] = None,
    output_mode: Optional[str] = None,
) -> None:
    pm = _JOB_PROMPT_MODE if prompt_mode is None else prompt_mode
    om = _JOB_OUTPUT_MODE if output_mode is None else output_mode

    payload = {
        "success": bool(success),
        "prompt_mode": pm,
        "output_mode": om,
        "mask_video_path": mask_video_path,
        "output_video_path": output_video_path,
        "fg_video_path": fg_video_path,
        "composited_video_path": composited_video_path,
        "bg_mode": bg_mode,
        "insert_mode": insert_mode,
        "error_message": error_message,
        "stats": stats or {"num_frames": 0, "fps": 0.0},
    }
    write_atomic(os.path.join(job_dir, "result.json"), json.dumps(payload, ensure_ascii=False, indent=2))

# -------------------------
# Environment / logging
# -------------------------

def setup_logging(job_dir: Path) -> None:
    log_fp = job_dir / "python.log.txt"
    log_fp.parent.mkdir(parents=True, exist_ok=True)
    log_f = open(log_fp, "a", encoding="utf-8", buffering=1)
    sys.stdout = log_f
    sys.stderr = log_f

def setup_proxy_safety() -> None:
    # ローカル宛はプロキシしない
    os.environ["NO_PROXY"] = "127.0.0.1,localhost"
    os.environ["no_proxy"] = "127.0.0.1,localhost"
    # 強制的にプロキシ無効化（切り分け優先）
    for k in ["HTTP_PROXY","HTTPS_PROXY","ALL_PROXY","http_proxy","https_proxy","all_proxy"]:
        os.environ.pop(k, None)

# -------------------------
# Video I/O (segment load)
# -------------------------

@dataclass
class SegmentInfo:
    fps: float
    num_frames: int
    width: int
    height: int
    start_sec: float
    end_sec: float

def load_video_segment_frames(
    video_path: str,
    start_sec: float,
    end_sec: float,
    max_seconds: int = 120,
    expected_num_frames: Optional[int] = None,
) -> Tuple[List[Image.Image], SegmentInfo]:
    """
    指定区間 [start_sec, end_sec] を読み込み。
    end_sec <= start_sec は不正として扱う。
    """
    cap = cv2.VideoCapture(video_path)
    if not cap.isOpened():
        raise RuntimeError(f"Failed to open video: {video_path}")

    fps = cap.get(cv2.CAP_PROP_FPS)
    fps = float(fps) if fps and fps > 0 else 30.0

    w = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH) or 0)
    h = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT) or 0)

    # 無効な範囲の場合は即座に処理を終了させる
    if end_sec <= start_sec:
        raise RuntimeError(f"Invalid segment range: start_sec={start_sec}, end_sec={end_sec}")

    # max_seconds 安全弁
    if max_seconds and (end_sec - start_sec) > float(max_seconds):
        end_sec = start_sec + float(max_seconds)

    # 可能なら時刻シーク
    cap.set(cv2.CAP_PROP_POS_MSEC, max(0.0, start_sec) * 1000.0)

    frames: List[Image.Image] = []
    t_limit = end_sec
    # cv2 は timestamp 取得が不安定なことがあるので、fps換算も併用
    start_frame_guess = int(round(start_sec * fps))
    exp_frames = int(expected_num_frames) if expected_num_frames is not None else 0
    if exp_frames > 0:
        end_frame_guess = start_frame_guess + exp_frames
    else:
        end_frame_guess = int(round(end_sec * fps))

    # 現在フレーム位置
    cur_idx = int(cap.get(cv2.CAP_PROP_POS_FRAMES) or 0)
    if cur_idx < start_frame_guess - 2:
        cap.set(cv2.CAP_PROP_POS_FRAMES, start_frame_guess)

    while True:
        if exp_frames > 0 and len(frames) >= exp_frames:
            break
        cur_frame = int(cap.get(cv2.CAP_PROP_POS_FRAMES) or 0)
        if exp_frames <= 0 and cur_frame >= end_frame_guess:
            break

        ret, frame_bgr = cap.read()
        if not ret:
            break

        # timestamp check (if available)
        if exp_frames <= 0:
            t_msec = cap.get(cv2.CAP_PROP_POS_MSEC)
            if t_msec and (t_msec / 1000.0) > t_limit:
                break

        frame_rgb = cv2.cvtColor(frame_bgr, cv2.COLOR_BGR2RGB)
        frames.append(Image.fromarray(frame_rgb))

        if len(frames) % 120 == 0:
            gc.collect()

    cap.release()

    if len(frames) == 0:
        raise RuntimeError("No frames loaded in the requested segment.")
    if exp_frames > 0 and len(frames) < exp_frames:
        short = exp_frames - len(frames)
        if short <= 2:
            print(f"[WARN] Segment was short by {short} frame(s). Padding with last frame.")
            last = frames[-1]
            for _ in range(short):
                frames.append(last.copy())
        else:
            raise RuntimeError(f"Segment frame shortage: expected={exp_frames}, got={len(frames)}")

    # w/h が取れなかった場合はフレームから
    if w <= 0 or h <= 0:
        w, h = frames[0].size

    info = SegmentInfo(
        fps=fps,
        num_frames=len(frames),
        width=w,
        height=h,
        start_sec=float(start_sec),
        end_sec=float(end_sec),
    )
    return frames, info

# -------------------------
# FFmpeg helpers (segment audio mux)
# -------------------------

def _resolve_ffmpeg_exe() -> str:
    # 1) imageio-ffmpeg bundled
    try:
        exe = imageio_ffmpeg.get_ffmpeg_exe()
        if exe and os.path.isfile(exe):
            return exe
    except Exception:
        pass

    # 2) env var
    env_exe = os.environ.get("IMAGEIO_FFMPEG_EXE")
    if env_exe and os.path.isfile(env_exe):
        return env_exe

    # 3) system
    sys_exe = shutil.which("ffmpeg")
    if sys_exe:
        return sys_exe

    raise RuntimeError(
        "ffmpeg executable was not found. Install imageio-ffmpeg or set IMAGEIO_FFMPEG_EXE."
    )

def _decode_subprocess_text(data: Any) -> str:
    if data is None:
        return ""
    if isinstance(data, str):
        return data
    if not isinstance(data, (bytes, bytearray)):
        return str(data)
    b = bytes(data)
    encodings = ["utf-8", "cp932", sys.getdefaultencoding() or "utf-8"]
    seen = set()
    for enc in encodings:
        if enc in seen:
            continue
        seen.add(enc)
        try:
            return b.decode(enc)
        except Exception:
            pass
    return b.decode("utf-8", errors="replace")

def _run_ffmpeg(cmd: List[str]) -> Tuple[int, str]:
    # バイト列として読み込み、UnicodeDecodeError回避のために自前でデコード
    r = subprocess.run(cmd, capture_output=True, text=False)
    stderr = _decode_subprocess_text(r.stderr)
    if stderr:
        return r.returncode, stderr
    return r.returncode, _decode_subprocess_text(r.stdout)

def preprocess_video_segment_for_tracking(
    source_video: str,
    out_path: str,
    start_sec: float,
    duration_sec: float,
    fps: Optional[float] = None,
    expected_num_frames: Optional[int] = None,
) -> str:
    if not source_video or not os.path.isfile(source_video):
        raise RuntimeError(f"source video not found: {source_video}")
    if duration_sec <= 0.0:
        raise RuntimeError(f"invalid segment duration: {duration_sec}")

    ffmpeg = _resolve_ffmpeg_exe()
    out_parent = os.path.dirname(out_path)
    if out_parent:
        os.makedirs(out_parent, exist_ok=True)
    if os.path.exists(out_path):
        try:
            os.remove(out_path)
        except Exception:
            pass

    vf = None
    fps_v = float(fps) if (fps is not None and float(fps) > 0.0) else 0.0
    exp_frames = int(expected_num_frames) if expected_num_frames is not None else 0
    if fps_v > 0.0:
        vf = f"fps={fps_v:.8f}"

    # フレーム数を明示するときはヘッドルームを追加して-frames:vでクランプするようにする
    duration_for_cut = float(duration_sec)
    if fps_v > 0.0 and exp_frames > 0:
        duration_for_cut = max(duration_for_cut, (exp_frames / fps_v) + (0.5 / fps_v))

    def _base_cmd(codec_args: List[str]) -> List[str]:
        cmd = [
            ffmpeg, "-y",
            "-ss", f"{start_sec:.6f}",
            "-i", source_video,
            "-t", f"{duration_for_cut:.6f}",
            "-map", "0:v:0",
            "-an", "-sn", "-dn",
        ]
        if vf:
            cmd += ["-vf", vf]
        cmd += codec_args
        if exp_frames > 0:
            cmd += ["-frames:v", str(exp_frames), "-fps_mode", "cfr"]
        cmd += [out_path]
        return cmd

    # NVENCを優先、libx264を使用
    cmd_nvenc = _base_cmd([
        "-c:v", "h264_nvenc",
        "-preset", "p4",
        "-cq", "23",
        "-b:v", "0",
        "-pix_fmt", "yuv420p",
        "-movflags", "+faststart",
    ])
    code, err = _run_ffmpeg(cmd_nvenc)
    if code == 0 and os.path.isfile(out_path) and os.path.getsize(out_path) > 0:
        return out_path

    cmd_x264 = _base_cmd([
        "-c:v", "libx264",
        "-preset", "veryfast",
        "-crf", "23",
        "-pix_fmt", "yuv420p",
        "-movflags", "+faststart",
    ])
    code2, err2 = _run_ffmpeg(cmd_x264)
    if code2 == 0 and os.path.isfile(out_path) and os.path.getsize(out_path) > 0:
        return out_path

    raise RuntimeError(f"Failed to preprocess segment video.\n[nvenc]\n{err}\n\n[libx264]\n{err2}")

def make_h264_aac_preview(
    video_noaudio: str,
    original_video: str,
    out_path: str,
    start_sec: float,
    end_sec: float,
    max_width: int = 960,
    crf: int = 23,
    preset: str = "veryfast",
    fps: Optional[int] = None,
    audio_bitrate: str = "96k",
) -> None:
    ffmpeg = _resolve_ffmpeg_exe()

    if not video_noaudio or not os.path.isfile(video_noaudio):
        raise RuntimeError(f"video_noaudio not found: {video_noaudio}")

    # scale: 幅をmax_width以下にして縦は自動、-2で偶数に揃える
    # min(960,iw) の “,” はフィルタ内で区切り扱いなので \, でエスケープ
    vf = f"scale='min({max_width}\\,iw)':-2"
    if fps is not None and int(fps) > 0:
        vf = vf + f",fps={int(fps)}"

    cmd = [
        ffmpeg, "-y",
        "-i", video_noaudio,
        "-ss", f"{start_sec:.6f}",
        "-to", f"{end_sec:.6f}",
        "-i", original_video,

        "-map", "0:v:0",
        "-map", "1:a:0?",

        "-vf", vf,
        "-c:v", "libx264",
        "-preset", str(preset),
        "-crf", str(int(crf)),
        "-pix_fmt", "yuv420p",
        "-tag:v", "avc1",
        "-movflags", "+faststart",

        "-c:a", "aac",
        "-b:a", str(audio_bitrate),

        "-shortest",
        out_path,
    ]

    code, err = _run_ffmpeg(cmd)
    if code != 0:
        raise RuntimeError(f"Failed to make preview (H.264/AAC).\n{err}")

def make_h264_preview_video_only(
    video_noaudio: str,
    out_path: str,
    max_width: int = 960,
    crf: int = 23,
    preset: str = "veryfast",
    fps: Optional[int] = None,
) -> None:
    ffmpeg = _resolve_ffmpeg_exe()
    if not video_noaudio or not os.path.isfile(video_noaudio):
        raise RuntimeError(f"video_noaudio not found: {video_noaudio}")

    vf = f"scale='min({max_width}\\,iw)':-2"
    if fps is not None and int(fps) > 0:
        vf = vf + f",fps={int(fps)}"

    cmd = [
        ffmpeg, "-y",
        "-i", video_noaudio,
        "-vf", vf,
        "-c:v", "libx264",
        "-preset", str(preset),
        "-crf", str(int(crf)),
        "-pix_fmt", "yuv420p",
        "-tag:v", "avc1",
        "-movflags", "+faststart",
        "-an",
        out_path,
    ]
    code, err = _run_ffmpeg(cmd)
    if code != 0:
        raise RuntimeError(f"Failed to make preview (H.264 video only).\n{err}")

def mux_audio_from_original_segment(
    video_noaudio: str,
    original_video: str,
    out_path: str,
    start_sec: float,
    end_sec: float,
) -> None:
    ffmpeg = _resolve_ffmpeg_exe()

    # copy 優先 → だめなら aac
    cmd_copy = [
        ffmpeg, "-y",
        "-ss", f"{start_sec:.6f}",
        "-to", f"{end_sec:.6f}",
        "-i", original_video,
        "-i", video_noaudio,
        "-map", "1:v:0",
        "-map", "0:a:0?",
        "-c:v", "copy",
        "-c:a", "copy",
        "-shortest",
        out_path,
    ]
    code, err = _run_ffmpeg(cmd_copy)
    if code == 0:
        return

    cmd_aac = [
        ffmpeg, "-y",
        "-ss", f"{start_sec:.6f}",
        "-to", f"{end_sec:.6f}",
        "-i", original_video,
        "-i", video_noaudio,
        "-map", "1:v:0",
        "-map", "0:a:0?",
        "-c:v", "copy",
        "-c:a", "aac",
        "-b:a", "192k",
        "-shortest",
        out_path,
    ]
    code2, err2 = _run_ffmpeg(cmd_aac)
    if code2 == 0:
        return

    raise RuntimeError(f"Failed to mux audio.\n[copy]\n{err}\n\n[aac]\n{err2}\n")

# -------------------------
# SAM3 model/session helpers (app.py 由来)
# -------------------------

MODEL_ID = "facebook/sam3"

_TRACKER_MODEL: Optional[Sam3TrackerVideoModel] = None
_TRACKER_PROCESSOR: Optional[Sam3TrackerVideoProcessor] = None

def ensure_models_loaded() -> Tuple[Sam3TrackerVideoModel, Sam3TrackerVideoProcessor]:
    global _TRACKER_MODEL, _TRACKER_PROCESSOR
    if _TRACKER_MODEL is None or _TRACKER_PROCESSOR is None:
        dev = torch.device(_JOB_DEVICE)
        _TRACKER_MODEL = Sam3TrackerVideoModel.from_pretrained(MODEL_ID, torch_dtype=_JOB_DTYPE).to(dev).eval()
        _TRACKER_PROCESSOR = Sam3TrackerVideoProcessor.from_pretrained(MODEL_ID)
    return _TRACKER_MODEL, _TRACKER_PROCESSOR

def _call_init_video_session(processor, **kwargs):
    sig = inspect.signature(processor.init_video_session)
    filtered = {k: v for k, v in kwargs.items() if k in sig.parameters}
    return processor.init_video_session(**filtered)

def _call_add_inputs(processor, **kwargs):
    sig = inspect.signature(processor.add_inputs_to_inference_session)
    filtered = {k: v for k, v in kwargs.items() if k in sig.parameters}
    return processor.add_inputs_to_inference_session(**filtered)

def _clear_points_for_object_all_frames(state: "AppState", obj_id: int) -> None:
    """
    Box を入れるときに「point が既に入っていると box を追加できない」版があるため、
    UI 側の cross 表示と整合させるために、該当 obj_id の point 記録を全フレームで消す。
    """
    if state is None:
        return
    oid = int(obj_id)
    changed_frames = []
    for fidx, mp in list(state.clicks_by_frame_obj.items()):
        if not isinstance(mp, dict):
            continue
        if oid in mp and mp[oid]:
            mp[oid] = []
            changed_frames.append(int(fidx))
    # 表示キャッシュを無効化
    for f in changed_frames:
        state.composited_frames.pop(f, None)

def _to_int_list(x):
    if x is None:
        return None
    if isinstance(x, torch.Tensor):
        x = x.detach().cpu().tolist()
    if isinstance(x, (list, tuple)) and len(x) == 1 and isinstance(x[0], (list, tuple)):
        x = x[0]
    try:
        return [int(v) for v in x]
    except Exception:
        return None

def _normalize_masks(m, obj_ids=None) -> torch.Tensor:
    if isinstance(m, np.ndarray):
        m = torch.from_numpy(m)
    if not isinstance(m, torch.Tensor):
        raise gr.Error(f"mask must be Tensor/ndarray, got: {type(m)}")

    n = len(obj_ids) if obj_ids is not None else None

    if m.ndim == 4:
        if n is not None:
            if m.shape[0] == n and m.shape[1] == 1:
                m = m[:, 0]
            elif m.shape[0] == 1 and m.shape[1] == n:
                m = m[0]
            else:
                if m.shape[0] == 1:
                    m = m[0]
                elif m.shape[1] == 1:
                    m = m[:, 0]
                else:
                    raise gr.Error(f"Unexpected 4D mask shape: {tuple(m.shape)} (obj_ids={obj_ids})")
        else:
            if m.shape[0] == 1:
                m = m[0]
            elif m.shape[1] == 1:
                m = m[:, 0]
            else:
                raise gr.Error(f"Unexpected 4D mask shape: {tuple(m.shape)}")
    elif m.ndim == 3:
        pass
    else:
        raise gr.Error(f"Unexpected mask tensor shape: {tuple(m.shape)}")

    if m.ndim != 3:
        raise gr.Error(f"Normalize failed: got {tuple(m.shape)}")
    return m

def pastel_color_for_object(obj_id: int) -> Tuple[int, int, int]:
    golden_ratio_conjugate = 0.61
    hue = (obj_id * golden_ratio_conjugate) % 1.0
    saturation = 0.45
    value = 1.0
    import colorsys
    r_f, g_f, b_f = colorsys.hsv_to_rgb(hue, saturation, value)
    return int(r_f * 255), int(g_f * 255), int(b_f * 255)

def overlay_masks_on_frame(
    frame: Image.Image,
    masks_per_object: Dict[int, np.ndarray],
    color_by_obj: Dict[int, Tuple[int, int, int]],
    alpha: float = 0.55,
) -> Image.Image:
    base = np.array(frame).astype(np.float32) / 255.0
    overlay = base.copy()

    for obj_id, mask in masks_per_object.items():
        if mask is None:
            continue
        mm = mask
        if isinstance(mm, torch.Tensor):
            mm = mm.detach().cpu().numpy()
        mm = np.asarray(mm)
        if mm.ndim == 3:
            mm = mm.squeeze()
        mm = (mm > 0).astype(np.float32)
        color = np.array(color_by_obj.get(obj_id, (255, 0, 0)), dtype=np.float32) / 255.0
        m = mm[..., None]
        overlay = (1.0 - alpha * m) * overlay + (alpha * m) * color

    out = np.clip(overlay * 255.0, 0, 255).astype(np.uint8)
    return Image.fromarray(out)

def _ensure_color_for_obj(color_by_obj: Dict[int, Tuple[int, int, int]], obj_id: int) -> None:
    if obj_id not in color_by_obj:
        color_by_obj[obj_id] = pastel_color_for_object(obj_id)

# -------------------------
# App state for job UI
# -------------------------

class AppState:
    def __init__(self) -> None:
        self.reset()

    def reset(self) -> None:
        self.video_frames: List[Image.Image] = []
        self.video_fps: float = 0.0
        self.segment_start_sec: float = 0.0
        self.segment_end_sec: float = 0.0
        self.source_video_path: str = ""
        self.job_id: str = ""
        self.job_dir: str = ""
        self.output_dir: str = ""
        # last rendered preview mp4 for Gradio (H.264/AAC)
        self.preview_mp4_path: str = ""

        self.inference_session = None
        self.masks_by_frame: Dict[int, Dict[int, np.ndarray]] = {}
        self.color_by_obj: Dict[int, Tuple[int, int, int]] = {}
        self.clicks_by_frame_obj: Dict[int, Dict[int, List[Tuple[int,int,int]]]] = {}
        self.boxes_by_frame_obj: Dict[int, Dict[int, List[Tuple[int,int,int,int]]]] = {}
        self.composited_frames: Dict[int, Image.Image] = {}

        self.current_frame_idx: int = 0
        self.current_obj_id: int = 1
        self.current_label: str = "positive"
        self.current_clear_old: bool = False
        self.current_prompt_type: str = "Points"

        self.pending_box_start: Optional[Tuple[int,int]] = None
        self.pending_box_start_frame_idx: Optional[int] = None
        self.pending_box_start_obj_id: Optional[int] = None

    @property
    def num_frames(self) -> int:
        return len(self.video_frames)

def _union_mask_for_frame(state: AppState, frame_idx: int, h: int, w: int) -> np.ndarray:
    masks = state.masks_by_frame.get(frame_idx, {})
    union = None
    for m in masks.values():
        if m is None:
            continue
        mm = m
        if isinstance(mm, torch.Tensor):
            mm = mm.detach().cpu().numpy()
        mm = np.asarray(mm)
        if mm.ndim == 3:
            mm = mm.squeeze()
        mm = (mm > 0).astype(np.uint8)
        if mm.shape != (h, w):
            mm = cv2.resize(mm, (w, h), interpolation=cv2.INTER_NEAREST)
        union = mm if union is None else (union | mm)
    if union is None:
        union = np.zeros((h, w), dtype=np.uint8)
    return union.astype(bool)

def compose_frame(state: AppState, frame_idx: int) -> Image.Image:
    frame_idx = int(np.clip(frame_idx, 0, state.num_frames - 1))
    frame = state.video_frames[frame_idx]
    out_img = frame.copy()

    masks = state.masks_by_frame.get(frame_idx, {})
    if masks:
        out_img = overlay_masks_on_frame(out_img, masks, state.color_by_obj, alpha=0.65)

    # clicks overlay
    clicks_map = state.clicks_by_frame_obj.get(frame_idx)
    if clicks_map:
        draw = ImageDraw.Draw(out_img)
        cross_half = 6
        for obj_id, pts in clicks_map.items():
            for x, y, lbl in pts:
                color = (0, 255, 0) if int(lbl) == 1 else (255, 0, 0)
                draw.line([(x - cross_half, y), (x + cross_half, y)], fill=color, width=2)
                draw.line([(x, y - cross_half), (x, y + cross_half)], fill=color, width=2)

    # pending box start marker
    if (
        state.pending_box_start is not None
        and state.pending_box_start_frame_idx == frame_idx
        and state.pending_box_start_obj_id is not None
    ):
        draw = ImageDraw.Draw(out_img)
        x, y = state.pending_box_start
        cross_half = 6
        color = state.color_by_obj.get(state.pending_box_start_obj_id, (255, 255, 255))
        draw.line([(x - cross_half, y), (x + cross_half, y)], fill=color, width=2)
        draw.line([(x, y - cross_half), (x, y + cross_half)], fill=color, width=2)

    # boxes overlay
    box_map = state.boxes_by_frame_obj.get(frame_idx)
    if box_map:
        draw = ImageDraw.Draw(out_img)
        for obj_id, boxes in box_map.items():
            color = state.color_by_obj.get(obj_id, (255, 255, 255))
            for x1, y1, x2, y2 in boxes:
                draw.rectangle([(x1, y1), (x2, y2)], outline=color, width=2)

    state.composited_frames[frame_idx] = out_img
    return out_img

def update_frame_display(state: AppState, frame_idx: int) -> Image.Image:
    frame_idx = int(np.clip(frame_idx, 0, state.num_frames - 1))
    cached = state.composited_frames.get(frame_idx)
    if cached is not None:
        return cached
    return compose_frame(state, frame_idx)

def reset_prompts_keep_video(state: AppState) -> None:
    """
    app.py の reset_session 相当：
    - 動画(frames/fps/segment/job情報)は保持
    - クリック/ボックス/マスク/キャッシュ/色など「プロンプト由来の状態」を全てクリア
    - inference_session は reset_inference_session() があればそれを使い、
      なければ video_frames から再 init する
    """
    if state is None:
        return
    if not state.video_frames:
        # 動画が無いなら何もしない（呼び出し側でメッセージ表示）
        return

    # 1) inference session をリセット（可能なら軽いAPI、無ければ作り直し）
    try:
        if state.inference_session is not None and hasattr(state.inference_session, "reset_inference_session"):
            state.inference_session.reset_inference_session()
        else:
            # recreate session from existing frames
            _, processor = ensure_models_loaded()
            raw_video = [np.array(fr) for fr in state.video_frames]  # RGB uint8
            dev = torch.device(_JOB_DEVICE)
            kwargs = dict(
                video=raw_video,
                inference_device=dev,
                processing_device="cpu",
                video_storage_device="cpu",
                inference_state_device="cpu",
                dtype=_JOB_DTYPE,
            )
            state.inference_session = _call_init_video_session(processor, **kwargs)

            # 一部バージョンで属性が必要なことがある
            if hasattr(state.inference_session, "inference_device"):
                state.inference_session.inference_device = dev
            if hasattr(state.inference_session, "cache") and hasattr(state.inference_session.cache, "inference_device"):
                state.inference_session.cache.inference_device = dev

            del raw_video
    except Exception:
        # session の再初期化に失敗しても、UI上は「消したように見せたい」ので
        # 例外は握りつぶしてログに出す（必要ならここで gr.Error にしてもOK）
        traceback.print_exc()

    # 2) prompts / caches をクリア
    state.masks_by_frame.clear()
    state.clicks_by_frame_obj.clear()
    state.boxes_by_frame_obj.clear()
    state.composited_frames.clear()
    state.color_by_obj.clear()

    # box の途中状態もクリア
    state.pending_box_start = None
    state.pending_box_start_frame_idx = None
    state.pending_box_start_obj_id = None
    gc.collect()

# -------------------------
# Click/box prompt handler
# -------------------------

def on_image_click(
    img: Image.Image,
    state: AppState,
    frame_idx: int,
    obj_id: int,
    label: str,
    clear_old: bool,
    prompt_type: str,
    evt: gr.SelectData,
) -> Tuple[Image.Image, AppState]:
    if state is None or state.inference_session is None:
        return img, state

    model, processor = ensure_models_loaded()

    # click coordinate
    x = y = None
    if evt is not None:
        try:
            if hasattr(evt, "index") and isinstance(evt.index, (list, tuple)) and len(evt.index) == 2:
                x, y = int(evt.index[0]), int(evt.index[1])
            elif hasattr(evt, "value") and isinstance(evt.value, dict) and "x" in evt.value and "y" in evt.value:
                x, y = int(evt.value["x"]), int(evt.value["y"])
        except Exception:
            x = y = None
    if x is None or y is None:
        raise gr.Error("Could not read click coordinates.")

    ann_frame_idx = int(frame_idx)
    ann_obj_id = int(obj_id)

    _ensure_color_for_obj(state.color_by_obj, ann_obj_id)

    if str(prompt_type).lower() == "boxes":
        # 2-click box
        if state.pending_box_start is None:
            frame_clicks = state.clicks_by_frame_obj.setdefault(ann_frame_idx, {})
            frame_clicks[ann_obj_id] = []
            state.composited_frames.pop(ann_frame_idx, None)
            state.pending_box_start = (int(x), int(y))
            state.pending_box_start_frame_idx = ann_frame_idx
            state.pending_box_start_obj_id = ann_obj_id
            return update_frame_display(state, ann_frame_idx), state

        x1, y1 = state.pending_box_start
        x2, y2 = int(x), int(y)
        state.pending_box_start = None
        state.pending_box_start_frame_idx = None
        state.pending_box_start_obj_id = None

        x_min, y_min = min(x1, x2), min(y1, y2)
        x_max, y_max = max(x1, x2), max(y1, y2)

        box3 = [[[x_min, y_min, x_max, y_max]]]
        _clear_points_for_object_all_frames(state, ann_obj_id)
        try:
            _call_add_inputs(
                processor,
                inference_session=state.inference_session,
                frame_idx=ann_frame_idx,
                obj_ids=[ann_obj_id],
                input_boxes=box3,
                clear_old_points=True,
                clear_old_boxes=True,
                clear_old_inputs=True,
            )
        except ValueError as e:
            msg = str(e)
            if "nested list with 4 levels" in msg or "Got 3 levels" in msg:
                box4 = [box3]  # [[[[x1,y1,x2,y2]]]]
                _call_add_inputs(
                    processor,
                    inference_session=state.inference_session,
                    frame_idx=ann_frame_idx,
                    obj_ids=[ann_obj_id],
                    input_boxes=box4,
                    clear_old_points=True,
                    clear_old_boxes=True,
                    clear_old_inputs=True,
                )
            else:
                raise

        frame_boxes = state.boxes_by_frame_obj.setdefault(ann_frame_idx, {})
        obj_boxes = frame_boxes.setdefault(ann_obj_id, [])
        obj_boxes.clear()
        obj_boxes.append((x_min, y_min, x_max, y_max))
        state.composited_frames.pop(ann_frame_idx, None)

    else:
        label_int = 1 if str(label).lower().startswith("pos") else 0
        frame_clicks = state.clicks_by_frame_obj.setdefault(ann_frame_idx, {})
        obj_clicks = frame_clicks.setdefault(ann_obj_id, [])

        if bool(clear_old):
            obj_clicks.clear()
            frame_boxes = state.boxes_by_frame_obj.setdefault(ann_frame_idx, {})
            frame_boxes[ann_obj_id] = []

        obj_clicks.append((int(x), int(y), int(label_int)))

        points = [[[[c[0], c[1]] for c in obj_clicks]]]
        labels = [[[c[2] for c in obj_clicks]]]

        _call_add_inputs(
            processor,
            inference_session=state.inference_session,
            frame_idx=ann_frame_idx,
            obj_ids=[ann_obj_id],
            input_points=points,
            input_labels=labels,
            clear_old_inputs=bool(clear_old),
        )
        state.composited_frames.pop(ann_frame_idx, None)

    # run inference for that frame
    with torch.no_grad():
        outputs = model(
            inference_session=state.inference_session,
            frame_idx=ann_frame_idx,
        )

    processed = processor.post_process_masks(
        [outputs.pred_masks],
        [[state.inference_session.video_height, state.inference_session.video_width]],
        binarize=False,
    )[0]

    out_obj_ids = _to_int_list(getattr(outputs, "object_ids", None))
    if out_obj_ids is None:
        out_obj_ids = [ann_obj_id]

    masks = _normalize_masks(processed, out_obj_ids)  # (K,H,W)
    K = masks.shape[0]
    if len(out_obj_ids) != K:
        out_obj_ids = out_obj_ids[:K]

    masks_for_frame = state.masks_by_frame.setdefault(ann_frame_idx, {})
    for k, oid in enumerate(out_obj_ids):
        _ensure_color_for_obj(state.color_by_obj, int(oid))
        mask_2d = (masks[k] > 0.0).to(torch.uint8).cpu().numpy()
        masks_for_frame[int(oid)] = mask_2d

    state.composited_frames.pop(ann_frame_idx, None)
    return update_frame_display(state, ann_frame_idx), state

# -------------------------
# Propagate
# -------------------------

def propagate_masks(state: AppState) -> Iterator[Tuple[AppState, str, Any, Image.Image]]:
    if state is None or state.inference_session is None:
        yield state, "Load failed.", gr.update(), None
        return

    model, processor = ensure_models_loaded()

    total = max(1, state.num_frames)
    seen_frames: set[int] = set()

    write_status(state.job_dir, state="running", phase="propagate", progress=0.35, message="Propagating...")

    # start UI update
    yield state, f"Propagating masks: 0/{total}", gr.update(), update_frame_display(state, state.current_frame_idx)

    last_frame_idx = 0

    def _iter_outputs(reverse: bool):
        # Keep API compatibility for older/newer transformers.
        kwargs = {"inference_session": state.inference_session, "reverse": bool(reverse)}
        try:
            return model.propagate_in_video_iterator(**kwargs)
        except TypeError:
            kwargs.pop("reverse", None)
            return model.propagate_in_video_iterator(**kwargs)

    def _consume(reverse: bool):
        nonlocal last_frame_idx
        for out in _iter_outputs(reverse=reverse):
            video_res_masks = processor.post_process_masks(
                [out.pred_masks],
                original_sizes=[[state.inference_session.video_height, state.inference_session.video_width]],
            )[0]
            frame_idx = int(out.frame_idx)
            masks3 = _normalize_masks(video_res_masks)
            out_obj_ids = _to_int_list(getattr(out, "object_ids", None))

            if out_obj_ids is None or len(out_obj_ids) != masks3.shape[0]:
                sess_obj_ids = [int(x) for x in getattr(state.inference_session, "obj_ids", [])]
                out_obj_ids = sess_obj_ids[: masks3.shape[0]]

            masks_for_frame = state.masks_by_frame.setdefault(frame_idx, {})
            for i, oid in enumerate(out_obj_ids):
                _ensure_color_for_obj(state.color_by_obj, int(oid))
                mask_2d = (masks3[i] > 0.0).to(torch.uint8).cpu().numpy()
                masks_for_frame[int(oid)] = mask_2d
            state.composited_frames.pop(frame_idx, None)

            last_frame_idx = frame_idx
            seen_frames.add(frame_idx)

            # status update
            covered = len(seen_frames)
            if covered % 20 == 0 or covered == total:
                prog = 0.35 + 0.25 * (covered / total)
                write_status(state.job_dir, state="running", phase="propagate", progress=prog,
                             message=f"Propagating... {covered}/{total}")

                state.current_frame_idx = last_frame_idx
                yield state, f"Propagating masks: {covered}/{total}", gr.update(value=last_frame_idx), update_frame_display(state, last_frame_idx)

    with torch.no_grad():
        # 指定されたフレームから最後まで
        for item in _consume(reverse=False):
            yield item
        # 指定よりも前のフレーム処理
        for item in _consume(reverse=True):
            yield item

    write_status(state.job_dir, state="running", phase="propagate", progress=0.60, message="Propagation done.")
    yield state, f"Propagated masks across {len(seen_frames)} frames.", gr.update(value=last_frame_idx), update_frame_display(state, last_frame_idx)

# -------------------------
# Render GB/BB video and finish
# -------------------------

def _bg_bgr_from_mode(mode: str) -> Tuple[int,int,int]:
    m = (mode or "GB").upper()
    if m.startswith("GB"):
        return (0, 255, 0)  # BGR green
    return (255, 0, 0)      # BGR blue

def _set_event_later(ev: threading.Event, delay_sec: float = 1.0) -> None:
    def _w():
        time.sleep(delay_sec)
        ev.set()
    threading.Thread(target=_w, daemon=True).start()
    
def render_fg_black_video(
    state: AppState,
    mux_audio: bool = True,
    codec: str = None,
) -> str:
    """
    fg(黒背景) を必ず生成する（要件2）
    - union mask 内だけ元RGB、外は黒
    - 音声は mux_audio が True の場合のみ元動画区間から付ける
    """
    if state is None or state.num_frames == 0:
        raise gr.Error("No frames loaded.")
    if not state.masks_by_frame:
        raise gr.Error("No masks found. Click/box prompt and/or propagate first.")

    out_dir = state.output_dir or state.job_dir
    os.makedirs(out_dir, exist_ok=True)

    s_ms = int(round(state.segment_start_sec * 1000.0))
    e_ms = int(round(state.segment_end_sec * 1000.0))
    seg_tag = f"{s_ms:09d}_{e_ms:09d}"
    out_noaudio = os.path.join(out_dir, f"sam3_fg_black_noaudio_{state.job_id}_{seg_tag}.mp4")
    out_final   = os.path.join(out_dir, f"sam3_fg_black_{state.job_id}_{seg_tag}.mp4")

    fps = float(state.video_fps) if state.video_fps and state.video_fps > 0 else 30.0
    first = state.video_frames[0]
    w, h = first.size[0], first.size[1]

    c = (codec or _JOB_GBBB_CODEC or "mp4v")
    c4 = (c + "mp4v")[:4]
    fourcc = cv2.VideoWriter_fourcc(*c4)
    writer = cv2.VideoWriter(out_noaudio, fourcc, fps, (w, h))
    if not writer.isOpened():
        raise RuntimeError(f"cv2.VideoWriter failed to open (codec={c4}).")

    write_status(state.job_dir, state="running", phase="render_fg", progress=0.62, message="Rendering fg (black bg)...")

    for idx in range(state.num_frames):
        frame_rgb = np.array(state.video_frames[idx], dtype=np.uint8)
        if frame_rgb.shape[0] != h or frame_rgb.shape[1] != w:
            frame_rgb = cv2.resize(frame_rgb, (w, h), interpolation=cv2.INTER_LINEAR)
        union = _union_mask_for_frame(state, idx, h, w)  # bool
        out_bgr = np.zeros((h, w, 3), dtype=np.uint8)
        out_bgr[union] = frame_rgb[:, :, ::-1][union]
        writer.write(out_bgr)

        if (idx + 1) % 60 == 0 or (idx + 1) == state.num_frames:
            prog = 0.62 + 0.10 * ((idx + 1) / state.num_frames)
            write_status(state.job_dir, state="running", phase="render_fg", progress=prog,
                         message=f"Rendering fg... {idx+1}/{state.num_frames}")

    writer.release()

    if mux_audio:
        try:
            write_status(state.job_dir, state="running", phase="audio_fg", progress=0.73, message="Muxing audio (fg)...")
            mux_audio_from_original_segment(
                video_noaudio=out_noaudio,
                original_video=state.source_video_path,
                out_path=out_final,
                start_sec=state.segment_start_sec,
                end_sec=state.segment_end_sec,
            )
            return out_final
        except Exception as e:
            print("[WARN] fg audio mux failed:", e)
            return out_noaudio
    return out_noaudio
def render_mask_video(state: AppState, codec: str = None) -> str:
    """
    mask動画を必ず生成する（要件2）
    - 0/255 の 3ch mp4 として保存（互換優先）
    """
    if state is None or state.num_frames == 0:
        raise gr.Error("No frames loaded.")
    if not state.masks_by_frame:
        raise gr.Error("No masks found. Click/box prompt and/or propagate first.")

    out_dir = state.output_dir or state.job_dir
    os.makedirs(out_dir, exist_ok=True)

    s_ms = int(round(state.segment_start_sec * 1000.0))
    e_ms = int(round(state.segment_end_sec * 1000.0))
    seg_tag = f"{s_ms:09d}_{e_ms:09d}"
    out_path = os.path.join(out_dir, f"sam3_mask_{state.job_id}_{seg_tag}.mp4")
    fps = float(state.video_fps) if state.video_fps and state.video_fps > 0 else 30.0
    first = state.video_frames[0]
    w, h = first.size[0], first.size[1]

    c = (codec or _JOB_GBBB_CODEC or "mp4v")
    c4 = (c + "mp4v")[:4]
    fourcc = cv2.VideoWriter_fourcc(*c4)
    writer = cv2.VideoWriter(out_path, fourcc, fps, (w, h))
    if not writer.isOpened():
        raise RuntimeError(f"cv2.VideoWriter failed to open (codec={c4}).")

    write_status(state.job_dir, state="running", phase="render_mask", progress=0.74, message="Rendering mask video...")
    for idx in range(state.num_frames):
        union = _union_mask_for_frame(state, idx, h, w)  # bool
        m = (union.astype(np.uint8) * 255)
        out_bgr = np.stack([m, m, m], axis=-1)  # 3ch
        writer.write(out_bgr)
        if (idx + 1) % 60 == 0 or (idx + 1) == state.num_frames:
            prog = 0.74 + 0.08 * ((idx + 1) / state.num_frames)
            write_status(state.job_dir, state="running", phase="render_mask", progress=prog,
                         message=f"Rendering mask... {idx+1}/{state.num_frames}")
    writer.release()
    return out_path


def render_gbbb_video(
    state: AppState,
    bg_mode: str,
    mux_audio: bool = True,
    codec: str = None,
) -> str:
    if state is None or state.num_frames == 0:
        raise gr.Error("No frames loaded.")
    if not state.masks_by_frame:
        raise gr.Error("No masks found. Click/box prompt and/or propagate first.")

    out_dir = state.output_dir or state.job_dir
    os.makedirs(out_dir, exist_ok=True)

    # filename safe tag (ms)
    s_ms = int(round(state.segment_start_sec * 1000.0))
    e_ms = int(round(state.segment_end_sec * 1000.0))
    seg_tag = f"{s_ms:09d}_{e_ms:09d}"
    bg = (bg_mode or "GB").upper()
    out_noaudio = os.path.join(out_dir, f"sam3_{bg}_noaudio_{state.job_id}_{seg_tag}.mp4")
    out_final = os.path.join(out_dir, f"sam3_{bg}_{state.job_id}_{seg_tag}.mp4")

    fps = float(state.video_fps) if state.video_fps and state.video_fps > 0 else 30.0
    first = state.video_frames[0]
    w, h = first.size[0], first.size[1]
    bg_bgr = _bg_bgr_from_mode(bg)

    write_status(state.job_dir, state="running", phase="render", progress=0.65, message="Rendering GB/BB video...")

    c = (codec or _JOB_GBBB_CODEC or "mp4v")
    c4 = (c + "mp4v")[:4]  # safety: ensure 4 chars
    fourcc = cv2.VideoWriter_fourcc(*c4)
    writer = cv2.VideoWriter(out_noaudio, fourcc, fps, (w, h))
    if not writer.isOpened():
        raise RuntimeError(f"cv2.VideoWriter failed to open (codec={c4}).")

    for idx in range(state.num_frames):
        frame_rgb = np.array(state.video_frames[idx], dtype=np.uint8)  # (h,w,3) RGB
        if frame_rgb.shape[0] != h or frame_rgb.shape[1] != w:
            frame_rgb = cv2.resize(frame_rgb, (w, h), interpolation=cv2.INTER_LINEAR)

        union = _union_mask_for_frame(state, idx, h, w)  # bool

        out_bgr = np.empty((h, w, 3), dtype=np.uint8)
        out_bgr[:] = bg_bgr
        out_bgr[union] = frame_rgb[:, :, ::-1][union]  # RGB->BGR

        writer.write(out_bgr)

        if (idx + 1) % 60 == 0 or (idx + 1) == state.num_frames:
            prog = 0.65 + 0.25 * ((idx + 1) / state.num_frames)
            write_status(state.job_dir, state="running", phase="render", progress=prog,
                         message=f"Rendering... {idx+1}/{state.num_frames}")

    writer.release()

    # audio mux
    if mux_audio:
        try:
            write_status(state.job_dir, state="running", phase="audio", progress=0.93, message="Muxing audio...")
            mux_audio_from_original_segment(
                video_noaudio=out_noaudio,
                original_video=state.source_video_path,
                out_path=out_final,
                start_sec=state.segment_start_sec,
                end_sec=state.segment_end_sec,
            )
        except Exception as e:
            # audio失敗でも映像だけは残す（運用上助かる）
            print("[WARN] audio mux failed:", e)
            out_final = out_noaudio
    else:
        out_final = out_noaudio

    write_status(state.job_dir, state="running", phase="render", progress=0.97, message="Render done.")
    return out_final


def render_preview_mp4(
    state: AppState,
    bg_mode: str,
    include_audio: bool = True,
    max_width: int = 960,
    crf: int = 23,
    preset: str = "veryfast",
    preview_fps: Optional[int] = None,
    audio_bitrate: str = "96k",
) -> str:
    """
    Render Preview の流れ:
      1) 中間: cv2(VideoWriter)でGB/BB(noaudio)を作る
      2) 変換: ffmpegで H.264(avc1) (+AAC) の “プレビュー用mp4” にする
    """
    if state is None or state.num_frames == 0:
        raise gr.Error("No frames loaded.")

    # 1) intermediate (GB/BB noaudio)
    write_status(state.job_dir, state="running", phase="preview_mid", progress=0.70, message="Rendering intermediate (no-audio)...")
    mid_noaudio = render_gbbb_video(state, bg_mode=bg_mode, mux_audio=False, codec=_JOB_GBBB_CODEC)

    # 2) preview path
    out_dir = state.output_dir or state.job_dir
    os.makedirs(out_dir, exist_ok=True)
    s_ms = int(round(state.segment_start_sec * 1000.0))
    e_ms = int(round(state.segment_end_sec * 1000.0))
    seg_tag = f"{s_ms:09d}_{e_ms:09d}"
    bg = (bg_mode or "GB").upper()
    preview_path = os.path.join(out_dir, f"sam3_{bg}_preview_{state.job_id}_{seg_tag}.mp4")

    write_status(state.job_dir, state="running", phase="preview_encode", progress=0.86, message="Encoding preview (H.264/AAC)...")
    if include_audio:
        make_h264_aac_preview(
            video_noaudio=mid_noaudio,
            original_video=state.source_video_path,
            out_path=preview_path,
            start_sec=state.segment_start_sec,
            end_sec=state.segment_end_sec,
            max_width=max_width,
            crf=crf,
            preset=preset,
            fps=preview_fps,
            audio_bitrate=audio_bitrate,
        )
    else:
        make_h264_preview_video_only(
            video_noaudio=mid_noaudio,
            out_path=preview_path,
            max_width=max_width,
            crf=crf,
            preset=preset,
            fps=preview_fps,
        )

    state.preview_mp4_path = preview_path
    write_status(state.job_dir, state="running", phase="preview_done", progress=0.92, message="Preview ready.")
    return preview_path


def finish_job(
    state: AppState,
    mux_audio: bool,
    done_event: threading.Event,
    bg_mode: str = "",
) -> str:
    """
    Render -> write result.json -> write status -> (optional) close signal
    Returns: message only (do NOT return video path to Gradio)
    """
    try:
        insert_mode = str(_JOB_INSERT_MODE_DEFAULT or "transparent").strip()
        insert_up = insert_mode.upper()
        # 1) 必須生成（fg黒背景 + mask）
        fg_path = render_fg_black_video(state, mux_audio=bool(mux_audio), codec=_JOB_GBBB_CODEC)
        mask_path = render_mask_video(state, codec=_JOB_GBBB_CODEC)

        # 2) insert_mode が GB/BB の場合だけ composite を追加生成
        composited_path = ""
        out_path = fg_path  # default: transparent は fg を挿入対象にする
        bg_mode_used = (_JOB_BG_MODE_DEFAULT or "GB").upper()

        if insert_up in ("GB", "BB"):
            bg_mode_used = insert_up
            composited_path = render_gbbb_video(
                state,
                bg_mode=bg_mode_used,
                mux_audio=bool(mux_audio),
                codec=_JOB_GBBB_CODEC,
            )
            out_path = composited_path  # GB/BB は合成済みを挿入対象にする
        stats = {"num_frames": int(state.num_frames), "fps": float(state.video_fps)}
        write_result(
            state.job_dir,
            True,
            output_video_path=out_path,
            fg_video_path=fg_path,
            mask_video_path=mask_path,
            composited_video_path=composited_path,
            bg_mode=bg_mode_used,
            insert_mode=insert_mode,
            stats=stats,
            prompt_mode=_JOB_PROMPT_MODE,
            output_mode=_JOB_OUTPUT_MODE,
        )
        write_status(state.job_dir, state="done", phase="done", progress=1.0, message="Done. You can close this tab.")

        # UIが反映される猶予を作ってから終了シグナル
        _set_event_later(done_event, delay_sec=1.0)

        return (
            "✅ **Finish 完了（result.json 書き込み済み）**\n\n"
            f"- 出力: `{out_path}`\n"
            "- AviUtl2 側が result.json を読み取って反映します。\n"
            "- このタブは閉じてOKです。"
        )
    except Exception as e:
        import traceback
        traceback.print_exc()
        write_result(state.job_dir, False, error_message=str(e))
        write_status(state.job_dir, state="failed", phase="done", progress=1.0, message=f"Failed: {e}")

        _set_event_later(done_event, delay_sec=1.0)

        return f"❌ **Failed**: {e}"

def fail_job(state: AppState, done_event: threading.Event) -> str:
    write_result(state.job_dir, False, error_message="User pressed Fail.")
    write_status(state.job_dir, state="failed", phase="done", progress=1.0, message="Failed by user.")
    done_event.set()
    return "Marked failed. You can close this tab."

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--job_dir", required=True)
    args = ap.parse_args()

    job_dir = Path(args.job_dir)
    job_dir.mkdir(parents=True, exist_ok=True)

    setup_logging(job_dir)
    setup_proxy_safety()

    print("=== sam3_gradio_job start ===")
    print("job_dir =", str(job_dir))
    print("HTTP_PROXY =", os.environ.get("HTTP_PROXY"))
    print("NO_PROXY =", os.environ.get("NO_PROXY"))

    req_path = job_dir / "request.json"
    if not req_path.exists():
        write_result(str(job_dir), False, error_message="request.json not found")
        write_status(str(job_dir), state="failed", phase="boot", progress=1.0, message="request.json not found")
        return

    with open(req_path, "r", encoding="utf-8") as f:
        req = json.load(f)

    _apply_request_defaults(req)

    print("DEVICE =", _JOB_DEVICE, "DTYPE =", str(_JOB_DTYPE))
    print("INSERT_MODE =", _JOB_INSERT_MODE_DEFAULT)

    job_id = req.get("job_id", "unknown")
    src = req.get("source_video_path", "")
    start = float(req.get("playback_start_sec", 0.0) or 0.0)
    end = float(req.get("playback_end_sec", 0.0) or 0.0)
    out_dir = req.get("output_dir", str(job_dir)) or str(job_dir)
    timeline = req.get("timeline", {}) or {}
    try:
        timeline_start_frame = int(timeline.get("start_frame", -1))
    except Exception:
        timeline_start_frame = -1
    try:
        timeline_end_frame = int(timeline.get("end_frame", -1))
    except Exception:
        timeline_end_frame = -1
    try:
        timeline_fps = float(timeline.get("fps", 0.0) or 0.0)
    except Exception:
        timeline_fps = 0.0
    timeline_num_frames = 0
    if timeline_start_frame >= 0 and timeline_end_frame >= timeline_start_frame:
        timeline_num_frames = timeline_end_frame - timeline_start_frame + 1
    timeline_duration_sec = 0.0
    if timeline_num_frames > 0 and timeline_fps > 0.0:
        timeline_duration_sec = timeline_num_frames / timeline_fps
        print(
            "TIMELINE_SEGMENT =",
            f"frames={timeline_num_frames}, fps={timeline_fps:.6f}, duration={timeline_duration_sec:.6f}s",
        )

    segment_start_sec = float(start)
    if timeline_duration_sec > 0.0:
        segment_duration_sec = float(timeline_duration_sec)
    else:
        segment_duration_sec = float(end - start)
    if segment_duration_sec <= 0.0:
        write_result(str(job_dir), False, error_message=f"Invalid segment duration: {segment_duration_sec}")
        write_status(str(job_dir), state="failed", phase="load_video", progress=1.0,
                     message=f"Invalid segment duration: {segment_duration_sec}")
        return
    segment_end_sec = segment_start_sec + segment_duration_sec
    print(
        "SEGMENT_REQUEST =",
        f"start={segment_start_sec:.6f}, end={segment_end_sec:.6f}, duration={segment_duration_sec:.6f}s",
    )

    options = req.get("options", {}) or {}
    # max_seconds=0 を “0のまま(無制限)” として扱う
    ms_val = options.get("max_seconds", 120)
    try:
        max_seconds = 120 if ms_val is None else int(ms_val)
    except Exception:
        max_seconds = 120

    # status: boot
    write_status(str(job_dir), state="running", phase="boot", progress=0.01, message="Loading model...")

    # load model
    try:
        ensure_models_loaded()
    except Exception as e:
        import traceback
        traceback.print_exc()
        write_result(str(job_dir), False, error_message=f"Model load failed: {e}")
        write_status(str(job_dir), state="failed", phase="boot", progress=1.0, message=f"Model load failed: {e}")
        return

    write_status(
        str(job_dir),
        state="running",
        phase="preprocess",
        progress=0.06,
        message="Preprocessing segment video (ffmpeg)...",
    )

    if not src:
        write_result(str(job_dir), False, error_message="source_video_path is empty")
        write_status(str(job_dir), state="failed", phase="load_video", progress=1.0, message="source_video_path is empty")
        return

    preprocessed_input_video = str(job_dir / "_segment_input_preprocessed.mp4")
    try:
        preprocess_video_segment_for_tracking(
            source_video=src,
            out_path=preprocessed_input_video,
            start_sec=segment_start_sec,
            duration_sec=segment_duration_sec,
            fps=(timeline_fps if timeline_fps > 0.0 else None),
            expected_num_frames=(timeline_num_frames if timeline_num_frames > 0 else None),
        )
    except Exception as e:
        import traceback
        traceback.print_exc()
        write_result(str(job_dir), False, error_message=f"Segment preprocess failed: {e}")
        write_status(str(job_dir), state="failed", phase="preprocess", progress=1.0, message=f"Segment preprocess failed: {e}")
        return

    write_status(str(job_dir), state="running", phase="load_video", progress=0.08, message="Loading preprocessed segment frames...")

    # load segment frames
    try:
        frames, seginfo = load_video_segment_frames(
            preprocessed_input_video,
            0.0,
            segment_duration_sec,
            max_seconds=0,
            expected_num_frames=(timeline_num_frames if timeline_num_frames > 0 else None),
        )
    except Exception as e:
        import traceback
        traceback.print_exc()
        write_result(str(job_dir), False, error_message=f"Video load failed: {e}")
        write_status(str(job_dir), state="failed", phase="load_video", progress=1.0, message=f"Video load failed: {e}")
        return

    write_status(str(job_dir), state="running", phase="init_session", progress=0.18, message="Initializing SAM3 video session...")

    # init inference session
    state0 = AppState()
    state0.job_id = str(job_id)
    state0.job_dir = str(job_dir)
    state0.output_dir = str(out_dir)
    state0.source_video_path = str(src)
    state0.segment_start_sec = float(segment_start_sec)
    state0.segment_end_sec = float(segment_end_sec)
    state0.video_frames = frames
    state0.video_fps = float(seginfo.fps)

    # Build raw video for processor session
    try:
        _, processor = ensure_models_loaded()
        raw_video = [np.array(fr) for fr in frames]  # RGB uint8
        dev = torch.device(_JOB_DEVICE)
        kwargs = dict(
            video=raw_video,
            inference_device=dev,
            processing_device="cpu",
            video_storage_device="cpu",
            inference_state_device="cpu",
            dtype=_JOB_DTYPE,
        )
        state0.inference_session = _call_init_video_session(processor, **kwargs)

        # 一部バージョンで属性が必要なことがある
        if hasattr(state0.inference_session, "inference_device"):
            state0.inference_session.inference_device = dev
        if hasattr(state0.inference_session, "cache") and hasattr(state0.inference_session.cache, "inference_device"):
            state0.inference_session.cache.inference_device = dev

        del raw_video
        gc.collect()
    except Exception as e:
        import traceback
        traceback.print_exc()
        write_result(str(job_dir), False, error_message=f"init_video_session failed: {e}")
        write_status(str(job_dir), state="failed", phase="init_session", progress=1.0, message=f"init_video_session failed: {e}")
        return

    write_status(str(job_dir), state="running", phase="ui", progress=0.25, message="Launching Gradio...")

    done_event = threading.Event()

    # Build UI
    with gr.Blocks(title=f"SAM3 Job {job_id}") as demo:
        app_state = gr.State(state0)

        gr.Markdown("## SAM3 BB/GB Generator")
        gr.Markdown("### 更新・使い方: [GitHub](https://github.com/clean262/sam3_bb_gb_generator), エラー報告・要望は[Issues](https://github.com/clean262/sam3_bb_gb_generator/issues)へ, 作者連絡先: [Twitter](https://x.com/clean123525)")
        gr.Markdown("### 紹介動画: [ニコニコ動画](https://www.nicovideo.jp/watch/sm45931905),  解説動画: Coming Soon...")
        gr.Markdown("### 作成した動画は[親作品登録](https://www.nicovideo.jp/watch/sm45931905)いただけると開発の励みになります。動画を見に行きます。")
        # gr.Markdown(f"- job_id: `{job_id}`")
        # gr.Markdown(f"- source_video_path: `{src}`")
        # gr.Markdown(f"- segment: `{seginfo.start_sec:.3f}` sec → `{seginfo.end_sec:.3f}` sec")
        gr.Markdown(f"- loaded frames: `{seginfo.num_frames}` @ `{seginfo.fps:.3f}` fps (device: `{_JOB_DEVICE}`, dtype: `{_JOB_DTYPE}`)")

        with gr.Row():
            preview = gr.Image(label="Preview (click to add prompts)", value=frames[0], interactive=False)
            with gr.Column(scale=0, min_width=240):
                frame_slider = gr.Slider(label="Frame", minimum=0, maximum=max(0, seginfo.num_frames - 1), step=1, value=0)
                propagate_btn = gr.Button("Propagate across segment", variant="primary")
                reset_prompts_btn = gr.Button("Reset prompts (keep video)", variant="secondary")
                propagate_status = gr.Markdown(value="")

        with gr.Row():
            obj_id_inp = gr.Number(value=1, precision=0, label="Object ID")
            label_radio = gr.Radio(choices=["positive", "negative"], value="positive", label="Point label")
            clear_old_chk = gr.Checkbox(value=False, label="Clear clicks for this Object ID (this frame)")
            prompt_type = gr.Radio(choices=["Points", "Boxes"], value="Points", label="Prompt type")

        with gr.Row():
            bg_choice = gr.Radio(choices=["GB", "BB"], value=_JOB_BG_MODE_DEFAULT, label="Preview BG (GB/BB)")
            mux_audio_chk = gr.Checkbox(value=_JOB_AUDIO_MUX_DEFAULT, label="Mux audio (ffmpeg)")
        preview_video = gr.Video(label="Render Preview (H.264/AAC)", interactive=False)
        with gr.Row():
            # Render Preview: “中間→preview変換” で H.264/AAC mp4 を作って Gradio に表示
            render_preview_btn = gr.Button("Render Preview (H.264/AAC)", variant="secondary")
            finish_btn = gr.Button("Finish (write result.json / no preview)", variant="primary")
            fail_btn = gr.Button("Fail", variant="secondary")

        out_msg = gr.Markdown(value="")

        # callbacks
        def _sync_frame(state_in: AppState, idx: int) -> Image.Image:
            state_in.current_frame_idx = int(idx)
            return update_frame_display(state_in, int(idx))

        frame_slider.change(fn=_sync_frame, inputs=[app_state, frame_slider], outputs=preview)

        def _click(
            img,
            state_in: AppState,
            frame_idx: int,
            obj_id: int,
            label: str,
            clear_old: bool,
            ptype: str,
            evt: gr.SelectData,   
        ):
            state_in.current_frame_idx = int(frame_idx)
            state_in.current_obj_id = int(obj_id)
            state_in.current_label = str(label)
            state_in.current_clear_old = bool(clear_old)
            state_in.current_prompt_type = str(ptype)
            return on_image_click(img, state_in, frame_idx, obj_id, label, clear_old, ptype, evt)


        preview.select(
            fn=_click,
            inputs=[preview, app_state, frame_slider, obj_id_inp, label_radio, clear_old_chk, prompt_type],
            outputs=[preview, app_state],
        )

        propagate_btn.click(
            fn=propagate_masks,
            inputs=[app_state],
            outputs=[app_state, propagate_status, frame_slider, preview],
        )

        def _reset_prompts(state_in: AppState, frame_idx: int):
            if state_in is None or state_in.num_frames == 0:
                return gr.update(), state_in, "⚠️ No video loaded.", ""

            # 現在フレームは保持
            keep_idx = int(np.clip(int(frame_idx), 0, state_in.num_frames - 1))
            reset_prompts_keep_video(state_in)
            state_in.current_frame_idx = keep_idx

            # 表示更新（マスク等が消えた素のフレームになる）
            img = update_frame_display(state_in, keep_idx)
            msg = "🧹 **Prompts cleared**（クリック/ボックス/マスクを全削除。動画は保持）"
            return img, state_in, msg, ""

        reset_prompts_btn.click(
            fn=_reset_prompts,
            inputs=[app_state, frame_slider],
            outputs=[preview, app_state, out_msg, propagate_status],
        )

        def _render_preview(state_in: AppState, bg: str, include_audio: bool):
            path = render_preview_mp4(
                state_in,
                bg_mode=bg,
                include_audio=bool(include_audio),
                max_width=1920,
                crf=23,
                preset="veryfast",
                preview_fps=None,     # 必要なら 24/30 などに
                audio_bitrate="96k",
            )
            msg = (
                "✅ **Render Preview 完了（H.264/AAC）**\n\n"
                f"- `{path}`\n"
                "- ※result.json はまだ書き込みません（Finishで書きます）"
            )
            return msg, path

        render_preview_btn.click(
            fn=_render_preview,
            inputs=[app_state, bg_choice, mux_audio_chk],
            outputs=[out_msg, preview_video],
        )

        def _finish(state_in: AppState, mux_audio: bool) -> str:
            # 最終挿入モードは request.json の output.insert_mode に従う
            return finish_job(state_in, mux_audio=bool(mux_audio), done_event=done_event)

        finish_btn.click(
            fn=_finish,
            inputs=[app_state, mux_audio_chk],
            outputs=[out_msg],   # out_video を外す
        )

        def _fail(state_in: AppState):
            return fail_job(state_in, done_event=done_event)

        fail_btn.click(fn=_fail, inputs=[app_state], outputs=[out_msg])

    # launch server
    try:
        demo.launch(
            server_name="127.0.0.1",
            server_port=None,
            share=False,
            prevent_thread_lock=True,
            quiet=False,
            inbrowser=False,
            allowed_paths=[str(Path(state0.output_dir).resolve())],
        )
        local_url = getattr(demo, "local_url", "") or ""
        print("local_url =", local_url)
        write_status(str(job_dir), state="waiting_user", phase="ui", progress=0.30, message="Gradio is running.", gradio_url=local_url)
    except Exception as e:
        import traceback
        traceback.print_exc()
        write_result(str(job_dir), False, error_message=str(e))
        write_status(str(job_dir), state="failed", phase="boot", progress=1.0, message=f"launch failed: {e}", gradio_url="")
        return

    # keep alive until done
    while not done_event.is_set():
        time.sleep(0.2)

    try:
        demo.close()
    except Exception:
        pass

    print("=== Done: exiting ===")

if __name__ == "__main__":
    main()
