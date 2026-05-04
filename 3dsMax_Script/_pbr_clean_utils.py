# -*- coding: utf-8 -*-
"""
PBR Studio — 独立工具函数
从 InteriorSceneStudioPro 中提取，不依赖 pymxs / 3ds Max / PySide。
"""

import re
import traceback


def safe_str(value, default=""):
    try:
        if value is None:
            return default
        s = str(value)
        if s.lower() in ("undefined", "none"):
            return default
        return s
    except Exception:
        return default


def clean_name_part(text, default="None"):
    s = safe_str(text, default).strip()
    if not s:
        s = default
    s = re.sub(r'[\[\]\{\}\(\)<>\:"/\\|?*\n\r\t]+', "_", s)
    s = re.sub(r"\s+", "_", s)
    s = re.sub(r"_+", "_", s)
    s = s.strip("_")
    return s or default


def status_text_for_exception(prefix):
    try:
        return "{}：{}".format(prefix, traceback.format_exc().splitlines()[-1])
    except Exception:
        return prefix
