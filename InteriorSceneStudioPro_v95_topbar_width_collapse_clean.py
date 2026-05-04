# -*- coding: utf-8 -*-
"""
Interior Scene Studio Pro V16 - 3ds Max / pymxs

面向室内设计 UE 导入前整理：
1. 对象修复：无材质补材质、缩放异常 Reset XForm + 转 Poly、轴心归底居中
2. 对象 / 灯光 / 相机 / 材质列表管理
3. 列表勾选工具：只勾选中、只勾未选中、全部、取消、反转
4. 延迟同步选择，避免列表选择卡顿
5. 问题检测与筛选
6. 重命名前预览确认
7. 一键撤回上次重命名
8. 操作日志
"""

import random
import re
import traceback
import html
import os
import sys
import json
import base64
import shutil
import subprocess
import webbrowser
import csv
import math
import tempfile
import time
import zipfile
import urllib.request
import urllib.parse
import urllib.error
import urllib.parse as _urlparse
from datetime import datetime

import pymxs

try:
    from PySide2 import QtWidgets, QtCore, QtGui
except Exception:
    from PySide6 import QtWidgets, QtCore, QtGui

QTWEBENGINE_ERROR = ""
QtWebEngineCore = None
WEBENGINE_PAGE_CLASS = object
try:
    from PySide2 import QtWebEngineWidgets
    try:
        from PySide2 import QtWebEngineCore
    except Exception:
        QtWebEngineCore = None
    HAS_QTWEBENGINE = True
except Exception as _qtweb_e1:
    try:
        from PySide6 import QtWebEngineWidgets
        try:
            from PySide6 import QtWebEngineCore
        except Exception:
            QtWebEngineCore = None
        HAS_QTWEBENGINE = True
    except Exception as _qtweb_e2:
        QtWebEngineWidgets = None
        QtWebEngineCore = None
        HAS_QTWEBENGINE = False
        QTWEBENGINE_ERROR = "{} / {}".format(_qtweb_e1, _qtweb_e2)
if HAS_QTWEBENGINE:
    WEBENGINE_PAGE_CLASS = getattr(QtWebEngineWidgets, "QWebEnginePage", None) or getattr(QtWebEngineCore, "QWebEnginePage", None) or object
    if WEBENGINE_PAGE_CLASS is object:
        HAS_QTWEBENGINE = False
        QTWEBENGINE_ERROR = QTWEBENGINE_ERROR or "QWebEnginePage unavailable"

# ── Windows 高 DPI / 缩放支持（适配 4K 150% 等任意缩放比）──────────────
os.environ.setdefault("QT_ENABLE_HIGHDPI_SCALING", "1")
os.environ.setdefault("QT_AUTO_SCREEN_SCALE_FACTOR", "1")
os.environ.setdefault("QT_SCALE_FACTOR_ROUNDING_POLICY", "PassThrough")
try:
    _dpi_app = QtWidgets.QApplication.instance()
    if _dpi_app is not None:
        _dpi_app.setAttribute(QtCore.Qt.AA_EnableHighDpiScaling, True)
        _dpi_app.setAttribute(QtCore.Qt.AA_UseHighDpiPixmaps, True)
except Exception:
    pass
try:
    from PySide6.QtGui import QGuiApplication as _QGuiApp
    _QGuiApp.setHighDpiScaleFactorRoundingPolicy(
        QtCore.Qt.HighDpiScaleFactorRoundingPolicy.PassThrough
    )
except Exception:
    pass

rt = pymxs.runtime


# ============================================================
# Qt 兼容
# ============================================================

def qt_enum(name, group_names=("WindowType", "ItemFlag", "CheckState", "AlignmentFlag")):
    if hasattr(QtCore.Qt, name):
        return getattr(QtCore.Qt, name)
    for gname in group_names:
        group = getattr(QtCore.Qt, gname, None)
        if group is not None and hasattr(group, name):
            return getattr(group, name)
    raise AttributeError(name)

QT_WINDOW = qt_enum("Window")
QT_STAY_ON_TOP = qt_enum("WindowStaysOnTopHint")
QT_CHECKED = qt_enum("Checked")
QT_UNCHECKED = qt_enum("Unchecked")
QT_ITEM_USER_CHECKABLE = qt_enum("ItemIsUserCheckable")
QT_ITEM_SELECTABLE = qt_enum("ItemIsSelectable")
QT_ITEM_ENABLED = qt_enum("ItemIsEnabled")
QT_ALIGN_CENTER = qt_enum("AlignCenter")
QT_ALIGN_VCENTER = qt_enum("AlignVCenter")
QT_HORIZONTAL = qt_enum("Horizontal", ("Orientation",))
QT_VERTICAL = qt_enum("Vertical", ("Orientation",))
QT_KEY_RETURN = qt_enum("Key_Return", ("Key",))
QT_KEY_ENTER = qt_enum("Key_Enter", ("Key",))
QT_SHIFT_MODIFIER = qt_enum("ShiftModifier", ("KeyboardModifier",))

try:
    EXTENDED_SELECTION = QtWidgets.QAbstractItemView.ExtendedSelection
except Exception:
    EXTENDED_SELECTION = QtWidgets.QAbstractItemView.SelectionMode.ExtendedSelection


# ============================================================
# 基础工具
# ============================================================

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


def get_anim_handle(obj):
    try:
        return int(rt.getHandleByAnim(obj))
    except Exception:
        try:
            return id(obj)
        except Exception:
            return 0


def unique_by_handle(items):
    result = []
    used = set()
    for item in items:
        if item is None:
            continue
        h = get_anim_handle(item)
        if h in used:
            continue
        used.add(h)
        result.append(item)
    return result


def get_class_name(obj):
    try:
        return clean_name_part(str(rt.classOf(obj)), "Unknown")
    except Exception:
        return "Unknown"


def get_super_class_name(obj):
    try:
        return clean_name_part(str(rt.superClassOf(obj)), "Unknown")
    except Exception:
        return "Unknown"


def status_text_for_exception(prefix):
    try:
        return "{}：{}".format(prefix, traceback.format_exc().splitlines()[-1])
    except Exception:
        return prefix


def color_to_rgba_css(color_value, alpha=0.6):
    try:
        q = QtGui.QColor(safe_str(color_value, "#FFFFFF"))
        if not q.isValid():
            q = QtGui.QColor("#FFFFFF")
        a = max(0.0, min(1.0, float(alpha)))
        return "rgba({},{},{},{:.3f})".format(q.red(), q.green(), q.blue(), a)
    except Exception:
        return safe_str(color_value, "#FFFFFF")


def is_valid_node(obj):
    try:
        return obj is not None and bool(rt.isValidNode(obj))
    except Exception:
        return False


def is_frozen(obj):
    try:
        return bool(obj.isFrozen)
    except Exception:
        return False


def is_hidden(obj):
    try:
        return bool(obj.isHidden)
    except Exception:
        return False


def is_group_head(obj):
    try:
        return is_valid_node(obj) and bool(rt.isGroupHead(obj))
    except Exception:
        return False


def is_group_member(obj):
    try:
        return is_valid_node(obj) and bool(rt.isGroupMember(obj))
    except Exception:
        return False


def get_group_head_node(obj):
    """返回节点所属的组头。组头返回自身，组成员尝试返回母组头。"""
    try:
        if is_group_head(obj):
            return obj
        if is_group_member(obj):
            head = rt.getGroupHead(obj)
            if is_valid_node(head):
                return head
    except Exception:
        pass
    return None


def is_group_open(obj):
    try:
        if is_group_head(obj):
            return bool(rt.isOpenGroupHead(obj))
    except Exception:
        pass
    return False


def get_group_member_count(obj):
    if not is_group_head(obj):
        return 0
    count = 0
    h = get_anim_handle(obj)
    for node in all_scene_nodes():
        try:
            head = get_group_head_node(node)
            if is_valid_node(head) and get_anim_handle(head) == h and get_anim_handle(node) != h:
                count += 1
        except Exception:
            pass
    return count


def set_group_open_state(obj, state=True):
    try:
        if is_group_head(obj):
            rt.setGroupOpen(obj, bool(state))
            return True
    except Exception:
        pass
    return False


def close_all_groups():
    count = 0
    for obj in all_scene_nodes():
        if is_group_head(obj):
            try:
                rt.setGroupOpen(obj, False)
                count += 1
            except Exception:
                pass
    try:
        rt.execute("for o in objects where isGroupHead o do (try(setGroupOpen o false)catch())")
    except Exception:
        pass
    return count


def get_layer_name(obj):
    try:
        return clean_name_part(obj.layer.name, "NoLayer")
    except Exception:
        return "NoLayer"


# ============================================================
# 类型判断
# ============================================================

def is_valid_geometry(obj):
    try:
        if not is_valid_node(obj):
            return False
        sc = get_super_class_name(obj).lower()
        if "geometry" in sc:
            return True
        try:
            if rt.isKindOf(obj, rt.GeometryClass):
                return True
        except Exception:
            pass
        return False
    except Exception:
        return False


def is_valid_light(obj):
    try:
        if not is_valid_node(obj):
            return False
        sc = get_super_class_name(obj).lower()
        cls = get_class_name(obj).lower()
        if "light" in sc or "light" in cls or "sun" in cls or "sky" in cls:
            return True
        try:
            if rt.isKindOf(obj, rt.Light):
                return True
        except Exception:
            pass
        return False
    except Exception:
        return False


def is_valid_camera(obj):
    try:
        if not is_valid_node(obj):
            return False
        sc = get_super_class_name(obj).lower()
        cls = get_class_name(obj).lower()
        if "camera" in sc or "camera" in cls or cls.startswith("cam"):
            return True
        try:
            if rt.isKindOf(obj, rt.Camera):
                return True
        except Exception:
            pass
        return False
    except Exception:
        return False


def all_scene_nodes():
    try:
        return [o for o in rt.objects if is_valid_node(o)]
    except Exception:
        return []


def get_scene_geometry():
    return unique_by_handle([o for o in all_scene_nodes() if is_valid_geometry(o)])


def get_selected_geometry():
    try:
        return unique_by_handle([o for o in rt.selection if is_valid_geometry(o)])
    except Exception:
        return []


def get_scene_object_list_nodes():
    return unique_by_handle([o for o in all_scene_nodes() if is_valid_geometry(o) or is_group_head(o)])


def get_selected_object_list_nodes():
    try:
        return unique_by_handle([o for o in rt.selection if is_valid_geometry(o) or is_group_head(o)])
    except Exception:
        return []


def get_selected_groups():
    groups = []
    try:
        for o in rt.selection:
            head = get_group_head_node(o)
            if is_valid_node(head):
                groups.append(head)
    except Exception:
        pass
    return unique_by_handle(groups)


def get_scene_groups():
    return unique_by_handle([o for o in all_scene_nodes() if is_group_head(o)])


def get_scene_lights():
    return unique_by_handle([o for o in all_scene_nodes() if is_valid_light(o)])


def get_selected_lights():
    try:
        return unique_by_handle([o for o in rt.selection if is_valid_light(o)])
    except Exception:
        return []


def get_scene_cameras():
    return unique_by_handle([o for o in all_scene_nodes() if is_valid_camera(o)])


def get_selected_cameras():
    try:
        return unique_by_handle([o for o in rt.selection if is_valid_camera(o)])
    except Exception:
        return []


# ============================================================
# 组 / 选择
# ============================================================

def open_all_groups():
    count = 0
    for obj in all_scene_nodes():
        try:
            if is_group_head(obj):
                rt.setGroupOpen(obj, True)
                count += 1
        except Exception:
            pass
    try:
        rt.execute("for o in objects where isGroupHead o do (try(setGroupOpen o true)catch())")
    except Exception:
        pass
    return count


def select_nodes_in_scene_fast(nodes, allow_func=None):
    valid_nodes = []
    for obj in nodes:
        if not is_valid_node(obj):
            continue
        if is_frozen(obj):
            continue
        if allow_func is not None and not allow_func(obj):
            continue
        valid_nodes.append(obj)

    try:
        rt.disableSceneRedraw()
    except Exception:
        pass
    try:
        rt.clearSelection()
        if valid_nodes:
            try:
                rt.select(valid_nodes)
            except Exception:
                for obj in valid_nodes:
                    try:
                        obj.isSelected = True
                    except Exception:
                        pass
    finally:
        try:
            rt.enableSceneRedraw()
        except Exception:
            pass
        try:
            rt.redrawViews()
        except Exception:
            pass
    return len(valid_nodes)


# ============================================================
# 材质
# ============================================================

def is_valid_material(mat):
    try:
        if mat is None:
            return False
        s = str(mat).lower()
        if s in ("undefined", "none"):
            return False
        return True
    except Exception:
        return False


def get_material_name(mat):
    if is_valid_material(mat):
        try:
            return clean_name_part(mat.name, "NoMat")
        except Exception:
            pass
    return "NoMat"


def get_object_material_name(obj):
    try:
        mat = obj.material
        if is_valid_material(mat):
            return get_material_name(mat)
    except Exception:
        pass
    return "NoMat"


def object_has_material(obj):
    try:
        return is_valid_material(obj.material)
    except Exception:
        return False


def renderer_family_from_class_name(class_name):
    s = safe_str(class_name).lower()
    if "vray" in s or "v_ray" in s:
        return "VRay"
    if "corona" in s:
        return "Corona"
    if "arnold" in s or "ai_" in s or "standard_surface" in s:
        return "Arnold"
    if "redshift" in s or "rs_" in s:
        return "Redshift"
    if "octane" in s:
        return "Octane"
    if "fstorm" in s:
        return "FStorm"
    if "physical" in s:
        return "Physical"
    if "standard" in s:
        return "Standard"
    if "multi" in s or "sub" in s:
        return "MultiSub"
    if "sun" in s:
        return "Sun"
    if "sky" in s:
        return "Sky"
    return "Type"


def get_material_family(mat):
    return renderer_family_from_class_name(get_class_name(mat))


def get_node_family(obj):
    return renderer_family_from_class_name(get_class_name(obj))


def is_multi_material(mat):
    if not is_valid_material(mat):
        return False
    cls = get_class_name(mat).lower()
    if "multi" in cls and ("material" in cls or "sub" in cls):
        return True
    try:
        _ = mat.materialList
        _ = mat.materialIDList
        return True
    except Exception:
        return False


def get_array_as_list(arr):
    try:
        return list(arr)
    except Exception:
        return []


def get_multi_material_subs(mat):
    result = []
    if not is_multi_material(mat):
        return result
    try:
        mats = get_array_as_list(mat.materialList)
    except Exception:
        mats = []
    try:
        ids = get_array_as_list(mat.materialIDList)
    except Exception:
        ids = []
    for i, sub in enumerate(mats):
        if not is_valid_material(sub):
            continue
        mat_id = i + 1
        if i < len(ids):
            try:
                mat_id = int(ids[i])
            except Exception:
                mat_id = i + 1
        result.append({"slot": i + 1, "mat_id": mat_id, "mat": sub})
    return result


def set_multi_slot_name(parent_mat, slot, name):
    try:
        arr = parent_mat.names
    except Exception:
        return False
    try:
        arr[slot - 1] = name
        return True
    except Exception:
        pass
    try:
        arr[slot] = name
        return True
    except Exception:
        pass
    return False


def random_color():
    return rt.color(random.randint(50, 230), random.randint(50, 230), random.randint(50, 230))


def safe_set_color(mat, color_value):
    for prop in ["diffuse", "diffuse_color", "base_color", "baseColor", "color", "albedo"]:
        try:
            setattr(mat, prop, color_value)
            return True
        except Exception:
            pass
    return False


def try_create_material_by_class(class_name):
    try:
        cls = getattr(rt, class_name)
        mat = cls()
        if is_valid_material(mat):
            return mat
    except Exception:
        pass
    try:
        mat = rt.execute("try ({}()) catch undefined".format(class_name))
        if is_valid_material(mat):
            return mat
    except Exception:
        pass
    return None


def create_auto_material(obj_name="Object"):
    color_value = random_color()
    renderer_name = ""
    try:
        renderer_name = str(rt.classOf(rt.renderers.current)).lower()
    except Exception:
        renderer_name = ""
    classes = []
    if "vray" in renderer_name or "v_ray" in renderer_name:
        classes += ["VRayMtl", "VRayMaterial"]
    if "corona" in renderer_name:
        classes += ["CoronaPhysicalMtl", "CoronaMtl", "CoronaLegacyMtl"]
    if "arnold" in renderer_name:
        classes += ["Arnold_Standard_Surface", "ai_standard_surface", "Standard_Surface"]
    if "redshift" in renderer_name:
        classes += ["RedshiftMaterial", "RS_Material"]
    if "octane" in renderer_name:
        classes += ["Octane_Universal_Material", "Octane_Diffuse_Material"]
    if "fstorm" in renderer_name:
        classes += ["FStormMtl"]
    classes += ["PhysicalMaterial", "StandardMaterial", "standardMaterial"]

    for class_name in classes:
        mat = try_create_material_by_class(class_name)
        if not is_valid_material(mat):
            continue
        try:
            mat.name = "UE_AutoMat_{}".format(clean_name_part(obj_name, "Object"))
        except Exception:
            pass
        safe_set_color(mat, color_value)
        return mat
    return None


def assign_random_material(obj):
    try:
        mat = create_auto_material(obj.name)
        if not is_valid_material(mat):
            return False, "创建材质失败"
        obj.material = mat
        return True, "补随机材质"
    except Exception:
        return False, status_text_for_exception("补材质失败")


# ============================================================
# 几何修复 / 检测
# ============================================================

def get_object_scale(obj):
    try:
        s = obj.scale
        return float(s.x), float(s.y), float(s.z)
    except Exception:
        return 1.0, 1.0, 1.0


def is_scale_100(obj, tolerance=0.0001):
    try:
        sx, sy, sz = get_object_scale(obj)
        return abs(sx - 1.0) <= tolerance and abs(sy - 1.0) <= tolerance and abs(sz - 1.0) <= tolerance
    except Exception:
        return True


def convert_to_poly(obj):
    try:
        rt.convertToPoly(obj)
        return True
    except Exception:
        pass
    try:
        rt.convertTo(obj, rt.Editable_Poly)
        return True
    except Exception:
        pass
    return False


def reset_xform_and_convert_poly(obj):
    try:
        rt.resetXForm(obj)
        rt.collapseStack(obj)
        if not convert_to_poly(obj):
            return False, "转多边形失败"
        return True, "重置变换并转多边形"
    except Exception:
        return False, status_text_for_exception("重置变换失败")


def get_world_bbox(obj):
    try:
        bbox = rt.nodeGetBoundingBox(obj, rt.matrix3(1))
        return bbox[0], bbox[1]
    except Exception:
        pass
    try:
        bbox = rt.nodeGetBoundingBox(obj, obj.transform)
        return bbox[0], bbox[1]
    except Exception:
        return None, None


def get_bottom_center_point(obj):
    try:
        mn, mx = get_world_bbox(obj)
        if mn is None or mx is None:
            return None
        return rt.point3((mn.x + mx.x) * 0.5, (mn.y + mx.y) * 0.5, mn.z)
    except Exception:
        return None


def is_pivot_bottom_center(obj, tolerance=0.01):
    try:
        target = get_bottom_center_point(obj)
        if target is None:
            return True
        p = obj.pivot
        return abs(p.x - target.x) <= tolerance and abs(p.y - target.y) <= tolerance and abs(p.z - target.z) <= tolerance
    except Exception:
        return True


def pivot_to_bottom_center(obj):
    try:
        target = get_bottom_center_point(obj)
        if target is None:
            return False, "无法计算包围盒"
        obj.pivot = target
        return True, "轴心归底居中"
    except Exception:
        return False, status_text_for_exception("轴心处理失败")


def is_negative_scale(obj, tolerance=0.0001):
    try:
        sx, sy, sz = get_object_scale(obj)
        return sx < -tolerance or sy < -tolerance or sz < -tolerance
    except Exception:
        return False


def is_non_uniform_scale(obj, tolerance=0.0001):
    try:
        sx, sy, sz = [abs(v) for v in get_object_scale(obj)]
        return max(sx, sy, sz) - min(sx, sy, sz) > tolerance
    except Exception:
        return False


def is_editable_poly_object(obj):
    try:
        cls = get_class_name(obj).lower()
        return "editable_poly" in cls or "editablepoly" in cls or cls == "editable_poly"
    except Exception:
        return False


def is_special_or_proxy_node(obj):
    try:
        cls = get_class_name(obj).lower()
        name = safe_str(getattr(obj, "name", ""), "").lower()
        keys = ["xref", "proxy", "vrayproxy", "vray_proxy", "coronaproxy", "corona_proxy", "redshiftproxy", "octaneproxy", "alembic", "abc", "forest", "railclone", "scatter"]
        return any(k in cls or k in name for k in keys)
    except Exception:
        return False


def has_modifier_stack(obj):
    try:
        return int(obj.modifiers.count) > 0
    except Exception:
        try:
            return len(list(obj.modifiers)) > 0
        except Exception:
            return False


def detect_geometry_issues(obj):
    issues = []
    if not is_valid_node(obj):
        return ["无效对象"]
    if is_group_head(obj):
        return ["组对象"]
    if not is_valid_geometry(obj):
        return ["非几何体"]
    if is_special_or_proxy_node(obj):
        issues.append("代理/外链")
    if is_hidden(obj):
        issues.append("隐藏")
    if is_frozen(obj):
        issues.append("冻结")
    if not object_has_material(obj):
        issues.append("无材质")
    if not is_scale_100(obj):
        issues.append("缩放异常")
    if is_non_uniform_scale(obj):
        issues.append("非等比缩放")
    if is_negative_scale(obj):
        issues.append("负缩放")
    if not is_pivot_bottom_center(obj):
        issues.append("轴心异常")
    if not is_editable_poly_object(obj):
        issues.append("非Poly")
    if has_modifier_stack(obj):
        issues.append("有修改器")
    return issues


def repair_geometry(obj, fix_material=True, fix_scale=True, fix_pivot=True, skip_frozen=False):
    try:
        if not is_valid_geometry(obj):
            return False, "跳过：非几何体"
        if is_special_or_proxy_node(obj):
            return True, "跳过：代理/外链/散布等特殊对象"
        if skip_frozen and is_frozen(obj):
            return True, "跳过：冻结对象"
        actions = []
        errors = []
        if fix_material and not object_has_material(obj):
            ok, msg = assign_random_material(obj)
            actions.append(msg) if ok else errors.append(msg)
        if fix_scale and not is_scale_100(obj):
            ok, msg = reset_xform_and_convert_poly(obj)
            actions.append(msg) if ok else errors.append(msg)
        if fix_pivot and not is_pivot_bottom_center(obj):
            ok, msg = pivot_to_bottom_center(obj)
            actions.append(msg) if ok else errors.append(msg)
        if errors:
            if actions:
                return False, "部分完成：{}；错误：{}".format("，".join(actions), "，".join(errors))
            return False, "失败：{}".format("，".join(errors))
        if actions:
            return True, "完成：{}".format("，".join(actions))
        return True, "无问题，跳过"
    except Exception:
        return False, status_text_for_exception("异常")


# ============================================================
# 命名计划 / 材质收集
# ============================================================

def build_used_node_names(ignore_nodes=None):
    ignore = set(get_anim_handle(n) for n in (ignore_nodes or []))
    used = set()
    for obj in all_scene_nodes():
        if get_anim_handle(obj) in ignore:
            continue
        used.add(safe_str(getattr(obj, "name", ""), "").lower())
    return used


def unique_name_from_set(base_name, used_names):
    name = base_name
    i = 1
    while name.lower() in used_names:
        name = "{}_{:03d}".format(base_name, i)
        i += 1
    used_names.add(name.lower())
    return name


def build_object_name(obj, prefix, index, padding, use_layer=True, use_material=True, use_group_tag=True):
    parts = [clean_name_part(prefix, "SM")]
    if use_group_tag:
        if is_group_head(obj):
            parts.append("GRP")
        elif is_group_member(obj):
            parts.append("GMB")
    if use_layer:
        parts.append(get_layer_name(obj))
    if use_material:
        parts.append(get_object_material_name(obj) if is_valid_geometry(obj) else "NoMat")
    parts.append(str(index).zfill(padding))
    return "_".join([p for p in parts if p])


def build_group_name(obj, prefix, index, padding, use_layer=True, use_group_tag=True, use_member_count=True):
    parts = [clean_name_part(prefix, "GRP")]
    if use_group_tag:
        parts.append("GRP")
    if use_layer:
        parts.append(get_layer_name(obj))
    if use_member_count:
        try:
            parts.append("M{:02d}".format(get_group_member_count(obj)))
        except Exception:
            parts.append("M00")
    parts.append(str(index).zfill(padding))
    return "_".join([p for p in parts if p])


def build_light_name(obj, prefix, index, padding, use_layer=True, use_type=True, use_light_tag=True):
    parts = [clean_name_part(prefix, "L")]
    if use_light_tag:
        parts.append("LGT")
    if use_layer:
        parts.append(get_layer_name(obj))
    if use_type:
        parts.append(get_node_family(obj))
        parts.append(get_class_name(obj))
    parts.append(str(index).zfill(padding))
    return "_".join([p for p in parts if p])


def build_camera_name(obj, prefix, index, padding, use_layer=True, use_type=True, use_camera_tag=True):
    parts = [clean_name_part(prefix, "CAM")]
    if use_camera_tag:
        parts.append("CAM")
    if use_layer:
        parts.append(get_layer_name(obj))
    if use_type:
        parts.append(get_node_family(obj))
        parts.append(get_class_name(obj))
    parts.append(str(index).zfill(padding))
    return "_".join([p for p in parts if p])


def make_node_rename_plan(nodes, build_func, prefix, start_index, padding, **kwargs):
    plan = []
    index = start_index
    used = build_used_node_names(ignore_nodes=nodes)
    for obj in nodes:
        if not is_valid_node(obj):
            plan.append({"kind": "node", "ref": obj, "old": "", "new": "", "ok": False, "note": "无效对象"})
            continue
        try:
            old_name = safe_str(getattr(obj, "name", ""), "Object")
            base = build_func(obj, prefix, index, padding, **kwargs)
            new_name = unique_name_from_set(base, used)
            plan.append({"kind": "node", "ref": obj, "old": old_name, "new": new_name, "ok": True, "note": ""})
            index += 1
        except Exception:
            plan.append({"kind": "node", "ref": obj, "old": safe_str(getattr(obj, "name", ""), "Object"), "new": "", "ok": False, "note": status_text_for_exception("生成失败")})
    return plan


def material_context_key(entry):
    mat = entry.get("mat")
    parent = entry.get("parent")
    role = entry.get("role", "MAT")
    slot = entry.get("slot", 0)
    return (get_anim_handle(mat), get_anim_handle(parent) if parent else 0, role, slot)


def collect_material_entries_from_material(mat, entries, parent=None, parent_name="", visited=None):
    if visited is None:
        visited = set()
    if not is_valid_material(mat):
        return
    h = get_anim_handle(mat)
    if parent is None:
        if h in visited:
            return
        visited.add(h)
    if is_multi_material(mat):
        entries.append({"mat": mat, "role": "MSO", "parent": parent, "parent_name": parent_name, "slot": 0, "mat_id": 0})
        for sub_info in get_multi_material_subs(mat):
            sub = sub_info.get("mat")
            slot = sub_info.get("slot", 0)
            mat_id = sub_info.get("mat_id", 0)
            entries.append({"mat": sub, "role": "SUB", "parent": mat, "parent_name": get_material_name(mat), "slot": slot, "mat_id": mat_id})
            if is_multi_material(sub):
                collect_material_entries_from_material(sub, entries, parent=mat, parent_name=get_material_name(mat), visited=visited)
    else:
        entries.append({"mat": mat, "role": "MAT", "parent": parent, "parent_name": parent_name, "slot": 0, "mat_id": 0})


def collect_scene_material_entries():
    entries = []
    for obj in get_scene_geometry():
        try:
            mat = obj.material
            if is_valid_material(mat):
                collect_material_entries_from_material(mat, entries)
        except Exception:
            pass
    try:
        for mat in rt.sceneMaterials:
            if is_valid_material(mat):
                collect_material_entries_from_material(mat, entries)
    except Exception:
        pass
    result = []
    used = set()
    for entry in entries:
        key = material_context_key(entry)
        if key in used:
            continue
        used.add(key)
        result.append(entry)
    return result


def collect_selected_material_entries():
    entries = []
    for obj in get_selected_geometry():
        try:
            mat = obj.material
            if is_valid_material(mat):
                collect_material_entries_from_material(mat, entries)
        except Exception:
            pass
    result = []
    used = set()
    for entry in entries:
        key = material_context_key(entry)
        if key in used:
            continue
        used.add(key)
        result.append(entry)
    return result


def collect_material_handles_recursive(mat, result=None):
    if result is None:
        result = set()
    if not is_valid_material(mat):
        return result
    result.add(get_anim_handle(mat))
    if is_multi_material(mat):
        for sub_info in get_multi_material_subs(mat):
            collect_material_handles_recursive(sub_info.get("mat"), result)
    return result


def build_material_usage_map():
    usage = {}
    for obj in get_scene_geometry():
        try:
            mat = obj.material
        except Exception:
            mat = None
        if not is_valid_material(mat):
            continue
        for h in collect_material_handles_recursive(mat):
            usage.setdefault(h, []).append(obj)
    for h in list(usage.keys()):
        usage[h] = unique_by_handle(usage[h])
    return usage


def build_material_name(entry, prefix, index, padding, use_class=True, use_parent=True):
    mat = entry.get("mat")
    role = entry.get("role", "MAT")
    mat_id = entry.get("mat_id", 0)
    slot = entry.get("slot", 0)
    parent_name = entry.get("parent_name", "")
    parts = [clean_name_part(prefix, "M")]
    if role == "MSO":
        parts.append("MSO")
    elif role == "SUB":
        parts.append("SUB")
        parts.append("ID{:02d}".format(mat_id) if mat_id else "SLOT{:02d}".format(slot))
    else:
        parts.append("MAT")
    if use_parent and role == "SUB":
        parts.append(clean_name_part(parent_name, "Parent"))
    if use_class:
        parts.append(get_material_family(mat))
        parts.append(get_class_name(mat))
    parts.append(str(index).zfill(padding))
    return "_".join([p for p in parts if p])


def build_used_material_names(ignore_entries=None):
    ignore = set()
    for entry in ignore_entries or []:
        mat = entry.get("mat")
        if is_valid_material(mat):
            ignore.add(get_anim_handle(mat))
    used = set()
    for entry in collect_scene_material_entries():
        mat = entry.get("mat")
        if not is_valid_material(mat):
            continue
        if get_anim_handle(mat) in ignore:
            continue
        used.add(get_material_name(mat).lower())
    return used


def make_material_rename_plan(entries, prefix, start_index, padding, use_class=True, use_parent=True):
    plan = []
    index = start_index
    used_names = build_used_material_names(ignore_entries=entries)
    renamed_handles = set()
    for entry in entries:
        mat = entry.get("mat")
        if not is_valid_material(mat):
            plan.append({"kind": "material", "entry": entry, "ref": mat, "old": "", "new": "", "ok": False, "note": "无效材质"})
            continue
        h = get_anim_handle(mat)
        if h in renamed_handles:
            plan.append({"kind": "material", "entry": entry, "ref": mat, "old": get_material_name(mat), "new": "", "ok": False, "note": "重复材质，跳过"})
            continue
        try:
            old_name = get_material_name(mat)
            base = build_material_name(entry, prefix, index, padding, use_class=use_class, use_parent=use_parent)
            new_name = unique_name_from_set(base, used_names)
            plan.append({"kind": "material", "entry": entry, "ref": mat, "old": old_name, "new": new_name, "ok": True, "note": entry.get("role", "MAT")})
            renamed_handles.add(h)
            index += 1
        except Exception:
            plan.append({"kind": "material", "entry": entry, "ref": mat, "old": get_material_name(mat), "new": "", "ok": False, "note": status_text_for_exception("生成失败")})
    return plan



# ============================================================
# 材质标准化工具（Physical / PBR Metal-Rough / OpenPBR）
# ============================================================

def safe_get_attr_any(obj, names, default=None):
    for name in names:
        try:
            value = getattr(obj, name)
            if value is not None and safe_str(value, "") != "":
                return value
        except Exception:
            pass
    return default


def safe_set_attr_any(obj, names, value):
    for name in names:
        try:
            setattr(obj, name, value)
            return True
        except Exception:
            pass
    return False


def safe_enable_map_slot(obj, prop_name):
    """
    3ds Max 的一些材质不只是设置 xxx_map，还需要 xxx_map_on=true 才会在 UI 和渲染中启用。
    V-Ray / Corona / Physical 不同版本命名不同，所以这里尽量宽松打开。
    """
    if not obj or not prop_name:
        return
    candidates = []
    p = safe_str(prop_name, "")

    if p.endswith("_map"):
        candidates.append(p + "_on")
        candidates.append(p[:-4] + "_map_on")
    if p.startswith("texmap_"):
        candidates.append(p + "_on")
        candidates.append(p.replace("texmap_", "map_") + "_on")
    if p.endswith("Map"):
        candidates.append(p + "On")
        candidates.append(p + "_on")
    if p.endswith("Texmap"):
        candidates.append(p + "On")
        candidates.append(p + "_on")

    candidates.append(p + "_on")
    candidates.append(p + "On")

    for name in candidates:
        try:
            setattr(obj, name, True)
        except Exception:
            pass


def configure_renderer_material_for_pbr(mat, target_mode="PBR Material Metal/Rough"):
    """
    创建 V-Ray / Corona 材质时，尽量切到更适合 Metal/Roughness 的模式。
    不同版本属性名不同，所以这里宽松尝试。
    """
    key = material_target_mode_key(target_mode)
    cls = get_class_name(mat).lower()

    if key == "vray" or "vray" in cls:
        # V-Ray PBR 工作流通常需要启用 Use Roughness，否则 roughness 会按 glossiness 逻辑反着来。
        safe_set_attr_any(mat, [
            "useRoughness", "use_roughness", "brdf_useRoughness", "BRDF_useRoughness",
            "reflection_useRoughness", "refl_useRoughness"
        ], True)
        safe_set_attr_any(mat, ["metalness", "metalness_on"], 1)

    if key == "corona" or "corona" in cls:
        # Corona Physical Material 本身就是 PBR/物理流程；这里只做宽松默认。
        safe_set_attr_any(mat, ["metalnessMode", "metalness_mode"], 1)


def material_prop_names_lower(mat):
    try:
        return set([safe_str(p, "").lower() for p in rt.getPropNames(mat)])
    except Exception:
        return set()


def anim_value_identity(value):
    """
    尽量取 Max 对象句柄，用于验证贴图是否真的写进材质槽。
    """
    try:
        return int(rt.getHandleByAnim(value))
    except Exception:
        pass
    try:
        return str(value)
    except Exception:
        return ""


def verify_material_slot_value(mat, prop_name, tex):
    """
    写入槽位后读回验证。
    如果读回不是同一个贴图对象，就不能算真正接上。
    """
    try:
        current = getattr(mat, prop_name)
    except Exception:
        return False

    try:
        if current == tex:
            return True
    except Exception:
        pass

    try:
        return anim_value_identity(current) == anim_value_identity(tex)
    except Exception:
        return False


def is_texmap_like_value(value):
    """
    一些 3ds Max 材质槽写入 Normal_Bump 后，读回对象可能不是同一个 Python 包装对象，
    甚至会被材质内部包装。这里用于兼容性判断：只确认槽里确实有贴图类对象。
    """
    if value is None:
        return False
    try:
        if safe_str(value, "").lower() in ("undefined", "none"):
            return False
    except Exception:
        pass
    try:
        if rt.isKindOf(value, rt.TextureMap):
            return True
    except Exception:
        pass
    # 兜底：能拿到 class 且不是基础数值，通常是 MaxWrapper / Texmap
    try:
        cls = get_class_name(value).lower()
        if any(k in cls for k in ["bitmap", "normal", "bump", "map", "tex"]):
            return True
    except Exception:
        pass
    return False


def allow_tolerant_slot_verification(mat, prop_name, channel_label):
    """
    只对 PBR Metal/Rough 的法线通道放宽验证。
    原因：Max 的 PBR 材质法线槽有时会包装 Normal_Bump，导致严格句柄比较失败；
    但它在 UI/渲染中实际已经接上了。
    V-Ray / Corona 仍然保持严格验证，避免又回到"脚本写了但实际没接"的问题。
    """
    cls = get_class_name(mat).lower()
    ch = safe_str(channel_label, "").lower()
    prop = safe_str(prop_name, "").lower()

    is_pbr_material = ("pbr" in cls or "gltf" in cls or "metalrough" in cls or "metal_rough" in cls)
    is_normal_channel = ("normal" in ch or "normal" in prop or "bump" in prop)

    return is_pbr_material and is_normal_channel


def set_material_slot_verified_for_channel(mat, prop_name, tex, channel_label=""):
    """
    默认严格验证；仅 PBR Metal/Rough 法线槽做兼容验证。
    V47 的问题是：严格失败后只读槽，没有重新写入；如果 propName 不在 getPropNames 里会直接失败。
    V48 改成：PBR法线允许先尝试写入，再检查槽里是否确实有贴图对象。
    """
    if set_material_slot_verified(mat, prop_name, tex):
        return True

    if not allow_tolerant_slot_verification(mat, prop_name, channel_label):
        return False

    try:
        setattr(mat, prop_name, tex)
        safe_enable_map_slot(mat, prop_name)
    except Exception:
        return False

    try:
        current = getattr(mat, prop_name)
    except Exception:
        return False

    return is_texmap_like_value(current)


def set_material_slot_verified(mat, prop_name, tex):
    """
    只有当前材质确实有这个属性，并且写入后能读回同一贴图，才返回 True。
    这样避免 V-Ray/Corona 中 setattr 似乎成功但 UI 实际没接上的误判。
    """
    if not mat or not prop_name or tex is None:
        return False

    props = material_prop_names_lower(mat)
    if props and safe_str(prop_name, "").lower() not in props:
        return False

    try:
        setattr(mat, prop_name, tex)
        safe_enable_map_slot(mat, prop_name)
    except Exception:
        return False

    return verify_material_slot_value(mat, prop_name, tex)


def safe_set_material_map_result(mat, prop_names, tex, channel_label=""):
    """
    更稳的材质贴图连接，并返回实际写入的材质槽名。
    返回：(是否成功, 实际槽位名)
    """
    if mat is None or tex is None:
        return False, ""

    # 1) 明确属性名：必须真实存在并能读回同一贴图，才算连接成功。
    for name in prop_names:
        if set_material_slot_verified_for_channel(mat, name, tex, channel_label):
            return True, name

    # 2) 对不同目标材质使用专用候选槽位
    cls = get_class_name(mat).lower()
    lower_label = safe_str(channel_label, "").lower()

    physical_fallbacks = {
        "basecolor": ["base_color_map", "mapM1"],
        "roughness": ["roughness_map", "mapM4"],
        "glossiness": ["roughness_map", "mapM4"],
        "metallic": ["metalness_map", "mapM5"],
        "metalness": ["metalness_map", "mapM5"],
        "normal": ["bump_map"],
        "normaldx": ["bump_map"],
        "normalgl": ["bump_map"],
        "height": ["bump_map", "displacement_map"],
        "displacement": ["displacement_map"],
        "opacity": ["cutout_map", "mapM12", "transparency_map", "mapM9"],
        "emissive": ["emit_color_map", "mapM17", "emission_map", "mapM16"],
        "specular": ["refl_color_map", "mapM3"],
    }

    vray_fallbacks = {
        "basecolor": ["texmap_diffuse", "diffuseMap", "diffuse_map", "map_diffuse"],
        "roughness": ["texmap_reflectionRoughness", "reflectionRoughnessMap", "refl_roughness_map", "roughness_map", "roughnessMap", "texmap_reflectionGlossiness", "reflectionGlossinessMap"],
        "glossiness": ["texmap_reflectionGlossiness", "reflectionGlossinessMap", "refl_glossiness_map"],
        "metallic": ["texmap_metalness", "metalnessMap", "metalness_map", "metallic_map"],
        "metalness": ["texmap_metalness", "metalnessMap", "metalness_map", "metallic_map"],
        "normal": ["texmap_bump", "bumpMap", "bump_map"],
        "normaldx": ["texmap_bump", "bumpMap", "bump_map"],
        "normalgl": ["texmap_bump", "bumpMap", "bump_map"],
        "height": ["texmap_bump", "bumpMap", "bump_map", "texmap_displacement", "displacementMap", "displacement_map"],
        "displacement": ["texmap_displacement", "displacementMap", "displacement_map"],
        "opacity": ["texmap_opacity", "opacityMap", "opacity_map"],
        "emissive": ["texmap_self_illumination", "selfIlluminationMap", "self_illum_map", "emissive_map"],
        "specular": ["texmap_reflection", "reflectionMap", "refl_map"],
        "ao": ["ao_map", "ambient_occlusion_map", "ambientOcclusionMap", "occlusion_map"],
    }

    corona_fallbacks = {
        "basecolor": ["baseTexmap", "baseColorTexmap", "diffuseTexmap", "texmapDiffuse", "base_color_map", "diffuse_map"],
        "roughness": ["baseRoughnessTexmap", "roughnessTexmap", "roughness_map", "base_roughness_map"],
        "glossiness": ["baseRoughnessTexmap", "roughnessTexmap", "glossinessTexmap", "glossiness_map"],
        "metallic": ["metalnessTexmap", "metallicTexmap", "metalness_map", "metallic_map"],
        "metalness": ["metalnessTexmap", "metallicTexmap", "metalness_map", "metallic_map"],
        "normal": ["bumpTexmap", "normalTexmap", "bump_map", "normal_map"],
        "normaldx": ["bumpTexmap", "normalTexmap", "bump_map", "normal_map"],
        "normalgl": ["bumpTexmap", "normalTexmap", "bump_map", "normal_map"],
        "height": ["bumpTexmap", "displacementTexmap", "height_map"],
        "displacement": ["displacementTexmap", "displacement_map"],
        "opacity": ["opacityTexmap", "alphaTexmap", "opacity_map"],
        "emissive": ["emissionTexmap", "selfIllumTexmap", "emissive_map"],
        "specular": ["reflectTexmap", "reflectionTexmap", "specular_map"],
        "ao": ["aoTexmap", "ambientOcclusionTexmap", "occlusionTexmap", "ao_map", "ambient_occlusion_map"],
    }

    for class_key, fallback_map in [("physical", physical_fallbacks), ("vray", vray_fallbacks), ("corona", corona_fallbacks)]:
        if class_key in cls:
            for key, names in fallback_map.items():
                if key in lower_label:
                    for name in names:
                        if set_material_slot_verified_for_channel(mat, name, tex, channel_label):
                            return True, name

    # 3) 扫描材质属性兜底，适配不同 Max / 渲染器版本
    try:
        props = [safe_str(p, "") for p in rt.getPropNames(mat)]
    except Exception:
        props = []

    keywords_by_channel = {
        "basecolor": ["base", "albedo", "diffuse", "color"],
        "roughness": ["rough"],
        "glossiness": ["gloss", "rough"],
        "metallic": ["metal"],
        "metalness": ["metal"],
        "normal": ["normal", "bump"],
        "normaldx": ["normal", "bump"],
        "normalgl": ["normal", "bump"],
        "ao": ["ao", "occlusion", "ambient"],
        "height": ["height", "bump", "displacement"],
        "displacement": ["displacement", "height"],
        "opacity": ["opacity", "alpha", "cutout", "transparency"],
        "emissive": ["emiss", "emit", "self"],
        "specular": ["spec", "reflection", "refl"],
    }

    keys = []
    for key, kws in keywords_by_channel.items():
        if key in lower_label:
            keys = kws
            break

    if keys:
        for p in props:
            low = p.lower()
            if ("map" not in low and "tex" not in low and "texture" not in low and "bump" not in low and "displace" not in low):
                continue
            if any(k in low for k in keys):
                if set_material_slot_verified_for_channel(mat, p, tex, channel_label):
                    return True, p

    return False, ""


def safe_set_material_map(mat, prop_names, tex, channel_label=""):
    ok, _prop = safe_set_material_map_result(mat, prop_names, tex, channel_label)
    return ok

def safe_copy_attr(src, dst, src_names, dst_names, transform=None):
    value = safe_get_attr_any(src, src_names, None)
    if value is None:
        return False
    try:
        if transform:
            value = transform(value)
    except Exception:
        pass
    return safe_set_attr_any(dst, dst_names, value)


def float_or_none(value):
    try:
        return float(value)
    except Exception:
        return None


def clamp01(value):
    try:
        return max(0.0, min(1.0, float(value)))
    except Exception:
        return value


def invert_glossiness(value):
    v = float_or_none(value)
    if v is None:
        return value
    return clamp01(1.0 - v)


def material_target_mode_key(target_mode):
    s = safe_str(target_mode, "PBR Material Metal/Rough").lower()
    if "vray" in s or "v-ray" in s:
        return "vray"
    if "corona" in s:
        return "corona"
    if "open" in s:
        return "openpbr"
    if "metal" in s or "rough" in s or "pbr" in s:
        return "pbr_metalrough"
    return "physical"


def material_target_mode_label(target_mode):
    key = material_target_mode_key(target_mode)
    if key == "vray":
        return "V-Ray Material"
    if key == "corona":
        return "Corona Material"
    if key == "openpbr":
        return "OpenPBR"
    if key == "pbr_metalrough":
        return "PBR Metal/Rough"
    return "Physical Material"


def target_material_class_candidates(target_mode):
    """
    不同 3ds Max / 渲染器版本里的类名不完全一致，所以按候选列表逐个尝试。
    找不到目标材质时会回退到 Physical / Standard，避免整次转换失败。
    """
    key = material_target_mode_key(target_mode)

    if key == "vray":
        return [
            "VRayMtl",
            "VRayMtl2",
            "VrayMtl",
            "V_Ray_Mtl",
            "PhysicalMaterial",
            "StandardMaterial",
            "standardMaterial",
        ]

    if key == "corona":
        return [
            "CoronaPhysicalMtl",
            "CoronaPhysicalMaterial",
            "CoronaMtl",
            "CoronaLegacyMtl",
            "PhysicalMaterial",
            "StandardMaterial",
            "standardMaterial",
        ]

    if key == "openpbr":
        return [
            "OpenPBRMaterial",
            "OpenPBR_Surface",
            "OpenPBRSurface",
            "openPBRMaterial",
            "Arnold_OpenPBR_Surface",
            "PhysicalMaterial",
            "StandardMaterial",
            "standardMaterial",
        ]

    if key == "pbr_metalrough":
        return [
            "PBRMetalRough",
            "PBR_Metal_Rough",
            "PBRMaterial",
            "PBRMetalRoughMaterial",
            "GLTFMaterial",
            "glTFMaterial",
            "PhysicalMaterial",
            "StandardMaterial",
            "standardMaterial",
        ]

    return [
        "PhysicalMaterial",
        "StandardMaterial",
        "standardMaterial",
    ]


def material_class_matches_target(class_name, target_mode):
    cls = safe_str(class_name, "").lower()
    key = material_target_mode_key(target_mode)

    if key == "vray":
        return "vray" in cls or "v_ray" in cls or "v-ray" in cls

    if key == "corona":
        return "corona" in cls

    if key == "openpbr":
        return "openpbr" in cls or "open_pbr" in cls

    if key == "pbr_metalrough":
        base = (
            "pbr" in cls or
            "gltf" in cls or
            "metalrough" in cls or
            "metal_rough" in cls
        )

    return "physical" in cls


def is_material_target_like(mat, target_mode):
    if not is_valid_material(mat):
        return False
    return material_class_matches_target(get_class_name(mat), target_mode)


def is_pbr_like_material(mat):
    cls = get_class_name(mat).lower()
    if not is_valid_material(mat):
        return False
    keywords = ["physicalmaterial", "physical_material", "pbr", "openpbr", "open_pbr", "gltf", "standard_surface"]
    return any(k in cls for k in keywords)


def is_complex_material_for_pbr(mat):
    """复杂/包装/混合材质默认不自动转，避免误伤。Multi/Sub 单独处理。"""
    cls = get_class_name(mat).lower()
    complex_keys = [
        "blend", "composite", "shellac", "topbottom", "top_bottom", "double_sided",
        "2_sided", "2sided", "mix", "layer", "layered", "override", "wrapper",
        "switch", "raytrace", "matte", "shadow", "carpaint", "toon", "hair", "skin",
        "sss", "subsurface"
    ]
    if is_multi_material(mat):
        return False
    return any(k in cls for k in complex_keys)


def is_probably_texmap(value):
    try:
        if value is None:
            return False
        cls = get_class_name(value).lower()
        s = safe_str(value, "").lower()
        keys = [
            "map", "bitmap", "texture", "tex", "noise", "falloff", "colorcorrection", "osl",
            "vraybitmap", "vrayhdri", "coronabitmap", "coronamap", "redshift", "rsbitmap",
            "arnold", "image", "octane", "fstorm", "hdr", "raster"
        ]
        return any(k in cls for k in keys) or any(k in s for k in keys)
    except Exception:
        return False


def texmap_external_file_path(tex):
    """
    判断一个贴图节点是否指向外部文件。
    这里不要求路径必须存在，因为有些项目贴图路径可能是相对路径或网络路径。
    """
    if tex is None:
        return ""

    # 常见 Bitmap / 渲染器 Bitmap / HDRI 节点的文件路径属性。
    # 不同版本的 V-Ray / Corona / Arnold / Redshift / Octane 类名和属性名会有差异，
    # 所以这里只做"宽松探测"，找到了就保留外部贴图引用。
    direct_props = [
        "filename", "fileName", "Filename", "FileName",
        "bitmapName", "bitmapname", "mapName", "sourceFileName",
        "file", "File", "filepath", "filePath", "FilePath", "file_path", "File_Path",
        "imageName", "imagename", "image", "Image",
        "HDRIMapName", "hdriMapName", "hdrimapname", "hdrFile", "hdrfile",
        "textureFile", "textureFilename", "texturefilename",
        "assetFilename", "assetFileName", "source", "Source",
        "url", "URL", "path", "Path"
    ]

    for prop in direct_props:
        try:
            v = getattr(tex, prop)
            s = safe_str(v, "")
            if s:
                return s
        except Exception:
            pass

    try:
        bmp = getattr(tex, "bitmap")
        for prop in ["filename", "fileName", "Filename", "FileName"]:
            try:
                s = safe_str(getattr(bmp, prop), "")
                if s:
                    return s
            except Exception:
                pass
    except Exception:
        pass

    return ""


def find_external_texmap(tex, depth=0, visited=None):
    """
    在程序贴图、ColorCorrection、Composite 等节点里尽量找到底层外部 Bitmap。
    找到后返回外部贴图节点；找不到则返回 None。
    这不是烘焙，只是"简化引用"：保留外部贴图，丢弃程序包装逻辑。
    """
    if visited is None:
        visited = set()

    if tex is None or depth > 4:
        return None

    try:
        h = get_anim_handle(tex)
        if h in visited:
            return None
        visited.add(h)
    except Exception:
        pass

    if texmap_external_file_path(tex):
        return tex

    # 常见包装贴图/程序贴图里的输入槽
    candidate_props = [
        "map", "texmap", "texture", "input", "input_map", "source", "source_map",
        "bitmap", "base_map", "color_map", "diffuse_map", "map1", "map2",
        "texmap1", "texmap2", "front", "back", "baseColorMap", "base_color_map",
        "child", "submap", "subMap", "subTexmap", "tex", "file_map", "image_map",
        "colorCorrectionMap", "correction_map", "main_map", "layer1", "layer2",
        "red", "green", "blue", "alpha",
        # V-Ray / Corona 额外槽位
        "tex_map", "inputTexmap", "color_input", "mapSlot", "texmapInput",
        "base", "coat", "sheen", "emission_map", "anisotropy_map", "IOR_map",
        "texmap_inside", "texmap_outside", "texmap_opacity", "texmap_bump",
        "texmap_reflect", "texmap_refract", "texmap_diffuse",
    ]

    for prop in candidate_props:
        try:
            child = getattr(tex, prop)
        except Exception:
            continue

        if child is None:
            continue

        # 有些属性是数组
        try:
            if isinstance(child, (list, tuple)):
                for sub in child:
                    found = find_external_texmap(sub, depth + 1, visited)
                    if found:
                        return found
                continue
        except Exception:
            pass

        if is_probably_texmap(child):
            found = find_external_texmap(child, depth + 1, visited)
            if found:
                return found

    # 兜底：尝试 MaxScript 属性列表。大型材质上可能稍慢，所以深度限制很小。
    if depth <= 1:
        try:
            props = list(rt.getPropNames(tex))
        except Exception:
            props = []

        for prop in props[:80]:
            try:
                name = safe_str(prop, "")
                child = getattr(tex, name)
            except Exception:
                continue

            if is_probably_texmap(child):
                found = find_external_texmap(child, depth + 1, visited)
                if found:
                    return found

    return None


def simplify_texmap_for_standardization(tex):
    """
    返回：new_texmap, note
    - 外部 Bitmap：原样保留
    - 程序贴图包着外部 Bitmap：简化为内部外部 Bitmap
    - 纯程序贴图：不复制，使用数值/中性默认值
    """
    if tex is None:
        return None, ""

    if texmap_external_file_path(tex):
        return tex, "保留外部贴图"

    found = find_external_texmap(tex)
    if found:
        return found, "程序/包装贴图已简化为底层外部贴图"

    if is_probably_texmap(tex):
        return None, "纯程序贴图无法安全转换，已简化为数值/默认值"

    return None, ""


def copy_texmap_with_simplify(src, dst, src_names, dst_names, notes, label, preserve_external_maps=True):
    tex = safe_get_attr_any(src, src_names, None)
    if tex is None:
        return False

    if preserve_external_maps:
        new_tex, note = simplify_texmap_for_standardization(tex)
        if new_tex is None:
            # 简化失败 → 回退到原始节点（保留 ColorCorrection / 程序贴图等，不丢弃贴图槽）
            if safe_set_attr_any(dst, dst_names, tex):
                notes.append("{}：保留原始节点（{}）".format(label, note or "简化失败"))
                return True
            if note:
                notes.append("{}：{}（迁移失败）".format(label, note))
            return False

        if safe_set_attr_any(dst, dst_names, new_tex):
            notes.append("{}：{}".format(label, note or "已迁移贴图"))
            return True

        return False

    if safe_set_attr_any(dst, dst_names, tex):
        notes.append("{}：已迁移贴图".format(label))
        return True

    return False


def set_standardized_material_defaults(dst, target_mode):
    key = material_target_mode_key(target_mode)

    if key in ("pbr_metalrough", "openpbr", "physical"):
        safe_set_attr_any(dst, ["metalness", "metallic", "metalness_value", "metallic_value"], safe_get_attr_any(dst, ["metalness", "metallic", "metalness_value", "metallic_value"], 0.0))
        safe_set_attr_any(dst, ["roughness", "roughness_value"], safe_get_attr_any(dst, ["roughness", "roughness_value"], 0.45))


def pbr_status_for_entry(entry, skip_already=True, try_complex=False, convert_multi_children=True, target_mode="PBR Material Metal/Rough"):
    mat = entry.get("mat")
    role = entry.get("role", "MAT")
    target_label = material_target_mode_label(target_mode)

    if not is_valid_material(mat):
        return dict(ok=False, judge="无效", action="跳过", note="无效材质")

    if role == "MSO" or is_multi_material(mat):
        subs = get_multi_material_subs(mat)
        if not convert_multi_children:
            return dict(ok=False, judge="Multi/Sub", action="跳过", note="未启用子材质转换")
        if not subs:
            return dict(ok=False, judge="Multi/Sub", action="跳过", note="没有有效子材质")
        return dict(ok=True, judge="Multi/Sub", action="保留母材质，转换子材质为 {}".format(target_label), note="母材质结构保留")

    if skip_already and is_material_target_like(mat, target_mode):
        return dict(ok=False, judge="已是目标材质", action="跳过", note="已经是 {}".format(target_label))

    if is_complex_material_for_pbr(mat) and not try_complex:
        return dict(ok=False, judge="复杂材质", action="跳过", note="建议人工检查或开启尝试复杂材质")

    family = get_material_family(mat)
    if is_complex_material_for_pbr(mat) and try_complex:
        return dict(ok=True, judge=family, action="简化并转为 {}".format(target_label), note="复杂材质会简化，外部贴图尽量保留")

    return dict(ok=True, judge=family, action="转为 {}".format(target_label), note="简单材质自动标准化")


def create_standardized_material_from_source(src_mat, prefix="MAT_STD", target_mode="PBR Material Metal/Rough", preserve_external_maps=True):
    """
    创建目标标准材质，并尽量迁移颜色和外部贴图。
    复杂程序贴图不强行烘焙，优先保留其底层外部 Bitmap。
    """
    notes = []
    dst = None
    used_class = ""

    for cls_name in target_material_class_candidates(target_mode):
        dst = try_create_material_by_class(cls_name)
        if is_valid_material(dst):
            used_class = cls_name
            break

    if not is_valid_material(dst):
        return None, ["无法创建目标材质"]

    target_label = material_target_mode_label(target_mode)

    try:
        dst.name = "{}_{}".format(clean_name_part(prefix, "MAT_STD"), get_material_name(src_mat))
    except Exception:
        pass

    if not material_class_matches_target(used_class, target_mode):
        notes.append("未找到 {} 类，已回退为 {}".format(target_label, used_class))

    # ---------- 基础色 ----------
    if not safe_copy_attr(
        src_mat,
        dst,
        ["base_color", "baseColor", "diffuse", "diffuse_color", "color", "albedo"],
        ["base_color", "baseColor", "base_color_value", "diffuse", "diffuse_color", "color", "albedo"]
    ):
        try:
            safe_set_color(dst, rt.color(180, 180, 180))
            notes.append("未找到基础色，使用中性灰")
        except Exception:
            pass

    copy_texmap_with_simplify(
        src_mat,
        dst,
        ["base_color_map", "baseColorMap", "diffuse_map", "diffuseMap", "texmap_diffuse", "albedo_map", "color_map", "texmap_color"],
        ["base_color_map", "baseColorMap", "diffuse_map", "diffuseMap", "texmap_diffuse", "albedo_map", "color_map", "baseColor_map"],
        notes,
        "BaseColor/Diffuse",
        preserve_external_maps=preserve_external_maps
    )

    # ---------- Roughness / Glossiness ----------
    if safe_copy_attr(src_mat, dst, ["roughness", "roughness_value"], ["roughness", "roughness_value"]):
        pass
    elif safe_copy_attr(src_mat, dst, ["reflection_glossiness", "refl_glossiness", "glossiness"], ["roughness", "roughness_value"], transform=invert_glossiness):
        notes.append("Glossiness 数值已反相为 Roughness")

    if copy_texmap_with_simplify(
        src_mat,
        dst,
        ["roughness_map", "roughnessMap", "texmap_roughness"],
        ["roughness_map", "roughnessMap", "texmap_roughness"],
        notes,
        "Roughness",
        preserve_external_maps=preserve_external_maps
    ):
        pass
    elif copy_texmap_with_simplify(
        src_mat,
        dst,
        ["reflection_glossiness_map", "refl_glossiness_map", "glossiness_map", "glossinessMap", "texmap_reflectionGlossiness"],
        ["roughness_map", "roughnessMap", "texmap_roughness"],
        notes,
        "Glossiness",
        preserve_external_maps=preserve_external_maps
    ):
        notes.append("Glossiness 贴图已接入 Roughness，可能需要人工反相检查")

    # ---------- Metalness ----------
    safe_copy_attr(
        src_mat,
        dst,
        ["metalness", "metallic", "metalness_value", "metallic_value"],
        ["metalness", "metallic", "metalness_value", "metallic_value"]
    )
    copy_texmap_with_simplify(
        src_mat,
        dst,
        ["metalness_map", "metallic_map", "metalnessMap", "metallicMap", "texmap_metalness"],
        ["metalness_map", "metallic_map", "metalnessMap", "metallicMap", "texmap_metalness"],
        notes,
        "Metalness/Metallic",
        preserve_external_maps=preserve_external_maps
    )

    # ---------- Normal / Bump ----------
    copy_texmap_with_simplify(
        src_mat,
        dst,
        ["normal_map", "normalMap", "normal_texture", "bump_map", "bumpMap", "texmap_bump", "bump_texmap"],
        ["normal_map", "normalMap", "normal_texture", "bump_map", "bumpMap", "texmap_bump", "bump_texmap"],
        notes,
        "Normal/Bump",
        preserve_external_maps=preserve_external_maps
    )

    # ---------- Opacity / Alpha ----------
    safe_copy_attr(
        src_mat,
        dst,
        ["opacity", "opacity_value", "transparency"],
        ["opacity", "opacity_value", "transparency"]
    )
    copy_texmap_with_simplify(
        src_mat,
        dst,
        ["opacity_map", "opacityMap", "transparency_map", "alpha_map", "cutout_map", "texmap_opacity", "texmap_cutout"],
        ["opacity_map", "opacityMap", "transparency_map", "alpha_map", "cutout_map", "texmap_opacity", "texmap_cutout"],
        notes,
        "Opacity/Alpha",
        preserve_external_maps=preserve_external_maps
    )

    set_standardized_material_defaults(dst, target_mode)
    return dst, notes


def create_physical_pbr_material_from_source(src_mat, prefix="PBR", target_mode="PBR Material Metal/Rough", preserve_external_maps=True):
    """兼容旧函数名。"""
    return create_standardized_material_from_source(
        src_mat,
        prefix=prefix,
        target_mode=target_mode,
        preserve_external_maps=preserve_external_maps
    )


def set_multimaterial_slot_material(parent_mat, slot, new_mat):
    try:
        arr = parent_mat.materialList
    except Exception:
        return False
    try:
        arr[slot - 1] = new_mat
        return True
    except Exception:
        pass
    try:
        arr[slot] = new_mat
        return True
    except Exception:
        pass
    return False


def collect_exact_material_references(old_mat):
    refs = []
    if not is_valid_material(old_mat):
        return refs
    old_h = get_anim_handle(old_mat)
    seen = set()
    for obj in get_scene_geometry():
        try:
            mat = obj.material
            if is_valid_material(mat) and get_anim_handle(mat) == old_h:
                key = ("node", get_anim_handle(obj))
                if key not in seen:
                    seen.add(key)
                    refs.append({"kind": "node", "node": obj, "old": old_mat})
        except Exception:
            pass
    for entry in collect_scene_material_entries():
        mat = entry.get("mat")
        if not is_valid_material(mat) or not is_multi_material(mat):
            continue
        parent = mat
        for sub in get_multi_material_subs(parent):
            sub_mat = sub.get("mat")
            slot = sub.get("slot", 0)
            if is_valid_material(sub_mat) and get_anim_handle(sub_mat) == old_h:
                key = ("slot", get_anim_handle(parent), slot)
                if key not in seen:
                    seen.add(key)
                    refs.append({"kind": "slot", "parent": parent, "slot": slot, "old": old_mat})
    return refs


def apply_material_reference(ref, mat):
    try:
        if ref.get("kind") == "node":
            node = ref.get("node")
            if is_valid_node(node):
                node.material = mat
                return True
        elif ref.get("kind") == "slot":
            parent = ref.get("parent")
            slot = ref.get("slot", 0)
            if is_valid_material(parent) and slot:
                return set_multimaterial_slot_material(parent, slot, mat)
    except Exception:
        pass
    return False


def make_pbr_conversion_plan(entries, skip_already=True, try_complex=False, convert_multi_children=True, target_mode="PBR Material Metal/Rough"):
    plan = []
    used_context = set()
    for entry in entries:
        key = material_context_key(entry)
        if key in used_context:
            continue
        used_context.add(key)
        mat = entry.get("mat")
        info = pbr_status_for_entry(entry, skip_already=skip_already, try_complex=try_complex, convert_multi_children=convert_multi_children, target_mode=target_mode)
        plan.append({"entry": entry, "mat": mat, "role": entry.get("role", "MAT"), "old": get_material_name(mat), "type": get_class_name(mat), "judge": info.get("judge", ""), "action": info.get("action", ""), "ok": info.get("ok", False), "note": info.get("note", ""), "target_mode": target_mode})
    return plan


# ============================================================
# UE 纹理流送整理（谨慎使用）
# ============================================================

IMAGE_EXTS = set([".png", ".jpg", ".jpeg", ".tga", ".tif", ".tiff", ".bmp", ".exr", ".hdr", ".psd"])


def is_power_of_two_int(n):
    try:
        n = int(n)
        return n > 0 and (n & (n - 1)) == 0
    except Exception:
        return False


def nearest_power_of_two_down(n, minimum=4):
    try:
        n = max(int(n), minimum)
        return max(minimum, 2 ** int(math.floor(math.log(n, 2))))
    except Exception:
        return minimum


def safe_abs_texture_path(path):
    s = safe_str(path, "")
    if not s:
        return ""
    try:
        # 3ds Max pathConfig 能处理项目相对路径、贴图路径等场景。
        v = rt.pathConfig.convertPathToAbsolute(s)
        if safe_str(v, ""):
            s = safe_str(v, s)
    except Exception:
        pass
    try:
        return os.path.normpath(s)
    except Exception:
        return s


def safe_texture_exists(path):
    try:
        return os.path.exists(path)
    except Exception:
        return False


def safe_image_size(path):
    """
    获取贴图尺寸。
    优先用 3ds Max openBitmap，避免依赖 Pillow；失败后再尝试 Pillow。
    """
    path = safe_abs_texture_path(path)
    if not path or not safe_texture_exists(path):
        return 0, 0, "文件不存在"

    # 先试 Max bitmap
    try:
        bm = rt.openBitmap(path)
        try:
            w = int(bm.width)
            h = int(bm.height)
            try: rt.close(bm)
            except Exception: pass
            if w > 0 and h > 0:
                return w, h, "MaxBitmap"
        except Exception:
            try: rt.close(bm)
            except Exception: pass
    except Exception:
        pass

    # 再试 Pillow
    try:
        from PIL import Image
        with Image.open(path) as im:
            return int(im.size[0]), int(im.size[1]), "Pillow"
    except Exception:
        pass

    return 0, 0, "无法读取尺寸"


def texture_channel_from_name_or_prop(path, prop_name=""):
    base = clean_name_part(os.path.splitext(os.path.basename(safe_str(path, "")))[0], "Tex").lower()
    p = safe_str(prop_name, "").lower()
    s = base + "_" + p
    if any(k in s for k in ["normal", "_n", "bump"]):
        return "Normal"
    if any(k in s for k in ["rough", "roughness", "_r"]):
        return "Roughness"
    if any(k in s for k in ["gloss", "glossiness"]):
        return "Glossiness"
    if any(k in s for k in ["metal", "metallic", "metalness", "_m"]):
        return "Metallic"
    if any(k in s for k in ["ao", "ambient", "occlusion"]):
        return "AO"
    if any(k in s for k in ["opacity", "alpha", "cutout", "mask"]):
        return "Opacity"
    if any(k in s for k in ["emissive", "emission", "selfillum", "self_illum"]):
        return "Emissive"
    if any(k in s for k in ["diffuse", "albedo", "basecolor", "base_color", "_d", "_bc", "color"]):
        return "BaseColor"
    return "Unknown"


def ue_texture_suffix(channel):
    return {
        "BaseColor": "BC",
        "Normal": "N",
        "Roughness": "R",
        "Glossiness": "GLOSS",
        "Metallic": "M",
        "AO": "AO",
        "Opacity": "OP",
        "Emissive": "E",
    }.get(channel, "TEX")


def ue_safe_texture_name(path, channel="Unknown", prefix="T", rename_opts=None):
    """
    rename_opts: dict with keys:
      prefix (str), sep (str), include_mat (bool), include_obj (bool),
      mat_name (str), obj_name (str)
    """
    if rename_opts:
        prefix = rename_opts.get("prefix", prefix) or prefix
    sep = (rename_opts.get("sep", "_") or "_") if rename_opts else "_"

    base = clean_name_part(os.path.splitext(os.path.basename(safe_str(path, "Texture")))[0], "Texture")
    suffix = ue_texture_suffix(channel)
    # 避免重复后缀
    low = base.lower()
    known = ["_bc", "_n", "_r", "_m", "_ao", "_op", "_e", "_tex", "_gloss"]
    if not any(low.endswith(k) for k in known):
        base = "{}{}{}".format(base, sep, suffix)

    # 可选：插入材质名 / 模型名
    if rename_opts:
        extra_parts = []
        if rename_opts.get("include_mat") and rename_opts.get("mat_name"):
            extra_parts.append(clean_name_part(rename_opts["mat_name"], ""))
        if rename_opts.get("include_obj") and rename_opts.get("obj_name"):
            extra_parts.append(clean_name_part(rename_opts["obj_name"], ""))
        if extra_parts:
            base = "{}{}{}".format(sep.join(extra_parts), sep, base)

    if not base.lower().startswith(prefix.lower() + sep):
        base = "{}{}{}".format(prefix, sep, base)
    return clean_name_part(base, "T_Texture")


def texture_streaming_recommendation(w, h, max_size, force_power2=False):
    issues = []
    actions = []
    if w <= 0 or h <= 0:
        return ["无法读取尺寸"], ["仅复制/跳过"]
    if w > max_size or h > max_size:
        issues.append("超过最大尺寸")
        actions.append("限制最大尺寸")
    if not (is_power_of_two_int(w) and is_power_of_two_int(h)):
        issues.append("非2幂尺寸")
        if force_power2:
            actions.append("强制2幂")
        else:
            actions.append("建议检查Mip/流送")
    if not issues:
        issues.append("流送友好")
        actions.append("可直接复制")
    return issues, actions


def compute_streaming_target_size(w, h, max_size=4096, force_power2=False, keep_aspect=True):
    """
    默认：保持比例，只限制最大边。
    force_power2=True：宽高分别向下取 2 幂，谨慎使用，可能改变比例。
    """
    try:
        w = int(w); h = int(h); max_size = int(max_size)
    except Exception:
        return w, h

    if w <= 0 or h <= 0:
        return w, h

    if force_power2:
        tw = min(nearest_power_of_two_down(w), max_size)
        th = min(nearest_power_of_two_down(h), max_size)
        return max(4, tw), max(4, th)

    # 推荐默认：保持比例，只限制最大边
    if max(w, h) <= max_size:
        return w, h

    scale = float(max_size) / float(max(w, h))
    tw = max(4, int(round(w * scale)))
    th = max(4, int(round(h * scale)))
    return tw, th


def update_texmap_path(tex, new_path):
    props = [
        "filename", "fileName", "Filename", "FileName",
        "bitmapName", "bitmapname", "mapName", "sourceFileName",
        "file", "File", "filepath", "filePath", "FilePath", "file_path",
        "imageName", "imagename", "HDRIMapName", "hdriMapName", "hdrFile",
        "textureFile", "textureFilename", "assetFilename", "assetFileName",
        "path", "Path"
    ]
    for prop in props:
        try:
            old = safe_str(getattr(tex, prop), "")
            if old:
                setattr(tex, prop, new_path)
                return True
        except Exception:
            pass
    try:
        bmp = getattr(tex, "bitmap")
        for prop in ["filename", "fileName", "Filename", "FileName"]:
            try:
                old = safe_str(getattr(bmp, prop), "")
                if old:
                    setattr(bmp, prop, new_path)
                    return True
            except Exception:
                pass
    except Exception:
        pass
    return False




def normalize_texture_path_string(value):
    """
    把材质/贴图属性里的字符串整理成可能的外部贴图路径。
    既支持存在文件，也支持丢失贴图路径；只要后缀像图片就记录。
    """
    s = safe_str(value, "").strip()
    if not s:
        return ""
    # 去掉常见包裹符号/URL前缀
    s = s.strip(" \t\r\n\"'")
    if s.lower().startswith("file://"):
        s = s[7:]
    if len(s) < 4:
        return ""
    try:
        s = os.path.expandvars(s)
    except Exception:
        pass
    ext = os.path.splitext(s.split("?")[0].split("#")[0])[1].lower()
    if ext not in IMAGE_EXTS:
        return ""
    return s


def is_external_texture_path_string(value):
    return bool(normalize_texture_path_string(value))


def add_texture_entry_from_path(path, owner_name="", prop_name="", entries=None, owner_ref=None, source_obj=None):
    if entries is None:
        entries = {}
    raw_path = normalize_texture_path_string(path) or safe_str(path, "")
    if not raw_path:
        return entries

    abs_path = safe_abs_texture_path(raw_path)
    if not abs_path:
        return entries

    key = abs_path.lower()
    ch = texture_channel_from_name_or_prop(abs_path, prop_name)
    if key not in entries:
        w, h, reader = safe_image_size(abs_path)
        entries[key] = dict(
            path=abs_path,
            original_path=raw_path,
            file=os.path.basename(abs_path),
            ext=os.path.splitext(abs_path)[1].lower(),
            channel=ch,
            owners=[],
            owner_materials=[],
            owner_nodes=[],
            texmaps=[],
            path_sources=[],
            width=w,
            height=h,
            reader=reader,
            exists=safe_texture_exists(abs_path),
            status="等待",
            output=""
        )

    if owner_name:
        entries[key]["owners"].append(owner_name)
    if is_valid_material(owner_ref):
        entries[key]["owner_materials"].append(owner_ref)
    if source_obj is not None:
        try:
            entries[key]["texmaps"].append(source_obj)
        except Exception:
            pass
        try:
            entries[key]["path_sources"].append((source_obj, prop_name))
        except Exception:
            pass
    return entries


def should_skip_deep_scan_child(child):
    """避免深扫时误钻进场景节点/大对象导致卡顿。"""
    if child is None:
        return True
    try:
        if isinstance(child, (int, float, bool)):
            return True
    except Exception:
        pass
    try:
        if isinstance(child, str):
            return True
    except Exception:
        pass
    try:
        if is_valid_node(child):
            return True
    except Exception:
        pass
    return False

def collect_texture_nodes_from_value(value, owner_name="", prop_name="", entries=None, visited=None, depth=0, owner_ref=None):
    """
    深度扫描外部贴图。

    V53 会尽量扫描：
    - Multi/Sub、Blend、Composite、Layered、Wrapper 等材质里的深层子材质；
    - Mix、ColorCorrection、Falloff、OSL、程序贴图里的子贴图；
    - 渲染器 Bitmap / OSL / 程序节点里的字符串文件路径属性；
    - 文件不存在的丢失贴图路径。
    """
    if entries is None:
        entries = {}
    if visited is None:
        visited = set()

    if value is None or depth > 14:
        return entries

    # 字符串路径直接记录，不递归。
    if is_external_texture_path_string(value):
        add_texture_entry_from_path(value, owner_name, prop_name, entries, owner_ref=owner_ref, source_obj=None)
        return entries

    try:
        h = get_anim_handle(value)
        if h in visited:
            return entries
        visited.add(h)
    except Exception:
        pass

    # 当前对象本身可能是 Bitmap / 渲染器 Bitmap，也可能是某些带路径的程序节点。
    if is_probably_texmap(value) or is_valid_material(value):
        try:
            path = texmap_external_file_path(value)
        except Exception:
            path = ""
        if path:
            add_texture_entry_from_path(path, owner_name, prop_name, entries, owner_ref=owner_ref, source_obj=value)

    try:
        props = list(rt.getPropNames(value))
    except Exception:
        props = []

    for prop in props[:500]:
        name = safe_str(prop, "")
        if not name:
            continue
        try:
            child = getattr(value, name)
        except Exception:
            continue
        if child is None:
            continue

        # 字符串路径属性，例如 filename / textureFile / OSL 参数路径。
        if is_external_texture_path_string(child):
            add_texture_entry_from_path(child, owner_name, name, entries, owner_ref=owner_ref, source_obj=value)
            continue

        if should_skip_deep_scan_child(child):
            continue

        can_recurse = False
        if is_valid_material(child) or is_probably_texmap(child):
            can_recurse = True
        else:
            try:
                _ = rt.getPropNames(child)
                can_recurse = True
            except Exception:
                can_recurse = False

        if can_recurse:
            collect_texture_nodes_from_value(child, owner_name, name, entries, visited, depth + 1, owner_ref=owner_ref)
            continue

        # 数组/Tab 属性兜底：materialList、mapList、OSL 参数数组等。
        try:
            seq = list(child)
        except Exception:
            seq = []
        for sub in seq[:200]:
            if is_external_texture_path_string(sub):
                add_texture_entry_from_path(sub, owner_name, name, entries, owner_ref=owner_ref, source_obj=value)
                continue
            if should_skip_deep_scan_child(sub):
                continue
            collect_texture_nodes_from_value(sub, owner_name, name, entries, visited, depth + 1, owner_ref=owner_ref)

    return entries


def enrich_texture_entries_with_scene_objects(entries):
    """
    给贴图条目补充"使用这张贴图的场景物体"。
    逻辑：贴图 -> 所属材质 -> 场景中使用该材质/子材质的几何体。
    """
    try:
        usage = build_material_usage_map()
    except Exception:
        usage = {}

    for e in entries:
        nodes = list(e.get("owner_nodes", []))
        seen = set()
        for obj in nodes:
            try:
                seen.add(get_anim_handle(obj))
            except Exception:
                pass

        for mat in e.get("owner_materials", []):
            if not is_valid_material(mat):
                continue
            try:
                h = get_anim_handle(mat)
            except Exception:
                h = None
            for obj in usage.get(h, []):
                try:
                    oh = get_anim_handle(obj)
                    if oh in seen:
                        continue
                    seen.add(oh)
                    nodes.append(obj)
                except Exception:
                    pass

        e["owner_nodes"] = unique_by_handle([o for o in nodes if is_valid_node(o)])
        e["owner_node_names"] = [safe_str(getattr(o, "name", ""), "") for o in e.get("owner_nodes", []) if is_valid_node(o)]
    return entries


def texture_owner_nodes_text(entry, max_names=3):
    nodes = entry.get("owner_nodes", [])
    if not nodes:
        # 退回显示材质/拥有者名，至少让用户知道来自哪个材质
        owners = []
        seen = set()
        for n in entry.get("owners", []):
            n = safe_str(n, "")
            if n and n.lower() not in seen:
                seen.add(n.lower())
                owners.append(n)
        if owners:
            text = "材质：" + "，".join(owners[:max_names])
            if len(owners) > max_names:
                text += " 等{}个".format(len(owners))
            return text
        return "-"

    names = []
    for obj in nodes:
        if is_valid_node(obj):
            names.append(safe_str(getattr(obj, "name", ""), "Object"))
    names = [n for n in names if n]
    if not names:
        return "{}个物体".format(len(nodes))

    s = "，".join(names[:max_names])
    if len(names) > max_names:
        s += " 等{}个".format(len(names))
    return "{}个：{}".format(len(nodes), s)


def collect_all_bitmaps_from_scene_instances(entries=None):
    """
    补充扫描：通过 getClassInstances 枚举场景内所有贴图实例，
    捕获被 ColorCorrection / Mix / Composite / Wrapper 等节点遮蔽、
    属性遍历无法触达的外部贴图文件引用。
    """
    if entries is None:
        entries = {}
    visited = set()

    def _add(tex, fallback_owner=""):
        try:
            h = get_anim_handle(tex)
            if h in visited:
                return
            visited.add(h)
            path = texmap_external_file_path(tex)
            if path:
                add_texture_entry_from_path(
                    path,
                    fallback_owner or get_class_name(tex),
                    "",
                    entries,
                    source_obj=tex
                )
        except Exception:
            pass

    # 尝试用 texturemap 超类一次覆盖所有贴图（Max 2020+）
    scanned = False
    try:
        sup = getattr(rt, "texturemap", None)
        if sup is not None:
            all_texmaps = rt.getClassInstances(sup)
            if all_texmaps is not None:
                for tex in all_texmaps:
                    _add(tex)
                scanned = True
    except Exception:
        pass

    if not scanned:
        # 超类不可用时逐类枚举（兜底）
        for cls_name in [
            "Bitmaptexture", "BitmapTexture", "bitmapTexture",
            "VRayBitmap", "VRayHDRI",
            "CoronaBitmap",
            "AiBitmap", "AiImage",
            "RSTex", "OctaneBitmapTexture",
        ]:
            try:
                cls = getattr(rt, cls_name)
                for tex in (rt.getClassInstances(cls) or []):
                    _add(tex, cls_name)
            except Exception:
                pass

    return entries


def collect_scene_texture_entries():
    entries = {}
    for mat_entry in collect_scene_material_entries():
        mat = mat_entry.get("mat")
        if is_valid_material(mat):
            collect_texture_nodes_from_value(mat, get_material_name(mat), "", entries, owner_ref=mat)
    # 兜底：用 getClassInstances 捕获 ColorCorrection 等包装节点内的外部贴图
    collect_all_bitmaps_from_scene_instances(entries)
    return enrich_texture_entries_with_scene_objects(list(entries.values()))


def collect_material_list_texture_entries(material_entries):
    entries = {}
    for mat_entry in material_entries:
        mat = mat_entry.get("mat")
        if is_valid_material(mat):
            collect_texture_nodes_from_value(mat, get_material_name(mat), "", entries, owner_ref=mat)
    collect_all_bitmaps_from_scene_instances(entries)
    return enrich_texture_entries_with_scene_objects(list(entries.values()))



def texture_output_base_path(entry, output_dir, ue_naming=True, rename_opts=None):
    src = safe_abs_texture_path(entry.get("path", ""))
    ext = os.path.splitext(src)[1].lower()
    channel = entry.get("channel", "Unknown")
    if ue_naming:
        opts = dict(rename_opts) if rename_opts else {}
        if not opts.get("mat_name"):
            opts["mat_name"] = entry.get("mat_name", "")
        if not opts.get("obj_name"):
            opts["obj_name"] = entry.get("obj_name", "")
        out_base = ue_safe_texture_name(src, channel, rename_opts=opts)
    else:
        out_base = clean_name_part(os.path.splitext(os.path.basename(src))[0], "Texture")
    return os.path.join(output_dir, out_base + ext)


def find_existing_qualified_texture_output(entry, output_dir, max_size=4096, require_power2=True, ue_naming=True):
    """
    如果输出目录里已经有同名或同名前缀的合格输出贴图，则直接复用，避免重复处理。
    """
    try:
        base = texture_output_base_path(entry, output_dir, ue_naming=ue_naming)
        candidates = []
        if os.path.exists(base):
            candidates.append(base)

        root, ext = os.path.splitext(base)
        folder = os.path.dirname(base)
        base_name = os.path.basename(root)

        if os.path.isdir(folder):
            for fn in os.listdir(folder):
                low = fn.lower()
                if not low.endswith(ext.lower()):
                    continue
                if fn.startswith(base_name + "_"):
                    candidates.append(os.path.join(folder, fn))

        for path in candidates:
            temp = dict(entry)
            temp["path"] = path
            refresh_texture_entry_info(temp)
            ok, _issues = texture_entry_passes_streaming(temp, max_size=max_size, require_power2=require_power2)
            if ok:
                return path
    except Exception:
        pass

    return ""


def copy_texture_only_for_ue(entry, output_dir, ue_naming=True, no_overwrite=True, rename_opts=None):
    """
    只复制，不做缩放/裁剪。用于"只复制，不处理"引擎。
    """
    src = safe_abs_texture_path(entry.get("path", ""))
    if not src or not os.path.exists(src):
        return False, "", "源贴图不存在"

    os.makedirs(output_dir, exist_ok=True)
    out_path = texture_output_base_path(entry, output_dir, ue_naming=ue_naming, rename_opts=rename_opts)

    if no_overwrite:
        root, ext = os.path.splitext(out_path)
        i = 1
        while os.path.exists(out_path):
            out_path = "{}_{:03d}{}".format(root, i, ext)
            i += 1

    shutil.copy2(src, out_path)
    return True, out_path, "只复制，不处理"

def try_process_texture_for_ue(entry, output_dir, max_size=4096, force_power2=False, keep_aspect=True, ue_naming=True, no_overwrite=True, rename_opts=None):
    """
    默认不覆盖源文件：复制到目标目录。
    如果 Pillow 可用，则在需要时调整尺寸；否则只复制。
    """
    src = safe_abs_texture_path(entry.get("path", ""))
    if not src or not safe_texture_exists(src):
        return False, "", "源贴图不存在"

    os.makedirs(output_dir, exist_ok=True)

    ext = os.path.splitext(src)[1].lower()
    if ext not in IMAGE_EXTS:
        # UE 可能支持部分特殊格式，但这里保守标记
        pass

    out_path = texture_output_base_path(entry, output_dir, ue_naming=ue_naming, rename_opts=rename_opts)

    if no_overwrite:
        i = 1
        root, ext2 = os.path.splitext(out_path)
        while os.path.exists(out_path):
            out_path = "{}_{:03d}{}".format(root, i, ext2)
            i += 1

    w = int(entry.get("width", 0) or 0)
    h = int(entry.get("height", 0) or 0)
    tw, th = compute_streaming_target_size(w, h, max_size=max_size, force_power2=force_power2, keep_aspect=keep_aspect)
    need_resize = (tw > 0 and th > 0 and (tw != w or th != h))

    if need_resize:
        try:
            from PIL import Image
            with Image.open(src) as im:
                # 尽量保留模式；Pillow 对 EXR/HDR/TGA 的支持取决于环境
                resample = getattr(Image, "LANCZOS", getattr(Image, "BICUBIC", 3))
                im2 = im.resize((tw, th), resample)
                im2.save(out_path)
            return True, out_path, "已复制并调整尺寸：{}x{} -> {}x{}".format(w, h, tw, th)
        except Exception as e:
            shutil.copy2(src, out_path)
            return True, out_path, "当前 Python 图像库无法缩放，已仅复制：{}".format(str(e).splitlines()[-1])

    shutil.copy2(src, out_path)
    return True, out_path, "已复制，尺寸保持 {}x{}".format(w, h)


def current_scene_base_name():
    try:
        name = safe_str(rt.maxFileName, "")
        if name:
            return clean_name_part(os.path.splitext(name)[0], "UntitledScene")
    except Exception:
        pass
    try:
        path = safe_str(rt.maxFilePath, "")
        if path:
            return clean_name_part(os.path.basename(os.path.normpath(path)), "UntitledScene")
    except Exception:
        pass
    return "UntitledScene"


def current_scene_folder():
    """当前 Max 文件所在目录；未保存时回退到 Documents。"""
    try:
        p = safe_str(rt.maxFilePath, "")
        if p and os.path.isdir(p):
            return os.path.normpath(p)
    except Exception:
        pass
    return os.path.join(os.path.expanduser("~"), "Documents")


def default_texture_root_dir():
    """选择根目录时优先使用当前模型所在目录。"""
    return current_scene_folder()


def make_scene_texture_output_dir(base_dir):
    """
    输出目录规则：
    1. 如果所选目录本身就叫模型同名 -> 直接使用
    2. 在所选目录下查找与模型同名的子文件夹：
       - 已存在 -> 直接使用
       - 不存在 -> 创建并使用
    """
    base_dir = safe_str(base_dir, "").strip()
    if not base_dir:
        base_dir = default_texture_root_dir()

    base_dir = os.path.normpath(base_dir)
    scene_name = current_scene_base_name()

    # 1. 目录名就是模型名，直接用
    if os.path.basename(base_dir).lower() == scene_name.lower():
        return base_dir

    # 2. 在 base_dir 下查找/创建模型同名子文件夹
    child = os.path.join(base_dir, scene_name)
    return child


def open_folder_in_os(path):
    path = safe_abs_texture_path(path)
    if not path:
        return False
    if os.path.isfile(path):
        path = os.path.dirname(path)
    try:
        os.startfile(path)
        return True
    except Exception:
        pass
    try:
        rt.shellLaunch(path, "")
        return True
    except Exception:
        pass
    return False


def open_file_in_os(path):
    path = safe_abs_texture_path(path)
    if not path or not os.path.exists(path):
        return False
    try:
        os.startfile(path)
        return True
    except Exception:
        pass
    try:
        rt.shellLaunch(path, "")
        return True
    except Exception:
        pass
    return False


def reveal_file_in_os(path):
    """打开文件所在位置，并尽量在资源管理器中选中该文件。"""
    path = safe_abs_texture_path(path)
    if not path:
        return False
    if os.path.isfile(path):
        try:
            if os.name == "nt":
                subprocess.Popen(["explorer", "/select,{}".format(path)])
                return True
        except Exception:
            pass
        return open_folder_in_os(os.path.dirname(path))
    return open_folder_in_os(path)


def collect_selected_object_texture_entries():
    entries = {}
    try:
        nodes = [o for o in rt.selection if is_valid_geometry(o)]
    except Exception:
        nodes = []
    for obj in nodes:
        try:
            mat = obj.material
        except Exception:
            mat = None
        if is_valid_material(mat):
            owner = "{} / {}".format(safe_str(getattr(obj, "name", ""), "Object"), get_material_name(mat))
            collect_texture_nodes_from_value(mat, owner, "", entries, owner_ref=mat)
            # 选中物体扫描时，直接把当前物体记录为使用者，避免只靠场景材质反查。
            try:
                for e in entries.values():
                    if mat in e.get("owner_materials", []) and obj not in e.get("owner_nodes", []):
                        e.setdefault("owner_nodes", []).append(obj)
            except Exception:
                pass
    return enrich_texture_entries_with_scene_objects(list(entries.values()))


def candidate_power_values(max_size=4096, min_size=16):
    vals = []
    v = 1
    while v <= max_size:
        if v >= min_size:
            vals.append(v)
        v *= 2
    return vals or [min_size]


def best_power2_target_size_for_crop(w, h, max_size=4096, min_size=16):
    """
    选择最适合原图比例的 2 幂目标尺寸。
    重要：只降不升。max_size 只是上限，不会把小图放大到 4096。
    目标是：不拉伸，通过居中裁剪尽量保留更多画面，然后缩放到目标尺寸。
    """
    try:
        w = int(w); h = int(h); max_size = int(max_size)
    except Exception:
        return 1024, 1024

    if w <= 0 or h <= 0:
        return 1024, 1024

    orig_aspect = float(w) / float(h)
    vals = candidate_power_values(max_size, min_size)

    best = None
    best_score = None

    for tw in vals:
        for th in vals:
            if tw > max_size or th > max_size:
                continue

            target_aspect = float(tw) / float(th)

            # 居中裁剪能保留的原图区域
            if target_aspect >= orig_aspect:
                crop_w = float(w)
                crop_h = float(w) / target_aspect
            else:
                crop_h = float(h)
                crop_w = float(h) * target_aspect

            # 只降不升：输出尺寸不能超过裁剪后的可用像素
            if tw > crop_w or th > crop_h:
                continue

            keep_ratio = max(0.0, min(1.0, (crop_w * crop_h) / float(w * h)))
            area = tw * th

            # 优先保留画面，其次尽量选择更大的非放大尺寸
            score = (keep_ratio, area)

            if best_score is None or score > best_score:
                best_score = score
                best = (tw, th)

    if best:
        return best

    # 极小图兜底：仍然不放大，向下取最接近的2幂
    return (
        max(min_size, min(nearest_power_of_two_down(min(w, max_size), min_size), w)),
        max(min_size, min(nearest_power_of_two_down(min(h, max_size), min_size), h))
    )


def center_crop_box_for_aspect(w, h, target_aspect):
    src_aspect = float(w) / float(h)
    if abs(src_aspect - target_aspect) < 0.0001:
        return (0, 0, w, h)

    if src_aspect > target_aspect:
        # 原图太宽，裁左右
        new_w = int(round(h * target_aspect))
        left = max(0, int((w - new_w) / 2))
        return (left, 0, left + new_w, h)

    # 原图太高，裁上下
    new_h = int(round(w / target_aspect))
    top = max(0, int((h - new_h) / 2))
    return (0, top, w, top + new_h)



def is_pillow_available():
    try:
        from PIL import Image
        return True
    except Exception:
        return False


def pillow_version_text():
    try:
        import PIL
        return safe_str(getattr(PIL, "__version__", ""), "可用")
    except Exception:
        return "不可用"


def current_python_for_pip():
    """
    尽量找到当前 3ds Max Python 的 python.exe。
    在一些嵌入式环境中 sys.executable 可能不是 python.exe，所以做多种猜测。
    """
    candidates = []

    try:
        exe = safe_str(sys.executable, "")
        if exe:
            candidates.append(exe)
            root = os.path.dirname(exe)
            candidates.append(os.path.join(root, "python.exe"))
            candidates.append(os.path.join(root, "Python", "python.exe"))
    except Exception:
        pass

    try:
        max_root = safe_str(rt.maxRoot, "")
        if max_root:
            candidates.append(os.path.join(max_root, "Python", "python.exe"))
            candidates.append(os.path.join(max_root, "Python311", "python.exe"))
            candidates.append(os.path.join(max_root, "Python310", "python.exe"))
            candidates.append(os.path.join(max_root, "Python39", "python.exe"))
            candidates.append(os.path.join(max_root, "Python37", "python.exe"))
    except Exception:
        pass

    try:
        # 常见安装路径兜底
        for year in ["2026", "2025", "2024", "2023", "2022"]:
            candidates.append(r"C:\Program Files\Autodesk\3ds Max {}\Python\python.exe".format(year))
            candidates.append(r"C:\Program Files\Autodesk\3ds Max {}\Python311\python.exe".format(year))
            candidates.append(r"C:\Program Files\Autodesk\3ds Max {}\Python310\python.exe".format(year))
            candidates.append(r"C:\Program Files\Autodesk\3ds Max {}\Python39\python.exe".format(year))
    except Exception:
        pass

    seen = set()
    for c in candidates:
        c = safe_abs_texture_path(c)
        if not c or c.lower() in seen:
            continue
        seen.add(c.lower())
        try:
            if os.path.exists(c) and os.path.basename(c).lower() == "python.exe":
                return c
        except Exception:
            pass

    return safe_str(sys.executable, "")


def launch_visible_command(title, commands):
    """
    打开可见 cmd 窗口执行命令，避免在 3ds Max UI 里长时间阻塞。

    V50 修复：
    旧版用 start/cmd 拼字符串，遇到
        "C:\\Program Files\\Autodesk\\3ds Max 2026\\Python\\python.exe"
    这种带空格路径时，Windows cmd 可能把 '"...\"' 当成程序名。
    现在改为写临时 .bat，再用 cmd.exe /k 打开，路径引号由 bat 文件自己处理。
    """
    try:
        safe_title = clean_name_part(title, "InteriorSceneStudio_Command")
        bat_path = os.path.join(tempfile.gettempdir(), safe_title + ".bat")

        lines = [
            "@echo off",
            "chcp 65001 >nul",
            "title {}".format(title),
            "echo Interior Scene Studio Pro",
            "echo.",
        ]

        for cmd in commands:
            lines.append(cmd)

        lines.extend([
            "echo.",
            "echo ------------------------------",
            "echo 命令执行完毕。如有错误，请把上面的错误信息截图发给开发者。",
            "echo Command finished. If there is an error, please copy/screenshot the message above.",
            "echo ------------------------------",
        ])

        with open(bat_path, "w", encoding="utf-8-sig") as f:
            f.write("\r\n".join(lines))

        creationflags = 0
        try:
            creationflags = subprocess.CREATE_NEW_CONSOLE
        except Exception:
            creationflags = 0x00000010

        subprocess.Popen(["cmd.exe", "/k", bat_path], creationflags=creationflags)
        return True, "已打开命令窗口：{}".format(bat_path)
    except Exception as e:
        return False, str(e)


def install_pillow_for_current_max_python():
    py = current_python_for_pip()
    if not py or not os.path.exists(py):
        return False, "没有找到 3ds Max Python.exe，请手动安装 Pillow"

    py = os.path.normpath(py)
    commands = [
        'set "PYEXE={}"'.format(py),
        'echo Python: %PYEXE%',
        'if not exist "%PYEXE%" (echo ERROR: Python.exe not found: %PYEXE% & exit /b 1)',
        '"%PYEXE%" -m ensurepip',
        '"%PYEXE%" -m pip install --upgrade pip',
        '"%PYEXE%" -m pip install pillow',
        'if errorlevel 1 (',
        '  echo.',
        '  echo Normal install failed. Trying --user install...',
        '  "%PYEXE%" -m pip install --user pillow',
        ')',
        'echo.',
        'echo Pillow install finished. Please restart 3ds Max, then click Detect again.'
    ]
    return launch_visible_command("Install Pillow for 3ds Max Python", commands)


def imagemagick_executable():
    # PATH 里优先
    magick = shutil.which("magick")
    if magick:
        return magick

    candidates = [
        r"C:\Program Files\ImageMagick-7.1.1-Q16-HDRI\magick.exe",
        r"C:\Program Files\ImageMagick-7.1.1-Q16\magick.exe",
        r"C:\Program Files\ImageMagick-7.1.0-Q16-HDRI\magick.exe",
        r"C:\Program Files\ImageMagick-7.1.0-Q16\magick.exe",
    ]

    try:
        root = r"C:\Program Files"
        if os.path.isdir(root):
            for name in os.listdir(root):
                if name.lower().startswith("imagemagick"):
                    candidates.append(os.path.join(root, name, "magick.exe"))
    except Exception:
        pass

    for c in candidates:
        try:
            if os.path.exists(c):
                return c
        except Exception:
            pass
    return ""


def is_imagemagick_available():
    return bool(imagemagick_executable())


def imagemagick_version_text():
    exe = imagemagick_executable()
    if not exe:
        return "不可用"
    try:
        p = subprocess.run([exe, "-version"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=5)
        out = (p.stdout or p.stderr or "").splitlines()
        return out[0] if out else "可用"
    except Exception:
        return "可用"


def install_imagemagick_with_winget():
    winget = shutil.which("winget")
    if not winget:
        return False, "没有检测到 winget，已建议打开官网下载页"
    commands = [
        'winget install --id ImageMagick.ImageMagick -e --source winget',
        'echo.',
        'echo ImageMagick install finished. Please restart 3ds Max, then click Detect again.'
    ]
    return launch_visible_command("Install ImageMagick", commands)


def open_imagemagick_download_page():
    try:
        webbrowser.open("https://imagemagick.org/script/download.php#windows")
        return True
    except Exception:
        pass
    try:
        rt.shellLaunch("https://imagemagick.org/script/download.php#windows", "")
        return True
    except Exception:
        return False


def force_process_texture_with_imagemagick(entry, output_dir, max_size=4096, ue_naming=True, no_overwrite=True, rename_opts=None):
    exe = imagemagick_executable()
    if not exe:
        return False, "", "ImageMagick 不可用"

    src = safe_abs_texture_path(entry.get("path", ""))
    if not src or not os.path.exists(src):
        return False, "", "源贴图不存在"

    os.makedirs(output_dir, exist_ok=True)

    ext = os.path.splitext(src)[1].lower()
    out_path = texture_output_base_path(entry, output_dir, ue_naming=ue_naming, rename_opts=rename_opts)
    if no_overwrite:
        root, ext2 = os.path.splitext(out_path)
        i = 1
        while os.path.exists(out_path):
            out_path = "{}_{:03d}{}".format(root, i, ext2)
            i += 1

    w = int(entry.get("width", 0) or 0)
    h = int(entry.get("height", 0) or 0)
    if w <= 0 or h <= 0:
        w, h, _ = safe_image_size(src)
    if w <= 0 or h <= 0:
        return False, "", "无法读取源贴图尺寸"

    tw, th = best_power2_target_size_for_crop(w, h, max_size=max_size)
    box = center_crop_box_for_aspect(w, h, float(tw) / float(th))
    crop_w = max(1, int(box[2] - box[0]))
    crop_h = max(1, int(box[3] - box[1]))

    try:
        cmd = [
            exe,
            src,
            "-auto-orient",
            "-gravity", "center",
            "-crop", "{}x{}+0+0".format(crop_w, crop_h),
            "+repage",
            "-resize", "{}x{}!".format(tw, th),
            out_path
        ]
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=180)
        if p.returncode != 0:
            return False, "", "ImageMagick 处理失败：{}".format((p.stderr or p.stdout or "").splitlines()[-1] if (p.stderr or p.stdout) else "未知错误")
        if os.path.exists(out_path):
            return True, out_path, "ImageMagick 强制合格：居中裁剪/缩放 {}x{} -> {}x{}，不拉伸".format(w, h, tw, th)
        return False, "", "ImageMagick 未生成输出文件"
    except Exception as e:
        return False, "", "ImageMagick 异常：{}".format(str(e).splitlines()[-1])


def force_process_texture_for_ue(entry, output_dir, max_size=4096, ue_naming=True, no_overwrite=True, rename_opts=None):
    """
    强制生成适合 UE 流送的输出贴图：
    - 不直接改原图
    - 使用居中裁剪 + 缩放
    - 不拉伸
    - 输出 2 幂尺寸，最大边不超过 max_size
    需要 Pillow。如果当前 Max Python 没有 Pillow，会返回失败。
    """
    src = safe_abs_texture_path(entry.get("path", ""))
    if not src or not os.path.exists(src):
        return False, "", "源贴图不存在"

    try:
        from PIL import Image
    except Exception:
        # Pillow 不可用时，尝试系统 ImageMagick。
        return force_process_texture_with_imagemagick(
            entry,
            output_dir,
            max_size=max_size,
            ue_naming=ue_naming,
            no_overwrite=no_overwrite,
            rename_opts=rename_opts
        )

    os.makedirs(output_dir, exist_ok=True)

    ext = os.path.splitext(src)[1].lower()
    if ext in [".hdr", ".exr", ".psd"]:
        # 对 HDR/EXR/PSD 自动处理风险较高；尽量保留格式但 Pillow 不一定支持。
        pass

    out_path = texture_output_base_path(entry, output_dir, ue_naming=ue_naming, rename_opts=rename_opts)
    if no_overwrite:
        root, ext2 = os.path.splitext(out_path)
        i = 1
        while os.path.exists(out_path):
            out_path = "{}_{:03d}{}".format(root, i, ext2)
            i += 1

    try:
        with Image.open(src) as im:
            w, h = im.size
            tw, th = best_power2_target_size_for_crop(w, h, max_size=max_size)
            box = center_crop_box_for_aspect(w, h, float(tw) / float(th))
            im_crop = im.crop(box)
            resample = getattr(Image, "LANCZOS", getattr(Image, "BICUBIC", 3))
            im_out = im_crop.resize((tw, th), resample)
            im_out.save(out_path)

        return True, out_path, "强制合格：居中裁剪/缩放 {}x{} -> {}x{}，不拉伸".format(w, h, tw, th)

    except Exception as e:
        return False, "", "强制处理失败：{}".format(str(e).splitlines()[-1])


def texture_output_passes_streaming(entry, max_size=4096, require_power2=True):
    out = safe_abs_texture_path(entry.get("output", ""))
    if not out or not os.path.exists(out):
        return False, ["未输出"]
    temp = dict(entry)
    temp["path"] = out
    refresh_texture_entry_info(temp)
    return texture_entry_passes_streaming(temp, max_size=max_size, require_power2=require_power2)


def texture_output_status_info(entry, max_size=4096, require_power2=True):
    out = safe_abs_texture_path(entry.get("output", ""))
    if not out or not os.path.exists(out):
        return dict(has_output=False, ok=False, issues=["未输出"], width=0, height=0, text="未输出")

    temp = dict(entry)
    temp["path"] = out
    refresh_texture_entry_info(temp)
    ok, issues = texture_entry_passes_streaming(temp, max_size=max_size, require_power2=require_power2)
    w = int(temp.get("width", 0) or 0)
    h = int(temp.get("height", 0) or 0)
    return dict(
        has_output=True,
        ok=ok,
        issues=issues,
        width=w,
        height=h,
        text=("输出合格 {}x{}".format(w, h) if ok else "输出不合格：{}".format("，".join(issues)))
    )


def texture_size_text(w, h):
    try:
        w = int(w or 0)
        h = int(h or 0)
    except Exception:
        w, h = 0, 0
    return "{}x{}".format(w, h) if w and h else "-"


def texture_qualified_size_text(entry, out_info=None, max_size=4096, force_power2=False):
    """
    列表里的"合格后尺寸"：
    - 如果已有输出，显示真实输出尺寸；
    - 如果还没输出，显示按当前设置预计得到的尺寸；
    """
    try:
        out_info = out_info or {}
        if out_info.get("has_output"):
            w = int(out_info.get("width", 0) or 0)
            h = int(out_info.get("height", 0) or 0)
            return "输出 " + texture_size_text(w, h)

        w = int(entry.get("width", 0) or 0)
        h = int(entry.get("height", 0) or 0)
        tw, th = compute_streaming_target_size(w, h, max_size=max_size, force_power2=force_power2, keep_aspect=True)
        if tw and th:
            if tw == w and th == h:
                return "保持 " + texture_size_text(tw, th)
            return "预计 " + texture_size_text(tw, th)
    except Exception:
        pass
    return "-"


def texture_output_is_success(entry, max_size=4096, require_power2=True):
    info = texture_output_status_info(entry, max_size=max_size, require_power2=require_power2)
    return bool(info.get("ok"))


def refresh_texture_entry_info(entry):
    path = safe_abs_texture_path(entry.get("path", ""))
    entry["path"] = path
    entry["file"] = os.path.basename(path) if path else entry.get("file", "")
    entry["exists"] = safe_texture_exists(path)
    w, h, reader = safe_image_size(path)
    entry["width"] = w
    entry["height"] = h
    entry["reader"] = reader
    if "output" not in entry:
        entry["output"] = ""
    return entry


def texture_entry_passes_streaming(entry, max_size=4096, require_power2=True):
    """
    合格标准：
    - 文件存在
    - 能读取尺寸
    - 最大边不超过 max_size
    - 如果 require_power2=True，则宽高都必须是 2 的幂
    """
    if not entry.get("exists"):
        return False, ["文件不存在"]

    w = int(entry.get("width", 0) or 0)
    h = int(entry.get("height", 0) or 0)

    if w <= 0 or h <= 0:
        return False, ["无法读取尺寸"]

    issues = []
    if w > max_size or h > max_size:
        issues.append("超过最大尺寸")

    if require_power2 and not (is_power_of_two_int(w) and is_power_of_two_int(h)):
        issues.append("非2幂尺寸")

    if issues:
        return False, issues

    return True, ["合格"]


def texture_entry_output_ready(entry):
    out = safe_abs_texture_path(entry.get("output", ""))
    return bool(out and os.path.exists(out))



# ============================================================
# PBR 贴图套装识别 / 一键建材质
# ============================================================

PBR_CHANNEL_ORDER = ["Preview", "BaseColor", "Roughness", "Glossiness", "Metallic", "Normal", "NormalDX", "NormalGL", "AO", "Height", "Displacement", "Opacity", "Emissive", "Specular", "ORM", "Unknown"]

PBR_CHANNEL_TOKENS = {
    "BaseColor": [
        "basecolor", "base_color", "base-colour", "basecolour", "albedo", "diffuse", "diff", "color", "colour", "col", "bc", "base"
    ],
    "Roughness": ["roughness", "rough", "rgh", "r"],
    "Glossiness": ["glossiness", "gloss", "glossy", "g"],
    "Metallic": ["metallic", "metalness", "metal", "met", "mtl", "m"],
    "Normal": ["normal", "normaldx", "normalgl", "nrm", "norm", "nor", "bump", "b"],
    "NormalDX": ["normaldx", "dx", "directx"],
    "NormalGL": ["normalgl", "gl", "opengl"],
    "AO": ["ao", "ambientocclusion", "ambient_occlusion", "occlusion", "occ"],
    "Height": ["height", "heightmap", "depth"],
    "Displacement": ["displacement", "displace", "disp", "displ", "dsp"],
    "Opacity": ["opacity", "alpha", "transparent", "transparency", "cutout", "mask"],
    "Emissive": ["emissive", "emission", "emit", "glow", "selfillum", "self_illum"],
    "Specular": ["specular", "spec", "reflection", "refl"],
    "ORM": ["orm", "arm", "rma", "occlusionroughnessmetallic", "ambientroughnessmetallic"]
}

PBR_RESOLUTION_TOKENS = set([
    "512", "1k", "2k", "3k", "4k", "6k", "8k", "12k", "16k",
    "1024", "2048", "4096", "8192", "16384", "udim", "1001", "1002", "1003", "1004"
])


def split_texture_name_tokens(name):
    base = os.path.splitext(os.path.basename(safe_str(name, "")))[0]
    # 拆 CamelCase，例如 BaseColor / NormalDX
    base = re.sub(r"([a-z])([A-Z])", r"\1_\2", base)
    low = base.lower()
    tokens = [t for t in re.split(r"[^a-z0-9]+", low) if t]
    compact = re.sub(r"[^a-z0-9]+", "", low)
    return base, low, compact, tokens


def detect_pbr_channel_from_filename(path):
    base, low, compact, tokens = split_texture_name_tokens(path)
    token_set = set(tokens)

    # packed ORM/ARM/RMA 先识别，避免被拆到 Rough/Metal/AO
    if any(k in compact for k in ["occlusionroughnessmetallic", "ambientroughnessmetallic"]):
        return "ORM", "Packed ORM"
    if any(t in token_set for t in ["orm", "arm", "rma"]):
        return "ORM", "Packed ORM"

    # 组合词优先
    if "basecolor" in compact or "basecolour" in compact:
        return "BaseColor", "BaseColor"
    if "ambientocclusion" in compact:
        return "AO", "Ambient Occlusion"
    if "normaldx" in compact or "directx" in compact:
        return "NormalDX", "Normal DX"
    if "normalgl" in compact or "opengl" in compact:
        return "NormalGL", "Normal GL"
    if "metalness" in compact:
        return "Metallic", "Metalness"
    if "roughness" in compact:
        return "Roughness", "Roughness"
    if "glossiness" in compact:
        return "Glossiness", "Glossiness"

    # token 规则；避免单字母 r/m/g/b 误判，单字母只在末尾/常见贴图名里采用
    strong_rules = [
        ("BaseColor", ["albedo", "diffuse", "diff", "colour", "color", "col", "bc"]),
        ("Normal", ["normal", "nrm", "norm", "nor", "bump"]),
        ("Roughness", ["rough", "rgh"]),
        ("Glossiness", ["gloss", "glossy"]),
        ("Metallic", ["metallic", "metal", "met", "mtl"]),
        ("AO", ["ao", "occlusion", "occ"]),
        ("Height", ["height", "depth"]),
        ("Displacement", ["displacement", "displace", "disp", "displ", "dsp"]),
        ("Opacity", ["opacity", "alpha", "transparent", "transparency", "cutout", "mask"]),
        ("Emissive", ["emissive", "emission", "emit", "glow", "selfillum"]),
        ("Specular", ["specular", "spec", "reflection", "refl"]),
    ]

    for channel, keys in strong_rules:
        if any(k in token_set for k in keys):
            return channel, channel

    # 单字母后缀，只有最后一个有效 token 时使用
    meaningful = [t for t in tokens if t not in PBR_RESOLUTION_TOKENS and t not in ["map", "tex", "texture"]]
    if meaningful:
        last = meaningful[-1]
        if last == "r":
            return "Roughness", "R suffix"
        if last == "m":
            return "Metallic", "M suffix"
        if last == "g":
            return "Glossiness", "G suffix"
        if last == "b":
            return "Normal", "B/Bump suffix"

    return "Unknown", ""


def is_probable_pbr_preview_image(path):
    ext = os.path.splitext(path)[1].lower()
    if ext not in [".png", ".jpg", ".jpeg"]:
        return False

    base, low, compact, tokens = split_texture_name_tokens(path)
    token_set = set(tokens)

    preview_keys = ["preview", "thumb", "thumbnail", "render", "sphere", "ball", "materialpreview", "matpreview", "sample"]
    if any(k in compact for k in preview_keys) or any(k in token_set for k in preview_keys):
        return True

    # 很多网站的预览图是一个没有 BaseColor/Roughness/Normal 等后缀的 PNG。
    # 如果它没有任何已知通道 token，且文件名接近文件夹名，就作为预览图候选。
    folder_name = clean_name_part(os.path.basename(os.path.dirname(path)), "").lower()
    name = clean_name_part(os.path.splitext(os.path.basename(path))[0], "").lower()

    known_channel_tokens = set()
    for toks in PBR_CHANNEL_TOKENS.values():
        for t in toks:
            known_channel_tokens.add(t.lower().replace("_", "").replace("-", ""))
            known_channel_tokens.add(t.lower())

    has_channel = False
    for t in tokens:
        if t in known_channel_tokens:
            has_channel = True
            break

    if not has_channel and ext == ".png":
        if folder_name and (name == folder_name or folder_name in name or name in folder_name):
            return True
        # 文件夹内唯一/主图情况也常见，但这里先保守：只把 PNG 且没有通道词的图作为预览候选。
        if len(tokens) <= 3:
            return True

    return False


def pbrset_preview_path(entry):
    if entry.get("preview"):
        return entry.get("preview")
    channels = entry.get("channels", {})
    for ch in ["BaseColor", "Albedo", "Diffuse", "Preview"]:
        if ch in channels:
            return channels[ch]
    for ch in ["Normal", "NormalDX", "NormalGL", "Roughness", "Metallic", "AO"]:
        if ch in channels:
            return channels[ch]
    return ""


def preferred_normal_channel(channels, preference="DirectX / DX（UE常用）"):
    pref = safe_str(preference, "").lower()
    if "dx" in pref or "direct" in pref:
        for ch in ["NormalDX", "Normal", "NormalGL"]:
            if ch in channels:
                return ch
    elif "gl" in pref or "open" in pref:
        for ch in ["NormalGL", "Normal", "NormalDX"]:
            if ch in channels:
                return ch
    else:
        for ch in ["Normal", "NormalDX", "NormalGL"]:
            if ch in channels:
                return ch
    return ""


def invert_glossiness_texture_to_roughness(src_path):
    """
    将 Glossiness 贴图反相生成 Roughness 副本。
    优先 Pillow，失败后 ImageMagick。输出到源贴图同级 _ISS_Generated_Roughness 文件夹。
    """
    src_path = safe_abs_texture_path(src_path)
    if not src_path or not os.path.exists(src_path):
        return "", "Glossiness源贴图不存在"

    out_dir = os.path.join(os.path.dirname(src_path), "_ISS_Generated_Roughness")
    try:
        os.makedirs(out_dir, exist_ok=True)
    except Exception:
        return "", "无法创建反相输出目录"

    base = clean_name_part(os.path.splitext(os.path.basename(src_path))[0], "Glossiness")
    ext = os.path.splitext(src_path)[1].lower()
    out_path = os.path.join(out_dir, base + "_Roughness_Inverted" + ext)

    # 不覆盖已有文件，便于重复测试
    root, ext2 = os.path.splitext(out_path)
    i = 1
    while os.path.exists(out_path):
        out_path = "{}_{:03d}{}".format(root, i, ext2)
        i += 1

    try:
        from PIL import Image, ImageOps
        with Image.open(src_path) as im:
            if im.mode in ("RGBA", "LA"):
                channels = im.split()
                rgb = Image.merge("RGB", channels[:3]) if im.mode == "RGBA" else channels[0].convert("RGB")
                inv = ImageOps.invert(rgb)
                if im.mode == "RGBA":
                    inv.putalpha(channels[3])
                im_out = inv
            else:
                im_out = ImageOps.invert(im.convert("RGB"))
            im_out.save(out_path)
        return out_path, "已用Pillow反相生成Roughness"
    except Exception as e:
        pass

    try:
        exe = imagemagick_executable()
        if exe:
            cmd = [exe, src_path, "-negate", out_path]
            p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=180)
            if p.returncode == 0 and os.path.exists(out_path):
                return out_path, "已用ImageMagick反相生成Roughness"
            return "", "ImageMagick反相失败"
    except Exception:
        pass

    return "", "没有可用的Pillow/ImageMagick，无法自动反相Glossiness"


def pbr_set_key_from_filename(path, channel):
    folder = os.path.dirname(safe_abs_texture_path(path))
    base, low, compact, tokens = split_texture_name_tokens(path)

    if channel == "Preview":
        folder_name = clean_name_part(os.path.basename(folder), "PBR_Material")
        return folder + "|" + folder_name, folder_name

    remove_tokens = set(["map", "tex", "texture", "material", "mat", "jpg", "jpeg", "png", "tif", "tiff", "exr", "hdr", "bmp", "tga"])
    remove_tokens |= PBR_RESOLUTION_TOKENS

    # 移除当前通道相关 token
    for ch, toks in PBR_CHANNEL_TOKENS.items():
        if ch == channel or channel == "ORM":
            remove_tokens.update([t.lower().replace("_", "").replace("-", "") for t in toks])
            remove_tokens.update([t.lower() for t in toks])

    # 额外组合词拆分
    remove_tokens.update(["base", "color", "colour", "ambient", "occlusion", "normal", "dx", "gl", "metal", "metallic", "metalness"])

    clean_tokens = []
    for t in tokens:
        if t in remove_tokens:
            continue
        if re.match(r"^\d{4}$", t):  # UDIM / 分辨率
            continue
        clean_tokens.append(t)

    if clean_tokens:
        name = "_".join(clean_tokens)
    else:
        name = os.path.basename(folder) or "PBR_Material"

    name = clean_name_part(name, "PBR_Material")
    return folder + "|" + name, name


def choose_better_pbr_map(old_path, new_path):
    """
    同一通道出现多张贴图时，尽量选更常见/更高质量的一张。
    - 优先非缩略图
    - 优先 4k/8k/2k 等较高清晰度标记
    - 其次保留先出现的
    """
    if not old_path:
        return new_path
    old_low = os.path.basename(old_path).lower()
    new_low = os.path.basename(new_path).lower()

    bad_keys = ["thumb", "preview", "small", "icon"]
    if any(k in old_low for k in bad_keys) and not any(k in new_low for k in bad_keys):
        return new_path

    score_map = {"16k": 16000, "12k": 12000, "8k": 8000, "8192": 8192, "6k": 6000, "4k": 4096, "4096": 4096, "3k": 3000, "2k": 2048, "2048": 2048, "1k": 1024, "1024": 1024}
    def score(s):
        return max([v for k, v in score_map.items() if k in s] or [0])

    if score(new_low) > score(old_low):
        return new_path

    return old_path


def scan_pbr_texture_sets(root_dir, recursive=True, group_by_folder=True):
    """
    扫描 PBR 贴图套装。
    默认 group_by_folder=True：
    - 一个文件夹里的贴图默认归为一个材质套装。
    - 这更符合 Poliigon / ambientCG / Poly Haven / Quixel 等下载包常见结构。
    - 例如 Leather026.png + Leather026_1K-JPG_Color.jpg + NormalDX/GL/Roughness 会合并为 Leather026 一个材质。
    group_by_folder=False 时才按文件名推断多个套装。
    """
    root_dir = safe_abs_texture_path(root_dir)
    sets = {}
    if not root_dir or not os.path.isdir(root_dir):
        return []

    walkers = os.walk(root_dir) if recursive else [(root_dir, [], os.listdir(root_dir))]

    for folder, _dirs, files in walkers:
        image_files = [fn for fn in files if os.path.splitext(fn)[1].lower() in IMAGE_EXTS]
        if not image_files:
            continue

        folder_set_name = clean_name_part(os.path.basename(folder), "PBR_Material")
        folder_key = folder + "|" + folder_set_name

        for fn in image_files:
            path = os.path.normpath(os.path.join(folder, fn))
            channel, note = detect_pbr_channel_from_filename(path)

            if channel == "Unknown" and is_probable_pbr_preview_image(path):
                channel = "Preview"

            if group_by_folder:
                key, set_name = folder_key, folder_set_name
            else:
                key, set_name = pbr_set_key_from_filename(path, channel)

            if key not in sets:
                sets[key] = dict(
                    name=set_name,
                    folder=folder,
                    channels={},
                    duplicates=[],
                    unknown=[],
                    preview="",
                    created_mat=None,
                    status="等待"
                )

            if channel == "Preview":
                # 预览图单独保存，不作为材质通道连接
                if not sets[key].get("preview"):
                    sets[key]["preview"] = path
                else:
                    sets[key]["duplicates"].append(path)
                continue

            if channel == "Unknown":
                sets[key]["unknown"].append(path)
                continue

            # 如果 NormalDX / NormalGL 同时存在，保留两张；后续按用户偏好选择。
            if channel in sets[key]["channels"]:
                old = sets[key]["channels"][channel]
                chosen = choose_better_pbr_map(old, path)
                if chosen != old:
                    sets[key]["duplicates"].append(old)
                    sets[key]["channels"][channel] = chosen
                else:
                    sets[key]["duplicates"].append(path)
            else:
                sets[key]["channels"][channel] = path

    result = list(sets.values())
    result.sort(key=lambda e: (safe_str(e.get("folder", "")).lower(), safe_str(e.get("name", "")).lower()))
    return result


def create_bitmap_texmap(path):
    path = safe_abs_texture_path(path)
    for cls_name in ["Bitmaptexture", "BitmapTexture", "bitmapTexture"]:
        try:
            cls = getattr(rt, cls_name)
            tex = cls()
            if tex is not None:
                try: tex.filename = path
                except Exception: pass
                try: tex.fileName = path
                except Exception: pass
                try: tex.bitmapName = path
                except Exception: pass
                return tex
        except Exception:
            pass

    try:
        tex = rt.execute('Bitmaptexture filename:@"{}"'.format(path.replace("\\", "\\\\")))
        if tex:
            return tex
    except Exception:
        pass

    return None


def create_normal_texmap(path):
    bitmap = create_bitmap_texmap(path)
    if bitmap is None:
        return None

    # V-Ray 推荐 VRayNormalMap -> Bump；如果没有 V-Ray，则使用 Max Normal_Bump。
    for cls_name in ["VRayNormalMap", "VrayNormalMap", "VRayNormalTex"]:
        try:
            cls = getattr(rt, cls_name)
            nm = cls()
            if nm:
                if safe_set_attr_any(nm, ["normal_map", "normalMap", "map", "texmap", "normal_texmap"], bitmap):
                    return nm
        except Exception:
            pass

    for cls_name in ["Normal_Bump", "NormalBump"]:
        try:
            cls = getattr(rt, cls_name)
            nm = cls()
            if nm:
                if safe_set_attr_any(nm, ["normal_map", "normalMap", "normal", "map", "texmap"], bitmap):
                    return nm
        except Exception:
            pass

    return bitmap


def pbr_channel_summary(channels):
    parts = []
    for ch in PBR_CHANNEL_ORDER:
        if ch in channels and ch not in ("Unknown", "Preview"):
            parts.append(ch)
    return " / ".join(parts) if parts else "未识别"


def pbr_set_issues(entry):
    channels = entry.get("channels", {})
    issues = []
    if "BaseColor" not in channels:
        issues.append("缺BaseColor")
    if "Normal" not in channels and "NormalDX" not in channels and "NormalGL" not in channels:
        issues.append("缺Normal")
    if "Roughness" not in channels and "Glossiness" in channels:
        issues.append("Glossiness需按选项处理")
    elif "Roughness" not in channels:
        issues.append("缺Roughness")
    if "ORM" in channels:
        issues.append("Packed ORM需人工确认")
    if entry.get("duplicates"):
        issues.append("有重复通道")
    if entry.get("unknown"):
        issues.append("有未识别贴图")
    return issues


def pbr_set_critical_issues(entry):
    channels = entry.get("channels", {})
    issues = []
    if "BaseColor" not in channels:
        issues.append("缺BaseColor")
    if "Normal" not in channels and "NormalDX" not in channels and "NormalGL" not in channels:
        issues.append("缺Normal")
    if "Roughness" not in channels and "Glossiness" not in channels:
        issues.append("缺Roughness/Glossiness")
    if "ORM" in channels and "Roughness" not in channels and "Metallic" not in channels and "AO" not in channels:
        issues.append("只有Packed ORM，未拆通道")
    return issues


def pbr_set_is_basic_complete(entry):
    return len(pbr_set_critical_issues(entry)) == 0


def pbr_set_display_issues(entry, normal_preference="DirectX / DX（UE常用）"):
    critical = pbr_set_critical_issues(entry)
    warnings = []
    channels = entry.get("channels", {})
    if "Roughness" not in channels and "Glossiness" in channels:
        warnings.append("Glossiness按选项处理")
    if entry.get("duplicates"):
        warnings.append("有重复/未使用贴图{}张".format(len(entry.get("duplicates", []))))
    if entry.get("unknown"):
        warnings.append("有未连接贴图{}张".format(len(entry.get("unknown", []))))
    mapping_issues = pbrset_mapping_completeness_issues(entry, normal_preference)
    warnings.extend(mapping_issues)

    if critical:
        return "，".join(critical + warnings)
    if warnings:
        return "需要手动映射；" + "，".join(warnings)
    return "可创建"


def serialize_pbrset_entry(entry):
    return dict(
        name=safe_str(entry.get("name", "")),
        folder=safe_str(entry.get("folder", "")),
        channels=dict(entry.get("channels", {})),
        duplicates=list(entry.get("duplicates", [])),
        unknown=list(entry.get("unknown", [])),
        preview=safe_str(entry.get("preview", "")),
        status=safe_str(entry.get("status", "等待")),
        slot_overrides=dict(entry.get("slot_overrides", {})),
        created_target=safe_str(entry.get("created_target", "")),
        created_class=safe_str(entry.get("created_class", "")),
        created_material_name=safe_str(entry.get("created_material_name", "")),
        created_signature=safe_str(entry.get("created_signature", ""))
    )


def deserialize_pbrset_entry(data):
    return dict(
        name=safe_str(data.get("name", "PBR_Material")),
        folder=safe_str(data.get("folder", "")),
        channels=dict(data.get("channels", {})),
        duplicates=list(data.get("duplicates", [])),
        unknown=list(data.get("unknown", [])),
        preview=safe_str(data.get("preview", "")),
        created_mat=None,
        status=safe_str(data.get("status", "已从材质库加载")),
        slot_overrides=dict(data.get("slot_overrides", {})),
        created_target=safe_str(data.get("created_target", "")),
        created_class=safe_str(data.get("created_class", "")),
        created_material_name=safe_str(data.get("created_material_name", "")),
        created_signature=safe_str(data.get("created_signature", ""))
    )


def pbrset_created_material_name_text(entry):
    mat = entry.get("created_mat")
    if is_valid_material(mat):
        return get_material_name(mat)
    name = safe_str(entry.get("created_material_name", ""), "")
    return name if name else "-"


def pbrset_created_type_text(entry):
    """
    PBR套装列表里显示创建材质类型。
    - 目标：用户选择的目标材质，如 V-Ray Material
    - 实际：Max 实际创建出来的类名，如 VRayMtl；如果目标类不可用发生回退，也能看出来。
    """
    target = safe_str(entry.get("created_target", ""), "")
    actual = safe_str(entry.get("created_class", ""), "")
    if target and actual:
        if target.lower() in actual.lower() or actual.lower() in target.lower():
            return actual
        return "{} / {}".format(target, actual)
    if actual:
        return actual
    if target:
        return target
    return "-"


def pbrset_preferred_material_base_name(entry):
    """
    材质命名优先使用预览图文件名。
    如果预览图名是 preview/thumb 这种泛称，则回退到套装名。
    """
    preview = safe_str(entry.get("preview", ""), "")
    if preview:
        base = os.path.splitext(os.path.basename(preview))[0]
        clean = clean_name_part(base, "")
        low = clean.lower()
        generic = set(["preview", "thumb", "thumbnail", "render", "sphere", "ball", "sample", "matpreview", "materialpreview"])
        if clean and low not in generic:
            return clean
    return clean_name_part(entry.get("name", "PBR_Material"), "PBR_Material")


def medit_slot_count():
    try:
        return int(len(rt.meditMaterials))
    except Exception:
        pass
    try:
        return int(rt.meditMaterials.count)
    except Exception:
        pass
    return 24


PBR_MANUAL_CHANNELS = [
    "不连接", "Preview", "BaseColor", "Roughness", "Glossiness", "Metallic",
    "Normal", "NormalDX", "NormalGL", "AO", "Height", "Displacement",
    "Opacity", "Emissive", "Specular", "ORM"
]


def pbrset_all_texture_files(entry):
    files = []
    seen = set()
    folder = safe_abs_texture_path(entry.get("folder", ""))

    if folder and os.path.isdir(folder):
        try:
            for fn in os.listdir(folder):
                path = os.path.normpath(os.path.join(folder, fn))
                if os.path.isfile(path) and os.path.splitext(fn)[1].lower() in IMAGE_EXTS:
                    key = path.lower()
                    if key not in seen:
                        seen.add(key)
                        files.append(path)
        except Exception:
            pass

    extra = list(entry.get("channels", {}).values()) + list(entry.get("unknown", [])) + list(entry.get("duplicates", [])) + [entry.get("preview", "")]
    for path in extra:
        path = safe_abs_texture_path(path)
        if path and path.lower() not in seen:
            seen.add(path.lower())
            files.append(path)

    files.sort(key=lambda p: os.path.basename(p).lower())
    return files


def pbrset_channel_for_file(entry, path):
    path = safe_abs_texture_path(path)
    if not path:
        return "不连接"

    if safe_abs_texture_path(entry.get("preview", "")).lower() == path.lower():
        return "Preview"

    for ch, p in entry.get("channels", {}).items():
        if safe_abs_texture_path(p).lower() == path.lower():
            return ch

    if any(safe_abs_texture_path(p).lower() == path.lower() for p in entry.get("unknown", [])):
        return "不连接"

    if any(safe_abs_texture_path(p).lower() == path.lower() for p in entry.get("duplicates", [])):
        return "不连接"

    ch, _note = detect_pbr_channel_from_filename(path)
    if ch == "Unknown" and is_probable_pbr_preview_image(path):
        return "Preview"
    if ch == "Unknown":
        return "不连接"
    return ch


def pbrset_mapping_signature(entry):
    """
    记录当前套装贴图映射状态。
    只要通道路径、预览图、未连接/重复列表变化，就视为需要重新创建材质。
    """
    try:
        channels = entry.get("channels", {})
        ch_pairs = []
        for ch in sorted(channels.keys()):
            ch_pairs.append("{}={}".format(ch, safe_abs_texture_path(channels.get(ch, ""))))

        unknown = sorted([safe_abs_texture_path(p) for p in entry.get("unknown", []) if p])
        duplicates = sorted([safe_abs_texture_path(p) for p in entry.get("duplicates", []) if p])
        preview = safe_abs_texture_path(entry.get("preview", ""))

        overrides = entry.get("slot_overrides", {})
        override_pairs = []
        try:
            for k in sorted(overrides.keys()):
                override_pairs.append("{}={}".format(k, overrides.get(k, "")))
        except Exception:
            pass

        return "|".join([
            "preview={}".format(preview),
            "channels={}".format(";".join(ch_pairs)),
            "unknown={}".format(";".join(unknown)),
            "duplicates={}".format(";".join(duplicates)),
            "slot_overrides={}".format(";".join(override_pairs)),
        ])
    except Exception:
        return ""


def apply_manual_pbrset_mapping(entry, mapping):
    channels = {}
    duplicates = []
    unknown = []
    preview = ""

    for path, channel in mapping:
        path = safe_abs_texture_path(path)
        if not path:
            continue

        channel = safe_str(channel, "不连接")
        if channel == "Preview":
            if not preview:
                preview = path
            else:
                duplicates.append(path)
            continue

        if channel == "不连接":
            unknown.append(path)
            continue

        if channel in channels:
            duplicates.append(path)
        else:
            channels[channel] = path

    entry["channels"] = channels
    entry["duplicates"] = duplicates
    entry["unknown"] = unknown
    entry["preview"] = preview
    # 映射变化后，旧材质不再可靠。保留旧材质在场景中，但当前列表不再复用它。
    entry["created_mat"] = None
    entry["created_target"] = ""
    entry["created_class"] = ""
    entry["created_signature"] = ""
    entry["status"] = "已手动映射，需重新创建"
    return entry


def pbrset_expected_unused_files(entry, normal_preference="DirectX / DX（UE常用）"):
    """
    允许不用的贴图：
    1. Preview；
    2. NormalDX/NormalGL 二选一时，未被选中的另一张法线；
    其它图片原则上都应该映射到可连接通道。
    """
    allowed = set()
    preview = safe_abs_texture_path(entry.get("preview", ""))
    if preview:
        allowed.add(preview.lower())

    channels = entry.get("channels", {})
    chosen_normal = preferred_normal_channel(channels, normal_preference)
    for ch in ["Normal", "NormalDX", "NormalGL"]:
        p = safe_abs_texture_path(channels.get(ch, ""))
        if p and ch != chosen_normal:
            # 只有在有两张法线时才允许未选中的那张不用
            if chosen_normal and ch in ("NormalDX", "NormalGL"):
                allowed.add(p.lower())

    return allowed


def pbrset_unusable_channel_issues(entry):
    """
    这些通道现在不会安全自动接入，必须让用户手动拆分/重映射。
    """
    issues = []
    channels = entry.get("channels", {})
    if "ORM" in channels:
        issues.append("ORM/ARM/RMA 是打包贴图，当前不会自动拆分，请手动拆分或映射为单独 AO/Roughness/Metallic")
    return issues


def pbrset_mapping_completeness_issues(entry, normal_preference="DirectX / DX（UE常用）"):
    """
    创建材质前检查：
    - 文件夹里的每张贴图是否都有可用去处；
    - Preview 可以不用；
    - NormalDX/NormalGL 二选一可以不用另一张；
    - Unknown / duplicates / ORM 等会阻止创建。
    """
    issues = []
    allowed_unused = pbrset_expected_unused_files(entry, normal_preference)

    mapped_paths = set()
    for p in entry.get("channels", {}).values():
        p = safe_abs_texture_path(p)
        if p:
            mapped_paths.add(p.lower())
    preview = safe_abs_texture_path(entry.get("preview", ""))
    if preview:
        mapped_paths.add(preview.lower())

    all_files = pbrset_all_texture_files(entry)
    unmapped = []
    for p in all_files:
        key = safe_abs_texture_path(p).lower()
        if key in allowed_unused:
            continue
        if key not in mapped_paths:
            unmapped.append(p)

    if unmapped:
        issues.append("有{}张贴图没有映射到可用通道".format(len(unmapped)))

    # duplicates 通常意味着同通道多张图未使用，除非它是允许不用的另一张法线或预览
    unused_duplicates = []
    for p in entry.get("duplicates", []):
        key = safe_abs_texture_path(p).lower()
        if key not in allowed_unused:
            unused_duplicates.append(p)
    if unused_duplicates:
        issues.append("有{}张重复/未使用贴图需要手动确认".format(len(unused_duplicates)))

    # unknown / 不连接
    unknown = []
    for p in entry.get("unknown", []):
        key = safe_abs_texture_path(p).lower()
        if key not in allowed_unused:
            unknown.append(p)
    if unknown:
        issues.append("有{}张未连接贴图需要手动映射".format(len(unknown)))

    issues.extend(pbrset_unusable_channel_issues(entry))
    return issues


def pbrset_is_creation_ready(entry, normal_preference="DirectX / DX（UE常用）"):
    critical = pbr_set_critical_issues(entry)
    complete = pbrset_mapping_completeness_issues(entry, normal_preference)
    return len(critical) == 0 and len(complete) == 0


def pbr_slot_override_key(target_mode, channel_label):
    return "{}|{}".format(material_target_mode_key(target_mode), safe_str(channel_label, ""))


def pbr_slot_learning_file():
    try:
        root = os.path.join(os.path.expanduser("~"), "Documents")
        return os.path.join(root, "InteriorSceneStudio_PBR_SlotLearning.json")
    except Exception:
        return os.path.join(os.getcwd(), "InteriorSceneStudio_PBR_SlotLearning.json")


def pbr_slot_learning_key(target_mode, mat_or_class, channel_label):
    try:
        cls = get_class_name(mat_or_class) if is_valid_material(mat_or_class) else safe_str(mat_or_class, "")
    except Exception:
        cls = safe_str(mat_or_class, "")
    return "{}|{}|{}".format(
        material_target_mode_key(target_mode),
        cls.lower(),
        safe_str(channel_label, "").lower()
    )


def pbr_slot_learning_family_key(target_mode, channel_label):
    return "{}|*|{}".format(material_target_mode_key(target_mode), safe_str(channel_label, "").lower())


def load_pbr_slot_learning():
    path = pbr_slot_learning_file()
    try:
        if os.path.exists(path):
            with open(path, "r", encoding="utf-8") as f:
                data = json.load(f)
            if isinstance(data, dict):
                return data
    except Exception:
        pass
    return {}


def save_pbr_slot_learning(data):
    path = pbr_slot_learning_file()
    try:
        folder = os.path.dirname(path)
        if folder and not os.path.isdir(folder):
            os.makedirs(folder, exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False, indent=2)
        return True
    except Exception:
        return False


def learn_pbr_slot(target_mode, mat, channel_label, prop_name):
    prop_name = safe_str(prop_name, "")
    if not prop_name or prop_name.startswith("UE独立"):
        return False
    data = load_pbr_slot_learning()
    data[pbr_slot_learning_key(target_mode, mat, channel_label)] = prop_name
    # 同一目标材质族的通道也记录一份，方便同类材质不同Class名时复用
    data[pbr_slot_learning_family_key(target_mode, channel_label)] = prop_name
    return save_pbr_slot_learning(data)


def learned_pbr_slots(target_mode, mat, channel_label):
    data = load_pbr_slot_learning()
    keys = [
        pbr_slot_learning_key(target_mode, mat, channel_label),
        pbr_slot_learning_family_key(target_mode, channel_label),
    ]
    result = []
    for k in keys:
        prop = safe_str(data.get(k, ""), "")
        if prop and prop not in result:
            result.append(prop)
    return result


def pbr_material_map_candidate_props(mat, channel_label=""):
    """
    从当前真实材质对象中列出可能可用的贴图槽。
    这是给"手动材质槽配置"窗口用的，不再只靠我们猜属性名。
    """
    result = []
    seen = set()

    try:
        props = [safe_str(p, "") for p in rt.getPropNames(mat)]
    except Exception:
        props = []

    label = safe_str(channel_label, "").lower()

    # 优先给当前通道常见关键词排序
    priority = []
    if any(k in label for k in ["base", "color", "diffuse", "albedo"]):
        priority = ["base", "albedo", "diffuse", "color", "texmap_diffuse", "mapm1"]
    elif "rough" in label:
        priority = ["rough", "gloss", "reflectionroughness", "base_roughness", "mapm4"]
    elif "gloss" in label:
        priority = ["gloss", "rough", "reflectionglossiness", "mapm4"]
    elif any(k in label for k in ["metal", "metallic"]):
        priority = ["metal", "metalness", "metallic", "mapm5"]
    elif "normal" in label or "bump" in label:
        priority = ["normal", "bump"]
    elif "height" in label or "displacement" in label:
        priority = ["displace", "displacement", "height", "bump"]
    elif "opacity" in label or "alpha" in label:
        priority = ["opacity", "alpha", "cutout", "transparency", "mapm12", "mapm9"]
    elif "emiss" in label:
        priority = ["emiss", "emission", "emit", "selfillum", "self_illum", "mapm17"]
    elif "ao" in label or "occlusion" in label:
        priority = ["ao", "occlusion", "diffuse", "base"]
    elif "spec" in label:
        priority = ["spec", "refl", "reflection"]

    def add_prop(p):
        if not p:
            return
        key = p.lower()
        if key in seen:
            return
        seen.add(key)
        result.append(p)

    # 常见全局候选，包含 PBR/Physical/V-Ray/Corona 常见名字
    common = [
        "base_color_map", "baseColorMap", "baseColor_map", "baseTexmap", "baseColorTexmap",
        "diffuse_map", "diffuseMap", "texmap_diffuse", "diffuseTexmap", "map_diffuse", "mapM1",
        "roughness_map", "roughnessMap", "texmap_roughness", "reflectionRoughnessMap", "texmap_reflectionRoughness",
        "baseRoughnessTexmap", "roughnessTexmap", "mapM4",
        "glossiness_map", "glossinessMap", "reflectionGlossinessMap", "texmap_reflectionGlossiness",
        "metalness_map", "metalnessMap", "metallic_map", "metallicMap", "metalnessTexmap", "metalnessMap", "mapM5",
        "normal_map", "normalMap", "bump_map", "bumpMap", "bumpTexmap", "normalTexmap", "texmap_bump",
        "ao_map", "ambient_occlusion_map", "ambientOcclusionMap", "occlusion_map",
        "height_map", "heightMap", "displacement_map", "displacementMap", "displacementTexmap", "displacementMap",
        "opacity_map", "opacityMap", "alpha_map", "cutout_map", "cutoutMap", "opacityTexmap", "texmap_opacity", "mapM12", "mapM9",
        "emission_color_map", "emissive_map", "emission_map", "emit_color_map", "emissionTexmap", "selfIllumMap", "mapM17", "mapM16",
        "specular_map", "specularMap", "reflection_map", "reflectionMap", "refl_map", "refl_color_map", "mapM3",
    ]

    # 先按当前通道关键词挑实际属性
    for pri in priority:
        for p in props:
            low = p.lower()
            if pri in low and ("map" in low or "tex" in low or "texture" in low or "bump" in low or "displace" in low or low.startswith("mapm")):
                add_prop(p)

    # 再加入 common 中当前材质确实存在的属性
    prop_set = set([p.lower() for p in props])
    for c in common:
        if c.lower() in prop_set:
            add_prop(c)

    # 最后加入所有看起来像贴图槽的实际属性
    for p in props:
        low = p.lower()
        if ("map" in low or "tex" in low or "texture" in low or "bump" in low or "displace" in low or low.startswith("mapm")):
            add_prop(p)

    return result


def pbr_create_tex_for_report_item(item):
    path = item.get("path", "")
    label = safe_str(item.get("channel", ""), "").lower()
    normal = ("normal" in label or "bump" in label)
    return create_normal_texmap(path) if normal else create_bitmap_texmap(path)


def pbr_try_set_specific_slot(mat, prop_name, tex, channel_label=""):
    if not mat or not prop_name or tex is None:
        return False
    if channel_label:
        return set_material_slot_verified_for_channel(mat, prop_name, tex, channel_label)
    return set_material_slot_verified(mat, prop_name, tex)


def baked_ao_multiply_output_path(base_path, ao_path):
    """
    生成 AO独立通道 的新贴图路径。不会修改原贴图。
    """
    base_path = safe_abs_texture_path(base_path)
    folder = os.path.join(os.path.dirname(base_path), "_ISS_Generated_AO_Multiply")
    try:
        os.makedirs(folder, exist_ok=True)
    except Exception:
        folder = os.path.dirname(base_path)

    base_name = clean_name_part(os.path.splitext(os.path.basename(base_path))[0], "BaseColor")
    out_name = base_name + "_AO_Multiply.png"
    out_path = os.path.join(folder, out_name)

    root, ext = os.path.splitext(out_path)
    i = 1
    while os.path.exists(out_path):
        out_path = "{}_{:03d}{}".format(root, i, ext)
        i += 1
    return out_path


def bake_ao_multiply_texture(base_path, ao_path):
    """
    更可靠的 AO 处理：
    不再依赖 Max / V-Ray 的 Composite/Mix 节点连接属性。
    直接把 BaseColor 和 AO 烘焙成一张新图：
        NewBaseColor = BaseColor * AO
    原图不修改，新图输出到 _ISS_Generated_AO_Multiply。
    """
    base_path = safe_abs_texture_path(base_path)
    ao_path = safe_abs_texture_path(ao_path)

    if not base_path or not os.path.exists(base_path):
        return "", "BaseColor贴图不存在"
    if not ao_path or not os.path.exists(ao_path):
        return "", "AO贴图不存在"

    out_path = baked_ao_multiply_output_path(base_path, ao_path)

    # 方案1：Pillow，最可控。
    try:
        from PIL import Image, ImageChops
        with Image.open(base_path) as base_img:
            base_mode = base_img.mode
            base_rgba = base_img.convert("RGBA")
            with Image.open(ao_path) as ao_img:
                # AO 使用灰度；如果尺寸不一致，缩放到 BaseColor 尺寸。
                ao_l = ao_img.convert("L")
                if ao_l.size != base_rgba.size:
                    try:
                        resample = Image.Resampling.LANCZOS
                    except Exception:
                        resample = Image.LANCZOS
                    ao_l = ao_l.resize(base_rgba.size, resample)

                r, g, b, a = base_rgba.split()
                r2 = ImageChops.multiply(r, ao_l)
                g2 = ImageChops.multiply(g, ao_l)
                b2 = ImageChops.multiply(b, ao_l)
                out_img = Image.merge("RGBA", (r2, g2, b2, a))
                out_img.save(out_path)

        if os.path.exists(out_path):
            return out_path, "已保留 AO 独立贴图"
    except Exception as e:
        pillow_err = str(e)
    else:
        pillow_err = ""

    # 方案2：ImageMagick 备用。
    try:
        exe = imagemagick_executable()
        if exe:
            cmd = [
                exe,
                base_path,
                "(",
                ao_path,
                "-colorspace", "Gray",
                "-resize", "%dx%d!" % safe_image_size(base_path)[:2],
                ")",
                "-compose", "multiply",
                "-composite",
                out_path
            ]
            # 有些 Windows shell 对括号参数不友好；subprocess 列表通常可以。
            p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=180)
            if p.returncode == 0 and os.path.exists(out_path):
                return out_path, "已用ImageMagick保留 AO 独立贴图"
            # 备用命令：让 IM 自己处理尺寸。
            cmd2 = [exe, base_path, ao_path, "-compose", "multiply", "-composite", out_path]
            p2 = subprocess.run(cmd2, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=180)
            if p2.returncode == 0 and os.path.exists(out_path):
                return out_path, "已用ImageMagick保留 AO 独立贴图"
    except Exception:
        pass

    return "", "无法烘焙AO合成贴图；Pillow/ImageMagick不可用或处理失败{}".format("：" + pillow_err if pillow_err else "")


def create_ao_multiply_texmap(base_path, ao_path):
    """
    返回一张已经烘焙好的 AO独立通道 Bitmap 贴图节点。
    这样能保证 AO 和 Color 两张贴图都真正参与最终 BaseColor，而不是依赖不稳定的 Max 合成节点属性。
    """
    out_path, msg = bake_ao_multiply_texture(base_path, ao_path)
    if not out_path:
        return None, msg

    tex = create_bitmap_texmap(out_path)
    if tex is None:
        return None, "AO合成图已生成，但Bitmap贴图节点创建失败：{}".format(out_path)

    return tex, msg + "：{}".format(out_path)

def ao_dedicated_prop_names():
    return [
        "ao_map", "AO_map", "ambient_occlusion_map", "ambientOcclusionMap",
        "occlusion_map", "aoTexmap", "ambientOcclusionTexmap", "occlusionTexmap",
        "texmap_ao", "texmap_ambient_occlusion"
    ]


def create_normal_texmap_variants(path):
    """
    PBR Metal/Rough 的 Normal 槽在不同 Max 版本中可能接受：
    - Bitmap 直连；
    - Normal_Bump 包装；
    - VRayNormalMap 包装。
    这里全部准备好，按顺序尝试。
    """
    variants = []
    bitmap = create_bitmap_texmap(path)
    if bitmap is not None:
        variants.append(("Bitmap直连", bitmap))

    # Max Normal_Bump
    for cls_name in ["Normal_Bump", "NormalBump"]:
        try:
            cls = getattr(rt, cls_name)
            nm = cls()
            if nm and bitmap is not None:
                if safe_set_attr_any(nm, ["normal_map", "normalMap", "normal", "map", "texmap"], bitmap):
                    variants.append((cls_name, nm))
        except Exception:
            pass

    # V-Ray Normal Map
    for cls_name in ["VRayNormalMap", "VrayNormalMap", "VRayNormalTex"]:
        try:
            cls = getattr(rt, cls_name)
            nm = cls()
            if nm and bitmap is not None:
                if safe_set_attr_any(nm, ["normal_map", "normalMap", "map", "texmap", "normal_texmap"], bitmap):
                    variants.append((cls_name, nm))
        except Exception:
            pass

    # 去重句柄
    result = []
    seen = set()
    for label, tex in variants:
        try:
            h = int(rt.getHandleByAnim(tex))
        except Exception:
            h = id(tex)
        if h not in seen:
            seen.add(h)
            result.append((label, tex))
    return result


def pbr_normal_slot_candidates(mat, target_mode, channel_label):
    base = [
        "normal_map", "normalMap", "normal_texture", "normalTexture",
        "normal_texmap", "normalTexmap", "bump_map", "bumpMap",
        "texmap_bump", "bump_texmap", "bumpTexture", "bump_texture",
        "normal", "bump"
    ]

    # 用户学习/手动规则优先
    learned = learned_pbr_slots(target_mode, mat, channel_label)

    # 当前材质真实属性
    detected = pbr_material_map_candidate_props(mat, channel_label)

    result = []
    for seq in (learned, base, detected):
        for p in seq:
            if p and p not in result:
                result.append(p)
    return result


def connect_pbr_normal_robust(mat, target_mode, entry, channel_label, path):
    """
    专门处理 PBR Metal/Rough 法线：
    - 尝试 Bitmap直连、Normal_Bump、VRayNormalMap；
    - 尝试多个 normal/bump 槽位；
    - 成功后学习该槽位，下次自动用。
    """
    if not path:
        return False, "", "", "缺少法线贴图路径"

    variants = create_normal_texmap_variants(path)
    if not variants:
        return False, "", "", "法线贴图节点创建失败"

    slots = pbr_normal_slot_candidates(mat, target_mode, channel_label)

    # 当前条目的手动覆盖优先
    overrides = entry.get("slot_overrides", {})
    override_prop = safe_str(overrides.get(pbr_slot_override_key(target_mode, channel_label), ""), "")
    if override_prop and override_prop not in slots:
        slots.insert(0, override_prop)

    for prop in slots:
        for variant_name, tex in variants:
            if set_material_slot_verified_for_channel(mat, prop, tex, channel_label):
                learn_pbr_slot(target_mode, mat, channel_label, prop)
                return True, prop, variant_name, ""

    return False, "", "", "PBR法线多策略连接失败"


def pbrset_required_connection_channels(entry, normal_preference="DirectX / DX（UE常用）", gloss_mode="反相生成Roughness副本"):
    """
    返回创建材质时应该真正接入材质槽的通道。
    规则：
    - Preview 不接；
    - NormalDX/NormalGL 按偏好二选一，没选中的那张允许不用；
    - ORM 目前不会自动拆通道，所以不列为可自动连接，前置检查会提示手动拆分/映射；
    - Glossiness 如果没有 Roughness，按用户选择决定是否必须接入/反相。
    """
    channels = entry.get("channels", {})
    required = []

    for ch in ["BaseColor", "Roughness", "Metallic", "AO", "Height", "Displacement", "Opacity", "Emissive", "Specular"]:
        if ch in channels:
            required.append(ch)

    chosen_normal = preferred_normal_channel(channels, normal_preference)
    if chosen_normal:
        required.append(chosen_normal)

    if "Roughness" not in channels and "Glossiness" in channels:
        gm = safe_str(gloss_mode, "").lower()
        if "跳过" in gm:
            required.append("Glossiness")
        elif "直接" in gm:
            required.append("Glossiness")
        else:
            required.append("Glossiness->Roughness")

    # 去重并保持顺序
    result = []
    seen = set()
    for ch in required:
        if ch not in seen:
            seen.add(ch)
            result.append(ch)
    return result


def pbrset_report_unconnected_text(report):
    if not report:
        return ""
    items = report.get("unconnected", [])
    if not items:
        return ""
    parts = []
    for item in items:
        ch = item.get("channel", "")
        reason = item.get("reason", "")
        parts.append("{}({})".format(ch, reason) if reason else ch)
    return "，".join(parts)


def create_material_from_pbr_texture_set(entry, target_mode="PBR Material Metal/Rough", prefix="M_PBR", normal_preference="DirectX / DX（UE常用）", gloss_mode="反相生成Roughness副本"):
    channels = dict(entry.get("channels", {}))
    notes = []
    connected = []
    unconnected = []
    required_channels = pbrset_required_connection_channels(entry, normal_preference, gloss_mode)
    mat = None
    used_class = ""

    for cls_name in target_material_class_candidates(target_mode):
        mat = try_create_material_by_class(cls_name)
        if is_valid_material(mat):
            used_class = cls_name
            break

    if not is_valid_material(mat):
        entry["_last_connection_report"] = dict(
            ok=False,
            required=required_channels,
            connected=[],
            unconnected=[dict(channel="Material", reason="无法创建目标材质", path="")]
        )
        return None, ["无法创建目标材质"]

    name_base = pbrset_preferred_material_base_name(entry)
    name = "{}_{}".format(clean_name_part(prefix, "M_PBR"), clean_name_part(name_base, "PBR_Material"))
    try:
        mat.name = name
    except Exception:
        pass

    configure_renderer_material_for_pbr(mat, target_mode)

    if used_class and not material_class_matches_target(used_class, target_mode):
        notes.append("目标类回退为 {}".format(used_class))

    # 默认数值
    safe_set_attr_any(mat, ["metalness", "metallic", "metalness_value", "metallic_value"], 0.0)
    safe_set_attr_any(mat, ["roughness", "roughness_value"], 0.55)

    def set_map(channel, prop_names, normal=False, note_label=None, source_channel=None):
        source_channel = source_channel or channel
        label = note_label or channel
        path = channels.get(channel)
        if not path:
            return False

        tex = create_normal_texmap(path) if normal else create_bitmap_texmap(path)
        if tex is None:
            notes.append("{}：贴图节点创建失败".format(label))
            unconnected.append(dict(channel=label, reason="贴图节点创建失败", path=path))
            return False

        # 用户手动指定的材质槽优先。这样第一次配置后，下次重新创建会自动套用。
        overrides = entry.get("slot_overrides", {})
        override_key = pbr_slot_override_key(target_mode, label)
        override_prop = safe_str(overrides.get(override_key, ""), "")
        if override_prop:
            if pbr_try_set_specific_slot(mat, override_prop, tex):
                learn_pbr_slot(target_mode, mat, label, override_prop)
                notes.append("{}：已连接到手动槽 {}".format(label, override_prop))
                connected.append(dict(channel=label, path=path, prop=override_prop))
                return True
            else:
                notes.append("{}：手动槽 {} 写入失败".format(label, override_prop))

        # 全局学习规则：以前用户修过同类材质/通道，下次直接先试
        for learned_prop in learned_pbr_slots(target_mode, mat, label):
            if pbr_try_set_specific_slot(mat, learned_prop, tex):
                notes.append("{}：已按学习规则连接到 {}".format(label, learned_prop))
                connected.append(dict(channel=label, path=path, prop=learned_prop))
                return True

        ok, used_prop = safe_set_material_map_result(mat, prop_names, tex, label)
        if ok:
            if used_prop:
                learn_pbr_slot(target_mode, mat, label, used_prop)
            notes.append("{}：已连接到 {}".format(label, used_prop or "材质槽"))
            connected.append(dict(channel=label, path=path, prop=used_prop))
            return True

        notes.append("{}：未找到可写入的材质槽".format(label))
        unconnected.append(dict(channel=label, reason="未找到可写入的材质槽", path=path))
        return False

    set_map("BaseColor", ["base_color_map", "baseColorMap", "base_color_texture", "baseColor_map", "baseColor_texture", "albedo_map", "albedoMap", "diffuse_map", "diffuseMap", "texmap_diffuse", "color_map", "texmap_color", "mapM1"], note_label="BaseColor")
    set_map("Roughness", ["roughness_map", "roughnessMap", "roughness_texture", "texmap_roughness", "roughness_texmap", "mapM4"], note_label="Roughness")

    if "Roughness" not in channels and "Glossiness" in channels:
        gm = safe_str(gloss_mode, "").lower()
        if "跳过" in gm:
            notes.append("Glossiness按用户选项跳过")
            unconnected.append(dict(channel="Glossiness", reason="用户选择跳过，但该贴图已映射", path=channels.get("Glossiness", "")))
        elif "直接" in gm:
            set_map("Glossiness", ["roughness_map", "roughnessMap", "texmap_roughness"], note_label="Glossiness直接接Roughness")
            notes.append("Glossiness未反相，需确认效果")
        else:
            inv_path, inv_note = invert_glossiness_texture_to_roughness(channels.get("Glossiness"))
            if inv_path:
                channels["Roughness"] = inv_path
                set_map("Roughness", ["roughness_map", "roughnessMap", "roughness_texture", "texmap_roughness", "roughness_texmap", "mapM4"], note_label="Roughness(由Glossiness反相)")
                notes.append(inv_note)
            else:
                # 反相失败时不强行接错，退回提示
                notes.append(inv_note + "；Glossiness未接入Roughness")
                unconnected.append(dict(channel="Glossiness->Roughness", reason=inv_note, path=channels.get("Glossiness", "")))

    set_map("Metallic", ["metalness_map", "metallic_map", "metalnessMap", "metallicMap", "metalness_texture", "metallic_texture", "texmap_metalness", "mapM5"], note_label="Metallic")
    chosen_normal = preferred_normal_channel(channels, normal_preference)
    if chosen_normal:
        normal_done = False
        if material_target_mode_key(target_mode) == "pbr_metalrough":
            ok_n, prop_n, variant_n, reason_n = connect_pbr_normal_robust(mat, target_mode, entry, chosen_normal, channels.get(chosen_normal, ""))
            if ok_n:
                notes.append("{}：PBR法线已用{}连接到{}".format(chosen_normal, variant_n, prop_n))
                connected.append(dict(channel=chosen_normal, path=channels.get(chosen_normal, ""), prop=prop_n))
                normal_done = True
            else:
                notes.append("{}：{}".format(chosen_normal, reason_n))

        if not normal_done:
            set_map(chosen_normal, ["normal_map", "normalMap", "normal_texture", "normalTexture", "normal_texmap", "normalTexmap", "bump_map", "bumpMap", "texmap_bump", "bump_texmap", "bumpTexture", "bump_texture"], normal=True, note_label=chosen_normal)

        if "NormalDX" in channels and "NormalGL" in channels:
            notes.append("检测到DX/GL两张法线，已按偏好选择：{}".format(chosen_normal))
    # AO 特殊处理：
    # 1) 有独立 AO 槽时接独立 AO 槽；
    # 2) 没有独立 AO 槽时，不烘焙、不接 Diffuse/BaseColor；
    # 3) 作为 UE 独立通道保留，连接对应表会显示它，方便到 UE 中单独连接/调参。
    if "AO" in channels:
        ao_connected = set_map("AO", ao_dedicated_prop_names(), note_label="AO")
        if not ao_connected:
            ao_path = channels.get("AO", "")
            notes.append("AO：当前目标材质无可靠独立AO槽，已保留为UE独立通道，不接入Max材质")
            connected.append(dict(channel="AO(UE保留)", path=ao_path, prop="UE独立通道 / 未接Max材质"))
            # 移除 set_map 失败时产生的 AO 未接入记录，因为这是有意保留，不是失败。
            unconnected[:] = [u for u in unconnected if u.get("channel") != "AO"]
    set_map("Height", ["height_map", "heightMap", "displacement_map", "displacementMap", "bump_map", "bumpMap"], note_label="Height")
    set_map("Displacement", ["displacement_map", "displacementMap", "height_map", "heightMap"], note_label="Displacement")
    set_map("Opacity", ["opacity_map", "opacityMap", "alpha_map", "cutout_map", "cutoutMap", "transparency_map", "texmap_opacity", "texmap_cutout", "mapM12", "mapM9"], note_label="Opacity")
    set_map("Emissive", ["emission_color_map", "emissive_map", "emission_map", "emit_color_map", "self_illum_map", "selfIllumMap", "mapM17", "mapM16"], note_label="Emissive")
    set_map("Specular", ["specular_map", "specularMap", "reflection_map", "refl_map", "refl_color_map", "mapM3"], note_label="Specular")

    if "ORM" in channels:
        notes.append("检测到Packed ORM/ARM/RMA，未自动拆分通道，请人工检查")
        unconnected.append(dict(channel="ORM", reason="Packed贴图未自动拆分", path=channels.get("ORM", "")))

    # 检查"应该连接"的通道是否都连接了。
    connected_labels = set([safe_str(x.get("channel", ""), "") for x in connected])
    for ch in required_channels:
        # Glossiness->Roughness 成功时连接标签是 Roughness(由Glossiness反相)
        if ch == "Glossiness->Roughness":
            ok = any("Roughness" in label and "Glossiness" in label for label in connected_labels)
        elif ch == "Glossiness":
            ok = any("Glossiness" in label for label in connected_labels)
        elif ch == "AO":
            ok = any(label == "AO" or label.startswith("AO") for label in connected_labels)
        else:
            ok = any(label == ch or label.startswith(ch) for label in connected_labels)
        if not ok:
            # 避免重复记录
            if not any(x.get("channel") == ch for x in unconnected):
                unconnected.append(dict(channel=ch, reason="已映射但未成功接入材质", path=channels.get(ch, "")))

    entry["_last_connection_report"] = dict(
        ok=(len(unconnected) == 0),
        required=required_channels,
        connected=connected,
        unconnected=unconnected
    )

    if unconnected:
        notes.append("未全部接入：{}".format(pbrset_report_unconnected_text(entry.get("_last_connection_report"))))

    return mat, notes


# ============================================================
# 预览对话框
# ============================================================

class PBRConnectionReportDialog(QtWidgets.QDialog):
    def __init__(self, mat, entry, report, parent=None):
        super(PBRConnectionReportDialog, self).__init__(parent)
        self.setWindowTitle("PBR材质连接报告 - {}".format(get_material_name(mat) if is_valid_material(mat) else entry.get("name", "")))
        self.resize(940, 560)

        layout = QtWidgets.QVBoxLayout(self)
        ok = bool((report or {}).get("ok", False))
        title = QtWidgets.QLabel("连接结果：{}".format("全部成功" if ok else "有贴图没有真正接入材质槽"))
        title.setObjectName("previewHint")
        layout.addWidget(title)

        self.table = QtWidgets.QTableWidget()
        connected = list((report or {}).get("connected", []))
        unconnected = list((report or {}).get("unconnected", []))
        rows = []
        for x in connected:
            rows.append(("成功", x.get("channel", ""), x.get("path", ""), x.get("prop", ""), ""))
        for x in unconnected:
            rows.append(("未接入", x.get("channel", ""), x.get("path", ""), "", x.get("reason", "")))

        self.table.setColumnCount(5)
        self.table.setRowCount(len(rows))
        self.table.setHorizontalHeaderLabels(["状态", "通道", "贴图", "材质槽", "原因"])

        for r, row in enumerate(rows):
            for c, val in enumerate(row):
                item = QtWidgets.QTableWidgetItem(safe_str(val, ""))
                if c == 2:
                    item.setToolTip(safe_str(val, ""))
                    item.setText(os.path.basename(safe_str(val, "")))
                self.table.setItem(r, c, item)

        try:
            self.table.horizontalHeader().setStretchLastSection(True)
            self.table.resizeColumnsToContents()
        except Exception:
            pass

        layout.addWidget(self.table, 1)

        buttons = QtWidgets.QDialogButtonBox(QtWidgets.QDialogButtonBox.Ok)
        try:
            buttons.button(QtWidgets.QDialogButtonBox.Ok).setText("知道了")
        except Exception:
            pass
        buttons.accepted.connect(self.accept)
        layout.addWidget(buttons)


class PBRConnectionTableDialog(QtWidgets.QDialog):
    """
    成功/失败都能看的"通道-贴图-材质槽"对应表。
    用户可以在这里修改已经接上的槽位，也可以给未接上的贴图指定槽位。
    """
    def __init__(self, mat, entry, report, target_mode, parent=None):
        super(PBRConnectionTableDialog, self).__init__(parent)
        self.mat = mat
        self.entry = entry
        self.report = report or {}
        self.target_mode = target_mode
        self.result = []
        self.setWindowTitle("PBR连接对应表 - {}".format(get_material_name(mat) if is_valid_material(mat) else entry.get("name", "")))
        self.resize(1180, 680)

        layout = QtWidgets.QVBoxLayout(self)
        title = QtWidgets.QLabel("检查每个通道对应哪张贴图、接到了哪个材质槽。发现错了可以直接改\"改为材质槽\"，应用后会重新写入并验证。")
        title.setObjectName("previewHint")
        try:
            title.setWordWrap(True)
        except Exception:
            pass
        layout.addWidget(title)

        connected = list(self.report.get("connected", []))
        unconnected = list(self.report.get("unconnected", []))
        rows = []
        for x in connected:
            rows.append(dict(status="成功", channel=x.get("channel", ""), path=x.get("path", ""), prop=x.get("prop", ""), reason=""))
        for x in unconnected:
            rows.append(dict(status="未接入", channel=x.get("channel", ""), path=x.get("path", ""), prop="", reason=x.get("reason", "")))

        self.table = QtWidgets.QTableWidget()
        self.table.setColumnCount(7)
        self.table.setRowCount(len(rows))
        self.table.setHorizontalHeaderLabels(["状态", "通道", "贴图", "当前材质槽", "改为材质槽", "原因", "预览"])
        self.combos = []

        for row, data in enumerate(rows):
            status = safe_str(data.get("status", ""), "")
            channel = safe_str(data.get("channel", ""), "")
            path = safe_abs_texture_path(data.get("path", ""))
            current_prop = safe_str(data.get("prop", ""), "")
            reason = safe_str(data.get("reason", ""), "")

            self.table.setItem(row, 0, QtWidgets.QTableWidgetItem(status))
            self.table.setItem(row, 1, QtWidgets.QTableWidgetItem(channel))

            file_item = QtWidgets.QTableWidgetItem(os.path.basename(path))
            file_item.setToolTip(path)
            self.table.setItem(row, 2, file_item)

            self.table.setItem(row, 3, QtWidgets.QTableWidgetItem(current_prop or "-"))

            combo = QtWidgets.QComboBox()
            combo.addItem("不修改", "")
            props = pbr_material_map_candidate_props(mat, channel)
            if current_prop and current_prop not in props:
                combo.addItem(current_prop, current_prop)
            for p in props:
                combo.addItem(p, p)
            combo.setProperty("channel", channel)
            combo.setProperty("path", path)
            combo.setProperty("current_prop", current_prop)
            self.table.setCellWidget(row, 4, combo)
            self.combos.append(combo)

            self.table.setItem(row, 5, QtWidgets.QTableWidgetItem(reason))

            icon_item = QtWidgets.QTableWidgetItem("")
            try:
                icon_item.setIcon(QtGui.QIcon(path))
            except Exception:
                pass
            self.table.setItem(row, 6, icon_item)

        try:
            self.table.setIconSize(QtCore.QSize(64, 64))
            self.table.verticalHeader().setDefaultSectionSize(72)
            self.table.setColumnWidth(0, 78)
            self.table.setColumnWidth(1, 150)
            self.table.setColumnWidth(2, 300)
            self.table.setColumnWidth(3, 190)
            self.table.setColumnWidth(4, 250)
            self.table.setColumnWidth(5, 220)
            self.table.setColumnWidth(6, 74)
            self.table.horizontalHeader().setStretchLastSection(True)
        except Exception:
            pass

        layout.addWidget(self.table, 1)

        hint = QtWidgets.QLabel("提示：这里是最终核对表。AO 不应该直接替换 Diffuse/BaseColor；如果没有独立 AO 槽，建议保留为 UE 独立通道，在 UE 材质中单独接入或参数化混合。")
        hint.setObjectName("hintLabel")
        try:
            hint.setWordWrap(True)
        except Exception:
            pass
        layout.addWidget(hint)

        buttons = QtWidgets.QDialogButtonBox(QtWidgets.QDialogButtonBox.Ok | QtWidgets.QDialogButtonBox.Cancel)
        try:
            buttons.button(QtWidgets.QDialogButtonBox.Ok).setText("应用修改")
            buttons.button(QtWidgets.QDialogButtonBox.Cancel).setText("关闭")
        except Exception:
            pass
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)
        layout.addWidget(buttons)

    def accept(self):
        self.result = []
        for combo in self.combos:
            prop = safe_str(combo.currentData(), "")
            if not prop:
                continue
            self.result.append(dict(
                channel=safe_str(combo.property("channel"), ""),
                path=safe_str(combo.property("path"), ""),
                current_prop=safe_str(combo.property("current_prop"), ""),
                prop=prop
            ))
        super(PBRConnectionTableDialog, self).accept()


class PBRMaterialSlotDialog(QtWidgets.QDialog):
    def __init__(self, mat, entry, report, target_mode, parent=None):
        super(PBRMaterialSlotDialog, self).__init__(parent)
        self.mat = mat
        self.entry = entry
        self.report = report or {}
        self.target_mode = target_mode
        self.result = []
        self.setWindowTitle("手动材质槽配置 - {}".format(get_material_name(mat)))
        self.resize(1080, 620)

        layout = QtWidgets.QVBoxLayout(self)
        title = QtWidgets.QLabel("有贴图已经映射，但没有真正接入当前材质。请为每个未接入通道选择当前材质的真实贴图槽；选择\"跳过\"则表示用户确认不用这张贴图。")
        title.setObjectName("previewHint")
        try:
            title.setWordWrap(True)
        except Exception:
            pass
        layout.addWidget(title)

        self.table = QtWidgets.QTableWidget()
        items = list(self.report.get("unconnected", []))
        self.table.setColumnCount(5)
        self.table.setRowCount(len(items))
        self.table.setHorizontalHeaderLabels(["通道", "贴图", "原因", "目标材质槽", "预览"])
        self.combos = []

        for row, item in enumerate(items):
            ch = safe_str(item.get("channel", ""), "")
            path = safe_abs_texture_path(item.get("path", ""))
            reason = safe_str(item.get("reason", ""), "")

            self.table.setItem(row, 0, QtWidgets.QTableWidgetItem(ch))
            file_item = QtWidgets.QTableWidgetItem(os.path.basename(path))
            file_item.setToolTip(path)
            self.table.setItem(row, 1, file_item)
            self.table.setItem(row, 2, QtWidgets.QTableWidgetItem(reason))

            combo = QtWidgets.QComboBox()
            combo.addItem("跳过 / 用户确认不用", "")
            props = pbr_material_map_candidate_props(mat, ch)
            for p in props:
                combo.addItem(p, p)
            combo.setProperty("channel", ch)
            combo.setProperty("path", path)
            self.table.setCellWidget(row, 3, combo)
            self.combos.append(combo)

            icon_item = QtWidgets.QTableWidgetItem("")
            try:
                icon_item.setIcon(QtGui.QIcon(path))
            except Exception:
                pass
            self.table.setItem(row, 4, icon_item)

        try:
            self.table.setIconSize(QtCore.QSize(64, 64))
            self.table.verticalHeader().setDefaultSectionSize(72)
            self.table.setColumnWidth(0, 150)
            self.table.setColumnWidth(1, 300)
            self.table.setColumnWidth(2, 260)
            self.table.setColumnWidth(3, 260)
            self.table.setColumnWidth(4, 80)
            self.table.horizontalHeader().setStretchLastSection(True)
        except Exception:
            pass

        layout.addWidget(self.table, 1)

        hint = QtWidgets.QLabel("提示：V-Ray、Corona、Physical、PBR 材质的槽位名称不同。注意 AO 不应直接抢占 Diffuse/BaseColor；无独立 AO 槽时建议保留 AO 独立贴图，在 UE 材质中单独接 AO 或与 BaseColor 参数化混合。")
        hint.setObjectName("hintLabel")
        try:
            hint.setWordWrap(True)
        except Exception:
            pass
        layout.addWidget(hint)

        buttons = QtWidgets.QDialogButtonBox(QtWidgets.QDialogButtonBox.Ok | QtWidgets.QDialogButtonBox.Cancel)
        try:
            buttons.button(QtWidgets.QDialogButtonBox.Ok).setText("应用槽位配置")
            buttons.button(QtWidgets.QDialogButtonBox.Cancel).setText("不配置，返回确认")
        except Exception:
            pass
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)
        layout.addWidget(buttons)

    def accept(self):
        self.result = []
        for combo in self.combos:
            ch = safe_str(combo.property("channel"), "")
            path = safe_str(combo.property("path"), "")
            prop = safe_str(combo.currentData(), "")
            self.result.append(dict(channel=ch, path=path, prop=prop))
        super(PBRMaterialSlotDialog, self).accept()


class PBRTextureMappingDialog(QtWidgets.QDialog):
    def __init__(self, entry, parent=None):
        super(PBRTextureMappingDialog, self).__init__(parent)
        self.entry = entry
        self.result_mapping = []
        self.setWindowTitle("PBR贴图手动映射 - {}".format(entry.get("name", "PBR_Material")))
        self.resize(980, 620)

        layout = QtWidgets.QVBoxLayout(self)
        title = QtWidgets.QLabel("把文件夹里的每张贴图指定到正确通道。自动识别不准或没有识别到时，可以在这里手动修正。")
        title.setObjectName("previewHint")
        try:
            title.setWordWrap(True)
        except Exception:
            pass
        layout.addWidget(title)

        self.table = QtWidgets.QTableWidget()
        self.table.setColumnCount(4)
        self.table.setHorizontalHeaderLabels(["预览", "文件名", "当前/建议通道", "手动指定通道"])

        files = pbrset_all_texture_files(entry)
        self.table.setRowCount(len(files))
        self.combos = []

        for row, path in enumerate(files):
            icon_item = QtWidgets.QTableWidgetItem("")
            try:
                icon_item.setIcon(QtGui.QIcon(path))
            except Exception:
                pass
            self.table.setItem(row, 0, icon_item)

            name_item = QtWidgets.QTableWidgetItem(os.path.basename(path))
            name_item.setToolTip(path)
            self.table.setItem(row, 1, name_item)

            current = pbrset_channel_for_file(entry, path)
            current_item = QtWidgets.QTableWidgetItem(current)
            current_item.setToolTip(path)
            self.table.setItem(row, 2, current_item)

            combo = QtWidgets.QComboBox()
            combo.addItems(PBR_MANUAL_CHANNELS)
            idx = combo.findText(current)
            combo.setCurrentIndex(idx if idx >= 0 else 0)
            combo.setProperty("path", path)
            self.table.setCellWidget(row, 3, combo)
            self.combos.append(combo)

        try:
            self.table.setIconSize(QtCore.QSize(64, 64))
            self.table.verticalHeader().setDefaultSectionSize(72)
            self.table.setColumnWidth(0, 76)
            self.table.setColumnWidth(1, 360)
            self.table.setColumnWidth(2, 160)
            self.table.setColumnWidth(3, 220)
            self.table.horizontalHeader().setStretchLastSection(True)
        except Exception:
            pass

        layout.addWidget(self.table, 1)

        hint = QtWidgets.QLabel("提示：Preview 只用于列表缩略图，不会接入材质。NormalDX / NormalGL 可以同时保留，创建材质时再按法线偏好选择其中一张。")
        hint.setObjectName("hintLabel")
        try:
            hint.setWordWrap(True)
        except Exception:
            pass
        layout.addWidget(hint)

        buttons = QtWidgets.QDialogButtonBox(QtWidgets.QDialogButtonBox.Ok | QtWidgets.QDialogButtonBox.Cancel)
        try:
            buttons.button(QtWidgets.QDialogButtonBox.Ok).setText("应用映射")
            buttons.button(QtWidgets.QDialogButtonBox.Cancel).setText("取消")
        except Exception:
            pass
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)
        layout.addWidget(buttons)

    def accept(self):
        self.result_mapping = []
        for combo in self.combos:
            path = safe_str(combo.property("path"), "")
            channel = combo.currentText()
            self.result_mapping.append((path, channel))
        super(PBRTextureMappingDialog, self).accept()


class RenamePreviewDialog(QtWidgets.QDialog):
    def __init__(self, plan, title="重命名前预览", parent=None):
        super(RenamePreviewDialog, self).__init__(parent)
        self.plan = plan
        self.setWindowTitle(title)
        self.resize(860, 560)
        layout = QtWidgets.QVBoxLayout(self)
        label = QtWidgets.QLabel("请确认本次重命名计划。只有状态为\"可执行\"的条目会被修改。")
        label.setObjectName("previewHint")
        layout.addWidget(label)
        self.table = QtWidgets.QTableWidget()
        self.table.setColumnCount(4)
        self.table.setHorizontalHeaderLabels(["状态", "旧名称", "新名称", "说明"])
        self.table.setRowCount(len(plan))
        for r, item in enumerate(plan):
            status = "可执行" if item.get("ok") else "跳过"
            values = [status, item.get("old", ""), item.get("new", ""), item.get("note", "")]
            for c, text in enumerate(values):
                cell = QtWidgets.QTableWidgetItem(safe_str(text, ""))
                if c == 0:
                    try:
                        cell.setTextAlignment(QT_ALIGN_CENTER)
                    except Exception:
                        pass
                self.table.setItem(r, c, cell)
        try:
            self.table.horizontalHeader().setStretchLastSection(True)
            self.table.resizeColumnsToContents()
            self.table.setAlternatingRowColors(True)
        except Exception:
            pass
        layout.addWidget(self.table)
        buttons = QtWidgets.QDialogButtonBox(QtWidgets.QDialogButtonBox.Ok | QtWidgets.QDialogButtonBox.Cancel)
        try:
            buttons.button(QtWidgets.QDialogButtonBox.Ok).setText("确认执行")
            buttons.button(QtWidgets.QDialogButtonBox.Cancel).setText("取消")
        except Exception:
            pass
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)
        layout.addWidget(buttons)


class PBRConversionPreviewDialog(QtWidgets.QDialog):
    def __init__(self, plan, parent=None):
        super(PBRConversionPreviewDialog, self).__init__(parent)
        self.plan = plan
        self.setWindowTitle("材质标准化前预览")
        self.resize(1080, 640)
        try:
            self.setMinimumSize(900, 520)
            self.setMaximumHeight(760)
        except Exception:
            pass

        layout = QtWidgets.QVBoxLayout(self)
        total = len(plan)
        actionable = len([p for p in plan if p.get("ok")])
        skipped = total - actionable
        max_preview_rows = 350
        shown_plan = plan[:max_preview_rows]

        label = QtWidgets.QLabel(
            "请确认材质标准化计划。总数 {}，可执行 {}，跳过 {}。{}"
            .format(
                total,
                actionable,
                skipped,
                "列表较长，仅显示前 {} 条；完整过程会在转换列表和日志中实时显示。".format(max_preview_rows)
                if total > max_preview_rows else
                "程序贴图会尽量简化并保留外部贴图。"
            )
        )
        label.setObjectName("previewHint")
        try:
            label.setWordWrap(True)
            label.setMaximumHeight(64)
        except Exception:
            pass
        layout.addWidget(label)

        self.table = QtWidgets.QTableWidget()
        self.table.setColumnCount(7)
        self.table.setHorizontalHeaderLabels(["状态", "材质", "类型", "角色", "判断", "即将执行", "说明"])
        self.table.setRowCount(len(shown_plan))
        for r, item in enumerate(shown_plan):
            values = [
                "可执行" if item.get("ok") else "跳过",
                item.get("old", ""),
                item.get("type", ""),
                item.get("role", ""),
                item.get("judge", ""),
                item.get("action", ""),
                item.get("note", "")
            ]
            for c, value in enumerate(values):
                cell = QtWidgets.QTableWidgetItem(safe_str(value, ""))
                if c == 0:
                    try: cell.setTextAlignment(QT_ALIGN_CENTER)
                    except Exception: pass
                self.table.setItem(r, c, cell)
        try:
            self.table.setAlternatingRowColors(True)
            self.table.horizontalHeader().setStretchLastSection(True)
            # 不再 resizeColumnsToContents；很多材质名很长时会把对话框撑得很难用。
            self.table.setColumnWidth(0, 72)
            self.table.setColumnWidth(1, 210)
            self.table.setColumnWidth(2, 150)
            self.table.setColumnWidth(3, 72)
            self.table.setColumnWidth(4, 110)
            self.table.setColumnWidth(5, 230)
        except Exception:
            pass

        layout.addWidget(self.table, 1)

        summary = QtWidgets.QLabel("提示：转换过程会分步执行，进度条和列表状态会实时更新；可以在材质标准化页使用\"停止标准化\"。")
        summary.setObjectName("hintLabel")
        try:
            summary.setWordWrap(True)
            summary.setMaximumHeight(52)
        except Exception:
            pass
        layout.addWidget(summary)

        buttons = QtWidgets.QDialogButtonBox(QtWidgets.QDialogButtonBox.Ok | QtWidgets.QDialogButtonBox.Cancel)
        try:
            buttons.button(QtWidgets.QDialogButtonBox.Ok).setText("确认标准化")
            buttons.button(QtWidgets.QDialogButtonBox.Cancel).setText("取消")
        except Exception:
            pass
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)
        layout.addWidget(buttons)


def dialog_accepted(dialog):
    try:
        return dialog.exec_() == QtWidgets.QDialog.Accepted
    except Exception:
        return dialog.exec() == QtWidgets.QDialog.DialogCode.Accepted



# ============================================================
# V10：配置、备份、体检、修复预览
# ============================================================

def user_documents_dir():
    try:
        doc = os.path.join(os.path.expanduser("~"), "Documents")
        if os.path.isdir(doc):
            return doc
    except Exception:
        pass
    try:
        return os.path.expanduser("~")
    except Exception:
        return "."


def studio_config_path():
    return os.path.join(user_documents_dir(), "InteriorSceneStudioPro_config.json")


def current_max_file_on_disk():
    try:
        folder = safe_str(rt.maxFilePath, "")
        name = safe_str(rt.maxFileName, "")
        if not folder or not name:
            return ""
        return os.path.join(folder, name)
    except Exception:
        return ""


def backup_current_max_file_copy(max_keep=3):
    """
    复制当前磁盘上的 .max 文件作为备份。
    max_keep：同一场景最多保留多少个备份；0 表示不限制。
    注意：这个方式不会自动保存当前未保存的场景改动，避免改变当前 Max 文件指向。
    """
    try:
        src = current_max_file_on_disk()
        if not src:
            return False, "当前场景还没有保存为 .max 文件，无法复制备份。请先保存文件。"
        if not os.path.exists(src):
            return False, "找不到当前 .max 文件：{}".format(src)

        backup_dir = os.path.join(os.path.dirname(src), "_InteriorSceneStudio_Backups")
        if not os.path.isdir(backup_dir):
            os.makedirs(backup_dir)

        base, ext = os.path.splitext(os.path.basename(src))
        clean_base = clean_name_part(base, "scene")
        ext = ext or ".max"
        stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        dst = os.path.join(backup_dir, "{}_backup_{}{}".format(clean_base, stamp, ext))
        shutil.copy2(src, dst)

        removed = prune_scene_backups(backup_dir, clean_base, ext, max_keep)
        if removed:
            return True, "{}；已清理旧备份 {} 个".format(dst, removed)
        return True, dst
    except Exception:
        return False, status_text_for_exception("备份失败")


def scene_backup_files(backup_dir, clean_base, ext):
    """返回当前场景对应的备份文件，按修改时间从旧到新排序。"""
    result = []
    try:
        prefix = "{}_backup_".format(clean_base)
        for name in os.listdir(backup_dir):
            if not name.startswith(prefix):
                continue
            if not name.lower().endswith(ext.lower()):
                continue
            path = os.path.join(backup_dir, name)
            if os.path.isfile(path):
                result.append(path)
        result.sort(key=lambda p: os.path.getmtime(p))
    except Exception:
        pass
    return result


def prune_scene_backups(backup_dir, clean_base, ext, max_keep=3):
    """只清理当前场景同名前缀的备份，不碰其它文件。0 表示不限制。"""
    try:
        max_keep = int(max_keep)
    except Exception:
        max_keep = 3
    if max_keep <= 0:
        return 0
    files = scene_backup_files(backup_dir, clean_base, ext)
    over = len(files) - max_keep
    if over <= 0:
        return 0
    removed = 0
    for path in files[:over]:
        try:
            os.remove(path)
            removed += 1
        except Exception:
            pass
    return removed


def planned_repair_actions(obj, fix_material=True, fix_scale=True, fix_pivot=True, skip_frozen=False):
    """
    只生成计划，不修改场景。
    """
    info = {
        "ref": obj,
        "name": safe_str(getattr(obj, "name", ""), "<无效对象>"),
        "type": get_class_name(obj),
        "issues": [],
        "actions": [],
        "ok": False,
        "note": ""
    }

    if not is_valid_node(obj):
        info["issues"] = ["无效对象"]
        info["note"] = "跳过：无效对象"
        return info

    if is_group_head(obj):
        info["issues"] = ["组对象"]
        info["note"] = "跳过：组对象不参与几何修复"
        return info

    if not is_valid_geometry(obj):
        info["issues"] = ["非几何体"]
        info["note"] = "跳过：非几何体"
        return info

    issues = detect_geometry_issues(obj)
    info["issues"] = issues[:]

    if is_special_or_proxy_node(obj):
        info["note"] = "跳过：代理 / 外链 / 散布等特殊对象，避免破坏资产"
        return info

    if skip_frozen and is_frozen(obj):
        info["note"] = "跳过：冻结对象"
        return info

    if fix_material and not object_has_material(obj):
        info["actions"].append("补随机材质")

    if fix_scale and not is_scale_100(obj):
        info["actions"].append("Reset XForm + 塌陷 + 转 Poly")

    if fix_pivot and not is_pivot_bottom_center(obj):
        info["actions"].append("轴心归底居中")

    if info["actions"]:
        info["ok"] = True
        info["note"] = "将执行 {} 项".format(len(info["actions"]))
    else:
        info["note"] = "无需要执行的修复"

    return info


def build_scene_health_rows():
    """
    生成体检报告行，不修改场景。
    """
    rows = []

    for obj in get_scene_geometry():
        issues = detect_geometry_issues(obj)
        rows.append({
            "kind": "模型",
            "name": safe_str(getattr(obj, "name", ""), ""),
            "type": get_class_name(obj),
            "layer": get_layer_name(obj),
            "issues": "，".join(issues) if issues else "无问题",
            "count": len(issues)
        })

    for grp in get_scene_groups():
        issues = []
        if is_frozen(grp):
            issues.append("冻结")
        if is_hidden(grp):
            issues.append("隐藏")
        if not is_group_open(grp):
            issues.append("关闭")
        rows.append({
            "kind": "组",
            "name": safe_str(getattr(grp, "name", ""), ""),
            "type": "Group",
            "layer": get_layer_name(grp),
            "issues": "，".join(issues) if issues else "无问题",
            "count": len(issues)
        })

    for light in get_scene_lights():
        issues = []
        if is_frozen(light):
            issues.append("冻结")
        if is_hidden(light):
            issues.append("隐藏")
        rows.append({
            "kind": "灯光",
            "name": safe_str(getattr(light, "name", ""), ""),
            "type": get_class_name(light),
            "layer": get_layer_name(light),
            "issues": "，".join(issues) if issues else "无问题",
            "count": len(issues)
        })

    for cam in get_scene_cameras():
        issues = []
        if is_frozen(cam):
            issues.append("冻结")
        if is_hidden(cam):
            issues.append("隐藏")
        rows.append({
            "kind": "相机",
            "name": safe_str(getattr(cam, "name", ""), ""),
            "type": get_class_name(cam),
            "layer": get_layer_name(cam),
            "issues": "，".join(issues) if issues else "无问题",
            "count": len(issues)
        })

    mat_entries = collect_scene_material_entries()
    seen = set()
    for entry in mat_entries:
        mat = entry.get("mat")
        if not is_valid_material(mat):
            continue
        h = get_anim_handle(mat)
        if h in seen:
            continue
        seen.add(h)
        issues = []
        name = get_material_name(mat)
        if name == "NoMat":
            issues.append("无名称")
        if entry.get("role") == "SUB":
            issues.append("子材质")
        if entry.get("role") == "MSO":
            issues.append("多维母材质")
        rows.append({
            "kind": "材质",
            "name": name,
            "type": get_class_name(mat),
            "layer": "-",
            "issues": "，".join(issues) if issues else "无问题",
            "count": len(issues)
        })

    return rows


class RepairPreviewDialog(QtWidgets.QDialog):
    def __init__(self, plan, parent=None):
        super(RepairPreviewDialog, self).__init__(parent)
        self.setWindowTitle("修复前预览")
        self.resize(980, 620)
        layout = QtWidgets.QVBoxLayout(self)

        action_count = len([p for p in plan if p.get("ok")])
        label = QtWidgets.QLabel("请确认修复计划。只有状态为\"将修复\"的条目会被处理。本次将修复 {} 个对象。".format(action_count))
        label.setObjectName("previewHint")
        layout.addWidget(label)

        self.table = QtWidgets.QTableWidget()
        self.table.setColumnCount(6)
        self.table.setHorizontalHeaderLabels(["状态", "对象", "类型", "检测问题", "即将执行", "说明"])
        self.table.setRowCount(len(plan))

        for r, item in enumerate(plan):
            status = "将修复" if item.get("ok") else "跳过"
            values = [
                status,
                item.get("name", ""),
                item.get("type", ""),
                "，".join(item.get("issues", [])) if item.get("issues") else "无问题",
                "，".join(item.get("actions", [])) if item.get("actions") else "-",
                item.get("note", "")
            ]
            for c, value in enumerate(values):
                cell = QtWidgets.QTableWidgetItem(safe_str(value, ""))
                self.table.setItem(r, c, cell)

        try:
            self.table.horizontalHeader().setStretchLastSection(True)
            self.table.resizeColumnsToContents()
            self.table.setAlternatingRowColors(True)
        except Exception:
            pass

        layout.addWidget(self.table)

        buttons = QtWidgets.QDialogButtonBox(QtWidgets.QDialogButtonBox.Ok | QtWidgets.QDialogButtonBox.Cancel)
        try:
            buttons.button(QtWidgets.QDialogButtonBox.Ok).setText("确认修复")
            buttons.button(QtWidgets.QDialogButtonBox.Cancel).setText("取消")
        except Exception:
            pass
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)
        layout.addWidget(buttons)



# ============================================================
# PBR 下载库管理
# ============================================================

def pbr_download_default_sites():
    return [
        dict(name="ambientCG", license="CC0", url="https://ambientcg.com/", note="免费PBR材质/HDRI/模型，适合室内材质库。"),
        dict(name="Poly Haven Textures", license="CC0", url="https://polyhaven.com/textures", note="免费PBR纹理，无需登录；也提供公开API。"),
        dict(name="Poly Haven API", license="CC0", url="https://api.polyhaven.com", note="Poly Haven公开API入口，适合后续扩展自动搜索。"),
        dict(name="CGBookcase", license="Free", url="https://www.cgbookcase.com/", note="免费PBR纹理，常见室内材质较多。"),
        dict(name="ShareTextures", license="Free / CC0", url="https://www.sharetextures.com/", note="免费PBR纹理和模型。请下载前查看具体授权。"),
        dict(name="3DTextures.me", license="Free / CC0", url="https://3dtextures.me/", note="免费无缝PBR和风格化贴图。请下载前查看具体授权。"),
        dict(name="TextureCan", license="Free", url="https://www.texturecan.com/", note="免费PBR纹理和模型。请下载前查看具体授权。"),
    ]


def pbr_download_sites_path():
    try:
        return os.path.join(installed_plugin_root(), "PBRDownloadSites.json")
    except Exception:
        return os.path.join(os.path.expanduser("~"), "PBRDownloadSites.json")


def pbr_default_library_dir():
    try:
        return os.path.join(os.path.expanduser("~"), "Documents", "PBR_Material_Library")
    except Exception:
        return os.getcwd()


def pbr_library_state_path():
    # 单独保存材质库目录，避免升级脚本或配置未手动加载时丢失。
    try:
        root = os.path.join(user_documents_dir(), "InteriorSceneStudioPro")
        os.makedirs(root, exist_ok=True)
        return os.path.join(root, "PBR_Download_State.json")
    except Exception:
        return os.path.join(user_documents_dir(), "InteriorSceneStudioPro_PBR_Download_State.json")


def load_saved_pbr_library_dir():
    try:
        path = pbr_library_state_path()
        if os.path.exists(path):
            with open(path, "r", encoding="utf-8") as f:
                data = json.load(f)
            d = safe_str(data.get("library_dir", ""), "")
            if d:
                return d
    except Exception:
        pass
    return pbr_default_library_dir()


def save_pbr_library_dir_state(folder):
    try:
        path = pbr_library_state_path()
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            json.dump(dict(library_dir=safe_str(folder, "")), f, ensure_ascii=False, indent=2)
        return True
    except Exception:
        return False


def pbr_extract_urls_from_text(text):
    text = safe_str(text, "")
    if not text:
        return []
    urls = re.findall(r'https?://[^\s\\\'"<>]+', text, flags=re.I)
    result = []
    seen = set()
    for u in urls:
        u = u.strip().rstrip(").,;，。；")
        if u and u.lower() not in seen:
            seen.add(u.lower())
            result.append(u)
    return result


def ai_extract_windows_paths_from_text(text):
    text = safe_str(text, "")
    if not text:
        return []
    # 识别常见 Windows 路径，避免太激进。
    candidates = re.findall(r'[A-Za-z]:\\\\[^\\n\\r\\t<>:"|?*]+', text)
    result = []
    seen = set()
    for p in candidates:
        p = p.strip().strip('"').strip("'").rstrip(".,;，。；)")
        if p and p.lower() not in seen:
            seen.add(p.lower())
            result.append(p)
    return result


def ai_first_existing_path(paths):
    for p in paths:
        try:
            if os.path.exists(p):
                return p
            folder = os.path.dirname(p)
            if folder and os.path.exists(folder):
                return folder
        except Exception:
            pass
    return ""


def find_chrome_executable():
    candidates = []
    try:
        env = os.environ
        for key in ["PROGRAMFILES", "PROGRAMFILES(X86)", "LOCALAPPDATA"]:
            base = env.get(key, "")
            if base:
                candidates.append(os.path.join(base, "Google", "Chrome", "Application", "chrome.exe"))
    except Exception:
        pass
    candidates.extend([
        r"C:\Program Files\Google\Chrome\Application\chrome.exe",
        r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe",
    ])
    for p in candidates:
        if p and os.path.exists(p):
            return p
    return ""


def open_url_in_chrome_or_browser(url):
    url = safe_str(url, "").strip()
    if not url:
        return False
    if not re.match(r"^(https?|file|chrome-extension)://", url, re.I) and not re.match(r"^chrome://", url, re.I):
        url = "https://" + url
    chrome = find_chrome_executable()
    if chrome:
        try:
            subprocess.Popen([chrome, url], shell=False)
            return True
        except Exception:
            pass
    try:
        return bool(webbrowser.open(url))
    except Exception:
        return False


def pbr_texture_file_extensions():
    return set([".jpg", ".jpeg", ".png", ".tif", ".tiff", ".tga", ".bmp", ".webp", ".exr", ".hdr", ".tx"])


def pbr_is_texture_file(path):
    try:
        return os.path.splitext(path)[1].lower() in pbr_texture_file_extensions()
    except Exception:
        return False


def pbr_folder_has_texture_files(folder):
    try:
        for root, dirs, files in os.walk(folder):
            for f in files:
                if pbr_is_texture_file(f):
                    return True
    except Exception:
        pass
    return False


def pbr_redundant_folder_info(folder):
    """
    判断是否出现常见冗余目录：目标文件夹里只有一个子文件夹，根目录没有贴图文件。
    """
    try:
        if not os.path.isdir(folder):
            return dict(redundant=False, reason="目录不存在")
        entries = [e for e in os.listdir(folder) if e not in [".DS_Store", "Thumbs.db"]]
        dirs = [e for e in entries if os.path.isdir(os.path.join(folder, e))]
        files = [e for e in entries if os.path.isfile(os.path.join(folder, e))]
        root_texture_files = [f for f in files if pbr_is_texture_file(f)]
        if len(dirs) == 1 and not root_texture_files:
            inner = os.path.join(folder, dirs[0])
            if pbr_folder_has_texture_files(inner):
                return dict(redundant=True, inner=inner, reason="目标目录外层只有一个子文件夹，贴图在子文件夹内")
        return dict(redundant=False, reason="")
    except Exception as e:
        return dict(redundant=False, reason=str(e))


def pbr_flatten_single_redundant_folder(folder):
    info = pbr_redundant_folder_info(folder)
    if not info.get("redundant"):
        return False, info.get("reason", "")
    inner = info.get("inner", "")
    if not inner or not os.path.isdir(inner):
        return False, "内部目录不存在"

    moved = 0
    try:
        for name in os.listdir(inner):
            src = os.path.join(inner, name)
            dst = os.path.join(folder, name)
            if os.path.exists(dst):
                dst = ensure_unique_path(dst)
            try:
                shutil.move(src, dst)
                moved += 1
            except Exception:
                pass
        try:
            os.rmdir(inner)
        except Exception:
            pass
        return moved > 0, "已整理冗余文件夹，移动{}项".format(moved)
    except Exception as e:
        return False, str(e)


def pbr_safe_material_folder_name(url, custom_name=""):
    if custom_name:
        return clean_name_part(custom_name, "PBR_Material")
    try:
        parsed = urllib.parse.urlparse(url)
        name = os.path.basename(parsed.path)
        name = os.path.splitext(name)[0] or parsed.netloc or "PBR_Material"
        return clean_name_part(name, "PBR_Material")
    except Exception:
        return "PBR_Material"


def pbr_download_filename_from_url(url):
    try:
        parsed = urllib.parse.urlparse(url)
        name = os.path.basename(parsed.path)
        name = urllib.parse.unquote(name)
        if not name or "." not in name:
            # 有些下载链接把文件名放在 query 参数里
            q = urllib.parse.parse_qs(parsed.query or "")
            for key in ["filename", "file", "name", "download", "asset"]:
                vals = q.get(key)
                if vals:
                    candidate = urllib.parse.unquote(vals[0])
                    if "." in candidate:
                        name = os.path.basename(candidate)
                        break
        if not name or "." not in name:
            name = "downloaded_pbr_asset.zip"
        return clean_name_part(name, "downloaded_pbr_asset.zip")
    except Exception:
        return "downloaded_pbr_asset.zip"


def pbr_material_name_from_download_file(filename):
    """
    V70：材质文件夹名优先用下载文件名去后缀。
    例如 Onyx001_2K.zip -> Onyx001_2K。
    不再把 _2K 这种规格自动删掉，因为用户明确希望保留压缩包原名。
    """
    name = safe_str(filename, "")
    name = urllib.parse.unquote(os.path.basename(name))
    base, ext = os.path.splitext(name)
    if base:
        return clean_name_part(base, "PBR_Material")
    return clean_name_part(name, "PBR_Material")


def pbr_material_name_from_url_or_file(url, filename=""):
    if filename:
        return pbr_material_name_from_download_file(filename)
    return pbr_material_name_from_download_file(pbr_download_filename_from_url(url))


def pbr_move_folder_contents(src_folder, dst_folder):
    moved = 0
    if not src_folder or not dst_folder or os.path.abspath(src_folder) == os.path.abspath(dst_folder):
        return 0
    os.makedirs(dst_folder, exist_ok=True)
    if not os.path.isdir(src_folder):
        return 0
    for name in os.listdir(src_folder):
        src = os.path.join(src_folder, name)
        dst = os.path.join(dst_folder, name)
        if os.path.exists(dst):
            dst = ensure_unique_path(dst)
        try:
            shutil.move(src, dst)
            moved += 1
        except Exception:
            pass
    try:
        os.rmdir(src_folder)
    except Exception:
        pass
    return moved


def ensure_unique_folder_path(path):
    path = safe_str(path, "")
    if not path or not os.path.exists(path):
        return path
    i = 1
    while True:
        p = "{}_{:03d}".format(path, i)
        if not os.path.exists(p):
            return p
        i += 1


def ensure_unique_path(path):
    path = safe_str(path, "")
    if not path:
        return path
    if not os.path.exists(path):
        return path
    root, ext = os.path.splitext(path)
    i = 1
    while True:
        p = "{}_{:03d}{}".format(root, i, ext)
        if not os.path.exists(p):
            return p
        i += 1


PBR_BROWSER_USER_AGENT = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"


def pbr_download_extensions():
    return [
        ".zip", ".rar", ".7z",
        ".jpg", ".jpeg", ".png", ".tif", ".tiff", ".tga", ".bmp", ".webp",
        ".exr", ".hdr", ".tx"
    ]


def pbr_normalize_extension(ext):
    ext = safe_str(ext, "").strip().lower()
    if not ext:
        return ""
    if not ext.startswith("."):
        ext = "." + ext
    if ext == ".jpeg":
        return ".jpg"
    if ext == ".tiff":
        return ".tif"
    return ext


def pbr_detect_extension(url, filename=""):
    candidates = []
    try:
        parsed = urllib.parse.urlparse(safe_str(url, ""))
        candidates.append(urllib.parse.unquote(parsed.path or ""))
        candidates.append(urllib.parse.unquote(parsed.query or ""))
    except Exception:
        candidates.append(safe_str(url, ""))
    if filename:
        candidates.insert(0, filename)

    known = sorted(pbr_download_extensions(), key=len, reverse=True)
    for c in candidates:
        low = safe_str(c, "").lower()
        for ext in known:
            if low.endswith(ext) or (ext + "?") in low or (ext + "&") in low or (ext + "%") in low or (ext + "=") in low:
                return pbr_normalize_extension(ext)
    return ""


def pbr_url_matches_extensions(url, extensions, filename="", allow_unknown=False):
    ext = pbr_detect_extension(url, filename=filename)
    if not ext:
        return bool(allow_unknown)
    selected = set([pbr_normalize_extension(e) for e in extensions or []])
    return ext in selected


def pbr_url_path_lower(url):
    try:
        parsed = urllib.parse.urlparse(url)
        path = urllib.parse.unquote(parsed.path or "")
        return path.lower()
    except Exception:
        return safe_str(url, "").lower()


def pbr_is_probably_download_url(url):
    u = safe_str(url, "").strip()
    if not u:
        return False
    low_path = pbr_url_path_lower(u)
    if any(low_path.endswith(ext) for ext in pbr_download_extensions()):
        return True
    # 一些下载链接把文件名放在 query 里
    low_all = u.lower()
    if any(ext in low_all for ext in [".zip?", ".zip&", ".jpg?", ".png?", ".exr?", ".hdr?"]):
        return True
    return False


def pbr_url_join(base_url, href):
    try:
        return urllib.parse.urljoin(base_url, href)
    except Exception:
        return href


def pbr_filename_from_content_disposition(value):
    try:
        value = safe_str(value, "")
        m = re.search(r"filename\\*=UTF-8\'\'([^;]+)", value, re.I)
        if m:
            return clean_name_part(urllib.parse.unquote(m.group(1)), "downloaded_pbr_asset.zip")
        m = re.search(r'filename="?([^";]+)"?', value, re.I)
        if m:
            return clean_name_part(urllib.parse.unquote(m.group(1)), "downloaded_pbr_asset.zip")
    except Exception:
        pass
    return ""


def pbr_material_name_from_filename(filename):
    return pbr_material_name_from_download_file(filename)


def pbr_is_supported_local_asset_file(path):
    try:
        path = safe_str(path, "").strip()
        if not path or not os.path.isfile(path):
            return False
        ext = os.path.splitext(path)[1].lower()
        return ext in set(pbr_download_extensions() + [".pdf", ".sbsar"])
    except Exception:
        return False


def pbr_is_sbsar_file(path_or_name):
    try:
        return os.path.splitext(safe_str(path_or_name, "").strip())[1].lower() == ".sbsar"
    except Exception:
        return False


def pbr_is_polyhaven_temp_download_url(url):
    try:
        parsed = urllib.parse.urlparse(safe_str(url, "").strip())
        host = safe_str(parsed.netloc, "").lower()
        path = safe_str(parsed.path, "")
        return host.endswith("polyhaven.com") and "/__download__/" in path.lower()
    except Exception:
        return False


def pbr_is_polyhaven_stable_asset_url(url):
    try:
        parsed = urllib.parse.urlparse(safe_str(url, "").strip())
        host = safe_str(parsed.netloc, "").lower()
        parts = [p for p in safe_str(parsed.path, "").split("/") if p]
        qs = urllib.parse.parse_qs(parsed.query or "")
        return host.endswith("polyhaven.com") and len(parts) >= 2 and parts[0] == "a" and ("iss_polyhaven" in qs or "download" in qs)
    except Exception:
        return False


def pbr_guess_polyhaven_slug_from_url(url, filename=""):
    try:
        parsed = urllib.parse.urlparse(safe_str(url, "").strip())
        path = safe_str(parsed.path, "")
        parts = [p for p in path.split("/") if p]
        candidates = []
        if len(parts) >= 2 and parts[0] == "a":
            candidates.append(parts[1])
        if "/__download__/" in path.lower():
            if parts:
                candidates.append(os.path.splitext(parts[-1])[0])
        if filename:
            candidates.append(os.path.splitext(os.path.basename(filename))[0])
        for cand in candidates:
            cand = safe_str(cand, "").strip()
            if not cand:
                continue
            cand = re.sub(r"_(1k|2k|4k|8k|16k|32k)$", "", cand, flags=re.I)
            cand = re.sub(r"_(jpg|png|exr|hdr|zip|blend|fbx|obj)$", "", cand, flags=re.I)
            cand = cand.strip("_- ")
            if cand:
                return cand
    except Exception:
        pass
    return ""


def pbr_polyhaven_query_value(url, key, default=""):
    try:
        parsed = urllib.parse.urlparse(safe_str(url, "").strip())
        vals = urllib.parse.parse_qs(parsed.query or "").get(key, [])
        if vals:
            return safe_str(vals[0], "").strip()
    except Exception:
        pass
    return safe_str(default, "").strip()


def pbr_polyhaven_desired_download_params(url, filename=""):
    desired = dict(
        resolution=safe_str(pbr_polyhaven_query_value(url, "iss_res", ""), "").lower(),
        download_type=safe_str(pbr_polyhaven_query_value(url, "iss_dl", ""), "").lower(),
        filename=safe_str(pbr_polyhaven_query_value(url, "download", filename or ""), "").strip()
    )
    if not desired["filename"]:
        desired["filename"] = safe_str(filename or pbr_download_filename_from_url(url), "").strip()
    if not desired["resolution"]:
        m = re.search(r"_(1k|2k|4k|8k|16k|32k)(?:[._-]|$)", desired["filename"], re.I)
        if m:
            desired["resolution"] = safe_str(m.group(1), "").lower()
    if not desired["download_type"]:
        ext = os.path.splitext(desired["filename"])[1].lower().lstrip(".")
        if ext:
            desired["download_type"] = ext
    if desired["download_type"] == "materialx":
        desired["download_type"] = "mtlx"
    return desired


def pbr_resolve_polyhaven_download_url(url, filename=""):
    """
    Poly Haven 的 /__download__/... 链接是临时的，稳定资产页链接也需要在下载瞬间解析。
    下载前根据 slug + 目标分辨率/格式，实时调用官方 files API 刷新出当前有效直链。
    """
    original = safe_str(url, "").strip()
    if not pbr_is_polyhaven_temp_download_url(original) and not pbr_is_polyhaven_stable_asset_url(original):
        return original, filename, ""
    slug = pbr_guess_polyhaven_slug_from_url(original, filename=filename)
    if not slug:
        return original, filename, "无法从 Poly Haven 临时链接推断素材名"
    desired = pbr_polyhaven_desired_download_params(original, filename=filename)
    api_url = "https://api.polyhaven.com/files/{}".format(urllib.parse.quote(slug))
    try:
        req = urllib.request.Request(api_url, headers={"User-Agent": PBR_BROWSER_USER_AGENT, "Accept": "application/json"})
        with urllib.request.urlopen(req, timeout=30) as resp:
            raw = resp.read().decode("utf-8", errors="replace")
        data = json.loads(raw)
    except Exception as e:
        return original, filename, "读取 Poly Haven files API 失败：{}".format(e)

    wanted_name = safe_str(desired.get("filename", "") or filename or pbr_download_filename_from_url(original), "").strip().lower()
    found = []

    def _add_candidate(u, meta=None):
        u = safe_str(u, "").strip()
        if not u or not re.match(r"^https?://", u, re.I):
            return
        fn = pbr_download_filename_from_url(u)
        found.append(dict(
            url=u,
            filename=fn,
            resolution=safe_str((meta or {}).get("resolution", ""), "").lower(),
            download_type=safe_str((meta or {}).get("download_type", ""), "").lower()
        ))

    def _walk(value, meta=None):
        if isinstance(value, dict):
            for k, v in value.items():
                if k in ("url", "download_url", "hdri") and isinstance(v, str):
                    extra = dict(meta or {})
                    if not extra.get("download_type"):
                        extra["download_type"] = safe_str(k, "").lower()
                    _add_candidate(v, extra)
                else:
                    extra = dict(meta or {})
                    lk = safe_str(k, "").lower()
                    if lk in ("1k", "2k", "4k", "8k", "16k", "32k"):
                        extra["resolution"] = lk
                    elif lk in ("blend", "gltf", "fbx", "usd", "mtlx", "hdr", "hdri", "zip", "jpg", "png", "exr"):
                        extra["download_type"] = "hdr" if lk == "hdri" else lk
                    _walk(v, extra)
        elif isinstance(value, list):
            for item in value:
                _walk(item, meta=meta)
        elif isinstance(value, str):
            if pbr_is_probably_download_url(value):
                _add_candidate(value, meta=meta)

    _walk(data)
    if not found:
        return original, filename, "Poly Haven files API 没有返回可下载文件"

    desired_res = safe_str(desired.get("resolution", ""), "").lower()
    desired_type = safe_str(desired.get("download_type", ""), "").lower()

    if desired_res and desired_type:
        for item in found:
            if item.get("resolution") == desired_res and item.get("download_type") == desired_type:
                return item.get("url", ""), item.get("filename", "") or filename, ""

    if wanted_name:
        for item in found:
            cand_name = safe_str(item.get("filename", ""), "").strip().lower()
            if cand_name == wanted_name:
                return item.get("url", ""), item.get("filename", "") or filename, ""

    basename = safe_str(os.path.basename(wanted_name), "").strip().lower() if wanted_name else ""
    if basename:
        for item in found:
            low = safe_str(item.get("filename", ""), "").strip().lower()
            if low == basename or low.endswith("/" + basename):
                return item.get("url", ""), item.get("filename", "") or filename, ""

    first = found[0]
    return first.get("url", ""), first.get("filename", "") or filename, "未精确匹配到原文件名，已改用 Poly Haven 当前可用下载项"



# ============================================================
# AI 小助手
# ============================================================

def ai_mask_key(key):
    key = safe_str(key, "")
    if len(key) <= 8:
        return "*" * len(key)
    return key[:4] + "*" * (len(key) - 8) + key[-4:]


def ai_join_messages_for_display(messages):
    lines = []
    for m in messages:
        role = m.get("role", "")
        content = safe_str(m.get("content", ""), "")
        if role == "user":
            lines.append("用户：\n{}".format(content))
        elif role == "assistant":
            lines.append("AI：\n{}".format(content))
        else:
            lines.append("{}：\n{}".format(role, content))
    return "\n\n".join(lines)


def ai_provider_presets():
    """
    V78：常用大模型接入方案。
    cost_type 用来把"免费"说清楚：
    - 本地免费：不需要API Key，不走云端额度。
    - 云端免费额度：通常有免费层/免费额度/限流，仍需要Key。
    - 需余额或付费：Key有效不代表能聊天，必须有项目额度/余额。
    - 聚合/自定义：由用户自己选择模型和额度规则。
    """
    return {
        "Ollama本地免费": dict(
            display_name="Ollama本地免费 [文本]",
            supports_vision=False,
            supports_image_generation=False,
            supports_image_editing=False,
            cost_type="本地免费（无需Key）",
            api_type="Ollama /api/chat",
            base_url="http://127.0.0.1:11434",
            model="qwen2.5:7b",
            key_url="https://ollama.com/download",
            billing_url="",
            usage_url="",
            doc_url="https://ollama.com/library",
            need_key=False,
            note="本地运行，不需要API Key；先安装Ollama并拉取模型。"
        ),
        "Ollama本地免费-视觉": dict(
            display_name="Ollama本地免费 [视觉看图]",
            supports_vision=True,
            supports_image_generation=False,
            supports_image_editing=False,
            cost_type="本地免费（无需Key）",
            api_type="Ollama /api/chat",
            base_url="http://127.0.0.1:11434",
            model="qwen2.5vl:7b",
            key_url="https://ollama.com/download",
            billing_url="",
            usage_url="",
            doc_url="https://ollama.com/library/qwen2.5vl",
            need_key=False,
            note="本地视觉方案；适合看图问答。是否能直接生成/编辑图片通常取决于具体模型与外部工作流，不等于本插件当前已支持直接改图。"
        ),
        "LM Studio本地免费": dict(
            display_name="LM Studio本地免费 [文本]",
            supports_vision=False,
            supports_image_generation=False,
            supports_image_editing=False,
            cost_type="本地免费（无需Key）",
            api_type="OpenAI兼容",
            base_url="http://127.0.0.1:1234/v1",
            model="local-model",
            key_url="https://lmstudio.ai/",
            billing_url="",
            usage_url="",
            doc_url="https://lmstudio.ai/docs",
            need_key=False,
            note="本地运行，不需要API Key；在LM Studio里开启Local Server。"
        ),
        "LM Studio本地免费-视觉": dict(
            display_name="LM Studio本地免费 [视觉看图]",
            supports_vision=True,
            supports_image_generation=False,
            supports_image_editing=False,
            cost_type="本地免费（无需Key）",
            api_type="OpenAI兼容",
            base_url="http://127.0.0.1:1234/v1",
            model="qwen2.5-vl-7b-instruct",
            key_url="https://lmstudio.ai/",
            billing_url="",
            usage_url="",
            doc_url="https://lmstudio.ai/docs",
            need_key=False,
            note="本地视觉方案；适合看图问答。是否能直接生成/编辑图片取决于你加载的具体模型与服务方式。"
        ),
        "Google Gemini": dict(
            display_name="Google Gemini [视觉；可直改图-需图像模型]",
            supports_vision=True,
            supports_image_generation=True,
            supports_image_editing=True,
            cost_type="云端免费额度（需Key，有限额）",
            api_type="OpenAI兼容",
            base_url="https://generativelanguage.googleapis.com/v1beta/openai",
            model="gemini-2.5-flash",
            key_url="https://aistudio.google.com/app/apikey",
            billing_url="https://aistudio.google.com/",
            usage_url="https://aistudio.google.com/",
            doc_url="https://ai.google.dev/gemini-api/docs/openai",
            need_key=True,
            note="Gemini 普通视觉模型能看图，但直接生成/编辑图片通常要用 gemini-2.5-flash-image 或 gemini-3-pro-image-preview 一类图像输出模型。当前插件尚未单独接入 Gemini 专用图片编辑流程。"
        ),
        "Groq": dict(
            display_name="Groq [文本]",
            supports_vision=False,
            supports_image_generation=False,
            supports_image_editing=False,
            cost_type="云端免费额度（需Key，有速率限制）",
            api_type="OpenAI兼容",
            base_url="https://api.groq.com/openai/v1",
            model="llama-3.3-70b-versatile",
            key_url="https://console.groq.com/keys",
            billing_url="https://console.groq.com/settings/billing",
            usage_url="https://console.groq.com/settings/usage",
            doc_url="https://console.groq.com/docs/openai",
            need_key=True,
            note="GroqCloud，OpenAI兼容，常用于高速推理；免费额度和速率限制以控制台为准。"
        ),
        "Groq Vision": dict(
            display_name="Groq [视觉看图 Llama 4 Scout]",
            supports_vision=True,
            supports_image_generation=False,
            supports_image_editing=False,
            cost_type="云端免费额度/预览（需Key，有速率限制）",
            api_type="OpenAI兼容",
            base_url="https://api.groq.com/openai/v1",
            model="meta-llama/llama-4-scout-17b-16e-instruct",
            key_url="https://console.groq.com/keys",
            billing_url="https://console.groq.com/settings/billing",
            usage_url="https://console.groq.com/settings/usage",
            doc_url="https://console.groq.com/docs/vision",
            need_key=True,
            note="Groq 官方视觉方案；支持 text + image_url，看图分析很合适。但它不是直接改图/返图模型。"
        ),
        "Cerebras": dict(
            display_name="Cerebras [文本]",
            supports_vision=False,
            supports_image_generation=False,
            supports_image_editing=False,
            cost_type="云端免费额度（需Key，有速率限制）",
            api_type="OpenAI兼容",
            base_url="https://api.cerebras.ai/v1",
            model="llama3.1-8b",
            key_url="https://cloud.cerebras.ai/",
            billing_url="https://cloud.cerebras.ai/",
            usage_url="https://cloud.cerebras.ai/",
            doc_url="https://inference-docs.cerebras.ai/resources/openai",
            need_key=True,
            note="Cerebras Inference，OpenAI兼容，主打高速推理；免费Key/额度以官网控制台为准。"
        ),
        "硅基流动 SiliconFlow": dict(
            display_name="硅基流动 SiliconFlow [自选；可看图/可改图取决于模型]",
            supports_vision=True,
            supports_image_generation=True,
            supports_image_editing=True,
            cost_type="云端免费额度（国内，需Key，有限额）",
            api_type="OpenAI兼容",
            base_url="https://api.siliconflow.cn/v1",
            model="Qwen/Qwen2.5-7B-Instruct",
            key_url="https://cloud.siliconflow.cn/account/ak",
            billing_url="https://cloud.siliconflow.cn/account/bill",
            usage_url="https://cloud.siliconflow.cn/account/usage",
            doc_url="https://docs.siliconflow.cn/en/userguide/capabilities/text-generation",
            need_key=True,
            note="硅基流动是模型聚合平台。是否支持看图、返图、直接编辑图片，完全取决于你实际选的模型；当前默认模型是文本模型。"
        ),
        "OpenRouter": dict(
            display_name="OpenRouter [自选；可看图/可返图/部分可改图]",
            supports_vision=True,
            supports_image_generation=True,
            supports_image_editing=True,
            cost_type="聚合平台（有免费模型，也有限额）",
            api_type="OpenAI兼容",
            base_url="https://openrouter.ai/api/v1",
            model="openai/gpt-4.1-mini",
            key_url="https://openrouter.ai/settings/keys",
            billing_url="https://openrouter.ai/settings/credits",
            usage_url="https://openrouter.ai/activity",
            doc_url="https://openrouter.ai/docs/api/reference/overview",
            need_key=True,
            note="聚合多家模型。看图取决于视觉模型；返图/直接改图要选 output_modalities 含 image 的模型。当前插件已能接收返图，但还没有单独做图像编辑专用工作流。"
        ),
        "Mistral": dict(
            display_name="Mistral [文本]",
            supports_vision=False,
            supports_image_generation=False,
            supports_image_editing=False,
            cost_type="云端需Key（可能有免费/试用额度）",
            api_type="OpenAI兼容",
            base_url="https://api.mistral.ai/v1",
            model="mistral-small-latest",
            key_url="https://console.mistral.ai/api-keys/",
            billing_url="https://console.mistral.ai/billing/",
            usage_url="https://console.mistral.ai/usage/",
            doc_url="https://docs.mistral.ai/api",
            need_key=True,
            note="Mistral API，Chat Completions接口；额度和计费以控制台为准。"
        ),
        "OpenAI": dict(
            display_name="OpenAI [视觉；可直改图-需图片API]",
            supports_vision=True,
            supports_image_generation=True,
            supports_image_editing=True,
            cost_type="需余额或付费（ChatGPT免费不等于API免费）",
            api_type="OpenAI兼容",
            base_url="https://api.openai.com/v1",
            model="gpt-4.1-mini",
            key_url="https://platform.openai.com/settings/organization/api-keys",
            billing_url="https://platform.openai.com/settings/organization/billing/overview",
            usage_url="https://platform.openai.com/usage",
            doc_url="https://platform.openai.com/docs",
            need_key=True,
            note="OpenAI 看图可用；直接生成/编辑图片通常要用 gpt-image-1 系列的 Images API 或 Responses image generation tool。当前插件主流程仍以聊天接口为主。"
        ),
        "DeepSeek": dict(
            display_name="DeepSeek [文本]",
            supports_vision=False,
            supports_image_generation=False,
            supports_image_editing=False,
            cost_type="需余额或付费（需Key）",
            api_type="OpenAI兼容",
            base_url="https://api.deepseek.com",
            model="deepseek-v4-flash",
            key_url="https://platform.deepseek.com/api_keys",
            billing_url="https://platform.deepseek.com/usage",
            usage_url="https://platform.deepseek.com/usage",
            doc_url="https://api-docs.deepseek.com/",
            need_key=True,
            note="DeepSeek官方API，OpenAI兼容；402/Insufficient Balance表示余额不足。"
        ),
        "阿里云百炼 / 通义千问": dict(
            display_name="阿里云百炼 / 通义千问 [自选；可看图取决于模型]",
            supports_vision=True,
            supports_image_generation=False,
            supports_image_editing=False,
            cost_type="需余额或免费额度（国内，需Key）",
            api_type="OpenAI兼容",
            base_url="https://dashscope.aliyuncs.com/compatible-mode/v1",
            model="qwen-plus",
            key_url="https://bailian.console.aliyun.com/?tab=model#/api-key",
            billing_url="https://bailian.console.aliyun.com/",
            usage_url="https://bailian.console.aliyun.com/",
            doc_url="https://help.aliyun.com/zh/model-studio/compatibility-of-openai-with-dashscope",
            need_key=True,
            note="通义家族里有视觉模型，但当前默认模型是文本模型。是否支持直接改图取决于你实际选的图像模型与接口能力。"
        ),
        "Moonshot / Kimi": dict(
            display_name="Moonshot / Kimi [文本]",
            supports_vision=False,
            supports_image_generation=False,
            supports_image_editing=False,
            cost_type="需余额或免费额度（国内，需Key）",
            api_type="OpenAI兼容",
            base_url="https://api.moonshot.cn/v1",
            model="moonshot-v1-8k",
            key_url="https://platform.moonshot.cn/console/api-keys",
            billing_url="https://platform.moonshot.cn/console/account",
            usage_url="https://platform.moonshot.cn/console/usage",
            doc_url="https://platform.moonshot.cn/docs",
            need_key=True,
            note="Moonshot/Kimi OpenAI兼容接口；额度以平台控制台为准。"
        ),
        "xAI / Grok": dict(
            display_name="xAI / Grok [自选；可看图取决于模型]",
            supports_vision=True,
            supports_image_generation=False,
            supports_image_editing=False,
            cost_type="需余额或付费（需Key）",
            api_type="OpenAI兼容",
            base_url="https://api.x.ai/v1",
            model="grok-4",
            key_url="https://console.x.ai/",
            billing_url="https://console.x.ai/",
            usage_url="https://console.x.ai/",
            doc_url="https://docs.x.ai/developers/rest-api-reference",
            need_key=True,
            note="是否支持看图取决于你选的 Grok 具体模型。这里先按可自选处理；直接改图能力不在当前默认接入范围内。"
        ),
        "Fireworks AI": dict(
            display_name="Fireworks AI [自选；可看图/可返图取决于模型]",
            supports_vision=True,
            supports_image_generation=True,
            supports_image_editing=False,
            cost_type="云端需Key（可能有免费/试用额度）",
            api_type="OpenAI兼容",
            base_url="https://api.fireworks.ai/inference/v1",
            model="accounts/fireworks/models/llama-v3p1-8b-instruct",
            key_url="https://app.fireworks.ai/",
            billing_url="https://app.fireworks.ai/",
            usage_url="https://app.fireworks.ai/",
            doc_url="https://docs.fireworks.ai/tools-sdks/openai-compatibility",
            need_key=True,
            note="平台可接多种模型。是否看图、返图取决于你选的具体模型；直接图片编辑能力需按模型单独确认。"
        ),
        "Together AI": dict(
            display_name="Together AI [自选；可看图/可返图取决于模型]",
            supports_vision=True,
            supports_image_generation=True,
            supports_image_editing=False,
            cost_type="云端需Key（可能有免费/试用额度）",
            api_type="OpenAI兼容",
            base_url="https://api.together.xyz/v1",
            model="meta-llama/Llama-3.3-70B-Instruct-Turbo",
            key_url="https://api.together.ai/settings/api-keys",
            billing_url="https://api.together.ai/settings/billing",
            usage_url="https://api.together.ai/settings/usage",
            doc_url="https://docs.together.ai/",
            need_key=True,
            note="平台可接视觉或图像输出模型，但是否能直接改图要看具体模型与接口。默认模型仍是文本模型。"
        ),
        "自定义OpenAI兼容": dict(
            display_name="自定义OpenAI兼容 [按模型决定：看图/返图/改图]",
            supports_vision=True,
            supports_image_generation=True,
            supports_image_editing=True,
            cost_type="自定义/代理网关",
            api_type="OpenAI兼容",
            base_url="",
            model="",
            key_url="",
            billing_url="",
            usage_url="",
            doc_url="",
            need_key=True,
            note="适合任何提供 OpenAI 兼容接口的平台。是否支持看图、返图、直接编辑图片完全取决于你接的模型和接口。"
        ),
    }


def ai_provider_capability_text(info):
    info = dict(info or {})
    vision = bool(info.get("supports_vision", False))
    gen = bool(info.get("supports_image_generation", False))
    edit = bool(info.get("supports_image_editing", False))
    if edit:
        return "支持看图，可生成/编辑图片"
    if gen:
        return "支持看图，可生成图片"
    if vision:
        return "支持看图"
    return "文本模型"


def ai_provider_key_from_name(name):
    name = safe_str(name, "")
    presets = ai_provider_presets()
    if name in presets:
        return name
    for key, info in presets.items():
        if safe_str(info.get("display_name", ""), "") == name:
            return key
    return name


def ai_provider_names():
    names = []
    for key, info in ai_provider_presets().items():
        names.append(safe_str(info.get("display_name", ""), "") or key)
    return names


def ai_read_http_error_body(error):
    try:
        body = error.read().decode("utf-8", errors="replace")
        return body
    except Exception:
        return ""


def ai_extract_error_message(raw_body):
    raw_body = safe_str(raw_body, "")
    if not raw_body:
        return ""
    try:
        data = json.loads(raw_body)
        err = data.get("error", data)
        if isinstance(err, dict):
            parts = []
            for k in ["message", "type", "code", "param"]:
                if err.get(k):
                    parts.append("{}: {}".format(k, err.get(k)))
            if parts:
                return "；".join(parts)
        if isinstance(err, str):
            return err
    except Exception:
        pass
    return raw_body[:800]


def ai_friendly_http_error(error, provider="", model=""):
    code = getattr(error, "code", None)
    reason = safe_str(getattr(error, "reason", ""), "")
    body = ai_read_http_error_body(error)
    detail = ai_extract_error_message(body)

    suggestions = []
    low_detail = (detail + " " + body).lower()
    quota_like = ("insufficient_quota" in low_detail or "exceeded your current quota" in low_detail or "insufficient balance" in low_detail or "payment required" in low_detail)
    if code == 402 or quota_like:
        title = "配额/余额不足，或当前项目未开通可用API额度"
        suggestions = [
            "登录当前平台控制台，检查余额、免费额度、项目额度或计费状态。",
            "OpenAI用户请确认当前API Key所属的项目/组织有可用额度，并已开通API计费。",
            "如果Key能访问 /models 但聊天接口报 insufficient_quota，通常说明Key有效但没有可用调用额度。",
            "换一个有额度的项目/API Key，或切换到 Ollama / LM Studio 本地模型测试插件功能。"
        ]
    elif code == 429:
        title = "请求过多 / 限流"
        suggestions = [
            "等待一会儿后再测试，避免连续点击\"测试连接\"。",
            "检查该平台的RPM/TPM限制或当前服务是否拥挤。",
            "换一个更轻量模型，或切换到本地 Ollama / LM Studio。",
            "如果是共享免费模型，可能是平台侧拥挤，稍后再试。"
        ]
    elif code == 401:
        title = "API Key无效或未填写"
        suggestions = [
            "检查API Key是否复制完整。",
            "确认Key属于当前选择的平台。",
            "确认Base URL和服务商方案匹配。"
        ]
    elif code == 403:
        title = "没有权限访问该模型或接口"
        suggestions = [
            "检查账号是否开通该模型权限。",
            "检查模型名是否在当前平台可用。",
            "检查账号地区、组织或计费状态限制。"
        ]
    elif code == 404:
        title = "接口地址或模型名不存在"
        suggestions = [
            "检查Base URL是否正确。",
            "检查模型名是否拼写正确。",
            "确认当前平台是否使用OpenAI兼容接口。"
        ]
    elif code in (400, 422):
        title = "请求格式或参数不被接口接受"
        suggestions = [
            "检查模型名、Base URL和接口类型。",
            "如果当前平台不支持OpenAI兼容格式，请换对应方案或自定义接口。"
        ]
    else:
        title = "AI接口HTTP错误"
        suggestions = [
            "检查网络、Base URL、模型名和API Key。",
            "稍后重试，或切换其它模型方案。"
        ]

    lines = []
    lines.append("AI连接失败：HTTP {} {}".format(code if code is not None else "", reason).strip())
    lines.append("原因判断：{}".format(title))
    if provider:
        lines.append("当前方案：{}".format(provider))
    if model:
        lines.append("当前模型：{}".format(model))
    if detail:
        lines.append("接口返回：{}".format(detail))
    lines.append("")
    lines.append("建议：")
    for s in suggestions:
        lines.append("- " + s)
    return "\n".join(lines)


def ai_friendly_url_error(error):
    msg = safe_str(error, "")
    return (
        "AI连接失败：无法连接到接口。\n\n"
        "可能原因：\n"
        "- Base URL填错。\n"
        "- 本地服务没有启动，例如 Ollama 或 LM Studio Local Server 未开启。\n"
        "- 网络或代理阻止了访问。\n\n"
        "底层信息：{}".format(msg)
    )


def ai_normalize_openai_base_url(base_url):
    base_url = safe_str(base_url, "").strip()
    return base_url.rstrip("/")


def ai_openai_chat_url(base_url):
    base = ai_normalize_openai_base_url(base_url)
    if not base:
        return ""
    if base.lower().endswith("/chat/completions"):
        return base
    return base + "/chat/completions"


def ai_openai_models_url(base_url):
    base = ai_normalize_openai_base_url(base_url)
    if not base:
        return ""
    if base.lower().endswith("/chat/completions"):
        base = base[: -len("/chat/completions")]
    return base.rstrip("/") + "/models"


def ai_auth_headers(api_key):
    headers = {
        "Accept": "application/json",
        "User-Agent": "InteriorSceneStudioPro/75"
    }
    if api_key:
        headers["Authorization"] = "Bearer " + safe_str(api_key, "").strip()
    return headers


def ai_http_get_json(url, api_key="", timeout=45):
    req = urllib.request.Request(url, headers=ai_auth_headers(api_key), method="GET")
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        raw = resp.read().decode("utf-8", errors="replace")
    try:
        return json.loads(raw), raw
    except Exception:
        return None, raw


def ai_extract_model_ids_from_models_response(data):
    ids = []
    try:
        items = data.get("data", []) if isinstance(data, dict) else []
        for it in items:
            if isinstance(it, dict) and it.get("id"):
                ids.append(safe_str(it.get("id"), ""))
    except Exception:
        pass
    return ids


def ai_temp_image_dir():
    path = os.path.join(tempfile.gettempdir(), "InteriorSceneStudioPro_AI")
    os.makedirs(path, exist_ok=True)
    return path


def ai_suffix_from_mime(mime):
    mime = safe_str(mime, "").strip().lower()
    mapping = {
        "image/jpeg": ".jpg",
        "image/jpg": ".jpg",
        "image/png": ".png",
        "image/webp": ".webp",
        "image/gif": ".gif",
        "image/bmp": ".bmp",
        "image/tiff": ".tif",
    }
    return mapping.get(mime, ".png")


def ai_save_temp_image_bytes(data, suffix=".png"):
    if not data:
        return ""
    try:
        with tempfile.NamedTemporaryFile(delete=False, suffix=suffix, dir=ai_temp_image_dir()) as f:
            f.write(data)
            return f.name
    except Exception:
        return ""


def ai_save_data_url_image(url):
    url = safe_str(url, "").strip()
    if not url.lower().startswith("data:image/") or ";base64," not in url:
        return ""
    try:
        header, b64 = url.split(",", 1)
        mime = header.split(";", 1)[0].split(":", 1)[-1].strip()
        raw = base64.b64decode(b64)
        return ai_save_temp_image_bytes(raw, ai_suffix_from_mime(mime))
    except Exception:
        return ""


def ai_download_remote_image(url, timeout=45):
    url = safe_str(url, "").strip()
    if not (url.lower().startswith("http://") or url.lower().startswith("https://")):
        return ""
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "InteriorSceneStudioPro/AIImage"})
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            raw = resp.read()
            mime = safe_str(resp.headers.get("Content-Type", ""), "").split(";", 1)[0].strip().lower()
        suffix = ai_suffix_from_mime(mime)
        if suffix == ".png":
            ext = os.path.splitext(urllib.parse.urlparse(url).path)[1].lower()
            if ext in (".jpg", ".jpeg", ".png", ".webp", ".gif", ".bmp", ".tif", ".tiff"):
                suffix = ext
        return ai_save_temp_image_bytes(raw, suffix)
    except Exception:
        return ""


def ai_collect_text_from_content_parts(content):
    if isinstance(content, str):
        return safe_str(content, "").strip()
    if not isinstance(content, list):
        return ""
    texts = []
    for part in content:
        if not isinstance(part, dict):
            continue
        typ = safe_str(part.get("type", ""), "").lower()
        if typ in ("text", "input_text", "output_text"):
            txt = safe_str(part.get("text", ""), "").strip()
            if txt:
                texts.append(txt)
    return "\n".join(texts).strip()


def ai_extract_openai_response_payload(result):
    text = ""
    image_paths = []

    def _add_path(path):
        path = safe_str(path, "").strip()
        if path and path not in image_paths and os.path.isfile(path):
            image_paths.append(path)

    def _add_image_candidate(item):
        if not isinstance(item, dict):
            return
        url = ""
        img = item.get("image_url", None)
        if isinstance(img, dict):
            url = safe_str(img.get("url", ""), "").strip()
        if not url:
            url = safe_str(item.get("url", ""), "").strip()
        if url.startswith("data:image/"):
            _add_path(ai_save_data_url_image(url))
        elif url.lower().startswith("http://") or url.lower().startswith("https://"):
            _add_path(ai_download_remote_image(url))
        b64 = safe_str(item.get("b64_json", "") or item.get("image_base64", "") or item.get("base64", ""), "").strip()
        if b64:
            mime = safe_str(item.get("mime_type", "") or item.get("mime", ""), "").strip().lower()
            try:
                _add_path(ai_save_temp_image_bytes(base64.b64decode(b64), ai_suffix_from_mime(mime)))
            except Exception:
                pass

    try:
        choices = result.get("choices", []) if isinstance(result, dict) else []
        if choices:
            msg = choices[0].get("message", {}) if isinstance(choices[0], dict) else {}
            content = msg.get("content", "")
            text = ai_collect_text_from_content_parts(content) if isinstance(content, list) else safe_str(content, "").strip()
            if isinstance(content, list):
                for part in content:
                    _add_image_candidate(part)
            if isinstance(msg.get("images", None), list):
                for part in msg.get("images", []):
                    _add_image_candidate(part if isinstance(part, dict) else dict(url=part))
    except Exception:
        pass

    try:
        data_items = result.get("data", []) if isinstance(result, dict) else []
        for item in data_items:
            _add_image_candidate(item if isinstance(item, dict) else {})
        if (not text) and isinstance(result, dict):
            text = safe_str(result.get("output_text", "") or result.get("text", ""), "").strip()
    except Exception:
        pass

    return dict(text=text, images=image_paths)


def ai_openai_compatible_request_full(base_url, api_key, model, messages, temperature=0.3, timeout=90, max_tokens=None, provider="OpenAI兼容接口"):
    chat_url = ai_openai_chat_url(base_url)
    if not chat_url:
        raise RuntimeError("Base URL为空")

    payload = dict(
        model=safe_str(model, "").strip(),
        messages=messages,
        temperature=float(temperature),
        stream=False
    )
    if max_tokens is not None:
        payload["max_tokens"] = int(max_tokens)

    data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    headers = ai_auth_headers(api_key)
    headers["Content-Type"] = "application/json"

    req = urllib.request.Request(chat_url, data=data, headers=headers, method="POST")
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            raw = resp.read().decode("utf-8", errors="replace")
    except urllib.error.HTTPError as e:
        raise RuntimeError(ai_friendly_http_error(e, provider=provider, model=model) + "\n\n最终请求地址：{}".format(chat_url))
    except urllib.error.URLError as e:
        raise RuntimeError(ai_friendly_url_error(e) + "\n\n最终请求地址：{}".format(chat_url))
    result = json.loads(raw)
    parsed = ai_extract_openai_response_payload(result)
    text = safe_str(parsed.get("text", ""), "").strip()
    if not text:
        try:
            text = safe_str(result["choices"][0]["message"]["content"], "").strip()
        except Exception:
            text = raw
    return dict(text=text, images=list(parsed.get("images", []) or []), raw=result)


def ai_openai_compatible_request(base_url, api_key, model, messages, temperature=0.3, timeout=90, max_tokens=None, provider="OpenAI兼容接口"):
    return ai_openai_compatible_request_full(
        base_url, api_key, model, messages,
        temperature=temperature, timeout=timeout, max_tokens=max_tokens, provider=provider
    ).get("text", "")


def ai_ollama_request(base_url, model, messages, temperature=0.3, timeout=120, max_tokens=None):
    base_url = safe_str(base_url, "").strip() or "http://127.0.0.1:11434"
    if base_url.rstrip("/").endswith("/api/chat"):
        url = base_url.rstrip("/")
    else:
        url = base_url.rstrip("/") + "/api/chat"

    payload = dict(
        model=safe_str(model, "").strip(),
        messages=messages,
        stream=False,
        options=dict(temperature=float(temperature), num_predict=int(max_tokens) if max_tokens is not None else -1)
    )
    data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    headers = {
        "Content-Type": "application/json",
        "Accept": "application/json",
        "User-Agent": "InteriorSceneStudioPro/72"
    }
    req = urllib.request.Request(url, data=data, headers=headers, method="POST")
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            raw = resp.read().decode("utf-8", errors="replace")
    except urllib.error.HTTPError as e:
        raise RuntimeError(ai_friendly_http_error(e, provider="Ollama本地", model=model))
    except urllib.error.URLError as e:
        raise RuntimeError(ai_friendly_url_error(e))
    result = json.loads(raw)
    try:
        return result["message"]["content"]
    except Exception:
        return raw


def ai_is_local_endpoint(url):
    low = safe_str(url, "").lower()
    return "127.0.0.1" in low or "localhost" in low or "0.0.0.0" in low


def pbr_analyze_material_folder(folder):
    """
    下载/解压后快速判断这个目录是否能作为 PBR 套装使用。
    不创建材质，只返回识别结果摘要。
    """
    try:
        if not folder or not os.path.isdir(folder):
            return dict(ok=False, complete=False, count=0, message="目录不存在")

        entries = scan_pbr_texture_sets(folder, recursive=True, group_by_folder=True)
        if not entries:
            return dict(ok=False, complete=False, count=0, message="未识别到PBR贴图")

        best = None
        best_score = -1
        for e in entries:
            channels = e.get("channels", {})
            score = len(channels)
            if e.get("preview"):
                score += 1
            if score > best_score:
                best_score = score
                best = e

        if not best:
            return dict(ok=False, complete=False, count=0, message="未识别到有效套装")

        channels = best.get("channels", {})
        channel_names = sorted(channels.keys())
        has_color = any(k in channels for k in ["BaseColor", "Diffuse", "Albedo"])
        has_normal = any(k in channels for k in ["Normal", "NormalDX", "NormalGL"])
        has_rough = "Roughness" in channels or "Glossiness" in channels
        complete = bool(has_color and has_normal and has_rough)
        msg = "识别到{}个套装；最佳：{}；通道：{}".format(len(entries), best.get("name", ""), ", ".join(channel_names) if channel_names else "无")
        return dict(ok=True, complete=complete, count=len(entries), best=best.get("name", ""), channels=channel_names, message=msg)
    except Exception as e:
        return dict(ok=False, complete=False, count=0, message="检测失败：{}".format(e))


def pbr_entry_is_failed(entry):
    try:
        s = safe_str(entry.get("status", ""), "")
        return s.startswith("失败") or "失败" in s
    except Exception:
        return False


class PBRBrowserPage(WEBENGINE_PAGE_CLASS):
    if HAS_QTWEBENGINE:
        directUrlDetected = QtCore.Signal(str)

        def acceptNavigationRequest(self, url, nav_type, isMainFrame):
            try:
                u = url.toString()
                if pbr_is_probably_download_url(u):
                    self.directUrlDetected.emit(u)
                    return False
            except Exception:
                pass
            return super(PBRBrowserPage, self).acceptNavigationRequest(url, nav_type, isMainFrame)




class AIPlainTextHighlighter(QtGui.QSyntaxHighlighter):
    """
    V80：记事本式纯文本高亮。
    仍然是可复制的纯文本，只对特定行做颜色/粗体提示。
    """
    def __init__(self, document, name_getter=None):
        super(AIPlainTextHighlighter, self).__init__(document)
        self._name_getter = name_getter if callable(name_getter) else (lambda: "AI小助手")
        self.f_user = self.make_format("#79C8FF", bold=True)
        self.f_ai = self.make_format("#8FE388", bold=True)
        self.f_system = self.make_format("#F0B96A", bold=True)
        self.f_error = self.make_format("#FF7676", bold=True)
        self.f_warn = self.make_format("#FFD36A", bold=True)
        self.f_ok = self.make_format("#7BE0A0", bold=True)
        self.f_hint = self.make_format("#8DD7FF", bold=False)

    def make_format(self, color, bold=False):
        fmt = QtGui.QTextCharFormat()
        fmt.setForeground(QtGui.QColor(color))
        if bold:
            fmt.setFontWeight(QtGui.QFont.Bold)
        return fmt

    def highlightBlock(self, text):
        s = safe_str(text, "").strip()
        if not s:
            return
        if s.startswith("【用户】"):
            self.setFormat(0, len(text), self.f_user)
        elif s.startswith("【{}】".format(self._name_getter())):
            self.setFormat(0, len(text), self.f_ai)
        elif s.startswith("【系统】"):
            self.setFormat(0, len(text), self.f_system)
        elif s.startswith("✅") or "成功" in s:
            self.setFormat(0, len(text), self.f_ok)
        elif s.startswith("⚠") or "警告" in s or "建议" in s:
            self.setFormat(0, len(text), self.f_warn)
        elif s.startswith("❌") or "失败" in s or "错误" in s or "Traceback" in s or "HTTP " in s:
            self.setFormat(0, len(text), self.f_error)
        elif s.startswith("- ") or s.startswith("处理：") or s.startswith("原因判断："):
            self.setFormat(0, len(text), self.f_hint)


class SceneHealthDialog(QtWidgets.QDialog):
    def __init__(self, rows, parent=None):
        super(SceneHealthDialog, self).__init__(parent)
        self.rows = rows
        self.setWindowTitle("场景体检报告")
        self.resize(1040, 680)

        layout = QtWidgets.QVBoxLayout(self)

        summary = {}
        issue_rows = 0
        for row in rows:
            kind = row.get("kind", "其它")
            summary[kind] = summary.get(kind, 0) + 1
            if row.get("count", 0) > 0:
                issue_rows += 1

        summary_text = "体检对象：{} 项；有标记项：{} 项；{}".format(
            len(rows),
            issue_rows,
            "，".join(["{} {}".format(k, v) for k, v in sorted(summary.items())])
        )
        label = QtWidgets.QLabel(summary_text)
        label.setObjectName("previewHint")
        layout.addWidget(label)

        self.table = QtWidgets.QTableWidget()
        self.table.setColumnCount(6)
        self.table.setHorizontalHeaderLabels(["分类", "名称", "类型", "图层", "问题 / 标记", "问题数"])
        self.table.setRowCount(len(rows))

        for r, row in enumerate(rows):
            values = [
                row.get("kind", ""),
                row.get("name", ""),
                row.get("type", ""),
                row.get("layer", ""),
                row.get("issues", ""),
                str(row.get("count", 0))
            ]
            for c, value in enumerate(values):
                self.table.setItem(r, c, QtWidgets.QTableWidgetItem(safe_str(value, "")))

        try:
            self.table.horizontalHeader().setStretchLastSection(True)
            self.table.resizeColumnsToContents()
            self.table.setAlternatingRowColors(True)
        except Exception:
            pass

        layout.addWidget(self.table)

        btn_row = QtWidgets.QHBoxLayout()
        self.btn_export = QtWidgets.QPushButton("导出 CSV 报告")
        self.btn_export.clicked.connect(self.export_csv)
        btn_row.addStretch()
        btn_row.addWidget(self.btn_export)

        buttons = QtWidgets.QDialogButtonBox(QtWidgets.QDialogButtonBox.Close)
        try:
            buttons.button(QtWidgets.QDialogButtonBox.Close).setText("关闭")
        except Exception:
            pass
        buttons.rejected.connect(self.reject)
        btn_row.addWidget(buttons)
        layout.addLayout(btn_row)

    def export_csv(self):
        try:
            folder = user_documents_dir()
            stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            path = os.path.join(folder, "InteriorSceneStudio_Report_{}.csv".format(stamp))
            with open(path, "w", newline="", encoding="utf-8-sig") as f:
                writer = csv.writer(f)
                writer.writerow(["分类", "名称", "类型", "图层", "问题 / 标记", "问题数"])
                for row in self.rows:
                    writer.writerow([
                        row.get("kind", ""),
                        row.get("name", ""),
                        row.get("type", ""),
                        row.get("layer", ""),
                        row.get("issues", ""),
                        row.get("count", 0)
                    ])
            QtWidgets.QMessageBox.information(self, "导出完成", "已导出报告：\n{}".format(path))
        except Exception:
            QtWidgets.QMessageBox.warning(self, "导出失败", status_text_for_exception("导出失败"))

# ============================================================
# 安装版图标
# ============================================================

def installed_plugin_root():
    try:
        return os.path.dirname(os.path.abspath(__file__))
    except Exception:
        return os.getcwd()


def installed_resource_search_dirs():
    dirs = []
    root = installed_plugin_root()
    dirs.append(root)
    try:
        for key in ["userSettings", "userScripts"]:
            d = ""
            try:
                d = rt.getDir(rt.Name(key))
            except Exception:
                try:
                    d = rt.getDir(key)
                except Exception:
                    d = ""
            if d:
                dirs.append(os.path.join(safe_str(d), "InteriorSceneStudioPro"))
    except Exception:
        pass
    try:
        dirs.append(os.path.join(os.path.expanduser("~"), "Documents", "InteriorSceneStudioPro"))
    except Exception:
        pass

    result = []
    seen = set()
    for d in dirs:
        d = safe_str(d, "")
        if d and d.lower() not in seen:
            seen.add(d.lower())
            result.append(d)
    return result


def installed_help_file_path():
    candidates = []
    for root in installed_resource_search_dirs():
        candidates.extend([
            os.path.join(root, "help", "InteriorSceneStudioPro_Help_v86.html"),
            os.path.join(root, "help", "InteriorSceneStudioPro_Help_v86.pdf"),
            os.path.join(root, "help", "InteriorSceneStudioPro_Help_v85.html"),
            os.path.join(root, "help", "InteriorSceneStudioPro_Help_v84.html"),
            os.path.join(root, "help", "InteriorSceneStudioPro_Help_v83.html"),
            os.path.join(root, "InteriorSceneStudioPro_Help_v86.html"),
            os.path.join(root, "InteriorSceneStudioPro_Help_v86.pdf"),
            os.path.join(root, "InteriorSceneStudioPro_Help_v62.pdf"),
            os.path.join(root, "InteriorSceneStudioPro_Help_v61.pdf"),
            os.path.join(root, "InteriorSceneStudioPro_Help_v60.pdf"),
            os.path.join(root, "help", "InteriorSceneStudioPro_Help_v60.pdf"),
        ])
    for p in candidates:
        if os.path.exists(p):
            return p
    return candidates[0] if candidates else os.path.join(installed_plugin_root(), "help", "InteriorSceneStudioPro_Help_v86.html")

def apply_installed_window_icon(widget):
    try:
        candidates = []
        for root in installed_resource_search_dirs():
            candidates.extend([
                os.path.join(root, "icons", "ISS_Icon_256.png"),
                os.path.join(root, "icons", "ISS_Icon_128.png"),
                os.path.join(root, "icons", "ISS_Icon_64.png"),
                os.path.join(root, "icons", "ISS_Icon_32.png"),
                os.path.join(root, "icons", "ISS_Icon.ico"),
            ])
        for path in candidates:
            if os.path.exists(path):
                widget.setWindowIcon(QtGui.QIcon(path))
                return True
    except Exception:
        pass
    return False


# ============================================================
# UI 主类
# ============================================================

class CardNavContainer(QtWidgets.QWidget):
    """左侧卡片导航 + 右侧内容区，保留 QTabWidget 常用接口，便于旧代码平滑迁移。"""
    def __init__(self, parent=None):
        super(CardNavContainer, self).__init__(parent)
        self._titles = []
        self._pages = []
        self._buttons = []

        root = QtWidgets.QHBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(12)

        self.left_panel = QtWidgets.QFrame()
        self.left_panel.setObjectName("leftNavPanel")
        try:
            self.left_panel.setMinimumWidth(140)
            self.left_panel.setMaximumWidth(140)
            self.left_panel.setSizePolicy(QtWidgets.QSizePolicy.Fixed, QtWidgets.QSizePolicy.Expanding)
        except Exception:
            pass
        left_lay = QtWidgets.QVBoxLayout(self.left_panel)
        left_lay.setContentsMargins(10, 8, 10, 8)
        left_lay.setSpacing(4)

        # 顶部标题已经独立到主窗口左上角；左侧只放功能导航。
        # 保留隐藏按钮对象，兼容旧逻辑里对 btn_nav_fold 的引用，避免重复显示"收起内容"。
        self.btn_nav_fold = PaintedButton("⟨ 收起内容", "side")
        self.btn_nav_fold.setObjectName("sideQuickButton")
        self.btn_nav_fold.clicked.connect(self.toggle_compact)
        self.btn_nav_fold.setVisible(False)

        self.nav_scroll = QtWidgets.QScrollArea()
        self.nav_scroll.setWidgetResizable(True)
        self.nav_scroll.setObjectName("leftNavScroll")
        try:
            self.nav_scroll.setFrameShape(QtWidgets.QFrame.NoFrame)
        except Exception:
            pass
        self.nav_inner = QtWidgets.QWidget()
        self.nav_inner.setObjectName("leftNavInner")
        self.nav_layout = QtWidgets.QVBoxLayout(self.nav_inner)
        self.nav_layout.setContentsMargins(0, 2, 0, 2)
        self.nav_layout.setSpacing(4)
        self.nav_layout.addStretch(1)
        self.nav_scroll.setWidget(self.nav_inner)
        left_lay.addWidget(self.nav_scroll, 1)

        self.right_panel = QtWidgets.QFrame()
        self.right_panel.setObjectName("contentHost")
        right_lay = QtWidgets.QVBoxLayout(self.right_panel)
        right_lay.setContentsMargins(14, 14, 14, 14)
        right_lay.setSpacing(10)

        self.header_card = QtWidgets.QFrame()
        self.header_card.setObjectName("contentHeaderCard")
        header_lay = QtWidgets.QVBoxLayout(self.header_card)
        header_lay.setContentsMargins(14, 12, 14, 12)
        header_lay.setSpacing(2)
        self.page_title = QtWidgets.QLabel("请选择左侧功能")
        self.page_title.setObjectName("pageTitleLabel")
        self.page_subtitle = QtWidgets.QLabel("这里会显示当前功能页。")
        self.page_subtitle.setObjectName("pageSubtitleLabel")
        try:
            self.page_subtitle.setWordWrap(True)
        except Exception:
            pass
        header_lay.addWidget(self.page_title)
        self.page_subtitle.setVisible(False)
        right_lay.addWidget(self.header_card)

        self.stack = QtWidgets.QStackedWidget()
        self.stack.setObjectName("contentStack")
        right_lay.addWidget(self.stack, 1)

        root.addWidget(self.left_panel, 0)
        root.addWidget(self.right_panel, 1)

    def _meta_for_title(self, title):
        mapping = {
            "模型管理": ("🧱", "模型", "对象修复、问题检测、重命名与选择同步"),
            "组管理": ("🗂", "组", "组对象浏览、筛选、重命名与开关组"),
            "灯光管理": ("💡", "灯光", "灯光列表、筛选、重命名与选择"),
            "相机管理": ("🎥", "相机", "相机浏览、重命名与快速定位"),
            "材质管理": ("🎨", "材质", "材质列表、命名整理、使用关系检查"),
            "材质标准化": ("⚙", "标准", "Physical / PBR / OpenPBR 等标准化转换"),
            "PBR贴图套装": ("🧩", "套装", "识别贴图套装并一键创建材质"),
            "PBR下载库": ("🌐", "下载", "常用材质资源站入口与下载辅助"),
            "UE贴图流送": ("🚀", "UE", "纹理检查、尺寸处理、UE流送整理"),
            "AI小助手": ("🤖", "AI", "说明、助手入口与外部帮助内容"),
        }
        return mapping.get(safe_str(title, ""), ("◼", safe_str(title, ""), "点击左侧卡片，在右侧展开完整功能。"))

    def _nav_button_text(self, title):
        icon, short, _desc = self._meta_for_title(title)
        # 左侧导航只显示图标 + 功能名称，不显示解释文字，减少占用面积。
        return "{}  {}".format(icon, title)

    def set_compact(self, compact=True):
        self.compact = bool(compact)
        try:
            self.left_panel.setMinimumWidth(140)
            self.left_panel.setMaximumWidth(140)
        except Exception:
            pass
        for btn, title in zip(self._buttons, self._titles):
            try:
                btn.setText(self._nav_button_text(title))
                btn.setFixedHeight(28)
                btn.setFixedWidth(116)
            except Exception:
                pass
        try:
            self.btn_nav_fold.setText("⟩ 展开内容" if getattr(self, "content_collapsed", False) else "⟨ 收起内容")
        except Exception:
            pass
        for name in ["side_quick_card", "side_status_card"]:
            try:
                getattr(self, name).setVisible(not self.compact)
            except Exception:
                pass

    def set_content_collapsed(self, collapsed=True):
        """折叠右侧内容区，只保留左侧功能导航。"""
        self.content_collapsed = bool(collapsed)
        try:
            self.right_panel.setVisible(not self.content_collapsed)
        except Exception:
            pass
        try:
            self.btn_nav_fold.setText("⟩ 展开内容" if self.content_collapsed else "⟨ 收起内容")
        except Exception:
            pass
        try:
            win_sync = self.window()
            if hasattr(win_sync, "sync_collapse_chrome"):
                win_sync.sync_collapse_chrome(self.content_collapsed)
        except Exception:
            pass
        try:
            win = self.window()
            if self.content_collapsed:
                # 收起后只剩左侧140px + 左右margin 18+18 = 176px，用 adjustSize 自动贴合
                win.setMinimumSize(0, 0)
                win.setMaximumWidth(220)
                QtWidgets.QApplication.processEvents()
                win.adjustSize()
            else:
                win.setMaximumWidth(16777215)  # QWIDGETSIZE_MAX
                win.setMinimumSize(760, 520)
                win.resize(1040, max(700, win.height()))
        except Exception:
            pass

    def toggle_compact(self):
        self.set_content_collapsed(not getattr(self, "content_collapsed", False))

    def _desc_for_title(self, title):
        mapping = {
            "模型管理": "对象修复、问题检测、重命名与选择同步",
            "组管理": "组对象浏览、筛选、重命名与开关组",
            "灯光管理": "灯光列表、筛选、重命名与选择",
            "相机管理": "相机浏览、重命名与快速定位",
            "材质管理": "材质列表、命名整理、使用关系检查",
            "材质标准化": "Physical / PBR / OpenPBR 等标准化转换",
            "PBR贴图套装": "识别贴图套装并一键创建材质",
            "PBR下载库": "常用材质资源站入口与下载辅助",
            "UE贴图流送": "纹理检查、尺寸处理、UE流送整理",
            "AI小助手": "说明、助手入口与外部帮助内容",
        }
        return mapping.get(safe_str(title, ""), "点击左侧卡片，在右侧展开完整功能。")

    def addTab(self, widget, title):
        idx = self.stack.addWidget(widget)
        self._pages.append(widget)
        self._titles.append(title)

        icon, short, desc = self._meta_for_title(title)
        btn = PaintedButton(self._nav_button_text(title), "nav")
        btn.setCheckable(True)
        btn.setObjectName("navCardButton")
        btn.setProperty("navTitle", title)
        btn.setProperty("navIcon", icon)
        btn.setProperty("navShort", short)
        try:
            btn.setFixedHeight(28)
            btn.setFixedWidth(116)
        except Exception:
            pass
        btn.clicked.connect(lambda _checked=False, i=idx: self.setCurrentIndex(i))
        insert_at = max(0, self.nav_layout.count() - 1)
        self.nav_layout.insertWidget(insert_at, btn)
        self._buttons.append(btn)

        if self.stack.count() == 1:
            self.setCurrentIndex(0)
        return idx

    def setCurrentIndex(self, index):
        try:
            index = int(index)
        except Exception:
            index = 0
        if index < 0 or index >= len(self._pages):
            return
        self.stack.setCurrentIndex(index)
        for i, btn in enumerate(self._buttons):
            try:
                btn.blockSignals(True)
                btn.setChecked(i == index)
            except Exception:
                pass
            finally:
                try:
                    btn.blockSignals(False)
                except Exception:
                    pass
        title = safe_str(self._titles[index], "")
        icon, _short, desc = self._meta_for_title(title)
        self.page_title.setText("{}  {}".format(icon, title))
        self.page_subtitle.setText("")

    def indexOf(self, widget):
        if widget is None:
            return -1
        for i, page in enumerate(self._pages):
            if page is widget:
                return i
            try:
                if page.isAncestorOf(widget):
                    return i
            except Exception:
                pass
        parent = widget
        while parent is not None:
            try:
                idx = self._pages.index(parent)
                return idx
            except Exception:
                pass
            try:
                parent = parent.parentWidget()
            except Exception:
                parent = None
        return -1


class PaintedButton(QtWidgets.QAbstractButton):
    """自绘按钮，绕开 3ds Max 宿主对 QPushButton 最终皮肤的覆盖。"""
    def __init__(self, text="", role_name="default", parent=None):
        super(PaintedButton, self).__init__(parent)
        self.setText(text)
        self.paint_role_name = safe_str(role_name, "default") or "default"
        self.setCursor(QtGui.QCursor(QtCore.Qt.PointingHandCursor))
        self.setMouseTracking(True)
        try:
            self.setStyleSheet("background: transparent; border: none; padding: 0px; margin: 0px;")
        except Exception:
            pass
        try:
            self.setAttribute(QtCore.Qt.WA_Hover, True)
        except Exception:
            pass
        try:
            wa_styled_bg = getattr(QtCore.Qt, "WA_StyledBackground")
        except Exception:
            try:
                wa_styled_bg = QtCore.Qt.WidgetAttribute.WA_StyledBackground
            except Exception:
                wa_styled_bg = None
        if wa_styled_bg is not None:
            try:
                self.setAttribute(wa_styled_bg, False)
            except Exception:
                pass
        try:
            self.setCheckable(False)
        except Exception:
            pass

    def _ui_palette(self):
        try:
            w = self.window()
            p = getattr(w, "_ui_palette", None) if w else None
            if isinstance(p, dict) and p:
                return p
        except Exception:
            pass
        return dict(
            text="#F4EEE7", line="#4B4036", button="#352E27", button_hover="#443A31",
            primary="#D79A52", primary_hover="#E4AD6A", primary_text="#1F1308",
            danger="#D85C4A", selection="#5B4027", selection_text="#FFF8EF"
        )

    def _role_metrics(self):
        role = safe_str(getattr(self, "paint_role_name", "default"), "default")
        radius_map = {"nav": 10, "side": 10, "top": 10, "primary": 10, "danger": 10, "default": 6}
        return role, radius_map.get(role, 7)

    def _role_colors(self):
        p = self._ui_palette()
        role, _radius = self._role_metrics()
        base_bg = p.get("button", "#352E27")
        base_fg = p.get("text", "#F4EEE7")
        base_border = p.get("line", "#4B4036")
        hover_bg = p.get("button_hover", "#443A31")
        hover_fg = base_fg
        hover_border = p.get("primary", "#D79A52")
        checked_bg = p.get("selection", "#5B4027")
        checked_fg = p.get("selection_text", "#FFF8EF")
        checked_border = p.get("primary", "#D79A52")
        if role == "primary":
            base_bg = p.get("primary", "#D79A52")
            base_fg = p.get("primary_text", "#1F1308")
            base_border = p.get("primary", "#D79A52")
            hover_bg = p.get("primary_hover", "#E4AD6A")
            hover_fg = base_fg
            hover_border = p.get("primary_hover", "#E4AD6A")
            checked_bg = hover_bg
            checked_fg = base_fg
            checked_border = hover_border
        elif role == "danger":
            base_bg = p.get("danger", "#D85C4A")
            base_fg = "#FFFFFF"
            base_border = p.get("danger", "#D85C4A")
            hover_bg = p.get("danger", "#D85C4A")
            hover_fg = "#FFFFFF"
            hover_border = p.get("primary_hover", "#E4AD6A")
            checked_bg = hover_bg
            checked_fg = hover_fg
            checked_border = hover_border
        elif role == "nav":
            checked_bg = p.get("primary", "#D79A52")
            checked_fg = p.get("primary_text", "#1F1308")
            checked_border = p.get("primary", "#D79A52")
        return dict(
            base_bg=base_bg, base_fg=base_fg, base_border=base_border,
            hover_bg=hover_bg, hover_fg=hover_fg, hover_border=hover_border,
            checked_bg=checked_bg, checked_fg=checked_fg, checked_border=checked_border
        )

    def paintEvent(self, event):
        _ = event
        p = QtGui.QPainter(self)
        try:
            p.setRenderHint(QtGui.QPainter.Antialiasing, True)
        except Exception:
            pass
        role, radius = self._role_metrics()
        inset_map = {"nav": (2, 2, -3, -3), "side": (2, 2, -3, -3), "top": (2, 2, -3, -3), "primary": (2, 2, -3, -3), "danger": (2, 2, -3, -3), "default": (1, 2, -2, -3)}
        dx1, dy1, dx2, dy2 = inset_map.get(role, (1, 1, -2, -2))
        rect = self.rect().adjusted(dx1, dy1, dx2, dy2)
        colors = self._role_colors()
        down = self.isDown()
        hover = self.underMouse()
        checked = self.isCheckable() and self.isChecked()
        enabled = self.isEnabled()
        if checked:
            bg = colors["checked_bg"]
            fg = colors["checked_fg"]
            border = colors["checked_border"]
        elif hover or down:
            bg = colors["hover_bg"]
            fg = colors["hover_fg"]
            border = colors["hover_border"]
        else:
            bg = colors["base_bg"]
            fg = colors["base_fg"]
            border = colors["base_border"]
        if not enabled:
            bg = self._ui_palette().get("button", "#352E27")
            fg = self._ui_palette().get("text", "#F4EEE7")
            border = self._ui_palette().get("line", "#4B4036")
        try:
            p.setPen(QtGui.QPen(QtGui.QColor(border), 1))
            p.setBrush(QtGui.QBrush(QtGui.QColor(bg)))
            p.drawRoundedRect(rect, radius, radius)
        except Exception:
            pass
        text_rect = rect.adjusted(5, 0, -5, 0)
        font = self.font()
        try:
            font.setBold(False)
            if role in ("nav", "side", "top", "primary", "danger"):
                font.setPointSize(7)
            p.setFont(font)
        except Exception:
            pass
        try:
            p.setPen(QtGui.QColor(fg))
            align = QtCore.Qt.AlignCenter
            if role == "nav":
                align = QtCore.Qt.AlignVCenter | QtCore.Qt.AlignLeft
                text_rect = rect.adjusted(10, 0, -6, 0)
            p.drawText(text_rect, align, self.text())
        except Exception:
            pass

    def sizeHint(self):
        try:
            fm = QtGui.QFontMetrics(self.font())
            txt = safe_str(self.text(), "")
            role, _radius = self._role_metrics()
            if role == "nav":
                return QtCore.QSize(116, 28)
            if role == "side":
                return QtCore.QSize(116, 28)
            w = max(52, fm.horizontalAdvance(txt) + 16)
            h = 28 if role in ("top", "primary", "danger") else 22
            return QtCore.QSize(w, h)
        except Exception:
            return QtCore.QSize(84, 28)

    def setCurrentWidget(self, widget):
        idx = self.indexOf(widget)
        if idx >= 0:
            self.setCurrentIndex(idx)

    def currentIndex(self):
        try:
            return int(self.stack.currentIndex())
        except Exception:
            return 0

    def currentWidget(self):
        try:
            return self.stack.currentWidget()
        except Exception:
            return None

    def tabText(self, index):
        try:
            index = int(index)
            return self._titles[index]
        except Exception:
            return ""

    def count(self):
        return len(self._pages)

    def setUsesScrollButtons(self, *_args, **_kwargs):
        return None

    def setElideMode(self, *_args, **_kwargs):
        return None

    def setDocumentMode(self, *_args, **_kwargs):
        return None


# ─── Chrome扩展推送接收服务器 ────────────────────────────────────────────────

try:
    import http.server as _http_server
    import json as _json_mod
    _HAS_HTTP_SERVER = True
except Exception:
    _HAS_HTTP_SERVER = False

_pbr_push_server_instance = None
_pbr_push_server_thread = None
_pbr_push_callback = None  # callable(url_list)
_pbr_push_server_port = None
_pbr_push_callback_lock = None
_pbr_push_ui_instance = None


def _json_response_bytes(data):
    return _json_mod.dumps(data, ensure_ascii=False).encode("utf-8")


def _safe_html_text(text):
    try:
        import html
        return html.escape(safe_str(text, ""))
    except Exception:
        return safe_str(text, "")


def _safe_local_path(path):
    path = safe_str(path, "").strip()
    if not path:
        return ""
    try:
        return os.path.abspath(path)
    except Exception:
        return path


def _web_ai_vendor_file(rel_path):
    rel_path = safe_str(rel_path, "").replace("\\", "/").lstrip("/")
    if not rel_path.startswith("vendor/katex/") or ".." in rel_path.split("/"):
        return None, "", ""
    try:
        base = os.path.abspath(os.path.join(os.path.dirname(__file__), "chrome_extension"))
    except Exception:
        base = os.path.abspath("chrome_extension")
    full = os.path.abspath(os.path.join(base, rel_path))
    try:
        if not full.startswith(base + os.sep) or not os.path.isfile(full):
            return None, "", ""
        ext = os.path.splitext(full)[1].lower()
        content_type = {
            ".css": "text/css; charset=utf-8",
            ".js": "application/javascript; charset=utf-8",
            ".woff2": "font/woff2",
            ".woff": "font/woff",
            ".ttf": "font/ttf",
        }.get(ext, "application/octet-stream")
        with open(full, "rb") as f:
            return f.read(), content_type, full
    except Exception:
        return None, "", ""


def _web_ai_html():
    return """<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>室内场景助手 Web AI</title>
<link rel="stylesheet" href="/vendor/katex/katex.min.css">
<style>
  :root {
    --bg: #ffffff;
    --sidebar: #f7f7f8;
    --panel: #ffffff;
    --control: #ffffff;
    --control-hover: #f7f7f8;
    --line: #e5e5e5;
    --text: #202123;
    --muted: #6b7280;
    --primary: #10a37f;
    --primary-strong: #0d8f70;
    --user: #f7f7f8;
    --assistant: #ffffff;
    --tool: #f1f5f9;
    --ok: #15803d;
    --warn: #b45309;
    --danger: #c2410c;
    --shadow: 0 1px 2px rgba(0,0,0,.05);
    --chrome-accent: var(--primary);
  }
  @media (prefers-color-scheme: dark) {
    :root {
      --bg: #212121;
      --sidebar: #171717;
      --panel: #212121;
      --control: #2f2f2f;
      --control-hover: #3a3a3a;
      --line: #343541;
      --text: #ececf1;
      --muted: #b4b4b4;
      --user: #2f2f2f;
      --assistant: #212121;
      --tool: #2a2b32;
      --shadow: none;
    }
  }
  * { box-sizing: border-box; }
  body {
    margin: 0;
    color: var(--text);
    font: 14px/1.5 -apple-system, BlinkMacSystemFont, "Segoe UI", "Microsoft YaHei UI", sans-serif;
    background: var(--bg);
    min-height: 100vh;
    overflow: hidden;
  }
  .shell {
    display: grid;
    grid-template-columns: 320px minmax(0,1fr);
    height: 100vh;
    transition: grid-template-columns .22s cubic-bezier(.2,.8,.2,1);
  }
  body.sidebar-collapsed .shell { grid-template-columns: 0 minmax(0,1fr); }
  .panel {
    background: var(--panel);
    border: 1px solid var(--line);
    box-shadow: var(--shadow);
  }
  .sidebar {
    background: var(--sidebar);
    border-width: 0 1px 0 0;
    box-shadow: none;
    padding: 14px;
    display: flex;
    flex-direction: column;
    gap: 12px;
    overflow: auto;
    height: 100vh;
    min-width: 0;
    transition: transform .22s cubic-bezier(.2,.8,.2,1), opacity .18s ease;
  }
  body.sidebar-collapsed .sidebar { padding: 0; border: 0; overflow: hidden; visibility: hidden; opacity: 0; }
  .titlebox {
    padding: 4px 2px 10px;
  }
  .titlebox h1 { margin: 0; font-size: 18px; font-weight: 700; }
  .titlebox p { margin: 4px 0 0; color: var(--muted); font-size: 12px; }
  .group { padding: 12px; background: var(--control); border: 1px solid var(--line); border-radius: 8px; }
  .group h2 { margin: 0 0 10px; font-size: 13px; font-weight: 700; color: var(--text); }
  .field { margin-bottom: 10px; }
  .field:last-child { margin-bottom: 0; }
  label { display: block; margin-bottom: 5px; font-size: 12px; color: var(--muted); font-weight: 600; }
  input[type=text], input[type=password], input[type=number], select, textarea {
    width: 100%;
    border: 1px solid var(--line);
    border-radius: 8px;
    background: var(--control);
    color: var(--text);
    padding: 9px 10px;
    outline: none;
  }
  input:focus, select:focus, textarea:focus { border-color: #b5b5b5; box-shadow: 0 0 0 2px rgba(16,163,127,.12); }
  input::placeholder, textarea::placeholder { color: var(--muted); opacity: .9; }
  textarea { resize: none; min-height: 92px; }
  .grid2 { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
  .row { display: flex; gap: 8px; align-items: center; flex-wrap: wrap; }
  button {
    border: 1px solid var(--line);
    border-radius: 8px;
    background: var(--control);
    color: var(--text);
    padding: 8px 11px;
    font: inherit;
    font-weight: 600;
    cursor: pointer;
  }
  button:hover { background: var(--control-hover); }
  button.primary { background: var(--primary); color: #fff; border-color: var(--primary); }
  button.primary:hover { background: var(--primary-strong); border-color: var(--primary-strong); }
  button.danger { color: #fff; background: var(--danger); border-color: var(--danger); }
  button:disabled { opacity: .5; cursor: default; }
  .main { min-width: 0; height: 100vh; display: flex; flex-direction: column; background: var(--bg); }
  .toolbar {
    border-width: 0 0 1px;
    box-shadow: none;
    padding: 10px 18px;
    display:flex;
    gap:10px;
    align-items:center;
    justify-content:space-between;
    position: relative;
    flex-shrink: 0;
  }
  .toolbar-left { display: flex; align-items: center; gap: 10px; min-width: 0; }
  .icon-button {
    width: 38px;
    height: 38px;
    padding: 0;
    border-radius: 999px;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    font-size: 18px;
    transition: background .16s ease, transform .16s ease;
  }
  .icon-button:hover { transform: translateY(-1px); }
  .toolbar-context { display: none; }
  .more-wrap { position: relative; }
  .action-menu {
    position: absolute;
    right: 0;
    top: 44px;
    width: 240px;
    padding: 8px;
    border: 1px solid var(--line);
    border-radius: 14px;
    background: var(--control);
    box-shadow: 0 14px 34px rgba(0,0,0,.16);
    z-index: 20;
    display: none;
    transform-origin: top right;
    animation: menuIn .14s cubic-bezier(.2,.8,.2,1);
  }
  .action-menu.open { display: flex; flex-direction: column; gap: 6px; }
  .action-menu button { width: 100%; text-align: left; justify-content: flex-start; }
  .toolbar .meta { color: var(--muted); font-size: 12px; }
  .chat { flex: 1; min-height: 0; border: 0; box-shadow: none; padding: 0; display: flex; flex-direction: column; }
  .messages { flex: 1; min-height: 0; overflow: auto; padding: 22px max(18px, calc((100% - 860px) / 2)); }
  .messages.empty { display: flex; align-items: center; justify-content: center; padding-bottom: 12vh; }
  .empty-state { text-align: center; color: var(--text); animation: fadeUp .28s ease both; }
  .empty-state h1 { margin: 0; font-size: clamp(24px, 4vw, 34px); font-weight: 650; letter-spacing: 0; }
  .msg { margin-bottom: 18px; display: flex; animation: fadeUp .22s ease both; }
  .msg.left { justify-content: flex-start; }
  .msg.right { justify-content: flex-end; }
  .msg.center { justify-content: center; }
  .bubble {
    max-width: min(760px, 86%);
    padding: 12px 14px;
    border-radius: 12px;
    border: 0;
    background: var(--assistant);
    white-space: pre-wrap;
    overflow-wrap: anywhere;
  }
  .msg.left .bubble { padding-left: 0; }
  .msg.right .bubble { background: var(--user); }
  .msg.center .bubble { background: var(--tool); max-width: 72%; color: var(--muted); font-size: 12px; }
  .name { display:block; margin-bottom: 6px; font-size: 12px; font-weight: 700; color: var(--muted); }
  .images { margin-top: 8px; display: flex; gap: 8px; flex-wrap: wrap; }
  .images img { width: 104px; height: 104px; object-fit: cover; border-radius: 8px; border: 1px solid var(--line); background: var(--control); }
  .bubble p { margin: 0 0 10px; }
  .bubble p:last-child { margin-bottom: 0; }
  .bubble ul, .bubble ol { margin: 8px 0 8px 22px; padding: 0; }
  .bubble li { margin: 4px 0; }
  .bubble h1, .bubble h2, .bubble h3 { margin: 14px 0 8px; line-height: 1.25; }
  .bubble h1 { font-size: 20px; }
  .bubble h2 { font-size: 17px; }
  .bubble h3 { font-size: 15px; }
  .bubble code {
    border-radius: 5px;
    background: var(--tool);
    padding: 2px 5px;
    font-family: Consolas, "SFMono-Regular", Menlo, monospace;
    font-size: 13px;
  }
  .bubble pre {
    margin: 10px 0;
    overflow: auto;
    border-radius: 8px;
    background: #0d1117;
    color: #e6edf3;
    padding: 12px;
  }
  .bubble pre code { background: transparent; color: inherit; padding: 0; }
  .bubble blockquote { margin: 10px 0; padding-left: 12px; border-left: 3px solid var(--line); color: var(--muted); }
  .bubble a { color: var(--chrome-accent); text-decoration: none; }
  .bubble a:hover { text-decoration: underline; }
  .math-inline, .math-block {
    font-family: Cambria Math, STIX Two Math, "Times New Roman", serif;
    background: transparent;
    border: 0;
    color: var(--text);
  }
  .katex { color: var(--text); }
  .katex-display { margin: 12px 0; overflow-x: auto; overflow-y: hidden; }
  .math-inline { display: inline-flex; align-items: center; gap: 2px; padding: 0 2px; vertical-align: middle; }
  .math-block { display: flex; align-items: center; justify-content: center; gap: 2px; margin: 12px 0; padding: 12px; border-radius: 8px; overflow-x: auto; text-align: center; background: var(--tool); }
  .frac { display: inline-flex; flex-direction: column; align-items: center; vertical-align: middle; margin: 0 3px; line-height: 1.05; }
  .frac-num { border-bottom: 1px solid currentColor; padding: 0 4px 2px; }
  .frac-den { padding: 2px 4px 0; }
  .sqrt { display: inline-flex; align-items: stretch; margin: 0 2px; }
  .sqrt-symbol { font-size: 1.24em; line-height: 1; }
  .sqrt-body { border-top: 1px solid currentColor; padding: 1px 3px 0; }
  .math-inline sup, .math-inline sub, .math-block sup, .math-block sub { font-size: .72em; line-height: 0; }
  .composer {
    width: min(860px, calc(100% - 36px));
    margin: 0 auto 16px;
    padding: 7px;
    border: 1px solid var(--line);
    border-radius: 28px;
    box-shadow: 0 8px 26px rgba(0,0,0,.08);
    background: var(--control);
    transition: border-color .16s ease, box-shadow .16s ease, transform .16s ease;
  }
  .composer:focus-within {
    border-color: color-mix(in srgb, var(--text) 24%, var(--line));
    box-shadow: 0 10px 30px rgba(0,0,0,.12);
  }
  .composer-line { display: grid; grid-template-columns: 34px minmax(0, 1fr) 34px; gap: 8px; align-items: end; }
  .composer textarea {
    border: 0;
    box-shadow: none;
    background: transparent;
    min-height: 34px;
    max-height: 132px;
    padding: 6px 4px;
    font-size: 15px;
    line-height: 22px;
  }
  .composer textarea:focus { border-color: transparent; box-shadow: none; }
  .composer-status { display: none; }
  #file-input { display: none; }
  .tool-button {
    width: 34px;
    height: 34px;
    padding: 0;
    margin: 0;
    border: 1px solid var(--line);
    border-radius: 999px;
    background: var(--control);
    color: var(--text);
    display: inline-flex;
    align-items: center;
    justify-content: center;
    font-size: 18px;
    cursor: pointer;
    transition: background .16s ease, transform .16s ease;
  }
  .tool-button:hover { background: var(--control-hover); transform: translateY(-1px); }
  #btn-send {
    border-radius: 999px;
    width: 34px;
    height: 34px;
    min-width: 34px;
    min-height: 34px;
    padding: 0;
    font-size: 18px;
    line-height: 1;
    transition: background .16s ease, transform .16s ease, opacity .16s ease;
  }
  #btn-send:hover:not(:disabled) { transform: translateY(-1px); }
  .status { min-height: 20px; font-size: 12px; color: var(--muted); }
  .diag {
    margin: 0 18px 18px;
    padding: 12px;
    white-space: pre-wrap;
    max-height: 220px;
    overflow: auto;
    color: var(--text);
    font-size: 12px;
    border-radius: 8px;
    background: var(--tool);
    border: 1px solid var(--line);
  }
  .statusbar, #action-status { display: none; }
  .chips { display:flex; gap:8px; flex-wrap:wrap; }
  .chip {
    padding: 6px 10px;
    border-radius: 999px;
    border: 1px solid var(--line);
    background: var(--control);
    font-size: 12px;
    color: var(--muted);
  }
  .hidden { display: none !important; }
  @keyframes fadeUp {
    from { opacity: 0; transform: translateY(8px); }
    to { opacity: 1; transform: translateY(0); }
  }
  @keyframes menuIn {
    from { opacity: 0; transform: translateY(-4px) scale(.98); }
    to { opacity: 1; transform: translateY(0) scale(1); }
  }
  @media (max-width: 1120px) {
    .shell { grid-template-columns: 1fr; }
    .sidebar { position: fixed; inset: 0 auto 0 0; width: min(320px, 88vw); z-index: 30; border-width: 0 1px 0 0; }
    body.sidebar-collapsed .sidebar { visibility: hidden; transform: translateX(-8px); }
    body.sidebar-collapsed .shell { grid-template-columns: 1fr; }
    body:not(.sidebar-collapsed)::after { content: ""; position: fixed; inset: 0; background: rgba(0,0,0,.42); z-index: 25; }
    .bubble { max-width: 100%; }
  }
</style>
</head>
<body class="sidebar-collapsed">
<div class="shell">
  <aside class="panel sidebar">
    <div class="titlebox">
      <h1>室内场景助手 Web AI</h1>
      <p>浏览器侧 AI 面板，直接复用 3ds Max 当前配置和聊天逻辑。</p>
    </div>
    <section class="group">
      <h2>连接状态</h2>
      <div class="chips">
        <div class="chip" id="state-provider">方案</div>
        <div class="chip" id="state-model">模型</div>
        <div class="chip" id="state-api">接口</div>
      </div>
      <div class="status" id="server-status"></div>
    </section>
    <section class="group hidden">
      <h2>AI 配置</h2>
      <div class="field"><label>方案</label><input id="provider" type="text"></div>
      <div class="field"><label>接口类型</label><input id="api_type" type="text"></div>
      <div class="field"><label>Base URL</label><input id="base_url" type="text"></div>
      <div class="field"><label>模型名</label><input id="model" type="text"></div>
      <div class="field"><label>API Key</label><input id="api_key" type="password" placeholder="留空表示不改"></div>
      <div class="grid2">
        <div class="field"><label>温度</label><input id="temperature" type="number" min="0" max="2" step="0.1"></div>
        <div class="field"><label>保留轮数</label><input id="history" type="number" min="0" max="20" step="1"></div>
      </div>
      <div class="grid2">
        <div class="field"><label>助手昵称</label><input id="robot_name" type="text"></div>
        <div class="field"><label>用户昵称</label><input id="user_name" type="text"></div>
      </div>
      <div class="field"><label>提问模板</label><input id="template" type="text"></div>
      <div class="row">
        <button class="primary" id="btn-save-config">保存配置</button>
        <button id="btn-test">测试连接</button>
        <button id="btn-diagnose">完整诊断</button>
        <button id="btn-toggle-diag">诊断日志</button>
      </div>
    </section>
    <section class="group">
      <h2>Max 联动配置</h2>
      <div class="status">Max 联动的 AI 参数只在 3ds Max 插件里修改。网页端只负责聊天、同步状态和常用操作，避免两边配置互相覆盖。</div>
    </section>
  </aside>
  <main class="main">
    <section class="panel toolbar">
      <div class="toolbar-left">
        <button class="icon-button" id="btn-sidebar-toggle" title="收起/展开边栏">☰</button>
        <div class="toolbar-context">
        <div class="chips">
          <div class="chip hidden" id="chat-font-chip">聊天字号 13</div>
          <div class="chip hidden" id="edit-mode-chip">图片编辑模式：关</div>
        </div>
        <div class="meta" id="scene-meta"></div>
        </div>
      </div>
      <div class="more-wrap">
        <button class="icon-button" id="btn-more-actions" title="更多操作">⋯</button>
        <div class="action-menu" id="action-menu">
          <button id="btn-refresh">刷新状态</button>
          <button id="btn-scene-summary">插入场景摘要</button>
          <button id="btn-recent-log">插入最近日志</button>
          <button class="danger" id="btn-clear-chat">清空对话</button>
        </div>
      </div>
    </section>
    <section class="panel chat">
      <div class="messages" id="messages"></div>
      <div class="composer">
        <div class="composer-line">
          <label class="tool-button" for="file-input" title="添加图片">＋</label>
          <input id="file-input" type="file" multiple accept=".png,.jpg,.jpeg,.bmp,.tif,.tiff,.webp,.gif,.exr,.hdr,image/*">
          <textarea id="prompt" placeholder="在这里输入问题。Enter 发送，Shift+Enter 换行。"></textarea>
          <button class="primary" id="btn-send" title="发送">↑</button>
        </div>
        <div class="status composer-status" id="image-status"></div>
        <div class="status" id="action-status"></div>
      </div>
    </section>
    <section class="panel diag hidden" id="diag-box"></section>
  </main>
</div>
<script src="/vendor/katex/katex.min.js"></script>
<script>
const els = {
  provider: document.getElementById('provider'),
  apiType: document.getElementById('api_type'),
  baseUrl: document.getElementById('base_url'),
  model: document.getElementById('model'),
  apiKey: document.getElementById('api_key'),
  temperature: document.getElementById('temperature'),
  history: document.getElementById('history'),
  robotName: document.getElementById('robot_name'),
  userName: document.getElementById('user_name'),
  template: document.getElementById('template'),
  serverStatus: document.getElementById('server-status'),
  stateProvider: document.getElementById('state-provider'),
  stateModel: document.getElementById('state-model'),
  stateApi: document.getElementById('state-api'),
  sceneMeta: document.getElementById('scene-meta'),
  fontChip: document.getElementById('chat-font-chip'),
  editChip: document.getElementById('edit-mode-chip'),
  messages: document.getElementById('messages'),
  prompt: document.getElementById('prompt'),
  fileInput: document.getElementById('file-input'),
  imageStatus: document.getElementById('image-status'),
  actionStatus: document.getElementById('action-status'),
  diagBox: document.getElementById('diag-box'),
  actionMenu: document.getElementById('action-menu')
};
let state = null;
let diagVisible = false;
const GREETINGS = [
  "今天想整理哪一部分场景？",
  "需要我帮你排查什么问题？",
  "今天要优化哪个模型或材质？",
  "想从哪里开始？"
];

function escapeHtml(v) {
  return String(v || '').replace(/[&<>\"']/g, (m) => ({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;',\"'\":'&#39;'}[m]));
}
function renderInlineMarkdown(text) {
  let s = escapeHtml(text);
  s = s.replace(/\\\\\\((.+?)\\\\\\)/gs, (_m, expr) => renderMath(expr.trim(), false));
  s = s.replace(/\\$(?!\\$)(.+?)(?<!\\$)\\$/gs, (_m, expr) => renderMath(expr.trim(), false));
  s = s.replace(/`([^`]+)`/g, (_m, code) => `<code>${escapeHtml(code)}</code>`);
  s = s.replace(/\\*\\*([^*]+)\\*\\*/g, "<strong>$1</strong>");
  s = s.replace(/__([^_]+)__/g, "<strong>$1</strong>");
  s = s.replace(/\\*([^*\\n]+)\\*/g, "<em>$1</em>");
  s = s.replace(/_([^_\\n]+)_/g, "<em>$1</em>");
  s = s.replace(/\\[([^\\]]+)\\]\\((https?:\\/\\/[^)\\s]+)\\)/g, '<a href="$2" target="_blank" rel="noreferrer">$1</a>');
  s = s.replace(/(https?:\\/\\/[^\\s<]+)/g, '<a href="$1" target="_blank" rel="noreferrer">$1</a>');
  return s;
}
const MATH_SYMBOLS = {
  "\\\\alpha": "α", "\\\\beta": "β", "\\\\gamma": "γ", "\\\\delta": "δ", "\\\\epsilon": "ε", "\\\\theta": "θ",
  "\\\\lambda": "λ", "\\\\mu": "μ", "\\\\pi": "π", "\\\\rho": "ρ", "\\\\sigma": "σ", "\\\\phi": "φ",
  "\\\\omega": "ω", "\\\\Delta": "Δ", "\\\\Theta": "Θ", "\\\\Lambda": "Λ", "\\\\Pi": "Π", "\\\\Sigma": "Σ",
  "\\\\Phi": "Φ", "\\\\Omega": "Ω", "\\\\times": "×", "\\\\cdot": "·", "\\\\pm": "±", "\\\\le": "≤",
  "\\\\ge": "≥", "\\\\neq": "≠", "\\\\approx": "≈", "\\\\infty": "∞", "\\\\sum": "∑", "\\\\int": "∫",
  "\\\\partial": "∂", "\\\\nabla": "∇", "\\\\rightarrow": "→", "\\\\leftarrow": "←", "\\\\to": "→"
};
function extractBrace(src, start) {
  if (src[start] !== "{") return null;
  let depth = 0;
  for (let i = start; i < src.length; i++) {
    if (src[i] === "{") depth++;
    if (src[i] === "}") depth--;
    if (depth === 0) return { value: src.slice(start + 1, i), end: i + 1 };
  }
  return null;
}
function renderKatex(expr, displayMode) {
  try {
    if (window.katex && typeof window.katex.renderToString === "function") {
      return window.katex.renderToString(expr, {
        displayMode: !!displayMode,
        throwOnError: false,
        strict: "ignore",
        trust: false,
        output: "html"
      });
    }
  } catch (_e) {}
  return "";
}
function renderMath(expr, displayMode) {
  const katexHtml = renderKatex(expr, displayMode);
  if (katexHtml) return katexHtml;
  let src = String(expr || "").trim();
  function walk(s) {
    let out = "";
    for (let i = 0; i < s.length; i++) {
      if (s.startsWith("\\\\frac", i)) {
        const a = extractBrace(s, i + 5);
        const b = a ? extractBrace(s, a.end) : null;
        if (a && b) {
          out += `<span class="frac"><span class="frac-num">${walk(a.value)}</span><span class="frac-den">${walk(b.value)}</span></span>`;
          i = b.end - 1;
          continue;
        }
      }
      if (s.startsWith("\\\\sqrt", i)) {
        const a = extractBrace(s, i + 5);
        if (a) {
          out += `<span class="sqrt"><span class="sqrt-symbol">√</span><span class="sqrt-body">${walk(a.value)}</span></span>`;
          i = a.end - 1;
          continue;
        }
      }
      const sym = Object.keys(MATH_SYMBOLS).find((key) => s.startsWith(key, i));
      if (sym) {
        out += MATH_SYMBOLS[sym];
        i += sym.length - 1;
        continue;
      }
      if ((s[i] === "^" || s[i] === "_") && i + 1 < s.length) {
        const tag = s[i] === "^" ? "sup" : "sub";
        if (s[i + 1] === "{") {
          const a = extractBrace(s, i + 1);
          if (a) {
            out += `<${tag}>${walk(a.value)}</${tag}>`;
            i = a.end - 1;
            continue;
          }
        }
        out += `<${tag}>${escapeHtml(s[i + 1])}</${tag}>`;
        i++;
        continue;
      }
      if (s[i] === "\\\\") continue;
      out += escapeHtml(s[i]);
    }
    return out;
  }
  return `<span class="${displayMode ? "math-block" : "math-inline"}">${walk(src)}</span>`;
}
function renderMarkdown(text) {
  const src = String(text || '').replace(/\\r\\n/g, '\\n');
  const blocks = [];
  let rest = src.replace(/```([a-zA-Z0-9_-]+)?\\n([\\s\\S]*?)```/g, (_m, lang, code) => {
    const token = `\\u0000CODE${blocks.length}\\u0000`;
    blocks.push(`<pre><code${lang ? ` data-lang="${escapeHtml(lang)}"` : ""}>${escapeHtml(code.trimEnd())}</code></pre>`);
    return token;
  });
  rest = rest.replace(/\\$\\$([\\s\\S]+?)\\$\\$/g, (_m, expr) => {
    const token = `\\u0000CODE${blocks.length}\\u0000`;
    blocks.push(renderMath(expr.trim(), true));
    return token;
  });
  rest = rest.replace(/\\\\\\[([\\s\\S]+?)\\\\\\]/g, (_m, expr) => {
    const token = `\\u0000CODE${blocks.length}\\u0000`;
    blocks.push(renderMath(expr.trim(), true));
    return token;
  });
  const lines = rest.split('\\n');
  const out = [];
  let list = null;
  function closeList() {
    if (list) {
      out.push(`</${list}>`);
      list = null;
    }
  }
  for (const raw of lines) {
    const line = raw.trimEnd();
    if (!line.trim()) {
      closeList();
      continue;
    }
    const tokenMatch = line.match(/^\\u0000CODE(\\d+)\\u0000$/);
    if (tokenMatch) {
      closeList();
      out.push(blocks[Number(tokenMatch[1])] || '');
      continue;
    }
    const heading = line.match(/^(#{1,3})\\s+(.+)$/);
    if (heading) {
      closeList();
      out.push(`<h${heading[1].length}>${renderInlineMarkdown(heading[2])}</h${heading[1].length}>`);
      continue;
    }
    const bullet = line.match(/^\\s*[-*]\\s+(.+)$/);
    if (bullet) {
      if (list !== 'ul') {
        closeList();
        list = 'ul';
        out.push('<ul>');
      }
      out.push(`<li>${renderInlineMarkdown(bullet[1])}</li>`);
      continue;
    }
    const ordered = line.match(/^\\s*\\d+\\.\\s+(.+)$/);
    if (ordered) {
      if (list !== 'ol') {
        closeList();
        list = 'ol';
        out.push('<ol>');
      }
      out.push(`<li>${renderInlineMarkdown(ordered[1])}</li>`);
      continue;
    }
    if (line.startsWith('> ')) {
      closeList();
      out.push(`<blockquote>${renderInlineMarkdown(line.slice(2))}</blockquote>`);
      continue;
    }
    closeList();
    out.push(`<p>${renderInlineMarkdown(line)}</p>`);
  }
  closeList();
  return out.join('');
}
function roleAlign(role) {
  if (role === 'user') return 'right';
  if (role === 'system' || role === 'script_running') return 'center';
  return 'left';
}
function roleName(msg, cfg) {
  if (msg.role === 'ai_thinking') return '';
  if (msg.role === 'user') return (cfg.user_name || '用户') + ':';
  if (msg.role === 'assistant') return (cfg.robot_name || 'AI小助手') + ':';
  if (msg.role === 'script_result') return '执行结果:';
  if (msg.role === 'script_running') return '处理中:';
  if (msg.role === 'system') return '提示:';
  return (msg.role || '消息') + ':';
}
function setActionStatus(text, type) {
  els.actionStatus.textContent = text || '';
  els.actionStatus.style.color = type === 'error' ? 'var(--danger)' : (type === 'ok' ? 'var(--ok)' : 'var(--muted)');
}
function renderMessages() {
  const cfg = (state && state.config) || {};
  const arr = (state && state.messages) || [];
  if (!arr.length) {
    const seed = new Date().getDate() % GREETINGS.length;
    els.messages.classList.add('empty');
    els.messages.innerHTML = `<div class="empty-state"><h1>${escapeHtml(GREETINGS[seed])}</h1></div>`;
    return;
  }
  els.messages.classList.remove('empty');
  els.messages.innerHTML = arr.map((msg) => {
    const imgs = Array.isArray(msg.images) ? msg.images : [];
    return `<div class="msg ${roleAlign(msg.role)}"><div class="bubble"><span class="name">${escapeHtml(roleName(msg, cfg))}</span>${renderMarkdown(msg.content || '')}${imgs.length ? `<div class="images">${imgs.map((it) => `<a href="${escapeHtml(it.url || '#')}" target="_blank" rel="noreferrer"><img src="${escapeHtml(it.thumb || it.url || '')}"></a>`).join('')}</div>` : ''}</div></div>`;
  }).join('');
  els.messages.scrollTop = els.messages.scrollHeight;
}
function resizePrompt() {
  els.prompt.style.height = '0px';
  const next = Math.min(Math.max(34, els.prompt.scrollHeight), 132);
  els.prompt.style.height = next + 'px';
}
function syncForm() {
  if (!state) return;
  const cfg = state.config || {};
  els.provider.value = cfg.provider || '';
  els.apiType.value = cfg.api_type || '';
  els.baseUrl.value = cfg.base_url || '';
  els.model.value = cfg.model || '';
  els.temperature.value = cfg.temperature ?? 0.3;
  els.history.value = cfg.history ?? 8;
  els.robotName.value = cfg.robot_name || 'AI小助手';
  els.userName.value = cfg.user_name || '用户';
  els.template.value = cfg.template || '';
  els.serverStatus.textContent = '本地服务端口 ' + (state.port || 19527) + '，3ds Max 端 ' + (state.online ? '已连接' : '未连接');
  els.stateProvider.textContent = '方案: ' + (cfg.provider || '未设置');
  els.stateModel.textContent = '模型: ' + (cfg.model || '未设置');
  els.stateApi.textContent = '接口: ' + (cfg.api_type || '未设置');
  els.sceneMeta.textContent = state.scene_summary_short || '';
  els.fontChip.textContent = '聊天字号 ' + String(cfg.display_font_size || 8);
  els.editChip.textContent = '图片编辑模式：' + ((cfg.image_edit_mode) ? '开' : '关');
  els.imageStatus.textContent = state.pending_image_text || '未附加图片';
  const diag = state.diagnosis || '';
  els.diagBox.textContent = diag;
  els.diagBox.classList.toggle('hidden', !diagVisible || !diag);
  renderMessages();
}
async function api(url, payload) {
  const resp = await fetch(url, {
    method: payload ? 'POST' : 'GET',
    headers: payload ? { 'Content-Type': 'application/json' } : undefined,
    body: payload ? JSON.stringify(payload) : undefined
  });
  const data = await resp.json().catch(() => ({}));
  if (!resp.ok || data.ok === false) throw new Error(data.error || data.message || ('HTTP ' + resp.status));
  return data;
}
async function loadState() {
  state = await api('/api/ai/state');
  syncForm();
}
function collectConfigPatch() {
  return {
    provider: els.provider.value.trim(),
    api_type: els.apiType.value.trim(),
    base_url: els.baseUrl.value.trim(),
    model: els.model.value.trim(),
    api_key: els.apiKey.value,
    temperature: Number(els.temperature.value || 0.3),
    history: Number(els.history.value || 8),
    robot_name: els.robotName.value.trim(),
    user_name: els.userName.value.trim(),
    template: els.template.value.trim()
  };
}
async function saveConfig() {
  setActionStatus('正在保存配置…');
  await api('/api/ai/config', collectConfigPatch());
  els.apiKey.value = '';
  await loadState();
  setActionStatus('配置已保存', 'ok');
}
async function sendMessage() {
  if (state && state.sending) return;
  const text = els.prompt.value.trim();
  if (!text) return;
  setActionStatus('正在发送…');
  const files = Array.from(els.fileInput.files || []);
  const images = await Promise.all(files.map((f) => new Promise((resolve, reject) => {
    const rd = new FileReader();
    rd.onload = () => resolve({ name: f.name, data_url: String(rd.result || '') });
    rd.onerror = () => reject(new Error('读取图片失败: ' + f.name));
    rd.readAsDataURL(f);
  })));
  const data = await api('/api/ai/send', { text, images });
  state = data;
  els.prompt.value = '';
  resizePrompt();
  els.fileInput.value = '';
  syncForm();
  setActionStatus('发送完成', 'ok');
}
document.getElementById('btn-refresh').addEventListener('click', () => loadState().catch((e) => setActionStatus(String(e), 'error')));
document.getElementById('btn-save-config').addEventListener('click', () => saveConfig().catch((e) => setActionStatus(String(e), 'error')));
document.getElementById('btn-send').addEventListener('click', () => sendMessage().catch((e) => setActionStatus(String(e), 'error')));
document.getElementById('btn-scene-summary').addEventListener('click', async () => {
  setActionStatus('正在插入场景摘要…');
  state = await api('/api/ai/scene_summary');
  syncForm();
  setActionStatus('已插入场景摘要', 'ok');
});
document.getElementById('btn-recent-log').addEventListener('click', async () => {
  setActionStatus('正在插入最近日志…');
  state = await api('/api/ai/recent_log');
  syncForm();
  setActionStatus('已插入最近日志', 'ok');
});
const btnToggleEdit = document.getElementById('btn-toggle-edit');
if (btnToggleEdit) {
  btnToggleEdit.addEventListener('click', async () => {
    setActionStatus('Max 联动图片编辑模式请在 3ds Max 插件里修改。', 'error');
  });
}
document.getElementById('btn-clear-chat').addEventListener('click', async () => {
  try {
    setActionStatus('正在清空对话…');
    state = await api('/api/ai/clear');
    syncForm();
    setActionStatus('对话已清空', 'ok');
  } catch (e) {
    setActionStatus(String(e), 'error');
  }
});
document.getElementById('btn-test').addEventListener('click', async () => {
  setActionStatus('正在测试连接…');
  state = await api('/api/ai/test');
  syncForm();
  setActionStatus('连接测试完成', 'ok');
});
document.getElementById('btn-diagnose').addEventListener('click', async () => {
  setActionStatus('正在完整诊断…');
  state = await api('/api/ai/diagnose');
  diagVisible = true;
  syncForm();
  setActionStatus('完整诊断完成', 'ok');
});
document.getElementById('btn-toggle-diag').addEventListener('click', () => {
  diagVisible = !diagVisible;
  syncForm();
});
document.getElementById('btn-sidebar-toggle').addEventListener('click', () => {
  document.body.classList.toggle('sidebar-collapsed');
});
document.getElementById('btn-more-actions').addEventListener('click', (e) => {
  e.stopPropagation();
  els.actionMenu.classList.toggle('open');
});
els.actionMenu.addEventListener('click', (e) => {
  if (e.target && e.target.tagName === 'BUTTON') {
    els.actionMenu.classList.remove('open');
  }
});
document.addEventListener('click', () => {
  els.actionMenu.classList.remove('open');
});
els.prompt.addEventListener('keydown', (e) => {
  if (e.key === 'Enter' && !e.shiftKey) {
    e.preventDefault();
    sendMessage().catch((err) => setActionStatus(String(err), 'error'));
  }
});
els.prompt.addEventListener('input', resizePrompt);
els.fileInput.addEventListener('change', () => {
  const files = Array.from(els.fileInput.files || []);
  els.imageStatus.textContent = files.length ? ('待发送图片：' + files.map((f) => f.name).join('；')) : '未附加图片';
});
resizePrompt();
loadState().catch((e) => setActionStatus(String(e), 'error'));
</script>
</body>
</html>"""


def validate_local_service_port(port):
    try:
        port = int(port)
    except Exception:
        return False, "端口必须是数字。"
    if port < 1025 or port > 65535:
        return False, "端口范围必须在 1025-65535 之间。"
    if port in (80, 443):
        return False, "不要使用 80 或 443，这些通常被系统/Web 服务占用。"
    if port in (21, 22, 25, 53, 110, 135, 139, 445, 3306, 3389, 5432):
        return False, "不要使用常见系统/数据库端口，请改用 1025 以上的自定义端口。"
    return True, ""


def is_local_port_available(port, host="127.0.0.1"):
    try:
        import socket
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        except Exception:
            pass
        try:
            s.bind((host, int(port)))
            return True, ""
        finally:
            try:
                s.close()
            except Exception:
                pass
    except Exception as e:
        return False, str(e)


class _PBRPushHandler(_http_server.BaseHTTPRequestHandler if _HAS_HTTP_SERVER else object):
    def log_message(self, *_args):
        pass  # 静默日志

    def _send_cors(self, code=200, content_type="application/json"):
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()

    def _ui(self):
        return _pbr_push_ui_instance

    def _read_json_body(self):
        length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(length) if length > 0 else b"{}"
        if not body:
            return {}
        return _json_mod.loads(body.decode("utf-8"))

    def _send_json(self, code, data):
        self._send_cors(code, "application/json; charset=utf-8")
        self.wfile.write(_json_response_bytes(data))

    def _send_html(self, html_text):
        self._send_cors(200, "text/html; charset=utf-8")
        self.wfile.write(safe_str(html_text, "").encode("utf-8"))

    def _send_bytes(self, code, data, content_type):
        self._send_cors(code, content_type)
        self.wfile.write(data or b"")

    def do_OPTIONS(self):
        self._send_cors()

    def do_GET(self):
        if self.path == "/ping":
            self._send_json(200, {"status": "ok", "service": "PBRPushServer"})
        elif self.path in ("/ai", "/webai", "/web-ai"):
            self._send_html(_web_ai_html())
        elif self.path.startswith("/vendor/katex/"):
            data, content_type, _path = _web_ai_vendor_file(self.path.lstrip("/"))
            if data is None:
                self._send_json(404, {"error": "not found"})
                return
            self._send_bytes(200, data, content_type)
        elif self.path.startswith("/api/ai/state"):
            ui = self._ui()
            if not ui:
                self._send_json(503, {"ok": False, "error": "3ds Max 插件实例不可用"})
                return
            self._send_json(200, ui.ai_web_state_payload())
        else:
            self._send_json(404, {"error": "not found"})

    def do_POST(self):
        if self.path == "/push":
            try:
                data = self._read_json_body()
                urls = data.get("urls", [])
                auto_start_download = bool(data.get("auto_start_download", False))
                if isinstance(urls, str):
                    urls = [urls]
                if callable(_pbr_push_callback) and urls:
                    _pbr_push_callback(urls, auto_start_download=auto_start_download)
                self._send_json(200, {"ok": True, "count": len(urls), "auto_started": auto_start_download})
            except Exception as e:
                self._send_json(400, {"error": str(e)})
        elif self.path.startswith("/api/ai/"):
            ui = self._ui()
            if not ui:
                self._send_json(503, {"ok": False, "error": "3ds Max 插件实例不可用"})
                return
            try:
                data = self._read_json_body()
                if self.path == "/api/ai/config":
                    payload = ui.ai_web_update_config(data or {})
                elif self.path == "/api/ai/send":
                    payload = ui.ai_web_send_message(data or {})
                elif self.path == "/api/ai/test":
                    payload = ui.ai_web_run_test()
                elif self.path == "/api/ai/diagnose":
                    payload = ui.ai_web_run_diagnose()
                elif self.path == "/api/ai/scene_summary":
                    payload = ui.ai_web_insert_scene_summary()
                elif self.path == "/api/ai/recent_log":
                    payload = ui.ai_web_insert_recent_log()
                elif self.path == "/api/ai/toggle_image_edit":
                    payload = ui.ai_web_toggle_image_edit()
                elif self.path == "/api/ai/clear":
                    payload = ui.ai_web_clear_chat()
                else:
                    self._send_json(404, {"ok": False, "error": "not found"})
                    return
                self._send_json(200, payload)
            except Exception as e:
                self._send_json(400, {"ok": False, "error": str(e)})
        else:
            self._send_json(404, {"error": "not found"})


def start_pbr_push_server(port=19527, callback=None):
    global _pbr_push_server_instance, _pbr_push_server_thread, _pbr_push_callback, _pbr_push_server_port, _pbr_push_callback_lock
    if not _HAS_HTTP_SERVER:
        return False, "http.server 模块不可用"
    ok, msg = validate_local_service_port(port)
    if not ok:
        return False, msg
    if _pbr_push_server_instance is not None:
        return True, "已在运行（端口 {}）".format(_pbr_push_server_port or port)
    if _pbr_push_callback_lock is None:
        try:
            import threading as _threading
            _pbr_push_callback_lock = _threading.Lock()
        except Exception:
            _pbr_push_callback_lock = None
    _pbr_push_callback = callback
    try:
        server_cls = getattr(_http_server, "ThreadingHTTPServer", _http_server.HTTPServer)
        srv = server_cls(("127.0.0.1", port), _PBRPushHandler)
        import threading as _threading
        t = _threading.Thread(target=srv.serve_forever, daemon=True)
        t.start()
        _pbr_push_server_instance = srv
        _pbr_push_server_thread = t
        _pbr_push_server_port = int(port)
        return True, "已启动（端口 {}）".format(port)
    except Exception as e:
        return False, "启动失败：{}".format(e)


def stop_pbr_push_server():
    global _pbr_push_server_instance, _pbr_push_server_thread, _pbr_push_server_port, _pbr_push_callback
    if _pbr_push_server_instance is None:
        return False, "服务未运行"
    try:
        _pbr_push_server_instance.shutdown()
        try:
            _pbr_push_server_instance.server_close()
        except Exception:
            pass
        _pbr_push_server_instance = None
        _pbr_push_server_thread = None
        _pbr_push_server_port = None
        _pbr_push_callback = None
        return True, "已停止"
    except Exception as e:
        return False, "停止失败：{}".format(e)


# ────────────────────────────────────────────────────────────────────────────

class InteriorSceneStudioPro(QtWidgets.QDialog):
    def __init__(self):
        super(InteriorSceneStudioPro, self).__init__()
        global _pbr_push_ui_instance
        _pbr_push_ui_instance = self
        self.setWindowTitle("室内场景助手 V95")
        # 默认按 1080P 显示器做更舒适的启动尺寸，再由 fit_window_to_screen 收进小屏幕。
        self.resize(1320, 900)
        self.setMinimumSize(1120, 760)
        try:
            self.setSizeGripEnabled(True)
        except Exception:
            pass
        self._button_override_refresh_done = False
        self._ui_font_size = 12
        self._ai_robot_name = "AI小助手"
        self._ai_web_busy = False
        self._ai_web_last_error = ""
        try:
            apply_installed_window_icon(self)
        except Exception:
            pass

        self.object_cache = []
        self.group_cache = []
        self.light_cache = []
        self.camera_cache = []
        self.material_cache = []
        self.pbr_cache = []
        self.pbrset_cache = []
        self.pbrset_medit_queue = []
        self.pbrset_medit_page = 0
        self.texture_cache = []
        self.material_usage_map = {}
        self.object_issue_map = {}
        self.rename_undo_stack = []
        self.pbr_undo_stack = []

        self.work_items = []
        self.work_rows = []
        self.index = 0
        self.running = False
        self.force_stop_requested = False
        self.redraw_disabled = False

        self.groups_opened_for_sync = False
        self.pending_sync_type = None
        self.auto_sync_delay = 220
        self.max_auto_sync_count = 300

        self.ignore_object_selection = False
        self.ignore_group_selection = False
        self.ignore_light_selection = False
        self.ignore_camera_selection = False
        self.ignore_material_selection = False
        self.ignore_pbr_selection = False
        self.ignore_texture_selection = False

        self.texture_timer = QtCore.QTimer()
        self.texture_timer.setSingleShot(False)
        self.texture_timer.timeout.connect(self.process_texture_streaming_step)
        self.texture_queue = []
        self.texture_index = 0
        self.texture_running = False
        self.texture_update_records = []
        self.texture_force_mode = False

        # V56：深度扫描可能很慢，改为分步扫描，避免界面像卡死。
        self.texture_scan_timer = QtCore.QTimer()
        self.texture_scan_timer.setSingleShot(False)
        self.texture_scan_timer.timeout.connect(self.process_texture_deep_scan_step)
        self.texture_scan_queue = []
        self.texture_scan_entries = {}
        self.texture_scan_index = 0
        self.texture_scan_running = False
        self.texture_scan_mode = ""

        self.selection_sync_timer = QtCore.QTimer()
        self.selection_sync_timer.setSingleShot(True)
        self.selection_sync_timer.timeout.connect(self.do_debounced_selection_sync)

        # 材质标准化转换队列：避免一次性处理大量材质导致 Max 看起来卡住
        self.pbr_conversion_timer = QtCore.QTimer()
        self.pbr_conversion_timer.setSingleShot(False)
        self.pbr_conversion_timer.timeout.connect(self.process_pbr_conversion_step)
        self.pbr_conversion_queue = []
        self.pbr_conversion_index = 0
        self.pbr_conversion_converted = {}
        self.pbr_conversion_undo_refs = []
        self.pbr_conversion_notes = []
        self.pbr_conversion_running = False
        self._pbr_push_pending_urls = []
        self._pbr_push_pending_lock = None

        self.build_ui()

    # ---------- UI ----------
    def build_ui(self):
        main = QtWidgets.QVBoxLayout(self)
        main.setContentsMargins(18, 18, 18, 14)
        main.setSpacing(12)

        top = QtWidgets.QFrame()
        top.setObjectName("topBar")
        top_lay = QtWidgets.QHBoxLayout(top)
        top_lay.setContentsMargins(0, 0, 0, 0)
        top_lay.setSpacing(12)
        self.top_bar = top

        self.top_title_panel = QtWidgets.QFrame()
        self.top_title_panel.setObjectName("topTitlePanel")
        try:
            self.top_title_panel.setMinimumWidth(140)
            self.top_title_panel.setMaximumWidth(140)
            # V94：标题区和右侧操作区保持同高；只固定宽度，不固定高度，避免标题卡片显得矮一截。
            self.top_title_panel.setMinimumHeight(58)
            self.top_title_panel.setSizePolicy(QtWidgets.QSizePolicy.Fixed, QtWidgets.QSizePolicy.Expanding)
        except Exception:
            pass
        title_lay = QtWidgets.QHBoxLayout(self.top_title_panel)
        title_lay.setContentsMargins(14, 0, 14, 0)
        title_lay.setSpacing(8)

        self.title_text = QtWidgets.QLabel("室内场景助手")
        self.title_text.setObjectName("windowTitleLabel")
        try:
            self.title_text.setAlignment(QT_ALIGN_VCENTER)
        except Exception:
            pass
        title_lay.addWidget(self.title_text, 1, QT_ALIGN_VCENTER)

        self.top_actions_panel = QtWidgets.QFrame()
        self.top_actions_panel.setObjectName("topActionsPanel")
        try:
            self.top_actions_panel.setMinimumHeight(58)
            self.top_actions_panel.setSizePolicy(QtWidgets.QSizePolicy.Expanding, QtWidgets.QSizePolicy.Expanding)
        except Exception:
            pass
        actions_lay = QtWidgets.QHBoxLayout(self.top_actions_panel)
        actions_lay.setContentsMargins(12, 0, 12, 0)
        actions_lay.setSpacing(8)
        actions_lay.addStretch(1)

        self.btn_scene_report = PaintedButton("🩺 体检", "primary")
        self.btn_scene_report.setObjectName("primaryButton")
        self.btn_scene_report.clicked.connect(self.show_scene_health_report)
        self.btn_backup_scene = PaintedButton("💾 备份", "top")
        self.btn_backup_scene.clicked.connect(self.backup_scene_from_ui)
        self.btn_undo_rename = PaintedButton("↶ 撤回", "danger")
        self.btn_undo_rename.setObjectName("dangerButton")
        self.btn_undo_rename.clicked.connect(self.undo_last_rename)
        self.btn_load_config = PaintedButton("⤓ 加载", "top")
        self.btn_load_config.clicked.connect(self.load_config)
        self.btn_save_config = PaintedButton("⤴ 保存", "top")
        self.btn_save_config.clicked.connect(self.save_config)
        self.btn_open_help = PaintedButton("📖 帮助", "top")
        self.btn_open_help.setToolTip("打开已安装的中文帮助文件。")
        self.btn_open_help.clicked.connect(self.open_help_file)

        for b in [self.btn_scene_report, self.btn_backup_scene, self.btn_undo_rename, self.btn_load_config, self.btn_save_config, self.btn_open_help]:
            try:
                b.setFixedHeight(28)
                b.setMinimumWidth(64)
                b.setSizePolicy(QtWidgets.QSizePolicy.Minimum, QtWidgets.QSizePolicy.Fixed)
            except Exception:
                pass
            actions_lay.addWidget(b, 0, QT_ALIGN_VCENTER)

        theme_label = QtWidgets.QLabel("主题")
        try:
            theme_label.setAlignment(QT_ALIGN_VCENTER)
        except Exception:
            pass
        actions_lay.addWidget(theme_label, 0, QT_ALIGN_VCENTER)
        self.skin_combo = QtWidgets.QComboBox()
        self.skin_combo.addItems(["暖木暗色", "曜石蓝黑", "石材灰", "奶油浅色", "米兰岩板", "莫兰迪绿", "铜黑展厅", "日式原木", "包豪斯白", "经典灰", "高对比深色"])
        self.skin_combo.setMinimumWidth(80)
        try:
            self.skin_combo.setMinimumHeight(28)
            self.skin_combo.setMaximumHeight(28)
            self.skin_combo.setSizePolicy(QtWidgets.QSizePolicy.Fixed, QtWidgets.QSizePolicy.Fixed)
        except Exception:
            pass
        self.skin_combo.currentTextChanged.connect(lambda _t: (self.apply_ui_style(), self.polish_inputs(), self.enhance_buttons_with_icons(), self.polish_layouts(), self._apply_button_style_overrides(), self.ai_update_chat_style()))
        actions_lay.addWidget(self.skin_combo, 0, QT_ALIGN_VCENTER)

        # 字号调整控件（滑块版，拖动更方便）
        font_sz_label = QtWidgets.QLabel("字号")
        try:
            font_sz_label.setAlignment(QT_ALIGN_VCENTER)
        except Exception:
            pass
        actions_lay.addWidget(font_sz_label, 0, QT_ALIGN_VCENTER)
        self.ui_font_decrease_btn = PaintedButton("A-", "top")
        self.ui_font_decrease_btn.setToolTip("Decrease UI font size (min 9px)")
        self.ui_font_decrease_btn.clicked.connect(lambda: self.adjust_ui_font_size(-1))
        self.ui_font_increase_btn = PaintedButton("A+", "top")
        self.ui_font_increase_btn.setToolTip("Increase UI font size (max 18px)")
        self.ui_font_increase_btn.clicked.connect(lambda: self.adjust_ui_font_size(1))
        for b in [self.ui_font_decrease_btn, self.ui_font_increase_btn]:
            try:
                b.setFixedSize(28, 28)
                b.setSizePolicy(QtWidgets.QSizePolicy.Fixed, QtWidgets.QSizePolicy.Fixed)
            except Exception:
                pass
        self.ui_font_val_label = QtWidgets.QLabel("12px")
        try:
            self.ui_font_val_label.setFixedWidth(36)
            self.ui_font_val_label.setAlignment(QT_ALIGN_CENTER)
        except Exception:
            pass
        self.refresh_ui_font_controls()
        actions_lay.addWidget(self.ui_font_decrease_btn, 0, QT_ALIGN_VCENTER)
        actions_lay.addWidget(self.ui_font_val_label, 0, QT_ALIGN_VCENTER)
        actions_lay.addWidget(self.ui_font_increase_btn, 0, QT_ALIGN_VCENTER)

        self.chk_keep_on_top = QtWidgets.QCheckBox("置顶")
        self.chk_keep_on_top.setToolTip("默认关闭，避免遮挡 3ds Max 打开文件时的丢失贴图、单位转换、Gamma 等系统对话框。需要插件一直在最前面时再开启。")
        self.chk_keep_on_top.setChecked(False)
        self.chk_keep_on_top.toggled.connect(self.set_window_on_top)
        actions_lay.addWidget(self.chk_keep_on_top, 0, QT_ALIGN_VCENTER)

        top_lay.addWidget(self.top_title_panel, 0)
        top_lay.addWidget(self.top_actions_panel, 1)
        main.addWidget(top)

        self.tabs = CardNavContainer()
        main.addWidget(self.tabs, 1)

        # V87：每个功能页都放进 QScrollArea。低分辨率下不再把窗口撑出屏幕，
        # 而是在卡片页内部滚动，保留全部按钮和列表。
        self.object_tab = self.make_scroll_tab("模型管理")
        self.group_tab = self.make_scroll_tab("组管理")
        self.light_tab = self.make_scroll_tab("灯光管理")
        self.camera_tab = self.make_scroll_tab("相机管理")
        self.material_tab = self.make_scroll_tab("材质管理")
        self.pbr_tab = self.make_scroll_tab("材质标准化")
        self.pbrset_tab = self.make_scroll_tab("PBR贴图套装")
        self.pbr_download_tab = self.make_scroll_tab("PBR下载库")
        self.texture_tab = self.make_scroll_tab("UE贴图流送")
        self.ai_tab = self.make_scroll_tab("AI小助手")
        self.build_object_tab()
        self.build_group_tab()
        self.build_light_tab()
        self.build_camera_tab()
        self.build_material_tab()
        self.build_pbr_tab()
        self.build_pbrset_tab()
        self.build_pbr_download_tab()
        self.build_texture_tab()
        self.build_ai_tab()
        self.polish_inputs()
        self.enhance_buttons_with_icons()
        self.polish_layouts()
        self._apply_button_style_overrides()

        bottom = QtWidgets.QFrame()
        bottom.setObjectName("bottomPanel")
        bottom_lay = QtWidgets.QHBoxLayout(bottom)
        bottom_lay.setContentsMargins(0, 0, 0, 0)
        bottom_lay.setSpacing(12)

        self.bottom_log_panel = QtWidgets.QFrame()
        self.bottom_log_panel.setObjectName("bottomLogPanel")
        try:
            self.bottom_log_panel.setFixedWidth(140)
            self.bottom_log_panel.setSizePolicy(QtWidgets.QSizePolicy.Fixed, QtWidgets.QSizePolicy.Preferred)
        except Exception:
            pass
        log_lay = QtWidgets.QVBoxLayout(self.bottom_log_panel)
        log_lay.setContentsMargins(10, 8, 10, 8)
        log_lay.setSpacing(6)
        log_title = QtWidgets.QLabel("操作日志")
        log_title.setObjectName("sectionTitle")
        log_title.setAlignment(QT_ALIGN_CENTER)
        log_lay.addWidget(log_title)
        self.btn_toggle_log = PaintedButton("打开日志", "side")
        self.btn_toggle_log.setObjectName("sideQuickButton")
        self.btn_toggle_log.clicked.connect(self.toggle_log_panel)
        log_lay.addWidget(self.btn_toggle_log)
        log_lay.addStretch(1)
        self.log_edit = QtWidgets.QPlainTextEdit()
        self.log_edit.setReadOnly(True)
        self.log_edit.setVisible(False)
        # 日志正文不再嵌入主窗口，保持隐藏缓冲；点"打开日志"弹出独立窗口。

        self.bottom_progress_panel = QtWidgets.QFrame()
        self.bottom_progress_panel.setObjectName("bottomProgressPanel")
        progress_lay = QtWidgets.QVBoxLayout(self.bottom_progress_panel)
        progress_lay.setContentsMargins(10, 8, 10, 8)
        progress_lay.setSpacing(6)
        status_row = QtWidgets.QHBoxLayout()
        self.status_label = QtWidgets.QLabel("等待操作")
        self.status_label.setObjectName("statusLabel")
        self.operation_label = QtWidgets.QLabel("当前任务：无")
        self.operation_label.setObjectName("hintLabel")
        self.operation_label.setMinimumWidth(260)
        self.btn_cancel_operation = PaintedButton("停止当前任务", "danger")
        self.btn_cancel_operation.setObjectName("dangerButton")
        self.btn_cancel_operation.setEnabled(False)
        self.btn_cancel_operation.clicked.connect(self.request_cancel_operation)
        status_row.addWidget(self.status_label, 1)
        status_row.addWidget(self.operation_label)
        status_row.addWidget(self.btn_cancel_operation)
        progress_lay.addLayout(status_row)
        self.bar = QtWidgets.QProgressBar()
        self.bar.setValue(0)
        progress_lay.addWidget(self.bar)

        bottom_lay.addWidget(self.bottom_log_panel, 0)
        bottom_lay.addWidget(self.bottom_progress_panel, 1)
        main.addWidget(bottom)
        self.install_sidebar_dashboard()
        self.timer = QtCore.QTimer()
        self.timer.timeout.connect(self.process_step)
        self.polish_inputs()
        self.enhance_buttons_with_icons()
        self.polish_layouts()
        self.apply_ui_style()
        self._apply_button_style_overrides()
        self.fit_window_to_screen()
        self.log("V95 已启动：顶部标题区与右侧操作区等高，体检/备份等按钮已加宽并垂直居中。")
        self.check_installed_resources()
        self.auto_load_config_silent()
        self.pbr_clipboard_last_text = ""
        self.pbr_clipboard_timer = QtCore.QTimer()
        self.pbr_clipboard_timer.setInterval(1000)
        self.pbr_clipboard_timer.timeout.connect(self.check_pbr_clipboard_links)
        self.pbr_clipboard_timer.start()
        self.pbr_push_queue_timer = QtCore.QTimer()
        self.pbr_push_queue_timer.setInterval(150)
        self.pbr_push_queue_timer.timeout.connect(self.process_pbr_push_queue)
        self.pbr_push_queue_timer.start()

    def install_sidebar_dashboard(self):
        """左侧底部辅助操作：只保留一个内容折叠按钮。"""
        try:
            left_lay = self.tabs.left_panel.layout()
        except Exception:
            return
        self.side_quick_card = QtWidgets.QFrame()
        self.side_quick_card.setObjectName("sideCollapseHolder")
        try:
            self.tabs.side_quick_card = self.side_quick_card
        except Exception:
            pass
        try:
            self.side_quick_card.setMinimumWidth(116)
            self.side_quick_card.setMaximumWidth(116)
            self.side_quick_card.setSizePolicy(QtWidgets.QSizePolicy.Fixed, QtWidgets.QSizePolicy.Fixed)
        except Exception:
            pass
        qlay = QtWidgets.QVBoxLayout(self.side_quick_card)
        qlay.setContentsMargins(0, 2, 0, 2)
        qlay.setSpacing(0)

        self.btn_collapse_side = PaintedButton("⟨ 收起内容", "side")
        self.btn_collapse_side.setObjectName("sideQuickButton")
        self.btn_collapse_side.setToolTip("收起右侧内容区、顶部右侧操作区和底部右侧进度区，只保留左侧导航。")
        self.btn_collapse_side.clicked.connect(self.toggle_compact_sidebar)
        qlay.addWidget(self.btn_collapse_side)

        # 左靠 / 右靠按钮已移除，保持左侧底部只留一个"收起内容"入口。
        qlay.addStretch(1)

        # addWidget 放到 nav_scroll 后面，视觉上就是左侧导航栏最下方。
        left_lay.addWidget(self.side_quick_card, 0)


    def toggle_compact_sidebar(self):
        try:
            self.tabs.toggle_compact()
            collapsed = bool(getattr(self.tabs, "content_collapsed", False))
            self.btn_collapse_side.setText("⟩ 展开内容" if collapsed else "⟨ 收起内容")
            self.sync_collapse_chrome(collapsed)
            # window resize is handled inside set_content_collapsed via tabs.toggle_compact()
        except Exception:
            pass

    def sync_collapse_chrome(self, collapsed=False):
        """折叠右侧内容时，同时收起顶部右侧操作区和底部右侧进度区；
        使日志面板宽度与左侧导航等宽，避免收起后两侧大片空白。"""
        try:
            collapsed = bool(collapsed)
            if hasattr(self, "top_actions_panel"):
                self.top_actions_panel.setVisible(not collapsed)
            if hasattr(self, "bottom_progress_panel"):
                self.bottom_progress_panel.setVisible(not collapsed)
            if hasattr(self, "btn_collapse_side"):
                self.btn_collapse_side.setText("⟩ 展开内容" if collapsed else "⟨ 收起内容")
        except Exception:
            pass

    def toggle_top_bar(self):
        # V93 起已取消标题栏小箭头隐藏按钮；保留空方法只为兼容旧配置/旧调用。
        return None

    def toggle_log_panel(self):
        """打开独立日志窗口，主界面底部只保留日志按钮区。"""
        try:
            self.open_log_dialog()
        except Exception:
            self.log(status_text_for_exception("打开日志窗口失败"))

    def open_log_dialog(self):
        try:
            dlg = getattr(self, "log_dialog", None)
            if dlg is not None and dlg.isVisible():
                try:
                    dlg.raise_()
                    dlg.activateWindow()
                except Exception:
                    pass
                return
        except Exception:
            pass

        dlg = QtWidgets.QDialog(self)
        dlg.setWindowTitle("操作日志")
        dlg.resize(760, 460)
        lay = QtWidgets.QVBoxLayout(dlg)
        lay.setContentsMargins(12, 12, 12, 12)
        lay.setSpacing(10)

        self.log_dialog_edit = QtWidgets.QPlainTextEdit()
        self.log_dialog_edit.setReadOnly(True)
        try:
            self.log_dialog_edit.setPlainText(self.log_edit.toPlainText())
            self.log_dialog_edit.moveCursor(QtGui.QTextCursor.End)
        except Exception:
            pass
        lay.addWidget(self.log_dialog_edit, 1)

        row = QtWidgets.QHBoxLayout()
        row.addStretch(1)
        btn_copy = QtWidgets.QPushButton("复制日志")
        btn_copy.clicked.connect(self.copy_log_to_clipboard)
        btn_clear = QtWidgets.QPushButton("清空日志")
        btn_clear.clicked.connect(self.clear_log_all)
        btn_close = QtWidgets.QPushButton("关闭")
        btn_close.clicked.connect(dlg.close)
        row.addWidget(btn_copy)
        row.addWidget(btn_clear)
        row.addWidget(btn_close)
        lay.addLayout(row)

        self.log_dialog = dlg
        try:
            dlg.setStyleSheet(self.styleSheet())
        except Exception:
            pass
        dlg.show()
        try:
            dlg.raise_()
            dlg.activateWindow()
        except Exception:
            pass

    def clear_log_all(self):
        try:
            self.log_edit.clear()
        except Exception:
            pass
        try:
            if hasattr(self, "log_dialog_edit") and self.log_dialog_edit:
                self.log_dialog_edit.clear()
        except Exception:
            pass

    def dock_window_left(self):
        self.dock_window_to_screen_edge("left")

    def dock_window_right(self):
        self.dock_window_to_screen_edge("right")

    def dock_window_to_screen_edge(self, side="left"):
        try:
            app = QtWidgets.QApplication.instance()
            screen = None
            try:
                screen = app.screenAt(self.frameGeometry().center()) if app else None
            except Exception:
                screen = None
            if screen is None and app:
                try:
                    screen = app.primaryScreen()
                except Exception:
                    screen = None
            if screen is not None:
                geo = screen.availableGeometry()
            else:
                geo = QtWidgets.QDesktopWidget().availableGeometry(self)
            w = min(max(self.width(), self.minimumWidth()), max(760, geo.width() - 40))
            h = min(max(self.height(), self.minimumHeight()), max(520, geo.height() - 40))
            x = geo.left() + 8 if side == "left" else geo.right() - w - 8
            y = geo.top() + 8
            self.resize(w, h)
            self.move(x, y)
            self.log("窗口已靠{}侧停靠。".format("左" if side == "left" else "右"))
        except Exception:
            self.log(status_text_for_exception("窗口停靠失败"))

    def check_installed_resources(self):
        try:
            help_ok = os.path.exists(installed_help_file_path())
            icon_ok = False
            for root in installed_resource_search_dirs():
                for name in ["ISS_Icon_256.png", "ISS_Icon_64.png", "ISS_Icon.ico"]:
                    if os.path.exists(os.path.join(root, "icons", name)):
                        icon_ok = True
                        break
                if icon_ok:
                    break
            if not help_ok or not icon_ok:
                self.log("安装资源检查：{}{}。可重新运行V62安装包修复。".format(
                    "帮助文件缺失；" if not help_ok else "",
                    "图标缺失；" if not icon_ok else ""
                ))
            else:
                self.log("安装资源检查正常：帮助文件和图标可用。")
        except Exception:
            pass

    def set_status(self, text):
        try:
            self.status_label.setText(text)
        except Exception:
            pass

    def log(self, text):
        line = "[{}] {}".format(datetime.now().strftime("%H:%M:%S"), text)
        try:
            self.log_edit.appendPlainText(line)
        except Exception:
            pass
        try:
            if hasattr(self, "log_dialog_edit") and self.log_dialog_edit and self.log_dialog_edit.isVisible():
                self.log_dialog_edit.appendPlainText(line)
                self.log_dialog_edit.moveCursor(QtGui.QTextCursor.End)
        except Exception:
            pass
        self.set_status(text)

    # ---------- 全局任务进度 / 稳定性 ----------
    def begin_operation(self, title, total=0, cancellable=True):
        try:
            self.operation_title = safe_str(title, "任务")
            self.operation_total = int(total or 0)
            self.operation_index = 0
            self.operation_cancel_requested = False
            self.operation_start_time = time.time()
            if hasattr(self, "btn_cancel_operation"):
                self.btn_cancel_operation.setEnabled(bool(cancellable))
            if hasattr(self, "operation_label"):
                self.operation_label.setText("当前任务：{}".format(self.operation_title))
            if hasattr(self, "bar"):
                if self.operation_total > 0:
                    self.bar.setRange(0, self.operation_total)
                    self.bar.setValue(0)
                else:
                    self.bar.setRange(0, 0)
            self.set_status("{}：开始".format(self.operation_title))
            QtWidgets.QApplication.processEvents()
        except Exception:
            pass

    def update_operation(self, index=None, total=None, message=""):
        try:
            if total is not None:
                self.operation_total = int(total or 0)
            if index is not None:
                self.operation_index = int(index or 0)

            elapsed = 0.0
            try:
                elapsed = max(0.0, time.time() - float(getattr(self, "operation_start_time", time.time())))
            except Exception:
                pass

            title = safe_str(getattr(self, "operation_title", ""), "任务")
            total = int(getattr(self, "operation_total", 0) or 0)
            idx = int(getattr(self, "operation_index", 0) or 0)

            if total > 0:
                pct = int(max(0, min(100, idx * 100.0 / max(total, 1))))
                if hasattr(self, "bar"):
                    self.bar.setRange(0, total)
                    self.bar.setValue(max(0, min(total, idx)))
                op_text = "{}：{}/{}  {}%  {:.1f}s".format(title, idx, total, pct, elapsed)
            else:
                op_text = "{}：运行中  {:.1f}s".format(title, elapsed)
                if hasattr(self, "bar"):
                    self.bar.setRange(0, 0)

            if message:
                op_text += " · " + safe_str(message, "")[:80]
            if hasattr(self, "operation_label"):
                self.operation_label.setText(op_text)
            if message:
                self.set_status("{}：{}".format(title, message))
            QtWidgets.QApplication.processEvents()
        except Exception:
            pass

    def finish_operation(self, message="完成", cancelled=False):
        try:
            title = safe_str(getattr(self, "operation_title", ""), "任务")
            total = int(getattr(self, "operation_total", 0) or 0)
            if hasattr(self, "bar"):
                if total > 0:
                    self.bar.setRange(0, total)
                    self.bar.setValue(total)
                else:
                    self.bar.setRange(0, 100)
                    self.bar.setValue(100)
            if hasattr(self, "btn_cancel_operation"):
                self.btn_cancel_operation.setEnabled(False)
            if hasattr(self, "operation_label"):
                self.operation_label.setText("当前任务：{}{}".format("已取消 · " if cancelled else "", message))
            self.operation_cancel_requested = False
            self.set_status("{}：{}".format(title, "已取消" if cancelled else message))
            QtWidgets.QApplication.processEvents()
        except Exception:
            pass

    def request_cancel_operation(self):
        try:
            self.operation_cancel_requested = True
            self.set_status("正在请求停止当前任务……")
            if hasattr(self, "operation_label"):
                self.operation_label.setText("当前任务：正在停止……请稍候")
            if getattr(self, "texture_running", False):
                self.stop_texture_streaming_process(forced=False)
            elif getattr(self, "texture_scan_running", False):
                self.stop_texture_deep_scan()
            elif getattr(self, "running", False):
                try:
                    self.stop()
                except Exception:
                    pass
            self.log("已请求停止当前任务。部分文件/材质操作会在当前小步骤完成后停止。")
        except Exception:
            self.log(status_text_for_exception("停止当前任务失败"))

    def check_operation_cancelled(self):
        try:
            QtWidgets.QApplication.processEvents()
        except Exception:
            pass
        return bool(getattr(self, "operation_cancel_requested", False))

    def copy_log_to_clipboard(self):
        try:
            QtWidgets.QApplication.clipboard().setText(self.log_edit.toPlainText())
            self.log("操作日志已复制到剪贴板")
        except Exception:
            self.log(status_text_for_exception("复制日志失败"))

    def safe_ui_step(self, index=None, total=None, message=""):
        self.update_operation(index=index, total=total, message=message)
        return self.check_operation_cancelled()

    def set_window_on_top(self, checked):
        """
        Max 自身的丢失贴图、单位转换、Gamma 等提示框经常不是本插件的子窗口。
        如果插件一直置顶，就会挡住这些 Max 系统对话框。
        所以 V17 默认不置顶，只在用户勾选时临时置顶。
        """
        try:
            if checked:
                self.setWindowFlags(QT_WINDOW | QT_STAY_ON_TOP)
            else:
                self.setWindowFlags(QT_WINDOW)

            self.show()

            if checked:
                try:
                    self.raise_()
                    self.activateWindow()
                except Exception:
                    pass

            self.log("窗口置顶：{}".format("已开启" if checked else "已关闭，Max 系统提示框不会再被插件压住"))
        except Exception:
            try:
                self.set_status(status_text_for_exception("切换窗口置顶失败"))
            except Exception:
                pass

    def make_scroll_tab(self, title):
        """创建小屏友好的滚动标签页。返回内部 QWidget，原 build_xxx_tab 继续往里面布局。"""
        inner = QtWidgets.QWidget()
        inner.setObjectName("scrollTabInner")
        try:
            inner.setSizePolicy(QtWidgets.QSizePolicy.Expanding, QtWidgets.QSizePolicy.MinimumExpanding)
        except Exception:
            pass

        scroll = QtWidgets.QScrollArea()
        scroll.setObjectName("cardScrollArea")
        scroll.setWidgetResizable(True)
        try:
            scroll.setFrameShape(QtWidgets.QFrame.NoFrame)
        except Exception:
            pass
        try:
            scroll_bar_as_needed = qt_enum("ScrollBarAsNeeded", ("ScrollBarPolicy",))
        except Exception:
            scroll_bar_as_needed = None
        if scroll_bar_as_needed is not None:
            scroll.setHorizontalScrollBarPolicy(scroll_bar_as_needed)
            scroll.setVerticalScrollBarPolicy(scroll_bar_as_needed)
        scroll.setWidget(inner)
        self.tabs.addTab(scroll, title)
        return inner

    def fit_window_to_screen(self):
        """按当前显示器可用区域自动压缩窗口，避免 1366x768 / 1280x720 这类屏幕显示不全。"""
        try:
            app = QtWidgets.QApplication.instance()
            desktop = app.desktop() if app else None
            if desktop:
                screen = desktop.availableGeometry(self)
            else:
                screen = QtWidgets.QDesktopWidget().availableGeometry(self)
            max_w = max(760, int(screen.width() * 0.94))
            max_h = max(520, int(screen.height() * 0.92))
            if self.width() > max_w or self.height() > max_h:
                self.resize(min(self.width(), max_w), min(self.height(), max_h))
            try:
                self.move(screen.x() + max(0, int((screen.width() - self.width()) / 2)), screen.y() + max(0, int((screen.height() - self.height()) / 2)))
            except Exception:
                pass
        except Exception:
            pass

    def prepare_tree(self, tree):
        tree.setSelectionMode(EXTENDED_SELECTION)
        tree.setAlternatingRowColors(True)
        try:
            tree.setRootIsDecorated(False)
            tree.setItemsExpandable(False)
            tree.setUniformRowHeights(True)
            tree.setIndentation(0)
            # V65：全局树高度不再固定 330，避免复杂页越堆越长。
            tree.setMinimumHeight(140)
        except Exception:
            pass

    def polish_inputs(self):
        # 3ds Max 2024 里 QSpinBox 上下箭头容易压住圆角边框；隐藏箭头后更干净。
        try:
            no_buttons = QtWidgets.QAbstractSpinBox.NoButtons
        except Exception:
            no_buttons = QtWidgets.QAbstractSpinBox.ButtonSymbols.NoButtons

        for spin in self.findChildren(QtWidgets.QSpinBox):
            try:
                spin.setButtonSymbols(no_buttons)
                if bool(spin.property("compactRenameSpin")):
                    spin.setFixedWidth(78)
                    spin.setFixedHeight(38)
                else:
                    spin.setMinimumWidth(92)
                    spin.setFixedHeight(40)
            except Exception:
                pass

        for line in self.findChildren(QtWidgets.QLineEdit):
            try:
                line.setMinimumHeight(40)
                if bool(line.property("compactRenamePrefix")):
                    line.setMinimumWidth(180)
                    line.setMaximumWidth(260)
                    line.setFixedHeight(38)
                    line.setSizePolicy(QtWidgets.QSizePolicy.Fixed, QtWidgets.QSizePolicy.Fixed)
            except Exception:
                pass

        for combo in self.findChildren(QtWidgets.QComboBox):
            try:
                combo.setMinimumHeight(40)
            except Exception:
                pass

        for btn in self.findChildren(QtWidgets.QPushButton):
            try:
                btn.setMinimumHeight(36)
                btn.setMinimumWidth(74)
                try:
                    btn.setCursor(QtGui.QCursor(QtCore.Qt.PointingHandCursor))
                    btn.setMouseTracking(True)
                    btn.setAttribute(QtCore.Qt.WA_Hover, True)
                except Exception:
                    pass
                try:
                    btn.setSizePolicy(QtWidgets.QSizePolicy.Expanding, QtWidgets.QSizePolicy.Fixed)
                except Exception:
                    pass
                f = btn.font()
                if f.pointSize() < 10:
                    f.setPointSize(10)
                btn.setFont(f)
            except Exception:
                pass

    def icon_for_button_text(self, text):
        s = safe_str(text, "")
        stripped = s.strip()
        if not stripped:
            return ""
        if stripped[0] in "🔍🎯🧱🧹📁🖼↻📂📤☑↗⛔⚠🔗🔎⬇🌐−➕📥💾🩺✎👁▶⏹↑↓⇄☐⚙":
            return ""

        rules = [
            ("场景体检", "🩺"), ("保存配置", "💾"), ("加载配置", "📂"),
            ("加载场景", "🌐"), ("加载整个", "🌐"), ("加载选中", "📥"), ("添加选中", "➕"), ("加载", "📥"),
            ("扫描", "🔍"), ("检测", "🔎"), ("筛选", "⚗"),
            ("AI", "🤖"), ("聊天字号", "🔠"), ("提问模板", "🧩"), ("识别推荐", "🔎"), ("执行操作", "▶"), ("打开网址", "🌐"), ("独立窗口", "🪟"), ("费用类型", "💳"), ("获取API", "🔑"), ("激活", "☑"), ("完整诊断", "🧪"), ("诊断", "🧪"), ("建议模型", "✨"), ("使用建议", "✨"), ("接口文档", "📘"), ("账单", "💳"), ("用量", "📊"), ("发送", "▶"), ("测试连接", "🔎"), ("场景摘要", "🩺"), ("最近日志", "📋"), ("接入说明", "❔"), ("最后回答", "📋"), ("帮助", "❔"), ("下载", "⬇"), ("Chrome", "🌐"), ("剪贴板", "📋"), ("冗余", "🔎"), ("重试", "↻"), ("失败", "⚠"), ("目标文件夹", "📁"), ("当前材质", "🔍"), ("改材质名", "✎"), ("浏览器", "🌐"), ("网站", "🌐"), ("链接", "🔗"), ("队列", "☰"), ("材质库", "📚"), ("清空", "🧹"), ("清除", "−"), ("移除", "−"), ("选择", "🎯"),
            ("手动同步", "🔗"), ("同步", "🔗"),
            ("打开全部", "▶"), ("打开勾选", "▶"), ("打开", "📂"),
            ("关闭全部", "⏹"), ("关闭勾选", "⏹"), ("关闭", "⏹"),
            ("重命名", "✎"), ("预览", "👁"), ("撤回", "↶"),
            ("开始修复", "⚙"), ("修复", "⚙"), ("强制停止", "⛔"), ("停止当前任务", "⛔"), ("复制日志", "📋"), ("停止", "⛔"),
            ("安装", "⬇"), ("下载", "🌐"), ("输出", "📤"), ("更新", "🔗"),
            ("重新检查", "↻"), ("源位置", "📁"), ("输出位置", "📁"), ("源贴图", "🖼"), ("输出贴图", "🖼"), ("所在位置", "📁"), ("外部打开", "🖼"),
            ("选择根目录", "📂"), ("选择目录", "📂"),
            ("打勾", "☑"), ("全部取消", "☐"), ("反转", "⇄"),
            ("升序", "↑"), ("降序", "↓"),
        ]

        for key, icon in rules:
            if key in stripped:
                return icon
        return ""

    def enhance_buttons_with_icons(self):
        """统一给按钮补充较大的符号图标，避免界面上按钮太多时难以区分。"""
        for btn in self.findChildren(QtWidgets.QPushButton):
            try:
                txt = btn.text()
                icon = self.icon_for_button_text(txt)
                if icon and not txt.strip().startswith(icon):
                    btn.setText("{}  {}".format(icon, txt))
                btn.setMinimumHeight(36)
                btn.setMinimumWidth(74)
                try:
                    btn.setCursor(QtGui.QCursor(QtCore.Qt.PointingHandCursor))
                    btn.setMouseTracking(True)
                    btn.setAttribute(QtCore.Qt.WA_Hover, True)
                except Exception:
                    pass
                try:
                    btn.setSizePolicy(QtWidgets.QSizePolicy.Expanding, QtWidgets.QSizePolicy.Fixed)
                except Exception:
                    pass
                f = btn.font()
                if f.pointSize() < 10:
                    f.setPointSize(10)
                btn.setFont(f)
            except Exception:
                pass

    def polish_layouts(self):
        """V58：统一卡片、网格、按钮栏的间距和对齐，让界面更像正式工具。"""
        try:
            for box in self.findChildren(QtWidgets.QGroupBox):
                lay = box.layout()
                if lay:
                    lay.setContentsMargins(14, 16, 14, 14)
                    lay.setSpacing(10)
                    if isinstance(lay, QtWidgets.QGridLayout):
                        lay.setHorizontalSpacing(10)
                        lay.setVerticalSpacing(10)
                        for c in range(8):
                            try:
                                lay.setColumnMinimumWidth(c, 92)
                            except Exception:
                                pass
                        try:
                            lay.setColumnStretch(1, 1)
                            lay.setColumnStretch(4, 1)
                        except Exception:
                            pass
            for frame in self.findChildren(QtWidgets.QFrame):
                lay = frame.layout()
                if lay:
                    lay.setSpacing(10)
        except Exception:
            pass

    def set_item_checkable(self, item, checked=True):
        item.setFlags(item.flags() | QT_ITEM_USER_CHECKABLE | QT_ITEM_SELECTABLE | QT_ITEM_ENABLED)
        item.setCheckState(0, QT_CHECKED if checked else QT_UNCHECKED)

    def card(self, title):
        box = QtWidgets.QGroupBox(title)
        return box

    def make_compact_rename_row(self, prefix_widget, start_spin, padding_spin):
        """
        V59：重命名里的"起始 / 位数"是小参数，不应该被大网格拉得很远。
        用一个独立横向小条把它们锁在前缀旁边。
        """
        frame = QtWidgets.QFrame()
        frame.setObjectName("compactRenameRow")
        frame.setMinimumHeight(52)
        try:
            frame.setSizePolicy(QtWidgets.QSizePolicy.Expanding, QtWidgets.QSizePolicy.Fixed)
        except Exception:
            pass

        lay = QtWidgets.QHBoxLayout(frame)
        lay.setContentsMargins(0, 5, 0, 7)
        lay.setSpacing(8)

        try:
            prefix_widget.setProperty("compactRenamePrefix", True)
            prefix_widget.setMinimumWidth(180)
            prefix_widget.setMaximumWidth(260)
            prefix_widget.setFixedHeight(38)
            prefix_widget.setSizePolicy(QtWidgets.QSizePolicy.Fixed, QtWidgets.QSizePolicy.Fixed)
        except Exception:
            pass

        for spin in (start_spin, padding_spin):
            try:
                spin.setProperty("compactRenameSpin", True)
                spin.setFixedWidth(78)
                spin.setFixedHeight(38)
                spin.setSizePolicy(QtWidgets.QSizePolicy.Fixed, QtWidgets.QSizePolicy.Fixed)
            except Exception:
                pass

        for text, widget in [("前缀", prefix_widget), ("起始", start_spin), ("位数", padding_spin)]:
            lab = QtWidgets.QLabel(text)
            lab.setObjectName("compactRenameLabel")
            try:
                lab.setMinimumHeight(38)
                lab.setAlignment(QtCore.Qt.AlignVCenter | QtCore.Qt.AlignRight)
            except Exception:
                pass
            lay.addWidget(lab, 0, QtCore.Qt.AlignVCenter)
            lay.addWidget(widget, 0, QtCore.Qt.AlignVCenter)

        lay.addStretch()
        return frame

    def make_check_bar(self, title, tree_attr):
        frame = QtWidgets.QFrame()
        frame.setObjectName("checkBar")
        layout = QtWidgets.QVBoxLayout(frame)
        layout.setContentsMargins(12, 10, 12, 10)
        layout.setSpacing(8)

        top = QtWidgets.QVBoxLayout()
        label_box = QtWidgets.QVBoxLayout()
        t = QtWidgets.QLabel(title)
        t.setObjectName("sectionTitle")
        h = QtWidgets.QLabel("搜索、排序和勾选只影响当前列表显示 / 执行范围，不删除场景内容。")
        h.setObjectName("hintLabel")
        try:
            h.setWordWrap(True)
        except Exception:
            pass
        label_box.addWidget(t)
        label_box.addWidget(h)
        top.addLayout(label_box)

        control_row = QtWidgets.QHBoxLayout()
        search_label = QtWidgets.QLabel("🔎 搜索")
        search_edit = QtWidgets.QLineEdit()
        search_edit.setObjectName("listSearchEdit")
        search_edit.setPlaceholderText("输入名称 / 类型 / 路径 / 状态")
        search_edit.setMinimumWidth(160)
        search_edit.textChanged.connect(lambda txt, attr=tree_attr: self.filter_tree_by_search(attr, txt))
        control_row.addWidget(search_label)
        control_row.addWidget(search_edit, 1)

        control_row.addWidget(QtWidgets.QLabel("排序"))
        sort_combo = QtWidgets.QComboBox()
        sort_combo.setObjectName("sortCombo")
        sort_combo.addItems([x[0] for x in self.get_sort_options(tree_attr)])
        sort_combo.setMinimumWidth(96)
        sort_order = QtWidgets.QPushButton("升序")
        sort_order.setObjectName("checkToolButton")
        sort_order.setCheckable(True)

        def run_sort():
            sort_order.setText("降序" if sort_order.isChecked() else "升序")
            self.sort_tree_by_label(tree_attr, sort_combo.currentText(), reverse=sort_order.isChecked())

        sort_combo.currentIndexChanged.connect(lambda _=0: run_sort())
        sort_order.clicked.connect(lambda _=False: run_sort())
        control_row.addWidget(sort_combo)
        control_row.addWidget(sort_order)
        top.addLayout(control_row)

        layout.addLayout(top)

        bottom = QtWidgets.QHBoxLayout()
        specs = [("打勾选中", "selected"), ("打勾未选中", "unselected"), ("全部打勾", "all"), ("全部取消", "none"), ("反转勾选", "invert")]
        for text, mode in specs:
            b = QtWidgets.QPushButton(text)
            b.setObjectName("checkToolButton")
            b.clicked.connect(lambda _=False, m=mode, attr=tree_attr: self.set_tree_check_state(attr, m))
            bottom.addWidget(b)
        layout.addLayout(bottom)
        return frame

    def filter_tree_by_search(self, tree_attr, text):
        tree = getattr(self, tree_attr, None)
        if tree is None:
            return

        query = safe_str(text, "").strip().lower()

        for row in range(tree.topLevelItemCount()):
            item = tree.topLevelItem(row)
            if not query:
                item.setHidden(False)
                continue

            hit = False
            for col in range(tree.columnCount()):
                try:
                    if query in item.text(col).lower():
                        hit = True
                        break
                except Exception:
                    pass

            item.setHidden(not hit)

    def get_sort_options(self, tree_attr):
        if tree_attr == "object_tree":
            return [("名称",0),("类型",1),("图层",2),("材质",3),("组",4),("冻结",5),("问题",6),("状态",7)]
        if tree_attr == "group_tree":
            return [("名称",0),("图层",1),("成员数",2),("打开",3),("冻结",4),("隐藏",5),("状态",6)]
        if tree_attr == "light_tree":
            return [("名称",0),("类型",1),("图层",2),("冻结",3),("状态",4)]
        if tree_attr == "camera_tree":
            return [("名称",0),("类型",1),("图层",2),("冻结",3),("状态",4)]
        if tree_attr == "material_tree":
            return [("名称",0),("类型",1),("角色",2),("母材质",3),("ID/槽",4),("状态",5)]
        if tree_attr == "pbr_tree":
            return [("名称",0),("类型",1),("角色",2),("母材质",3),("判断",4),("动作",5),("状态",6)]
        if tree_attr == "pbrset_tree":
            return [("材质套装",0),("通道",1),("问题",2),("创建材质名",3),("创建材质类型",4),("文件夹",5),("状态",6)]
        if tree_attr == "texture_tree":
            return [("贴图",0),("通道",1),("源尺寸",2),("合格后尺寸",3),("源图状态",4),("输出状态",5),("2幂",6),("问题/建议",7),("相关物体",8),("源路径",9),("输出路径",10),("引用数",11),("处理状态",12)]
        return [("名称",0)]

    def cache_for_tree(self, tree_attr):
        if tree_attr == "object_tree": return self.object_cache
        if tree_attr == "group_tree": return self.group_cache
        if tree_attr == "light_tree": return self.light_cache
        if tree_attr == "camera_tree": return self.camera_cache
        if tree_attr == "material_tree": return self.material_cache
        if tree_attr == "pbr_tree": return self.pbr_cache
        if tree_attr == "pbrset_tree": return self.pbrset_cache
        if tree_attr == "texture_tree": return self.texture_cache
        return []

    def refresh_tree_by_attr(self, tree_attr):
        if tree_attr == "object_tree":
            self.refresh_object_tree()
        elif tree_attr == "group_tree":
            self.refresh_group_tree()
        elif tree_attr == "light_tree":
            self.refresh_light_tree()
        elif tree_attr == "camera_tree":
            self.refresh_camera_tree()
        elif tree_attr == "material_tree":
            self.refresh_material_tree()
        elif tree_attr == "pbr_tree":
            self.refresh_pbr_tree()
        elif tree_attr == "pbrset_tree":
            self.refresh_pbrset_tree()
        elif tree_attr == "texture_tree":
            self.refresh_texture_tree()

    def label_for_tree(self, tree_attr):
        if tree_attr == "object_tree": return "模型列表"
        if tree_attr == "group_tree": return "组列表"
        if tree_attr == "light_tree": return "灯光列表"
        if tree_attr == "camera_tree": return "相机列表"
        if tree_attr == "material_tree": return "材质列表"
        if tree_attr == "pbr_tree": return "材质标准化列表"
        if tree_attr == "pbrset_tree": return "PBR贴图套装列表"
        if tree_attr == "texture_tree": return "UE贴图流送列表"
        return "列表"

    def item_display_name_for_cache(self, tree_attr, item):
        try:
            if tree_attr in ("material_tree", "pbr_tree"):
                return get_material_name(item.get("mat")) if isinstance(item, dict) else "材质"
            if tree_attr == "pbrset_tree":
                return item.get("name", "PBR套装") if isinstance(item, dict) else "PBR套装"
            if tree_attr == "texture_tree":
                return item.get("file", "贴图") if isinstance(item, dict) else "贴图"
            return safe_str(getattr(item, "name", ""), "对象")
        except Exception:
            return "对象"

    def remove_selected_rows_from_tree(self, tree_attr):
        """
        从当前工具列表清除高亮选择的行。
        注意：这里只清除列表记录，不删除 3ds Max 场景中的对象 / 材质。
        """
        tree = getattr(self, tree_attr, None)
        cache = self.cache_for_tree(tree_attr)

        if tree is None or cache is None:
            self.log("移除失败：没有找到对应列表")
            return

        rows = []
        for item in tree.selectedItems():
            row = tree.indexOfTopLevelItem(item)
            if row >= 0:
                rows.append(row)

        rows = sorted(set(rows), reverse=True)

        if not rows:
            self.log("{}：没有高亮选择的行可清除".format(self.label_for_tree(tree_attr)))
            return

        removed_names = []
        removed_count = 0

        for row in rows:
            if 0 <= row < len(cache):
                removed_names.append(self.item_display_name_for_cache(tree_attr, cache[row]))
                try:
                    cache.pop(row)
                    removed_count += 1
                except Exception:
                    pass

        self.refresh_tree_by_attr(tree_attr)

        if removed_count:
            preview = "，".join(removed_names[:5])
            more = " 等" if len(removed_names) > 5 else ""
            self.log("{}：已从列表清除 {} 行：{}{}".format(self.label_for_tree(tree_attr), removed_count, preview, more))
        else:
            self.log("{}：没有行被清除".format(self.label_for_tree(tree_attr)))

    def key_for_cache_item(self, tree_attr, item):
        if tree_attr in ("material_tree", "pbr_tree"):
            try: return material_context_key(item)
            except Exception: return id(item)
        return get_anim_handle(item)

    def capture_checked_keys(self, tree_attr):
        tree = getattr(self, tree_attr, None)
        cache = self.cache_for_tree(tree_attr)
        result = set()
        if tree is None:
            return result
        for row in range(tree.topLevelItemCount()):
            item = tree.topLevelItem(row)
            if item and item.checkState(0) == QT_CHECKED and row < len(cache):
                result.add(self.key_for_cache_item(tree_attr, cache[row]))
        return result

    def restore_checked_keys(self, tree_attr, checked_keys):
        tree = getattr(self, tree_attr, None)
        cache = self.cache_for_tree(tree_attr)
        if tree is None:
            return
        old_flags = (self.ignore_object_selection, self.ignore_group_selection, self.ignore_light_selection, self.ignore_camera_selection, self.ignore_material_selection)
        self.ignore_object_selection = self.ignore_group_selection = self.ignore_light_selection = self.ignore_camera_selection = self.ignore_material_selection = True
        try:
            for row in range(tree.topLevelItemCount()):
                item = tree.topLevelItem(row)
                if item and row < len(cache):
                    item.setCheckState(0, QT_CHECKED if self.key_for_cache_item(tree_attr, cache[row]) in checked_keys else QT_UNCHECKED)
        finally:
            self.ignore_object_selection, self.ignore_group_selection, self.ignore_light_selection, self.ignore_camera_selection, self.ignore_material_selection = old_flags

    def sort_tree_by_label(self, tree_attr, label, reverse=False):
        cache = self.cache_for_tree(tree_attr)
        if not cache:
            return
        checked_keys = self.capture_checked_keys(tree_attr)

        def node_key(obj):
            if label == "名称": return safe_str(getattr(obj, "name", ""), "").lower()
            if label == "类型": return get_class_name(obj).lower()
            if label == "图层": return get_layer_name(obj).lower()
            if label == "材质": return get_object_material_name(obj).lower()
            if label == "组": return ("0" if is_group_head(obj) else ("1" if is_group_member(obj) else "2"))
            if label == "冻结": return "1" if is_frozen(obj) else "0"
            if label == "隐藏": return "1" if is_hidden(obj) else "0"
            if label == "打开": return "1" if is_group_open(obj) else "0"
            if label == "成员数": return "%06d" % get_group_member_count(obj)
            if label == "问题": return "，".join(self.object_issue_map.get(get_anim_handle(obj), [])).lower()
            return safe_str(getattr(obj, "name", ""), "").lower()

        def mat_key(entry):
            mat = entry.get("mat") if isinstance(entry, dict) else None
            parent = entry.get("parent") if isinstance(entry, dict) else None
            if label == "名称": return get_material_name(mat).lower()
            if label == "类型": return get_class_name(mat).lower()
            if label == "角色": return safe_str(entry.get("role", ""), "").lower()
            if label == "母材质": return get_material_name(parent).lower() if is_valid_material(parent) else ""
            if label == "ID/槽": return "%06d_%06d" % (int(entry.get("mat_id", 0) or 0), int(entry.get("slot", 0) or 0))
            if label == "判断": return pbr_status_for_entry(entry).get("judge", "").lower()
            if label == "动作": return pbr_status_for_entry(entry).get("action", "").lower()
            return get_material_name(mat).lower()

        def pbrset_key(entry):
            if label == "材质套装": return safe_str(entry.get("name", ""), "").lower()
            if label == "通道": return pbr_channel_summary(entry.get("channels", {})).lower()
            if label == "问题": return pbr_set_display_issues(entry).lower()
            if label == "创建材质名": return pbrset_created_material_name_text(entry).lower()
            if label == "创建材质类型": return pbrset_created_type_text(entry).lower()
            if label == "文件夹": return safe_str(entry.get("folder", ""), "").lower()
            if label == "状态": return safe_str(entry.get("status", ""), "").lower()
            return safe_str(entry.get("name", ""), "").lower()

        def texture_key(entry):
            if label == "贴图": return safe_str(entry.get("file", ""), "").lower()
            if label == "通道": return safe_str(entry.get("channel", ""), "").lower()
            if label == "源尺寸": return "%08d_%08d" % (int(entry.get("width", 0) or 0), int(entry.get("height", 0) or 0))
            if label == "合格后尺寸": return texture_qualified_size_text(entry).lower()
            if label == "源状态": return "1" if safe_texture_exists(entry.get("path", "")) else "0"
            if label == "输出状态": return texture_output_status_info(entry).get("text", "").lower()
            if label == "2幂": return "1" if is_power_of_two_int(entry.get("width", 0)) and is_power_of_two_int(entry.get("height", 0)) else "0"
            if label in ("问题/建议", "问题"): return "，".join(texture_entry_passes_streaming(entry)[1]).lower()
            if label == "相关物体": return texture_owner_nodes_text(entry).lower()
            if label == "源路径": return safe_str(entry.get("path", ""), "").lower()
            if label == "输出路径": return safe_str(entry.get("output", ""), "").lower()
            if label == "引用数": return "%08d" % len(entry.get("texmaps", []))
            if label == "处理状态": return safe_str(entry.get("status", ""), "").lower()
            return safe_str(entry.get("file", ""), "").lower()

        try:
            if tree_attr == "material_tree":
                cache.sort(key=mat_key, reverse=reverse)
                self.refresh_material_tree()
            elif tree_attr == "pbr_tree":
                cache.sort(key=mat_key, reverse=reverse)
                self.refresh_pbr_tree()
            elif tree_attr == "object_tree":
                cache.sort(key=node_key, reverse=reverse)
                self.refresh_object_tree()
            elif tree_attr == "group_tree":
                cache.sort(key=node_key, reverse=reverse)
                self.refresh_group_tree()
            elif tree_attr == "light_tree":
                cache.sort(key=node_key, reverse=reverse)
                self.refresh_light_tree()
            elif tree_attr == "camera_tree":
                cache.sort(key=node_key, reverse=reverse)
                self.refresh_camera_tree()
            elif tree_attr == "pbrset_tree":
                cache.sort(key=pbrset_key, reverse=reverse)
                self.refresh_pbrset_tree()
            elif tree_attr == "texture_tree":
                cache.sort(key=texture_key, reverse=reverse)
                self.refresh_texture_tree()
            self.restore_checked_keys(tree_attr, checked_keys)
            self.log("已按 {} {}排列".format(label, "降序" if reverse else "升序"))
        except Exception:
            self.log(status_text_for_exception("排序失败"))

    # ---------- 样式 ----------
    def apply_ui_style(self):
        try:
            theme = self.skin_combo.currentText()
        except Exception:
            theme = "暖木暗色"
        palettes = {
            "暖木暗色": dict(bg="#151311", card="#24201C", card2="#2D2721", field="#191714", text="#F4EEE7", muted="#B8A99A", line="#4B4036", button="#352E27", button_hover="#443A31", primary="#D79A52", primary_hover="#E4AD6A", primary_text="#1F1308", danger="#D85C4A", selection="#5B4027", selection_text="#FFF8EF", accent="#3A2B1D"),
            "曜石蓝黑": dict(bg="#0E1117", card="#171B24", card2="#1F2633", field="#10141C", text="#EEF4FF", muted="#A5B4C7", line="#2E3A4D", button="#222B3A", button_hover="#2D394C", primary="#5EA1FF", primary_hover="#7AB3FF", primary_text="#07111E", danger="#FF5D5D", selection="#1E4F86", selection_text="#FFFFFF", accent="#162842"),
            "石材灰": dict(bg="#1B1D1F", card="#282B2E", card2="#303438", field="#202326", text="#F2F4F5", muted="#B4BABF", line="#474D53", button="#363B40", button_hover="#444A50", primary="#9CC7C0", primary_hover="#B4D9D3", primary_text="#0A1B19", danger="#EF6A5B", selection="#3B5C61", selection_text="#FFFFFF", accent="#28383A"),
            "奶油浅色": dict(bg="#F3EFE7", card="#FFFDF8", card2="#F8F2E8", field="#FFFDF8", text="#27221D", muted="#756A5F", line="#D9CDBE", button="#EFE6D8", button_hover="#E5D8C7", primary="#B8773D", primary_hover="#CC8A4F", primary_text="#FFFFFF", danger="#C44D3D", selection="#E9D0B7", selection_text="#271D14", accent="#EAD6BE"),
            "米兰岩板": dict(bg="#111312", card="#1D201E", card2="#2A2E2B", field="#171A18", text="#F0F1EB", muted="#A7AEA5", line="#3E4640", button="#2C312D", button_hover="#394039", primary="#C8B98F", primary_hover="#D8CAA0", primary_text="#17130A", danger="#D76D5D", selection="#4E5141", selection_text="#FFFDF5", accent="#303226"),
            "莫兰迪绿": dict(bg="#101816", card="#1A2622", card2="#24342E", field="#141D1A", text="#EDF6F1", muted="#A7BAB1", line="#365048", button="#253630", button_hover="#30453E", primary="#8DB7A2", primary_hover="#A2CAB6", primary_text="#091411", danger="#D86A63", selection="#355C4F", selection_text="#FFFFFF", accent="#233A33"),
            "铜黑展厅": dict(bg="#0C0A09", card="#171210", card2="#241A16", field="#100D0B", text="#FFF4EB", muted="#BFA99A", line="#463126", button="#2B1F1A", button_hover="#3A2A23", primary="#C97945", primary_hover="#E08D55", primary_text="#160B05", danger="#E15B4B", selection="#5C321E", selection_text="#FFF8F2", accent="#2C1B13"),
            "日式原木": dict(bg="#EDE5D8", card="#FFF9EF", card2="#F2E7D7", field="#FFFDF7", text="#2D2720", muted="#7D7064", line="#D1C1AD", button="#E7D8C2", button_hover="#DBC6AA", primary="#92704A", primary_hover="#AA8458", primary_text="#FFFFFF", danger="#B94F42", selection="#D7BE9D", selection_text="#1E1710", accent="#E5D3B8"),
            "包豪斯白": dict(bg="#F5F5F2", card="#FFFFFF", card2="#ECEDE8", field="#FFFFFF", text="#111111", muted="#5F6468", line="#D2D5D6", button="#E8EAEC", button_hover="#DDE1E5", primary="#0066CC", primary_hover="#0B79E0", primary_text="#FFFFFF", danger="#D72638", selection="#CDE3FF", selection_text="#0A1E32", accent="#F0D64B"),
            "经典灰": dict(bg="#D9D9D9", card="#ECECEC", card2="#D4D4D4", field="#FFFFFF", text="#202020", muted="#4F4F4F", line="#9A9A9A", button="#E1E1E1", button_hover="#D0D0D0", primary="#2F6DB5", primary_hover="#3C7FD0", primary_text="#FFFFFF", danger="#B6403A", selection="#B8D7FF", selection_text="#111111", accent="#C8C8C8"),
            "高对比深色": dict(bg="#000000", card="#101010", card2="#1B1B1B", field="#050505", text="#FFFFFF", muted="#D0D0D0", line="#5A5A5A", button="#242424", button_hover="#333333", primary="#00A7FF", primary_hover="#2DB8FF", primary_text="#000000", danger="#FF2D55", selection="#005BBB", selection_text="#FFFFFF", accent="#061D30"),
        }
        p = palettes.get(theme, palettes["暖木暗色"])
        try:
            self._ui_palette = dict(p)
        except Exception:
            pass
        _fsz = getattr(self, "_ui_font_size", 12)
        p = dict(p)
        p["font_base"] = "{}px".format(_fsz)
        p["font_small"] = "{}px".format(max(8, _fsz - 2))
        p["font_large"] = "{}px".format(_fsz + 1)
        qss = """
        QDialog { background: @bg@; color: @text@; font-family: "等线", "DengXian", "微软雅黑", "Microsoft YaHei", Arial; font-size: @font_base@; }
        QWidget { color: @text@; font-family: "Microsoft YaHei UI", "Microsoft YaHei", "DengXian", "Segoe UI", Arial; }
        QLabel { color: @text@; }
        QToolTip { background: @card2@; color: @text@; border: 1px solid @line@; padding: 6px; }
        QFrame#topBar, QFrame#bottomPanel { background: transparent; border: none; }
        QFrame#topTitlePanel, QFrame#topActionsPanel, QFrame#bottomLogPanel, QFrame#bottomProgressPanel { background: @card@; border: 1px solid @line@; border-radius: 16px; }
        QFrame#topActionCard { background: @card2@; border: 1px solid @line@; border-radius: 16px; padding: 4px; }
        QScrollArea#cardScrollArea { background: transparent; border: none; }
        QWidget#scrollTabInner { background: transparent; }
        QLabel#windowTitleLabel { color: @text@; font-size: @font_large@; font-weight: 900; padding: 0px; }
        QLabel#windowSubtitleLabel, QLabel#hintLabel { color: @muted@; font-size: @font_small@; font-weight: 600; }
        QLabel#sectionTitle { color: @text@; font-size: @font_base@; font-weight: 900; }
        QLabel#statusLabel { background: @card2@; color: @text@; border: 1px solid @line@; border-radius: 12px; padding: 4px 8px; font-weight: 800; }
        QFrame#leftNavPanel { background: @card@; border: 1px solid @line@; border-radius: 22px; }
        QFrame#sideQuickCard, QFrame#sideStatusCard { background: @card2@; border: 1px solid @line@; border-radius: 18px; }
        QFrame#sideCollapseHolder { background: transparent; border: none; padding: 0; margin: 0; }
        QPushButton#sideQuickButton { background: @button@; color: @text@; border: 1px solid @line@; border-radius: 13px; min-height: 18px; padding: 2px 5px; font-size: @font_base@; font-weight: 900; }
        QPushButton#sideQuickButton:hover { background: @button_hover@; border-color: @primary@; }
        QLabel#sideSmallTitle { color: @primary@; font-size: @font_base@; font-weight: 900; }
        QScrollArea#leftNavScroll, QWidget#leftNavInner { background: transparent; border: none; }
        QLabel#navPanelTitle { color: @text@; font-size: @font_base@; font-weight: 900; }
        QLabel#navPanelHint { color: @muted@; font-size: @font_small@; font-weight: 600; }
        QFrame#contentHost { background: @card@; border: 1px solid @line@; border-radius: 22px; }
        QFrame#contentHeaderCard { background: @card2@; border: 1px solid @line@; border-radius: 16px; }
        QLabel#pageTitleLabel { color: @text@; font-size: @font_large@; font-weight: 900; }
        QLabel#pageSubtitleLabel { color: @muted@; font-size: @font_base@; font-weight: 700; }
        QPushButton#navCardButton { background: @card2@; color: @text@; border: 1px solid @line@; border-radius: 18px; min-height: 26px; padding: 3px 8px; text-align: left; font-size: @font_base@; font-weight: 900; }
        QPushButton#navCardButton:hover { background: @button_hover@; color: @text@; border: 1px solid @primary@; }
        QPushButton#navCardButton:checked { background: @primary@; color: @primary_text@; border: 1px solid @primary@; }
        QPushButton#navCardButton:checked:hover { background: @primary_hover@; color: @primary_text@; border: 1px solid @primary_hover@; }
        QGroupBox { background: @card@; color: @text@; border: 1px solid @line@; border-radius: 10px; margin-top: 10px; padding: 10px 8px 8px 8px; font-weight: 900; }
        QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 14px; padding: 2px 6px; color: @primary@; background: @accent@; border-radius: 5px; }
        QCheckBox, QRadioButton { color: @text@; spacing: 4px; font-size: @font_base@; font-weight: 700; }
        QLineEdit, QSpinBox, QComboBox { background: @field@; color: @text@; border: 1px solid @line@; border-radius: 7px; min-height: 20px; padding: 2px 7px; font-size: @font_base@; font-weight: 800; }
        QLineEdit:hover, QSpinBox:hover, QComboBox:hover { border-color: @primary@; background: @card2@; }
        QLineEdit:focus, QSpinBox:focus, QComboBox:focus { border: 2px solid @primary@; background: @field@; }
        QSpinBox { padding-right: 14px; }
        QSpinBox::up-button, QSpinBox::down-button { width: 0px; height: 0px; border: none; }
        QComboBox::drop-down { border: none; width: 28px; }
        QComboBox QAbstractItemView { background: @card@; color: @text@; border: 1px solid @line@; selection-background-color: @selection@; selection-color: @selection_text@; outline: 0; }
        QTabWidget::pane { background: @card@; border: 1px solid @line@; border-radius: 14px; top: -1px; }
        QTabWidget::tab-bar { left: 8px; }
        QTabBar::tab { background: @button@; color: @text@; border: 1px solid @line@; border-bottom: none; border-top-left-radius: 12px; border-top-right-radius: 12px; padding: 3px 8px; margin-right: 2px; font-weight: 900; min-height: 16px; }
        QTabBar::tab:hover { background: @button_hover@; color: @text@; border-color: @primary@; }
        QTabBar::tab:selected { background: @primary@; color: @primary_text@; border-color: @primary@; }
        QTabBar::tab:!selected { margin-top: 4px; }
        QScrollBar:vertical { background: @card2@; width: 12px; margin: 2px; border-radius: 6px; }
        QScrollBar::handle:vertical { background: @button_hover@; min-height: 30px; border: 1px solid @line@; border-radius: 5px; }
        QScrollBar::handle:vertical:hover { background: @primary@; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; border: none; background: transparent; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
        QScrollBar:horizontal { background: @card2@; height: 12px; margin: 2px; border-radius: 6px; }
        QScrollBar::handle:horizontal { background: @button_hover@; min-width: 30px; border: 1px solid @line@; border-radius: 5px; }
        QScrollBar::handle:horizontal:hover { background: @primary@; }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; border: none; background: transparent; }
        QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }
        QSplitter::handle { background: @line@; border: none; }
        QSplitter::handle:hover { background: @primary@; }
        QPushButton { background: @button@; color: @text@; border: 1px solid @line@; border-radius: 7px; min-height: 18px; padding: 2px 6px; font-size: @font_base@; font-weight: 900; }
        QFrame#topActionsPanel QPushButton { min-height: 20px; min-width: 44px; padding: 2px 7px; border-radius: 8px; }
        QFrame#topActionsPanel QComboBox { min-height: 20px; padding-top: 2px; padding-bottom: 2px; }
        QPushButton:hover { background: @button_hover@; color: @text@; border: 1px solid @primary@; }
        QPushButton:pressed { background: @primary@; color: @primary_text@; border: 1px solid @primary_hover@; padding-top: 4px; padding-bottom: 2px; }
        QPushButton:checked { background: @selection@; color: @selection_text@; border: 1px solid @primary@; }
        QPushButton:checked:hover { background: @primary@; color: @primary_text@; border: 1px solid @primary_hover@; }
        QPushButton:disabled { background: @card2@; color: @muted@; border: 1px solid @line@; }
        QPushButton#primaryButton { background: @primary@; color: @primary_text@; border: 1px solid @primary@; }
        QPushButton#primaryButton:hover { background: @primary_hover@; color: @primary_text@; border: 1px solid @primary_hover@; }
        QPushButton#primaryButton:pressed { background: @button_hover@; color: @text@; border: 1px solid @primary@; }
        QPushButton#dangerButton { background: @danger@; color: #FFFFFF; border: 1px solid @danger@; }
        QPushButton#dangerButton:hover { background: @danger@; color: #FFFFFF; border: 1px solid @primary_hover@; }
        QPushButton#dangerButton:pressed { background: @button_hover@; color: @text@; border: 1px solid @danger@; }
        QPushButton#checkToolButton { background: @accent@; color: @text@; min-height: 18px; border: 1px solid @line@; }
        QPushButton#checkToolButton:hover { background: @button_hover@; color: @text@; border: 1px solid @primary@; }
        QPushButton#checkToolButton:pressed { background: @primary@; color: @primary_text@; border: 1px solid @primary_hover@; }
        QPushButton#checkToolButton:checked { background: @selection@; color: @selection_text@; border: 1px solid @primary@; }
        QPushButton#checkToolButton:checked:hover { background: @primary@; color: @primary_text@; border: 1px solid @primary_hover@; }
        QFrame#checkBar { background: @card2@; border: 1px solid @line@; border-top-left-radius: 18px; border-top-right-radius: 18px; }
        QFrame#compactRenameRow { background: transparent; border: none; min-height: 26px; }
        QLabel#compactRenameLabel { color: @muted@; font-size: @font_base@; font-weight: 900; padding-left: 2px; padding-right: 1px; }
        #listSearchEdit { min-width: 150px; font-size: @font_base@; font-weight: 800; padding-left: 6px; }
        #checkToolButton { min-height: 18px; font-size: @font_base@; font-weight: 900; padding-left: 6px; padding-right: 6px; }
        QTreeWidget, QTableWidget, QPlainTextEdit { background: @field@; color: @text@; border: 1px solid @line@; alternate-background-color: @card2@; selection-background-color: @selection@; selection-color: @selection_text@; font-size: @font_base@; font-family: "Microsoft YaHei UI", "Microsoft YaHei", "DengXian", "Segoe UI", Arial; }
        QTreeWidget:hover, QTableWidget:hover, QPlainTextEdit:hover { border-color: @primary@; }
        QTreeWidget { border-top: none; border-bottom-left-radius: 16px; border-bottom-right-radius: 16px; }
        QTreeWidget::item { min-height: 16px; padding: 1px 4px; }
        QTreeWidget::item:hover { background: @button_hover@; color: @text@; }
        QTreeWidget::item:selected { background: @selection@; color: @selection_text@; }
        QTableWidget::item:selected { background: @selection@; color: @selection_text@; }
        QHeaderView::section { background: @card2@; color: @text@; border: none; border-right: 1px solid @line@; border-bottom: 1px solid @line@; padding: 3px 5px; font-weight: 900; }
        QProgressBar { background: @card2@; border: 1px solid @line@; border-radius: 4px; height: 10px; text-align: center; color: @text@; font-weight: 900; }
        QProgressBar::chunk { background: @primary@; border-radius: 3px; }
        """
        for k, v in p.items():
            qss = qss.replace("@{}@".format(k), v)
        try:
            self.setStyleSheet(qss)
            for w in [self] + self.findChildren(QtWidgets.QWidget):
                try:
                    # 3ds Max 2024 的 Qt 主题有时不会主动刷新 hover 状态，这里强制启用鼠标追踪和 hover 属性。
                    try:
                        w.setMouseTracking(True)
                    except Exception:
                        pass
                    try:
                        wa_hover = getattr(QtCore.Qt, "WA_Hover")
                    except Exception:
                        try:
                            wa_hover = QtCore.Qt.WidgetAttribute.WA_Hover
                        except Exception:
                            wa_hover = None
                    if wa_hover is not None:
                        try:
                            w.setAttribute(wa_hover, True)
                        except Exception:
                            pass
                    try:
                        wa_styled_bg = getattr(QtCore.Qt, "WA_StyledBackground")
                    except Exception:
                        try:
                            wa_styled_bg = QtCore.Qt.WidgetAttribute.WA_StyledBackground
                        except Exception:
                            wa_styled_bg = None
                    if wa_styled_bg is not None:
                        try:
                            w.setAttribute(wa_styled_bg, True)
                        except Exception:
                            pass
                    try:
                        w.update()
                    except Exception:
                        pass
                except Exception:
                    pass
        except Exception:
            pass

    def showEvent(self, event):
        try:
            super(InteriorSceneStudioPro, self).showEvent(event)
        except Exception:
            pass
        try:
            if not bool(getattr(self, "_button_override_refresh_done", False)):
                self._button_override_refresh_done = True
                QtCore.QTimer.singleShot(180, self._deferred_finalize_button_styles)
                QtCore.QTimer.singleShot(420, self._deferred_finalize_button_styles)
        except Exception:
            pass

    def _deferred_finalize_button_styles(self):
        try:
            self._apply_button_style_overrides()
            QtWidgets.QApplication.processEvents()
        except Exception:
            pass

    def _button_qss_by_role(self, role_name):
        try:
            p = getattr(self, "_ui_palette", {}) or {}
            text = p.get("text", "#F4EEE7")
            line = p.get("line", "#4B4036")
            button = p.get("button", "#352E27")
            button_hover = p.get("button_hover", "#443A31")
            primary = p.get("primary", "#D79A52")
            primary_hover = p.get("primary_hover", "#E4AD6A")
            primary_text = p.get("primary_text", "#1F1308")
            danger = p.get("danger", "#D85C4A")
            selection = p.get("selection", "#5B4027")
            selection_text = p.get("selection_text", "#FFF8EF")
            radius_map = {
                "nav": 18,
                "side": 13,
                "top": 8,
                "primary": 8,
                "danger": 8,
                "default": 7,
            }
            radius = radius_map.get(role_name, 7)
            base_bg = button
            base_fg = text
            base_border = line
            hover_bg = button_hover
            hover_fg = text
            hover_border = primary
            checked_bg = selection
            checked_fg = selection_text
            checked_border = primary
            if role_name == "primary":
                base_bg = primary
                base_fg = primary_text
                base_border = primary
                hover_bg = primary_hover
                hover_fg = primary_text
                hover_border = primary_hover
                checked_bg = primary_hover
                checked_fg = primary_text
                checked_border = primary_hover
            elif role_name == "danger":
                base_bg = danger
                base_fg = "#FFFFFF"
                base_border = danger
                hover_bg = danger
                hover_fg = "#FFFFFF"
                hover_border = primary_hover
                checked_bg = danger
                checked_fg = "#FFFFFF"
                checked_border = primary_hover
            elif role_name == "nav":
                checked_bg = primary
                checked_fg = primary_text
                checked_border = primary
            return (
                "QPushButton {"
                "background:%s; color:%s; border:1px solid %s; border-radius:%dpx; "
                "padding:2px 6px; font-weight:900;"
                "}"
                "QPushButton:hover { background:%s; color:%s; border:1px solid %s; }"
                "QPushButton:pressed { background:%s; color:%s; border:1px solid %s; }"
                "QPushButton:checked { background:%s; color:%s; border:1px solid %s; }"
            ) % (
                base_bg, base_fg, base_border, radius,
                hover_bg, hover_fg, hover_border,
                hover_bg, hover_fg, hover_border,
                checked_bg, checked_fg, checked_border,
            )
        except Exception:
            return ""

    def _apply_button_style_overrides(self):
        try:
            for btn in self.findChildren(QtWidgets.QPushButton):
                try:
                    if isinstance(btn, PaintedButton):
                        try:
                            btn.setStyleSheet("")
                        except Exception:
                            pass
                        try:
                            btn.update()
                        except Exception:
                            pass
                        continue
                    name = safe_str(btn.objectName(), "")
                    try:
                        wa_styled_bg = getattr(QtCore.Qt, "WA_StyledBackground")
                    except Exception:
                        try:
                            wa_styled_bg = QtCore.Qt.WidgetAttribute.WA_StyledBackground
                        except Exception:
                            wa_styled_bg = None
                    if wa_styled_bg is not None:
                        try:
                            btn.setAttribute(wa_styled_bg, True)
                        except Exception:
                            pass
                    try:
                        btn.setAutoFillBackground(False)
                    except Exception:
                        pass
                    if name == "navCardButton":
                        btn.setStyleSheet(self._button_qss_by_role("nav"))
                    elif name == "sideQuickButton":
                        btn.setStyleSheet(self._button_qss_by_role("side"))
                    elif name == "primaryButton":
                        btn.setStyleSheet(self._button_qss_by_role("primary"))
                    elif name == "dangerButton":
                        btn.setStyleSheet(self._button_qss_by_role("danger"))
                    elif btn.parent() is getattr(self, "top_actions_panel", None):
                        btn.setStyleSheet(self._button_qss_by_role("top"))
                    try:
                        btn.update()
                    except Exception:
                        pass
                except Exception:
                    pass
        except Exception:
            pass

    def apply_ui_font_size(self):
        """响应字号 Slider 变化：更新 _ui_font_size 并重新应用样式。"""
        try:
            self._ui_font_size = max(9, min(18, int(getattr(self, "_ui_font_size", 12))))
        except Exception:
            self._ui_font_size = 12
        self.refresh_ui_font_controls()
        try:
            self.apply_ui_style()
            self.polish_inputs()
            self.enhance_buttons_with_icons()
            self.polish_layouts()
            self._apply_button_style_overrides()
        except Exception:
            pass

    def resizeEvent(self, event):
        try:
            super(InteriorSceneStudioPro, self).resizeEvent(event)
        except Exception:
            pass
        try:
            self.ai_render_chat()
        except Exception:
            pass

    def refresh_ui_font_controls(self):
        try:
            size = max(9, min(18, int(getattr(self, "_ui_font_size", 12))))
        except Exception:
            size = 12
            self._ui_font_size = size
        try:
            self.ui_font_val_label.setText("{}px".format(size))
        except Exception:
            pass
        try:
            self.ui_font_decrease_btn.setEnabled(size > 9)
            self.ui_font_increase_btn.setEnabled(size < 18)
        except Exception:
            pass

    def adjust_ui_font_size(self, delta):
        try:
            current = int(getattr(self, "_ui_font_size", 12))
        except Exception:
            current = 12
        new_size = max(9, min(18, current + int(delta)))
        if new_size == current:
            self.refresh_ui_font_controls()
            return
        self._ui_font_size = new_size
        self.apply_ui_font_size()

    # ---------- V10：配置 / 备份 / 体检 ----------

    def show_scene_health_report(self):
        self.log("开始生成场景体检报告...")
        rows = build_scene_health_rows()
        dlg = SceneHealthDialog(rows, self)
        dlg.exec_() if hasattr(dlg, "exec_") else dlg.exec()
        issue_count = len([r for r in rows if r.get("count", 0) > 0])
        self.log("场景体检完成：{} 项，{} 项带标记/问题".format(len(rows), issue_count))

    def get_backup_keep_count(self):
        try:
            return int(self.backup_keep_spin.value())
        except Exception:
            return 3

    def backup_scene_from_ui(self):
        ok, msg = backup_current_max_file_copy(max_keep=self.get_backup_keep_count())
        if ok:
            self.log("已复制备份：{}".format(msg))
            try:
                QtWidgets.QMessageBox.information(self, "备份完成", "已复制当前磁盘文件到：\n{}\n\n最多保留备份数：{}\n注意：如果当前场景有未保存改动，请手动保存后再备份一次。".format(msg, self.get_backup_keep_count()))
            except Exception:
                pass
        else:
            self.log(msg)
            try:
                QtWidgets.QMessageBox.warning(self, "备份失败", msg)
            except Exception:
                pass
        return ok

    def open_help_file(self):
        try:
            path = installed_help_file_path()
            if os.path.exists(path):
                if open_file_in_os(path):
                    self.log("已打开帮助文件：{}".format(path))
                else:
                    self.log("打开帮助文件失败：{}".format(path))
            else:
                self.log("未找到帮助文件：{}".format(path))
                try:
                    QtWidgets.QMessageBox.information(self, "帮助文件", "未找到帮助文件：\n{}\n\n请重新安装完整安装包，或打开安装包 help 文件夹里的帮助文件。".format(path))
                except Exception:
                    pass
        except Exception:
            self.log(status_text_for_exception("打开帮助文件失败"))

    def collect_config(self):
        data = {}
        try: data["skin"] = self.skin_combo.currentText()
        except Exception: pass
        try: data["ui_font_size"] = int(getattr(self, "_ui_font_size", 12))
        except Exception: pass
        try: data["window"] = dict(keep_on_top=self.chk_keep_on_top.isChecked())
        except Exception: pass
        try:
            data["object"] = dict(prefix=self.obj_prefix.text(), start=self.obj_start_index.value(), padding=self.obj_padding.value(),
                                  layer=self.chk_obj_layer.isChecked(), material=self.chk_obj_material.isChecked(), group_tag=self.chk_obj_group_tag.isChecked())
        except Exception: pass
        try:
            data["group"] = dict(prefix=self.group_prefix.text(), start=self.group_start_index.value(), padding=self.group_padding.value(),
                                 layer=self.chk_group_layer.isChecked(), group_tag=self.chk_group_tag.isChecked())
        except Exception: pass
        try:
            data["light"] = dict(prefix=self.light_prefix.text(), start=self.light_start_index.value(), padding=self.light_padding.value(),
                                 layer=self.chk_light_layer.isChecked(), type=self.chk_light_type.isChecked(), tag=self.chk_light_tag.isChecked())
        except Exception: pass
        try:
            data["camera"] = dict(prefix=self.camera_prefix.text(), start=self.camera_start_index.value(), padding=self.camera_padding.value(),
                                  layer=self.chk_camera_layer.isChecked(), type=self.chk_camera_type.isChecked(), tag=self.chk_camera_tag.isChecked())
        except Exception: pass
        try:
            data["material"] = dict(prefix=self.mat_prefix.text(), start=self.mat_start_index.value(), padding=self.mat_padding.value(),
                                    klass=self.chk_mat_class.isChecked(), parent=self.chk_mat_parent.isChecked())
        except Exception: pass
        try:
            data["standardize"] = dict(target=self.current_material_target_mode(), prefix=self.pbr_prefix.text(),
                                       skip_existing=self.chk_pbr_skip_existing.isChecked(), convert_mso=self.chk_pbr_convert_mso.isChecked(),
                                       try_complex=self.chk_pbr_try_complex.isChecked(), simplify_maps=self.chk_pbr_simplify_maps.isChecked())
        except Exception: pass
        try:
            data["pbrset"] = dict(folder=self.pbrset_folder.text(), recursive=self.chk_pbrset_recursive.isChecked(),
                                  group_by_folder=self.chk_pbrset_group_by_folder.isChecked(),
                                  target=self.pbrset_target_combo.currentText(), prefix=self.pbrset_prefix.text(),
                                  normal=self.pbrset_normal_combo.currentText(), gloss=self.pbrset_gloss_combo.currentText(),
                                  assign_selected=self.chk_pbrset_assign_selected.isChecked())
        except Exception: pass
        try:
            data["pbr_download"] = dict(library_dir=self.pbr_library_dir.text(),
                                        extract_zip=self.chk_pbr_download_extract_zip.isChecked(),
                                        delete_archive=self.chk_pbr_delete_archive_after_extract.isChecked() if hasattr(self, "chk_pbr_delete_archive_after_extract") else True,
                                        flatten_redundant=self.chk_pbr_flatten_redundant_folder.isChecked() if hasattr(self, "chk_pbr_flatten_redundant_folder") else True,
                                        watch_clipboard=self.chk_pbr_watch_clipboard.isChecked() if hasattr(self, "chk_pbr_watch_clipboard") else False,
                                        push_port=self.pbr_push_port_spin.value() if hasattr(self, "pbr_push_port_spin") else 19527,
                                        no_overwrite=self.chk_pbr_download_no_overwrite.isChecked(),
                                        allowed_types={k: v.isChecked() for k, v in self.pbr_download_type_checks.items()} if hasattr(self, "pbr_download_type_checks") else {})
            try:
                save_pbr_library_dir_state(self.pbr_library_dir.text())
            except Exception:
                pass
        except Exception: pass
        try:
            self.ai_save_current_provider_config()
            data["ai_assistant"] = dict(
                provider=self.ai_provider_combo.currentText(),
                providers=getattr(self, "ai_provider_configs", {}),
                api_type=self.ai_api_type_combo.currentText(),
                base_url=self.ai_base_url.text(),
                model=self.ai_model.text(),
                save_key=self.ai_save_key_chk.isChecked(),
                api_key=self.ai_api_key.text() if self.ai_save_key_chk.isChecked() else "",
                temperature=self.ai_temperature.value(),
                history=self.ai_history_spin.value(),
                template=self.ai_template_combo.currentText(),
                display_font_size=self.ai_font_size_spin.value() if hasattr(self, "ai_font_size_spin") else 8,
                robot_name=getattr(self, "_ai_robot_name", "AI小助手"),
                user_name=getattr(self, "_ai_user_name", "用户"),
                config_collapsed=bool(getattr(self, "_ai_config_collapsed", False)),
                image_edit_mode=bool(getattr(self, "_ai_image_edit_mode", False))
            )
        except Exception: pass
        try:
            data["texture_streaming"] = dict(output_dir=self.texture_output_dir.text(), max_size=self.texture_max_size.value(),
                                             engine=self.current_texture_engine(), large_warn=self.texture_large_warn_size.value(),
                                             ue_name=self.chk_texture_ue_name.isChecked(), no_overwrite=self.chk_texture_no_overwrite.isChecked(),
                                             skip_existing_good=self.chk_texture_skip_existing_good.isChecked(),
                                             require_power2=self.chk_texture_require_power2.isChecked(), only_problem=self.chk_texture_only_problem.isChecked(),
                                             sync_objects=self.chk_texture_sync_objects.isChecked(),
                                             force_power2=self.chk_texture_force_power2.isChecked(),
                                             rename_prefix=self.ue_rename_prefix.text(),
                                             rename_sep=self.ue_rename_sep.text(),
                                             rename_include_mat=self.chk_ue_rename_include_mat.isChecked(),
                                             rename_include_obj=self.chk_ue_rename_include_obj.isChecked())
        except Exception: pass
        try:
            data["repair"] = dict(material=self.chk_fix_material.isChecked(), scale=self.chk_fix_scale.isChecked(), pivot=self.chk_fix_pivot.isChecked(),
                                  skip_frozen=self.chk_skip_frozen_repair.isChecked(), auto_backup=self.chk_auto_backup.isChecked(), backup_keep=self.get_backup_keep_count())
        except Exception: pass
        try:
            data["sync"] = dict(limit=self.max_auto_sync_count)
        except Exception: pass
        return data

    def apply_config_data(self, data):
        try:
            skin = data.get("skin")
            if skin:
                idx = self.skin_combo.findText(skin)
                if idx >= 0:
                    self.skin_combo.setCurrentIndex(idx)
        except Exception: pass
        try:
            fsz = int(data.get("ui_font_size", 12))
            self._ui_font_size = max(9, min(18, fsz))
            self.refresh_ui_font_controls()
        except Exception: pass

        win = data.get("window", {})
        try:
            self.chk_keep_on_top.setChecked(bool(win.get("keep_on_top", self.chk_keep_on_top.isChecked())))
        except Exception: pass

        obj = data.get("object", {})
        try:
            self.obj_prefix.setText(obj.get("prefix", self.obj_prefix.text()))
            self.obj_start_index.setValue(int(obj.get("start", self.obj_start_index.value())))
            self.obj_padding.setValue(int(obj.get("padding", self.obj_padding.value())))
            self.chk_obj_layer.setChecked(bool(obj.get("layer", self.chk_obj_layer.isChecked())))
            self.chk_obj_material.setChecked(bool(obj.get("material", self.chk_obj_material.isChecked())))
            self.chk_obj_group_tag.setChecked(bool(obj.get("group_tag", self.chk_obj_group_tag.isChecked())))
        except Exception: pass

        grp = data.get("group", {})
        try:
            self.group_prefix.setText(grp.get("prefix", self.group_prefix.text()))
            self.group_start_index.setValue(int(grp.get("start", self.group_start_index.value())))
            self.group_padding.setValue(int(grp.get("padding", self.group_padding.value())))
            self.chk_group_layer.setChecked(bool(grp.get("layer", self.chk_group_layer.isChecked())))
            self.chk_group_tag.setChecked(bool(grp.get("group_tag", self.chk_group_tag.isChecked())))
        except Exception: pass

        light = data.get("light", {})
        try:
            self.light_prefix.setText(light.get("prefix", self.light_prefix.text()))
            self.light_start_index.setValue(int(light.get("start", self.light_start_index.value())))
            self.light_padding.setValue(int(light.get("padding", self.light_padding.value())))
            self.chk_light_layer.setChecked(bool(light.get("layer", self.chk_light_layer.isChecked())))
            self.chk_light_type.setChecked(bool(light.get("type", self.chk_light_type.isChecked())))
            self.chk_light_tag.setChecked(bool(light.get("tag", self.chk_light_tag.isChecked())))
        except Exception: pass

        cam = data.get("camera", {})
        try:
            self.camera_prefix.setText(cam.get("prefix", self.camera_prefix.text()))
            self.camera_start_index.setValue(int(cam.get("start", self.camera_start_index.value())))
            self.camera_padding.setValue(int(cam.get("padding", self.camera_padding.value())))
            self.chk_camera_layer.setChecked(bool(cam.get("layer", self.chk_camera_layer.isChecked())))
            self.chk_camera_type.setChecked(bool(cam.get("type", self.chk_camera_type.isChecked())))
            self.chk_camera_tag.setChecked(bool(cam.get("tag", self.chk_camera_tag.isChecked())))
        except Exception: pass

        mat = data.get("material", {})
        try:
            self.mat_prefix.setText(mat.get("prefix", self.mat_prefix.text()))
            self.mat_start_index.setValue(int(mat.get("start", self.mat_start_index.value())))
            self.mat_padding.setValue(int(mat.get("padding", self.mat_padding.value())))
            self.chk_mat_class.setChecked(bool(mat.get("klass", self.chk_mat_class.isChecked())))
            self.chk_mat_parent.setChecked(bool(mat.get("parent", self.chk_mat_parent.isChecked())))
        except Exception: pass

        std = data.get("standardize", {})
        try:
            target = std.get("target")
            if target and hasattr(self, "pbr_target_combo"):
                idx = self.pbr_target_combo.findText(target)
                if idx >= 0:
                    self.pbr_target_combo.setCurrentIndex(idx)
            self.pbr_prefix.setText(std.get("prefix", self.pbr_prefix.text()))
            self.chk_pbr_skip_existing.setChecked(bool(std.get("skip_existing", self.chk_pbr_skip_existing.isChecked())))
            self.chk_pbr_convert_mso.setChecked(bool(std.get("convert_mso", self.chk_pbr_convert_mso.isChecked())))
            self.chk_pbr_try_complex.setChecked(bool(std.get("try_complex", self.chk_pbr_try_complex.isChecked())))
            self.chk_pbr_simplify_maps.setChecked(bool(std.get("simplify_maps", self.chk_pbr_simplify_maps.isChecked())))
        except Exception: pass

        pbrset = data.get("pbrset", {})
        try:
            self.pbrset_folder.setText(pbrset.get("folder", self.pbrset_folder.text()))
            self.chk_pbrset_recursive.setChecked(bool(pbrset.get("recursive", self.chk_pbrset_recursive.isChecked())))
            try:
                self.chk_pbrset_group_by_folder.setChecked(bool(pbrset.get("group_by_folder", self.chk_pbrset_group_by_folder.isChecked())))
            except Exception: pass
            target = pbrset.get("target")
            if target:
                idx = self.pbrset_target_combo.findText(target)
                if idx >= 0:
                    self.pbrset_target_combo.setCurrentIndex(idx)
            self.pbrset_prefix.setText(pbrset.get("prefix", self.pbrset_prefix.text()))
            try:
                n = pbrset.get("normal")
                if n:
                    idx = self.pbrset_normal_combo.findText(n)
                    if idx >= 0:
                        self.pbrset_normal_combo.setCurrentIndex(idx)
            except Exception: pass
            try:
                g = pbrset.get("gloss")
                if g:
                    idx = self.pbrset_gloss_combo.findText(g)
                    if idx >= 0:
                        self.pbrset_gloss_combo.setCurrentIndex(idx)
            except Exception: pass
            self.chk_pbrset_assign_selected.setChecked(bool(pbrset.get("assign_selected", self.chk_pbrset_assign_selected.isChecked())))
        except Exception: pass

        pd = data.get("pbr_download", {})
        try:
            lib_dir = pd.get("library_dir", "") or load_saved_pbr_library_dir()
            self.pbr_library_dir.setText(lib_dir)
            if lib_dir:
                save_pbr_library_dir_state(lib_dir)
            self.chk_pbr_download_extract_zip.setChecked(bool(pd.get("extract_zip", self.chk_pbr_download_extract_zip.isChecked())))
            if hasattr(self, "chk_pbr_delete_archive_after_extract"):
                self.chk_pbr_delete_archive_after_extract.setChecked(bool(pd.get("delete_archive", self.chk_pbr_delete_archive_after_extract.isChecked())))
            if hasattr(self, "chk_pbr_flatten_redundant_folder"):
                self.chk_pbr_flatten_redundant_folder.setChecked(bool(pd.get("flatten_redundant", self.chk_pbr_flatten_redundant_folder.isChecked())))
            if hasattr(self, "chk_pbr_watch_clipboard"):
                self.chk_pbr_watch_clipboard.setChecked(bool(pd.get("watch_clipboard", self.chk_pbr_watch_clipboard.isChecked())))
            if hasattr(self, "pbr_push_port_spin"):
                self.pbr_push_port_spin.setValue(int(pd.get("push_port", 19527)))
            self.chk_pbr_download_no_overwrite.setChecked(bool(pd.get("no_overwrite", self.chk_pbr_download_no_overwrite.isChecked())))
            allowed = pd.get("allowed_types", {})
            if isinstance(allowed, dict) and hasattr(self, "pbr_download_type_checks"):
                for k, v in allowed.items():
                    if k in self.pbr_download_type_checks:
                        self.pbr_download_type_checks[k].setChecked(bool(v))
        except Exception: pass

        ai = data.get("ai_assistant", {})
        try:
            self.ai_provider_configs = ai.get("providers", {}) if isinstance(ai.get("providers", {}), dict) else {}
            # 兼容旧配置：如果没有 per-provider，就把旧配置写入当前 provider。
            old_provider = ai.get("provider", "")
            if old_provider and old_provider not in self.ai_provider_configs:
                self.ai_provider_configs[old_provider] = dict(
                    api_type=ai.get("api_type", ""),
                    base_url=ai.get("base_url", ""),
                    model=ai.get("model", ""),
                    save_key=bool(ai.get("save_key", False)),
                    api_key=ai.get("api_key", "") if ai.get("save_key", False) else "",
                    temperature=ai.get("temperature", self.ai_temperature.value()),
                    history=ai.get("history", self.ai_history_spin.value())
                )

            idx = self.ai_provider_combo.findText(ai.get("provider", self.ai_provider_combo.currentText()))
            if idx >= 0:
                self.ai_provider_combo.setCurrentIndex(idx)
            else:
                try:
                    key = ai_provider_key_from_name(ai.get("provider", ""))
                    info = ai_provider_presets().get(key, {})
                    disp = info.get("display_name", "")
                    if disp:
                        idx = self.ai_provider_combo.findText(disp)
                        if idx >= 0:
                            self.ai_provider_combo.setCurrentIndex(idx)
                except Exception:
                    pass
            self.ai_active_provider_name = ai_provider_key_from_name(self.ai_provider_combo.currentText())
            self.ai_apply_preset()

            try:
                self.ai_temperature.setValue(float(ai.get("temperature", self.ai_temperature.value())))
                self.ai_history_spin.setValue(int(ai.get("history", self.ai_history_spin.value())))
            except Exception:
                pass
            try:
                self.ai_font_size_spin.setValue(int(ai.get("display_font_size", 8)))
                self.ai_update_chat_style()
            except Exception:
                pass
            try:
                rname = ai.get("robot_name", "AI小助手") or "AI小助手"
                self._ai_robot_name = rname
                self.ai_robot_name_edit.setText(rname)
            except Exception:
                pass
            try:
                uname = ai.get("user_name", "用户") or "用户"
                self._ai_user_name = uname
                self.ai_user_name_edit.setText(uname)
            except Exception:
                self._ai_user_name = "用户"
            try:
                self._ai_config_collapsed = bool(ai.get("config_collapsed", False))
            except Exception:
                self._ai_config_collapsed = False
            try:
                self._ai_image_edit_mode = bool(ai.get("image_edit_mode", False))
            except Exception:
                self._ai_image_edit_mode = False
            idx = self.ai_template_combo.findText(ai.get("template", self.ai_template_combo.currentText()))
            if idx >= 0:
                self.ai_template_combo.setCurrentIndex(idx)
            try:
                self.ai_refresh_image_edit_mode_ui()
            except Exception:
                pass
            try:
                self.ai_refresh_config_collapse_ui()
            except Exception:
                pass
        except Exception: pass

        texcfg = data.get("texture_streaming", {})
        try:
            self.texture_output_dir.setText(texcfg.get("output_dir", self.texture_output_dir.text()))
            self.texture_max_size.setValue(int(texcfg.get("max_size", self.texture_max_size.value())))
            try:
                self.texture_large_warn_size.setValue(int(texcfg.get("large_warn", self.texture_large_warn_size.value())))
            except Exception: pass
            try:
                eng = texcfg.get("engine")
                if eng:
                    idx = self.texture_engine_combo.findText(eng)
                    if idx >= 0:
                        self.texture_engine_combo.setCurrentIndex(idx)
            except Exception: pass
            self.chk_texture_ue_name.setChecked(bool(texcfg.get("ue_name", self.chk_texture_ue_name.isChecked())))
            self.chk_texture_no_overwrite.setChecked(bool(texcfg.get("no_overwrite", self.chk_texture_no_overwrite.isChecked())))
            try:
                self.chk_texture_skip_existing_good.setChecked(bool(texcfg.get("skip_existing_good", self.chk_texture_skip_existing_good.isChecked())))
            except Exception: pass
            self.chk_texture_require_power2.setChecked(bool(texcfg.get("require_power2", self.chk_texture_require_power2.isChecked())))
            self.chk_texture_only_problem.setChecked(bool(texcfg.get("only_problem", self.chk_texture_only_problem.isChecked())))
            self.chk_texture_force_power2.setChecked(bool(texcfg.get("force_power2", self.chk_texture_force_power2.isChecked())))
            try:
                self.ue_rename_prefix.setText(texcfg.get("rename_prefix", "T_") or "T_")
                self.ue_rename_sep.setText(texcfg.get("rename_sep", "_") or "_")
                self.chk_ue_rename_include_mat.setChecked(bool(texcfg.get("rename_include_mat", False)))
                self.chk_ue_rename_include_obj.setChecked(bool(texcfg.get("rename_include_obj", False)))
            except Exception: pass
        except Exception: pass

        rep = data.get("repair", {})
        try:
            self.chk_fix_material.setChecked(bool(rep.get("material", self.chk_fix_material.isChecked())))
            self.chk_fix_scale.setChecked(bool(rep.get("scale", self.chk_fix_scale.isChecked())))
            self.chk_fix_pivot.setChecked(bool(rep.get("pivot", self.chk_fix_pivot.isChecked())))
            self.chk_skip_frozen_repair.setChecked(bool(rep.get("skip_frozen", self.chk_skip_frozen_repair.isChecked())))
            self.chk_auto_backup.setChecked(bool(rep.get("auto_backup", self.chk_auto_backup.isChecked())))
            if hasattr(self, "backup_keep_spin"):
                self.backup_keep_spin.setValue(int(rep.get("backup_keep", self.backup_keep_spin.value())))
        except Exception: pass

        self.apply_ui_style()

    def auto_load_config_silent(self):
        try:
            path = studio_config_path()
            if os.path.exists(path):
                with open(path, "r", encoding="utf-8") as f:
                    data = json.load(f)
                self.apply_config_data(data)
                try:
                    self.apply_ui_style()
                    self.polish_inputs()
                    self.enhance_buttons_with_icons()
                    self.polish_layouts()
                    self._apply_button_style_overrides()
                    QtWidgets.QApplication.processEvents()
                    self.apply_ui_style()
                    self._apply_button_style_overrides()
                except Exception:
                    pass
                self.log("已自动加载配置：{}".format(path))
        except Exception:
            self.log(status_text_for_exception("自动加载配置失败"))

    def save_config(self):
        try:
            path = studio_config_path()
            with open(path, "w", encoding="utf-8") as f:
                json.dump(self.collect_config(), f, ensure_ascii=False, indent=2)
            self.log("配置已保存：{}".format(path))
            try:
                QtWidgets.QMessageBox.information(self, "保存配置", "配置已保存：\n{}".format(path))
            except Exception:
                pass
        except Exception:
            self.log(status_text_for_exception("保存配置失败"))

    def save_config_silent(self):
        try:
            path = studio_config_path()
            with open(path, "w", encoding="utf-8") as f:
                json.dump(self.collect_config(), f, ensure_ascii=False, indent=2)
            return True
        except Exception:
            self.log(status_text_for_exception("静默保存配置失败"))
            return False

    def load_config(self):
        try:
            path = studio_config_path()
            if not os.path.exists(path):
                self.log("没有找到配置文件：{}".format(path))
                try:
                    QtWidgets.QMessageBox.information(self, "加载配置", "没有找到配置文件：\n{}".format(path))
                except Exception:
                    pass
                return
            with open(path, "r", encoding="utf-8") as f:
                data = json.load(f)
            self.apply_config_data(data)
            try:
                self.ai_refresh_config_summary()
            except Exception:
                pass
            self.log("配置已加载：{}".format(path))
        except Exception:
            self.log(status_text_for_exception("加载配置失败"))


    # ---------- 通用勾选 ----------
    def set_tree_check_state(self, tree_attr, mode):
        tree = getattr(self, tree_attr, None)
        if tree is None:
            return
        rows = []
        for row in range(tree.topLevelItemCount()):
            item = tree.topLevelItem(row)
            if item is not None and not item.isHidden():
                rows.append(row)
        total = len(rows)
        if total <= 0:
            self.log("当前显示列表为空，无法批量勾选")
            return
        selected = set()
        for item in tree.selectedItems():
            row = tree.indexOfTopLevelItem(item)
            if row >= 0 and row in rows:
                selected.add(row)
        old_flags = (self.ignore_object_selection, self.ignore_group_selection, self.ignore_light_selection, self.ignore_camera_selection, self.ignore_material_selection)
        self.ignore_object_selection = self.ignore_group_selection = self.ignore_light_selection = self.ignore_camera_selection = self.ignore_material_selection = True
        checked = 0
        changed = 0
        try:
            # selected / unselected：先取消当前显示行，再只勾对应行。隐藏行保持原状，避免筛选后误改。
            for row in rows:
                item = tree.topLevelItem(row)
                old = item.checkState(0)
                if mode == "selected":
                    new = QT_CHECKED if row in selected else QT_UNCHECKED
                elif mode == "unselected":
                    new = QT_UNCHECKED if row in selected else QT_CHECKED
                elif mode == "all":
                    new = QT_CHECKED
                elif mode == "none":
                    new = QT_UNCHECKED
                elif mode == "invert":
                    new = QT_UNCHECKED if old == QT_CHECKED else QT_CHECKED
                else:
                    new = old
                if new != old:
                    item.setCheckState(0, new)
                    changed += 1
                if new == QT_CHECKED:
                    checked += 1
        finally:
            self.ignore_object_selection, self.ignore_group_selection, self.ignore_light_selection, self.ignore_camera_selection, self.ignore_material_selection = old_flags
        self.log("勾选完成：当前显示 {} 行，已勾 {} 行，变化 {} 行".format(total, checked, changed))

    # ---------- 对象页 ----------
    def build_object_tab(self):
        main = QtWidgets.QVBoxLayout(self.object_tab)
        main.setContentsMargins(12, 12, 12, 12)
        top_row = QtWidgets.QHBoxLayout()
        load_box = self.card("① 加载范围")
        load_lay = QtWidgets.QVBoxLayout(load_box)
        radio_lay = QtWidgets.QHBoxLayout()
        self.rb_selected = QtWidgets.QRadioButton("当前选择")
        self.rb_list = QtWidgets.QRadioButton("对象列表勾选项")
        self.rb_scene = QtWidgets.QRadioButton("整个场景几何体")
        self.rb_selected.setChecked(True)
        for r in (self.rb_selected, self.rb_list, self.rb_scene):
            radio_lay.addWidget(r)
        load_lay.addLayout(radio_lay)
        btn_lay = QtWidgets.QHBoxLayout()
        for text, func in [("加载选中", self.load_selected_to_object_list), ("添加选中", self.add_selected_to_object_list), ("加载场景对象/组", self.load_scene_to_object_list), ("🧹 清空列表", self.clear_object_list)]:
            b = QtWidgets.QPushButton(text); b.clicked.connect(func); btn_lay.addWidget(b)
        self.btn_remove_object_rows = QtWidgets.QPushButton("− 清除选择")
        self.btn_remove_object_rows.setObjectName("dangerButton")
        self.btn_remove_object_rows.setToolTip("只从模型列表中清除高亮选择的行，不删除场景对象。")
        self.btn_remove_object_rows.clicked.connect(lambda _=False: self.remove_selected_rows_from_tree("object_tree"))
        btn_lay.addWidget(self.btn_remove_object_rows)
        load_lay.addLayout(btn_lay)
        sync_lay = QtWidgets.QHBoxLayout()
        self.chk_sync_object_selection = QtWidgets.QCheckBox("列表选择同步场景")
        self.chk_sync_object_selection.setChecked(True)
        self.btn_sync_object_selection = QtWidgets.QPushButton("手动同步选择")
        self.btn_sync_object_selection.clicked.connect(lambda: self.sync_object_selection_to_scene(force=True))
        sync_lay.addWidget(self.chk_sync_object_selection)
        sync_lay.addWidget(self.btn_sync_object_selection)
        sync_lay.addStretch()
        load_lay.addLayout(sync_lay)
        top_row.addWidget(load_box, 1)

        scan_box = self.card("② 检测 / 筛选")
        scan_lay = QtWidgets.QVBoxLayout(scan_box)
        scan_buttons = QtWidgets.QHBoxLayout()
        self.btn_scan_list = QtWidgets.QPushButton("扫描当前列表")
        self.btn_scan_scene = QtWidgets.QPushButton("扫描整个场景")
        self.btn_scan_list.setObjectName("primaryButton")
        self.btn_scan_scene.setObjectName("primaryButton")
        self.btn_scan_list.clicked.connect(lambda: self.scan_object_issues(load_scene=False))
        self.btn_scan_scene.clicked.connect(lambda: self.scan_object_issues(load_scene=True))
        scan_buttons.addWidget(self.btn_scan_list); scan_buttons.addWidget(self.btn_scan_scene)
        scan_lay.addLayout(scan_buttons)
        filter_lay = QtWidgets.QHBoxLayout()
        filter_lay.addWidget(QtWidgets.QLabel("问题筛选"))
        self.object_filter_combo = QtWidgets.QComboBox()
        self.object_filter_combo.addItems(["全部", "有问题", "无材质", "缩放异常", "非等比缩放", "负缩放", "轴心异常", "非Poly", "有修改器", "冻结", "隐藏", "代理/外链", "组对象", "无问题"])
        self.object_filter_combo.currentTextChanged.connect(lambda _t: self.apply_object_filter())
        filter_lay.addWidget(self.object_filter_combo)
        scan_lay.addLayout(filter_lay)
        top_row.addWidget(scan_box, 1)
        main.addLayout(top_row)

        repair_box = self.card("③ 修复选项")
        repair_lay = QtWidgets.QHBoxLayout(repair_box)
        self.chk_fix_material = QtWidgets.QCheckBox("无材质补随机材质")
        self.chk_fix_scale = QtWidgets.QCheckBox("缩放异常 Reset XForm + 转 Poly")
        self.chk_fix_pivot = QtWidgets.QCheckBox("轴心归底居中")
        self.chk_skip_frozen_repair = QtWidgets.QCheckBox("跳过冻结对象")
        self.chk_auto_backup = QtWidgets.QCheckBox("执行前复制备份")
        self.backup_keep_spin = QtWidgets.QSpinBox()
        self.backup_keep_spin.setRange(0, 200)
        self.backup_keep_spin.setValue(3)
        try:
            self.backup_keep_spin.setSpecialValueText("不限制")
        except Exception:
            pass
        self.backup_keep_spin.setToolTip("同一场景最多保留多少个自动备份；默认3个，0表示不限制")
        for c in [self.chk_fix_material, self.chk_fix_scale, self.chk_fix_pivot, self.chk_auto_backup]: c.setChecked(True)
        for c in [self.chk_fix_material, self.chk_fix_scale, self.chk_fix_pivot, self.chk_skip_frozen_repair, self.chk_auto_backup]: repair_lay.addWidget(c)
        repair_lay.addWidget(QtWidgets.QLabel("最多保留备份"))
        repair_lay.addWidget(self.backup_keep_spin)
        repair_lay.addStretch()
        self.btn_start = QtWidgets.QPushButton("开始修复")
        self.btn_start.setObjectName("primaryButton")
        self.btn_stop = QtWidgets.QPushButton("停止")
        self.btn_stop.setObjectName("dangerButton")
        self.btn_stop.setEnabled(False)
        self.btn_force_stop = QtWidgets.QPushButton("强制停止")
        self.btn_force_stop.setObjectName("dangerButton")
        self.btn_force_stop.setEnabled(False)
        self.btn_start.clicked.connect(self.start_repair)
        self.btn_stop.clicked.connect(self.stop)
        self.btn_force_stop.clicked.connect(self.force_stop)
        repair_lay.addWidget(self.btn_start); repair_lay.addWidget(self.btn_stop); repair_lay.addWidget(self.btn_force_stop)
        main.addWidget(repair_box)

        rename_box = self.card("④ 重命名预览")
        ren_lay = QtWidgets.QGridLayout(rename_box)
        self.obj_prefix = QtWidgets.QLineEdit("SM")
        self.obj_start_index = QtWidgets.QSpinBox(); self.obj_start_index.setRange(0, 999999); self.obj_start_index.setValue(1)
        self.obj_padding = QtWidgets.QSpinBox(); self.obj_padding.setRange(1, 8); self.obj_padding.setValue(3)
        self.chk_obj_layer = QtWidgets.QCheckBox("带图层名"); self.chk_obj_layer.setChecked(True)
        self.chk_obj_material = QtWidgets.QCheckBox("带材质名"); self.chk_obj_material.setChecked(True)
        self.chk_obj_group_tag = QtWidgets.QCheckBox("带组标记"); self.chk_obj_group_tag.setChecked(True)
        self.btn_rename_objects_all = QtWidgets.QPushButton("重命名列表全部")
        self.btn_rename_objects_checked = QtWidgets.QPushButton("重命名打勾项")
        self.btn_rename_objects_selected = QtWidgets.QPushButton("重命名高亮选择")
        self.btn_rename_objects_checked.setObjectName("primaryButton")
        self.btn_rename_objects_all.clicked.connect(lambda: self.rename_objects_by_scope("all"))
        self.btn_rename_objects_checked.clicked.connect(lambda: self.rename_objects_by_scope("checked"))
        self.btn_rename_objects_selected.clicked.connect(lambda: self.rename_objects_by_scope("selected"))
        ren_lay.addWidget(self.make_compact_rename_row(self.obj_prefix, self.obj_start_index, self.obj_padding), 0, 0, 1, 6)
        ren_lay.addWidget(self.chk_obj_layer,1,0); ren_lay.addWidget(self.chk_obj_material,1,1); ren_lay.addWidget(self.chk_obj_group_tag,1,2)
        ren_lay.addWidget(self.btn_rename_objects_all,2,0,1,2); ren_lay.addWidget(self.btn_rename_objects_checked,2,2,1,2); ren_lay.addWidget(self.btn_rename_objects_selected,2,4,1,2)
        main.addWidget(rename_box)

        self.object_tree = QtWidgets.QTreeWidget()
        self.object_tree.setColumnCount(8)
        self.object_tree.setHeaderLabels(["对象", "类型", "图层", "材质", "组", "冻结", "问题", "状态"])
        self.prepare_tree(self.object_tree)
        self.object_tree.itemSelectionChanged.connect(self.request_object_selection_sync)
        main.addWidget(self.make_check_bar("对象列表", "object_tree"))
        main.addWidget(self.object_tree, 1)

    # ---------- 组页 ----------
    def build_group_tab(self):
        main = QtWidgets.QVBoxLayout(self.group_tab)
        main.setContentsMargins(12, 12, 12, 12)
        load_box = self.card("组加载 / 选择 / 打开关闭 / 重命名")
        lay = QtWidgets.QGridLayout(load_box)
        for i, (text, func) in enumerate([
            ("加载选中所属组", self.load_selected_to_group_list),
            ("添加选中所属组", self.add_selected_to_group_list),
            ("加载场景组", self.load_scene_to_group_list),
            ("🧹 清空列表", self.clear_group_list)
        ]):
            b = QtWidgets.QPushButton(text)
            b.clicked.connect(func)
            lay.addWidget(b, 0, i)

        self.btn_remove_group_rows = QtWidgets.QPushButton("− 清除选择")
        self.btn_remove_group_rows.setObjectName("dangerButton")
        self.btn_remove_group_rows.setToolTip("只从组列表中清除高亮选择的行，不删除场景组。")
        self.btn_remove_group_rows.clicked.connect(lambda _=False: self.remove_selected_rows_from_tree("group_tree"))
        lay.addWidget(self.btn_remove_group_rows, 0, 4)

        self.chk_sync_group_selection = QtWidgets.QCheckBox("组列表选择同步到场景，冻结组不选择")
        self.chk_sync_group_selection.setChecked(True)
        self.btn_sync_group_selection = QtWidgets.QPushButton("手动同步组选择")
        self.btn_sync_group_selection.clicked.connect(lambda: self.sync_group_selection_to_scene(force=True))
        self.btn_open_checked_groups = QtWidgets.QPushButton("打开勾选组")
        self.btn_close_checked_groups = QtWidgets.QPushButton("关闭勾选组")
        self.btn_open_all_groups = QtWidgets.QPushButton("打开全部组")
        self.btn_close_all_groups = QtWidgets.QPushButton("关闭全部组")
        self.btn_open_checked_groups.clicked.connect(lambda: self.set_checked_groups_open_state(True))
        self.btn_close_checked_groups.clicked.connect(lambda: self.set_checked_groups_open_state(False))
        self.btn_open_all_groups.clicked.connect(self.open_all_groups_from_ui)
        self.btn_close_all_groups.clicked.connect(self.close_all_groups_from_ui)
        lay.addWidget(self.chk_sync_group_selection, 1, 0, 1, 2)
        lay.addWidget(self.btn_sync_group_selection, 1, 2)
        lay.addWidget(self.btn_open_checked_groups, 2, 0)
        lay.addWidget(self.btn_close_checked_groups, 2, 1)
        lay.addWidget(self.btn_open_all_groups, 2, 2)
        lay.addWidget(self.btn_close_all_groups, 2, 3)

        self.group_prefix = QtWidgets.QLineEdit("GRP")
        self.group_start_index = QtWidgets.QSpinBox(); self.group_start_index.setRange(0, 999999); self.group_start_index.setValue(1)
        self.group_padding = QtWidgets.QSpinBox(); self.group_padding.setRange(1, 8); self.group_padding.setValue(3)
        self.chk_group_layer = QtWidgets.QCheckBox("带图层名"); self.chk_group_layer.setChecked(True)
        self.chk_group_tag = QtWidgets.QCheckBox("带 GRP 标记"); self.chk_group_tag.setChecked(True)
        self.chk_group_count = QtWidgets.QCheckBox("带成员数量"); self.chk_group_count.setChecked(True)
        self.btn_rename_groups_all = QtWidgets.QPushButton("重命名列表全部")
        self.btn_rename_groups_checked = QtWidgets.QPushButton("重命名打勾项")
        self.btn_rename_groups_selected = QtWidgets.QPushButton("重命名高亮选择")
        self.btn_rename_groups_checked.setObjectName("primaryButton")
        self.btn_rename_groups_all.clicked.connect(lambda: self.rename_groups_by_scope("all"))
        self.btn_rename_groups_checked.clicked.connect(lambda: self.rename_groups_by_scope("checked"))
        self.btn_rename_groups_selected.clicked.connect(lambda: self.rename_groups_by_scope("selected"))
        lay.addWidget(self.make_compact_rename_row(self.group_prefix, self.group_start_index, self.group_padding), 3, 0, 1, 6)
        lay.addWidget(self.chk_group_layer, 4, 0); lay.addWidget(self.chk_group_tag, 4, 1); lay.addWidget(self.chk_group_count, 4, 2)
        lay.addWidget(self.btn_rename_groups_all, 5, 0, 1, 2); lay.addWidget(self.btn_rename_groups_checked, 5, 2, 1, 2); lay.addWidget(self.btn_rename_groups_selected, 5, 4, 1, 2)

        main.addWidget(load_box)
        self.group_tree = QtWidgets.QTreeWidget()
        self.group_tree.setColumnCount(7)
        self.group_tree.setHeaderLabels(["组", "图层", "成员数", "打开", "冻结", "隐藏", "状态"])
        self.prepare_tree(self.group_tree)
        self.group_tree.itemSelectionChanged.connect(self.request_group_selection_sync)
        main.addWidget(self.make_check_bar("组列表", "group_tree"))
        main.addWidget(self.group_tree, 1)

    # ---------- 灯光页 ----------
    def build_light_tab(self):
        main = QtWidgets.QVBoxLayout(self.light_tab); main.setContentsMargins(12,12,12,12)
        load_box = self.card("灯光加载 / 同步 / 重命名")
        lay = QtWidgets.QGridLayout(load_box)
        for i,(text,func) in enumerate([("加载选中灯光",self.load_selected_to_light_list),("添加选中灯光",self.add_selected_to_light_list),("加载场景灯光",self.load_scene_to_light_list),("🧹 清空列表",self.clear_light_list)]):
            b=QtWidgets.QPushButton(text); b.clicked.connect(func); lay.addWidget(b,0,i)
        self.btn_remove_light_rows = QtWidgets.QPushButton("− 清除选择")
        self.btn_remove_light_rows.setObjectName("dangerButton")
        self.btn_remove_light_rows.setToolTip("只从灯光列表中清除高亮选择的行，不删除场景灯光。")
        self.btn_remove_light_rows.clicked.connect(lambda _=False: self.remove_selected_rows_from_tree("light_tree"))
        lay.addWidget(self.btn_remove_light_rows, 0, 4)
        self.chk_sync_light_selection = QtWidgets.QCheckBox("列表选择同步到场景，自动打开组，冻结灯光不选择"); self.chk_sync_light_selection.setChecked(True)
        self.btn_sync_light_selection = QtWidgets.QPushButton("手动同步灯光选择"); self.btn_sync_light_selection.clicked.connect(lambda: self.sync_light_selection_to_scene(force=True))
        self.light_prefix=QtWidgets.QLineEdit("L"); self.light_start_index=QtWidgets.QSpinBox(); self.light_start_index.setRange(0,999999); self.light_start_index.setValue(1)
        self.light_padding=QtWidgets.QSpinBox(); self.light_padding.setRange(1,8); self.light_padding.setValue(3)
        self.chk_light_layer=QtWidgets.QCheckBox("带图层名"); self.chk_light_layer.setChecked(True)
        self.chk_light_type=QtWidgets.QCheckBox("带类型"); self.chk_light_type.setChecked(True)
        self.chk_light_tag=QtWidgets.QCheckBox("带 LGT 标记"); self.chk_light_tag.setChecked(True)
        self.btn_rename_lights_all=QtWidgets.QPushButton("重命名列表全部"); self.btn_rename_lights_checked=QtWidgets.QPushButton("重命名打勾项"); self.btn_rename_lights_selected=QtWidgets.QPushButton("重命名高亮选择")
        self.btn_rename_lights_checked.setObjectName("primaryButton")
        self.btn_rename_lights_all.clicked.connect(lambda: self.rename_lights_by_scope("all")); self.btn_rename_lights_checked.clicked.connect(lambda: self.rename_lights_by_scope("checked")); self.btn_rename_lights_selected.clicked.connect(lambda: self.rename_lights_by_scope("selected"))
        lay.addWidget(self.chk_sync_light_selection,1,0,1,2); lay.addWidget(self.btn_sync_light_selection,1,2)
        lay.addWidget(self.make_compact_rename_row(self.light_prefix, self.light_start_index, self.light_padding), 2, 0, 1, 6)
        lay.addWidget(self.chk_light_layer,3,0); lay.addWidget(self.chk_light_type,3,1); lay.addWidget(self.chk_light_tag,3,2)
        lay.addWidget(self.btn_rename_lights_all,4,0,1,2); lay.addWidget(self.btn_rename_lights_checked,4,2,1,2); lay.addWidget(self.btn_rename_lights_selected,4,4,1,2)
        main.addWidget(load_box)
        self.light_tree=QtWidgets.QTreeWidget(); self.light_tree.setColumnCount(5); self.light_tree.setHeaderLabels(["灯光","类型","图层","冻结","状态"]); self.prepare_tree(self.light_tree); self.light_tree.itemSelectionChanged.connect(self.request_light_selection_sync)
        main.addWidget(self.make_check_bar("灯光列表", "light_tree")); main.addWidget(self.light_tree,1)

    # ---------- 相机页 ----------
    def build_camera_tab(self):
        main = QtWidgets.QVBoxLayout(self.camera_tab); main.setContentsMargins(12,12,12,12)
        load_box = self.card("相机加载 / 同步 / 重命名")
        lay = QtWidgets.QGridLayout(load_box)
        for i,(text,func) in enumerate([("加载选中相机",self.load_selected_to_camera_list),("添加选中相机",self.add_selected_to_camera_list),("加载场景相机",self.load_scene_to_camera_list),("🧹 清空列表",self.clear_camera_list)]):
            b=QtWidgets.QPushButton(text); b.clicked.connect(func); lay.addWidget(b,0,i)
        self.btn_remove_camera_rows = QtWidgets.QPushButton("− 清除选择")
        self.btn_remove_camera_rows.setObjectName("dangerButton")
        self.btn_remove_camera_rows.setToolTip("只从相机列表中清除高亮选择的行，不删除场景相机。")
        self.btn_remove_camera_rows.clicked.connect(lambda _=False: self.remove_selected_rows_from_tree("camera_tree"))
        lay.addWidget(self.btn_remove_camera_rows, 0, 4)
        self.chk_sync_camera_selection = QtWidgets.QCheckBox("列表选择同步到场景，自动打开组，冻结相机不选择"); self.chk_sync_camera_selection.setChecked(True)
        self.btn_sync_camera_selection = QtWidgets.QPushButton("手动同步相机选择"); self.btn_sync_camera_selection.clicked.connect(lambda: self.sync_camera_selection_to_scene(force=True))
        self.camera_prefix=QtWidgets.QLineEdit("CAM"); self.camera_start_index=QtWidgets.QSpinBox(); self.camera_start_index.setRange(0,999999); self.camera_start_index.setValue(1)
        self.camera_padding=QtWidgets.QSpinBox(); self.camera_padding.setRange(1,8); self.camera_padding.setValue(3)
        self.chk_camera_layer=QtWidgets.QCheckBox("带图层名"); self.chk_camera_layer.setChecked(True)
        self.chk_camera_type=QtWidgets.QCheckBox("带类型"); self.chk_camera_type.setChecked(True)
        self.chk_camera_tag=QtWidgets.QCheckBox("带 CAM 标记"); self.chk_camera_tag.setChecked(True)
        self.btn_rename_cameras_all=QtWidgets.QPushButton("重命名列表全部"); self.btn_rename_cameras_checked=QtWidgets.QPushButton("重命名打勾项"); self.btn_rename_cameras_selected=QtWidgets.QPushButton("重命名高亮选择")
        self.btn_rename_cameras_checked.setObjectName("primaryButton")
        self.btn_rename_cameras_all.clicked.connect(lambda: self.rename_cameras_by_scope("all")); self.btn_rename_cameras_checked.clicked.connect(lambda: self.rename_cameras_by_scope("checked")); self.btn_rename_cameras_selected.clicked.connect(lambda: self.rename_cameras_by_scope("selected"))
        lay.addWidget(self.chk_sync_camera_selection,1,0,1,2); lay.addWidget(self.btn_sync_camera_selection,1,2)
        lay.addWidget(self.make_compact_rename_row(self.camera_prefix, self.camera_start_index, self.camera_padding), 2, 0, 1, 6)
        lay.addWidget(self.chk_camera_layer,3,0); lay.addWidget(self.chk_camera_type,3,1); lay.addWidget(self.chk_camera_tag,3,2)
        lay.addWidget(self.btn_rename_cameras_all,4,0,1,2); lay.addWidget(self.btn_rename_cameras_checked,4,2,1,2); lay.addWidget(self.btn_rename_cameras_selected,4,4,1,2)
        main.addWidget(load_box)
        self.camera_tree=QtWidgets.QTreeWidget(); self.camera_tree.setColumnCount(5); self.camera_tree.setHeaderLabels(["相机","类型","图层","冻结","状态"]); self.prepare_tree(self.camera_tree); self.camera_tree.itemSelectionChanged.connect(self.request_camera_selection_sync)
        main.addWidget(self.make_check_bar("相机列表", "camera_tree")); main.addWidget(self.camera_tree,1)

    # ---------- 材质页 ----------
    def build_material_tab(self):
        main = QtWidgets.QVBoxLayout(self.material_tab); main.setContentsMargins(12,12,12,12)
        load_box = self.card("材质加载 / 同步 / 重命名")
        lay = QtWidgets.QGridLayout(load_box)
        for i,(text,func) in enumerate([("加载选中物体材质",self.load_selected_to_material_list),("添加选中物体材质",self.add_selected_to_material_list),("加载场景材质",self.load_scene_to_material_list),("🧹 清空列表",self.clear_material_list)]):
            b=QtWidgets.QPushButton(text); b.clicked.connect(func); lay.addWidget(b,0,i)
        self.btn_remove_material_rows = QtWidgets.QPushButton("− 清除选择")
        self.btn_remove_material_rows.setObjectName("dangerButton")
        self.btn_remove_material_rows.setToolTip("只从材质列表中清除高亮选择的行，不删除场景材质。")
        self.btn_remove_material_rows.clicked.connect(lambda _=False: self.remove_selected_rows_from_tree("material_tree"))
        lay.addWidget(self.btn_remove_material_rows, 0, 4)
        self.chk_sync_material_selection=QtWidgets.QCheckBox("材质选择同步使用它的物体，自动打开组，冻结物体不选择"); self.chk_sync_material_selection.setChecked(True)
        self.btn_sync_material_selection=QtWidgets.QPushButton("手动同步材质关联物体"); self.btn_sync_material_selection.clicked.connect(lambda: self.sync_material_selection_to_scene(force=True))
        self.mat_prefix=QtWidgets.QLineEdit("M"); self.mat_start_index=QtWidgets.QSpinBox(); self.mat_start_index.setRange(0,999999); self.mat_start_index.setValue(1)
        self.mat_padding=QtWidgets.QSpinBox(); self.mat_padding.setRange(1,8); self.mat_padding.setValue(3)
        self.chk_mat_class=QtWidgets.QCheckBox("带材质/渲染器类型"); self.chk_mat_class.setChecked(True)
        self.chk_mat_parent=QtWidgets.QCheckBox("子材质带母材质名"); self.chk_mat_parent.setChecked(True)
        self.btn_rename_materials_all=QtWidgets.QPushButton("重命名列表全部"); self.btn_rename_materials_checked=QtWidgets.QPushButton("重命名打勾项"); self.btn_rename_materials_selected=QtWidgets.QPushButton("重命名高亮选择")
        self.btn_rename_materials_checked.setObjectName("primaryButton")
        self.btn_rename_materials_all.clicked.connect(lambda: self.rename_materials_by_scope("all")); self.btn_rename_materials_checked.clicked.connect(lambda: self.rename_materials_by_scope("checked")); self.btn_rename_materials_selected.clicked.connect(lambda: self.rename_materials_by_scope("selected"))
        lay.addWidget(self.chk_sync_material_selection,1,0,1,2); lay.addWidget(self.btn_sync_material_selection,1,2)
        lay.addWidget(self.make_compact_rename_row(self.mat_prefix, self.mat_start_index, self.mat_padding), 2, 0, 1, 6)
        lay.addWidget(self.chk_mat_class,3,0); lay.addWidget(self.chk_mat_parent,3,1)
        lay.addWidget(self.btn_rename_materials_all,4,0,1,2); lay.addWidget(self.btn_rename_materials_checked,4,2,1,2); lay.addWidget(self.btn_rename_materials_selected,4,4,1,2)
        main.addWidget(load_box)
        self.material_tree=QtWidgets.QTreeWidget(); self.material_tree.setColumnCount(6); self.material_tree.setHeaderLabels(["材质","类型","角色","母材质","ID/槽","状态"]); self.prepare_tree(self.material_tree); self.material_tree.itemSelectionChanged.connect(self.request_material_selection_sync)
        main.addWidget(self.make_check_bar("材质列表", "material_tree")); main.addWidget(self.material_tree,1)

    def build_pbr_tab(self):
        main = QtWidgets.QVBoxLayout(self.pbr_tab); main.setContentsMargins(12,12,12,12)
        load_box = self.card("材质标准化 · Physical / PBR Metal-Rough / OpenPBR")
        lay = QtWidgets.QGridLayout(load_box)
        btns = [("从材质列表载入", self.load_material_list_to_pbr_list),("加载选中物体材质", self.load_selected_to_pbr_list),("添加选中物体材质", self.add_selected_to_pbr_list),("加载场景材质", self.load_scene_to_pbr_list),("🧹 清空列表", self.clear_pbr_list)]
        for i, (text, func) in enumerate(btns):
            b = QtWidgets.QPushButton(text); b.clicked.connect(func); lay.addWidget(b, 0, i)
        self.btn_remove_pbr_rows = QtWidgets.QPushButton("− 清除选择")
        self.btn_remove_pbr_rows.setObjectName("dangerButton")
        self.btn_remove_pbr_rows.setToolTip("只从材质标准化列表中清除高亮选择的行，不删除场景材质。")
        self.btn_remove_pbr_rows.clicked.connect(lambda _=False: self.remove_selected_rows_from_tree("pbr_tree"))
        lay.addWidget(self.btn_remove_pbr_rows, 0, 5)

        self.pbr_target_combo = QtWidgets.QComboBox()
        self.pbr_target_combo.addItems(["PBR Material Metal/Rough", "V-Ray Material", "Corona Physical Material", "Physical Material", "OpenPBR"])
        self.pbr_target_combo.setCurrentIndex(0)
        self.pbr_target_combo.setToolTip("默认推荐 PBR Metal/Rough，最适合下载的 PBR 贴图套装和 UE 流程；找不到目标类时会自动回退。")
        self.pbr_target_combo.currentIndexChanged.connect(lambda _=0: self.refresh_pbr_tree())

        self.pbr_prefix = QtWidgets.QLineEdit("MAT_STD")
        self.chk_pbr_skip_existing = QtWidgets.QCheckBox("跳过已是目标材质"); self.chk_pbr_skip_existing.setChecked(True)
        self.chk_pbr_convert_mso = QtWidgets.QCheckBox("Multi/Sub 保留母材质并转换子材质"); self.chk_pbr_convert_mso.setChecked(True)
        self.chk_pbr_try_complex = QtWidgets.QCheckBox("复杂材质也尝试转换（谨慎）"); self.chk_pbr_try_complex.setChecked(False)
        self.chk_pbr_simplify_maps = QtWidgets.QCheckBox("简化程序贴图，优先保留外部贴图"); self.chk_pbr_simplify_maps.setChecked(True)
        self.chk_sync_pbr_selection = QtWidgets.QCheckBox("列表选择同步场景")
        self.chk_sync_pbr_selection.setChecked(True)
        self.chk_sync_pbr_selection.setToolTip("选择材质标准化列表里的材质时，同步选择场景中使用该材质的物体。冻结物体不参与选择。")
        self.btn_sync_pbr_selection = QtWidgets.QPushButton("手动同步选择")
        self.btn_sync_pbr_selection.clicked.connect(lambda: self.sync_pbr_selection_to_scene(force=True))

        self.btn_preview_pbr_all = QtWidgets.QPushButton("预览标准化列表全部")
        self.btn_preview_pbr_checked = QtWidgets.QPushButton("预览标准化打勾项")
        self.btn_preview_pbr_selected = QtWidgets.QPushButton("预览标准化高亮选择")
        self.btn_preview_pbr_checked.setObjectName("primaryButton")
        self.btn_undo_pbr = QtWidgets.QPushButton("撤回上次材质标准化")
        self.btn_undo_pbr.setObjectName("dangerButton")
        self.btn_stop_pbr = QtWidgets.QPushButton("停止标准化")
        self.btn_stop_pbr.setObjectName("dangerButton")
        self.btn_stop_pbr.setEnabled(False)
        self.btn_preview_pbr_all.clicked.connect(lambda: self.preview_pbr_conversion_by_scope("all"))
        self.btn_preview_pbr_checked.clicked.connect(lambda: self.preview_pbr_conversion_by_scope("checked"))
        self.btn_preview_pbr_selected.clicked.connect(lambda: self.preview_pbr_conversion_by_scope("selected"))
        self.btn_undo_pbr.clicked.connect(self.undo_last_pbr_conversion)
        self.btn_stop_pbr.clicked.connect(self.stop_pbr_conversion)

        lay.addWidget(QtWidgets.QLabel("目标材质"), 1, 0); lay.addWidget(self.pbr_target_combo, 1, 1, 1, 2)
        lay.addWidget(QtWidgets.QLabel("新材质前缀"), 1, 3); lay.addWidget(self.pbr_prefix, 1, 4)
        lay.addWidget(self.chk_pbr_skip_existing, 2, 0, 1, 2); lay.addWidget(self.chk_pbr_convert_mso, 2, 2, 1, 2); lay.addWidget(self.chk_pbr_try_complex, 2, 4, 1, 2)
        lay.addWidget(self.chk_pbr_simplify_maps, 3, 0, 1, 3)
        lay.addWidget(self.chk_sync_pbr_selection, 3, 3, 1, 2)
        lay.addWidget(self.btn_sync_pbr_selection, 3, 5)
        lay.addWidget(self.btn_preview_pbr_all, 4, 0, 1, 2); lay.addWidget(self.btn_preview_pbr_checked, 4, 2, 1, 2); lay.addWidget(self.btn_preview_pbr_selected, 4, 4, 1, 2); lay.addWidget(self.btn_stop_pbr, 4, 6); lay.addWidget(self.btn_undo_pbr, 4, 7)
        hint = QtWidgets.QLabel("说明：这是保守标准化。复杂材质默认跳过；转换会分步执行并实时更新进度；程序贴图不会烘焙，会尽量提取底层外部 Bitmap，避免贴图丢失；Glossiness 贴图可能需要人工反相检查。")
        hint.setObjectName("hintLabel"); lay.addWidget(hint, 5, 0, 1, 7)
        main.addWidget(load_box)
        self.pbr_tree = QtWidgets.QTreeWidget(); self.pbr_tree.setColumnCount(7); self.pbr_tree.setHeaderLabels(["材质", "类型", "角色", "母材质", "判断", "动作", "状态"]); self.prepare_tree(self.pbr_tree); self.pbr_tree.itemSelectionChanged.connect(self.request_pbr_selection_sync)
        main.addWidget(self.make_check_bar("材质标准化列表", "pbr_tree")); main.addWidget(self.pbr_tree, 1)

    # ============================================================
    # 列表刷新
    # ============================================================
    def refresh_object_tree(self):
        self.ignore_object_selection = True; self.object_tree.clear()
        for obj in self.object_cache:
            item = QtWidgets.QTreeWidgetItem()
            item.setText(0, safe_str(getattr(obj,"name",""), "<无效对象>"))
            item.setText(1, get_class_name(obj))
            item.setText(2, get_layer_name(obj) if is_valid_node(obj) else "NoLayer")
            item.setText(3, get_object_material_name(obj) if is_valid_geometry(obj) else "NoMat")
            item.setText(4, "GRP" if is_group_head(obj) else ("GMB" if is_group_member(obj) else "-"))
            item.setText(5, "是" if is_frozen(obj) else "否")
            issues = self.object_issue_map.get(get_anim_handle(obj), [])
            item.setText(6, "，".join(issues) if issues else "未检测")
            item.setText(7, "等待")
            self.set_item_checkable(item, True)
            self.object_tree.addTopLevelItem(item)
        for i in range(8): self.object_tree.resizeColumnToContents(i)
        self.ignore_object_selection = False
        self.apply_object_filter()
        self.log("对象列表数量：{}".format(len(self.object_cache)))

    def refresh_group_tree(self):
        self.ignore_group_selection = True
        self.group_tree.clear()
        for obj in self.group_cache:
            item = QtWidgets.QTreeWidgetItem()
            item.setText(0, safe_str(getattr(obj, "name", ""), "<无效组>"))
            item.setText(1, get_layer_name(obj) if is_valid_node(obj) else "NoLayer")
            item.setText(2, str(get_group_member_count(obj)))
            item.setText(3, "是" if is_group_open(obj) else "否")
            item.setText(4, "是" if is_frozen(obj) else "否")
            item.setText(5, "是" if is_hidden(obj) else "否")
            item.setText(6, "等待")
            self.set_item_checkable(item, True)
            self.group_tree.addTopLevelItem(item)
        for i in range(7):
            self.group_tree.resizeColumnToContents(i)
        self.ignore_group_selection = False
        self.log("组列表数量：{}".format(len(self.group_cache)))

    def refresh_light_tree(self):
        self.ignore_light_selection=True; self.light_tree.clear()
        for obj in self.light_cache:
            item=QtWidgets.QTreeWidgetItem(); item.setText(0,safe_str(getattr(obj,"name",""),"<无效灯光>")); item.setText(1,get_class_name(obj)); item.setText(2,get_layer_name(obj)); item.setText(3,"是" if is_frozen(obj) else "否"); item.setText(4,"等待"); self.set_item_checkable(item,True); self.light_tree.addTopLevelItem(item)
        for i in range(5): self.light_tree.resizeColumnToContents(i)
        self.ignore_light_selection=False; self.log("灯光列表数量：{}".format(len(self.light_cache)))

    def refresh_camera_tree(self):
        self.ignore_camera_selection=True; self.camera_tree.clear()
        for obj in self.camera_cache:
            item=QtWidgets.QTreeWidgetItem(); item.setText(0,safe_str(getattr(obj,"name",""),"<无效相机>")); item.setText(1,get_class_name(obj)); item.setText(2,get_layer_name(obj)); item.setText(3,"是" if is_frozen(obj) else "否"); item.setText(4,"等待"); self.set_item_checkable(item,True); self.camera_tree.addTopLevelItem(item)
        for i in range(5): self.camera_tree.resizeColumnToContents(i)
        self.ignore_camera_selection=False; self.log("相机列表数量：{}".format(len(self.camera_cache)))

    def refresh_material_tree(self):
        self.ignore_material_selection=True; self.material_tree.clear()
        for entry in self.material_cache:
            mat=entry.get("mat"); parent=entry.get("parent"); role=entry.get("role","MAT"); slot=entry.get("slot",0); mid=entry.get("mat_id",0)
            item=QtWidgets.QTreeWidgetItem(); item.setText(0,get_material_name(mat)); item.setText(1,get_class_name(mat)); item.setText(2,role); item.setText(3,get_material_name(parent) if is_valid_material(parent) else "-"); item.setText(4,"ID:{} / Slot:{}".format(mid,slot) if role=="SUB" else "-"); item.setText(5,"等待"); self.set_item_checkable(item,True); self.material_tree.addTopLevelItem(item)
        for i in range(6): self.material_tree.resizeColumnToContents(i)
        self.ignore_material_selection=False; self.log("材质列表数量：{}".format(len(self.material_cache)))

    def build_pbrset_tab(self):
        main = QtWidgets.QVBoxLayout(self.pbrset_tab)
        main.setContentsMargins(12, 12, 12, 12)

        box = self.card("PBR 贴图套装 · 一键识别通道并创建材质")
        lay = QtWidgets.QGridLayout(box)

        self.pbrset_folder = QtWidgets.QLineEdit(current_scene_folder())
        self.btn_choose_pbrset_folder = QtWidgets.QPushButton("📂 选择贴图文件夹")
        self.btn_choose_pbrset_folder.clicked.connect(self.choose_pbrset_folder)

        self.chk_pbrset_recursive = QtWidgets.QCheckBox("扫描子文件夹")
        self.chk_pbrset_recursive.setChecked(True)
        self.chk_pbrset_group_by_folder = QtWidgets.QCheckBox("按文件夹合并为一个材质")
        self.chk_pbrset_group_by_folder.setChecked(True)
        self.chk_pbrset_group_by_folder.setToolTip("推荐开启：一个没有子目录的PBR贴图文件夹默认只生成一个材质。")

        self.pbrset_target_combo = QtWidgets.QComboBox()
        self.pbrset_target_combo.addItems(["PBR Material Metal/Rough", "V-Ray Material", "Corona Physical Material", "Physical Material", "OpenPBR"])
        self.pbrset_target_combo.setCurrentIndex(0)
        self.pbrset_prefix = QtWidgets.QLineEdit("M_PBR")

        self.pbrset_normal_combo = QtWidgets.QComboBox()
        self.pbrset_normal_combo.addItems(["DirectX / DX（UE常用）", "OpenGL / GL", "自动"])
        self.pbrset_normal_combo.setToolTip("同一套材质里同时有 NormalDX 和 NormalGL 时，按这里选择默认使用哪一张。")

        self.pbrset_gloss_combo = QtWidgets.QComboBox()
        self.pbrset_gloss_combo.addItems(["反相生成Roughness副本", "直接当Roughness使用", "跳过Glossiness"])
        self.pbrset_gloss_combo.setToolTip("只有找不到Roughness但找到Glossiness时生效。反相需要Pillow或ImageMagick。")

        # V33：创建材质永远不自动赋给对象。保留隐藏变量只是为了兼容旧配置文件。
        self.chk_pbrset_assign_selected = QtWidgets.QCheckBox("")
        self.chk_pbrset_assign_selected.setChecked(False)
        self.chk_pbrset_assign_selected.setEnabled(False)
        self.chk_pbrset_assign_selected.setVisible(False)

        self.btn_scan_pbrset = QtWidgets.QPushButton("🔍 扫描PBR套装")
        self.btn_scan_pbrset.setObjectName("primaryButton")
        self.btn_scan_pbrset.clicked.connect(self.scan_pbrset_folder)

        self.btn_create_pbrset_all = QtWidgets.QPushButton("📤 创建基本完整项")
        self.btn_create_pbrset_checked = QtWidgets.QPushButton("☑ 创建打勾完整项")
        self.btn_create_pbrset_selected = QtWidgets.QPushButton("↗ 创建高亮完整项")
        self.btn_create_pbrset_checked.setObjectName("primaryButton")
        self.btn_create_pbrset_all.clicked.connect(lambda: self.create_pbrset_materials_by_scope("all"))
        self.btn_create_pbrset_checked.clicked.connect(lambda: self.create_pbrset_materials_by_scope("checked"))
        self.btn_create_pbrset_selected.clicked.connect(lambda: self.create_pbrset_materials_by_scope("selected"))

        self.btn_assign_created_pbrset = QtWidgets.QPushButton("🔗 当前高亮材质赋给选中物体")
        self.btn_assign_created_pbrset.setToolTip("只使用当前高亮选择的一个材质套装。若还没创建，会先创建再赋给选中模型。")
        self.btn_assign_created_pbrset.clicked.connect(self.assign_created_pbrset_to_selection)

        self.btn_pbrset_to_medit = QtWidgets.QPushButton("🖼 导入高亮到材质编辑器")
        self.btn_pbrset_to_medit.setToolTip("把当前高亮选择的PBR材质导入材质编辑器。若还没创建，会先创建。")
        self.btn_pbrset_to_medit.clicked.connect(lambda: self.import_pbrsets_to_medit("selected"))

        self.btn_pbrset_checked_to_medit = QtWidgets.QPushButton("☑ 导入打勾到材质编辑器")
        self.btn_pbrset_checked_to_medit.setToolTip("把打勾的PBR材质批量导入材质编辑器；超过槽位数量会分页。")
        self.btn_pbrset_checked_to_medit.clicked.connect(lambda: self.import_pbrsets_to_medit("checked"))

        self.btn_pbrset_all_to_medit = QtWidgets.QPushButton("🧱 导入全部到材质编辑器")
        self.btn_pbrset_all_to_medit.setToolTip("把列表全部基本完整PBR材质导入材质编辑器；超过槽位数量会分页。")
        self.btn_pbrset_all_to_medit.clicked.connect(lambda: self.import_pbrsets_to_medit("all"))

        self.btn_pbrset_next_medit_page = QtWidgets.QPushButton("➡ 下一页材质球")
        self.btn_pbrset_next_medit_page.setToolTip("当要导入的材质超过材质编辑器槽位数量时，点击导入下一页。")
        self.btn_pbrset_next_medit_page.clicked.connect(self.import_next_pbrset_medit_page)

        self.btn_manual_map_pbrset = QtWidgets.QPushButton("🧩 手动贴图映射")
        self.btn_manual_map_pbrset.setToolTip("打开当前高亮PBR套装的贴图映射窗口，手动指定每张贴图的通道。")
        self.btn_manual_map_pbrset.clicked.connect(self.manual_map_current_pbrset_textures)

        self.btn_manual_slot_pbrset = QtWidgets.QPushButton("🎚 手动材质槽")
        self.btn_manual_slot_pbrset.setToolTip("如果材质已经创建但贴图没全接上，可手动选择当前材质真实贴图槽。")
        self.btn_manual_slot_pbrset.clicked.connect(self.manual_config_current_pbrset_slots)

        self.btn_pbr_connection_report = QtWidgets.QPushButton("📋 连接报告")
        self.btn_pbr_connection_report.setToolTip("查看当前高亮PBR套装最近一次创建材质的贴图连接报告。")
        self.btn_pbr_connection_report.clicked.connect(self.show_current_pbr_connection_report)

        self.btn_pbr_connection_table = QtWidgets.QPushButton("🧾 连接对应表")
        self.btn_pbr_connection_table.setToolTip("查看并修改 通道-贴图-材质槽 的一一对应关系。")
        self.btn_pbr_connection_table.clicked.connect(self.show_current_pbr_connection_table)

        self.btn_clear_pbrset_unknown = QtWidgets.QPushButton("🧹 清除未识别")
        self.btn_clear_pbrset_unknown.setToolTip("只清除列表里的未识别贴图记录，不删除磁盘文件。")
        self.btn_clear_pbrset_unknown.clicked.connect(self.clear_pbrset_unknown_records)

        self.btn_save_pbr_library = QtWidgets.QPushButton("💾 保存PBR材质库")
        self.btn_save_pbr_library.setToolTip("保存当前PBR套装识别结果为插件材质库 JSON，方便以后加载。")
        self.btn_save_pbr_library.clicked.connect(self.save_pbr_material_library)

        self.btn_load_pbr_library = QtWidgets.QPushButton("📂 加载PBR材质库")
        self.btn_load_pbr_library.setToolTip("加载之前保存的PBR材质库 JSON。")
        self.btn_load_pbr_library.clicked.connect(self.load_pbr_material_library)

        lay.addWidget(QtWidgets.QLabel("贴图文件夹"), 0, 0)
        lay.addWidget(self.pbrset_folder, 0, 1, 1, 5)
        lay.addWidget(self.btn_choose_pbrset_folder, 0, 6)
        lay.addWidget(self.chk_pbrset_recursive, 1, 0)
        lay.addWidget(self.chk_pbrset_group_by_folder, 1, 1, 1, 2)
        lay.addWidget(QtWidgets.QLabel("目标材质"), 1, 3)
        lay.addWidget(self.pbrset_target_combo, 1, 4, 1, 2)
        lay.addWidget(self.btn_scan_pbrset, 1, 6)

        lay.addWidget(QtWidgets.QLabel("材质前缀"), 2, 0)
        lay.addWidget(self.pbrset_prefix, 2, 1)
        lay.addWidget(QtWidgets.QLabel("法线偏好"), 2, 2)
        lay.addWidget(self.pbrset_normal_combo, 2, 3, 1, 2)
        lay.addWidget(QtWidgets.QLabel("Glossiness处理"), 2, 5)
        lay.addWidget(self.pbrset_gloss_combo, 2, 6)

        lay.addWidget(self.btn_create_pbrset_all, 3, 0, 1, 2)
        lay.addWidget(self.btn_create_pbrset_checked, 3, 2)
        lay.addWidget(self.btn_create_pbrset_selected, 3, 3)
        lay.addWidget(self.btn_assign_created_pbrset, 3, 4, 1, 3)

        lay.addWidget(self.btn_manual_map_pbrset, 4, 0)
        lay.addWidget(self.btn_manual_slot_pbrset, 4, 1)
        lay.addWidget(self.btn_pbr_connection_report, 4, 2)
        lay.addWidget(self.btn_pbr_connection_table, 4, 3)
        lay.addWidget(self.btn_clear_pbrset_unknown, 4, 4)
        lay.addWidget(self.btn_save_pbr_library, 4, 5)
        lay.addWidget(self.btn_load_pbr_library, 4, 6)
        lay.addWidget(self.btn_pbrset_to_medit, 5, 5, 1, 2)

        lay.addWidget(self.btn_pbrset_checked_to_medit, 5, 0, 1, 2)
        lay.addWidget(self.btn_pbrset_all_to_medit, 5, 2, 1, 2)
        lay.addWidget(self.btn_pbrset_next_medit_page, 5, 4)

        hint = QtWidgets.QLabel("说明：V43 会生成连接对应表。AO 不再直接接漫反射；优先独立 AO 槽，否则保留 AO 独立贴图，失败时必须手动处理或确认不用。")
        hint.setObjectName("hintLabel")
        try: hint.setWordWrap(True)
        except Exception: pass
        lay.addWidget(hint, 6, 0, 1, 7)

        main.addWidget(box)

        self.pbrset_tree = QtWidgets.QTreeWidget()
        try:
            self.pbrset_tree.setIconSize(QtCore.QSize(72, 72))
        except Exception:
            pass
        self.pbrset_tree.setColumnCount(7)
        self.pbrset_tree.setHeaderLabels(["材质套装", "识别通道", "问题/提醒", "创建材质名", "创建材质类型", "文件夹", "状态"])
        self.prepare_tree(self.pbrset_tree)
        try:
            self.pbrset_tree.setContextMenuPolicy(qt_enum("CustomContextMenu", ("ContextMenuPolicy",)))
            self.pbrset_tree.customContextMenuRequested.connect(self.on_pbrset_tree_context_menu)
            self.pbrset_tree.itemDoubleClicked.connect(lambda item, col: self.assign_created_pbrset_to_selection())
            self.pbrset_tree.setDragEnabled(True)
        except Exception:
            pass
        main.addWidget(self.make_check_bar("PBR贴图套装列表", "pbrset_tree"))
        main.addWidget(self.pbrset_tree, 1)

    # ---------- PBR 下载库 ----------
    def load_pbr_download_sites(self):
        path = pbr_download_sites_path()
        sites = []
        try:
            if os.path.exists(path):
                with open(path, "r", encoding="utf-8") as f:
                    data = json.load(f)
                if isinstance(data, list):
                    sites = data
        except Exception:
            sites = []

        if not sites:
            sites = pbr_download_default_sites()

        result = []
        seen = set()
        for s in sites:
            url = safe_str(s.get("url", ""), "")
            name = safe_str(s.get("name", ""), "")
            key = (name + "|" + url).lower()
            if url and key not in seen:
                seen.add(key)
                result.append(dict(
                    name=name or url,
                    license=safe_str(s.get("license", ""), "Free"),
                    url=url,
                    note=safe_str(s.get("note", ""), "")
                ))
        return result

    def save_pbr_download_sites(self):
        try:
            sites = []
            for i in range(self.pbr_site_tree.topLevelItemCount()):
                item = self.pbr_site_tree.topLevelItem(i)
                sites.append(dict(name=item.text(0), license=item.text(1), note=item.text(2), url=item.text(3)))
            path = pbr_download_sites_path()
            with open(path, "w", encoding="utf-8") as f:
                json.dump(sites, f, ensure_ascii=False, indent=2)
            self.log("PBR下载站点已保存：{}".format(path))
        except Exception:
            self.log(status_text_for_exception("保存PBR下载站点失败"))

    def refresh_pbr_site_tree(self):
        self.pbr_site_tree.clear()
        for s in self.load_pbr_download_sites():
            item = QtWidgets.QTreeWidgetItem()
            item.setText(0, s.get("name", ""))
            item.setText(1, s.get("license", ""))
            item.setText(2, s.get("note", ""))
            item.setText(3, s.get("url", ""))
            self.pbr_site_tree.addTopLevelItem(item)
        for i in range(4):
            self.pbr_site_tree.resizeColumnToContents(i)

        # V64：默认选中第一个站点，避免用户点"内置打开网站"没有明显动作。
        try:
            if self.pbr_site_tree.topLevelItemCount() > 0 and not self.pbr_site_tree.selectedItems():
                first = self.pbr_site_tree.topLevelItem(0)
                self.pbr_site_tree.setCurrentItem(first)
                first.setSelected(True)
                if hasattr(self, "pbr_browser_url"):
                    self.pbr_browser_url.setText(first.text(3))
        except Exception:
            pass

    def choose_pbr_library_folder(self):
        try:
            d = QtWidgets.QFileDialog.getExistingDirectory(self, "选择PBR材质库文件夹", self.pbr_library_dir.text().strip() or load_saved_pbr_library_dir())
            if d:
                self.pbr_library_dir.setText(d)
                save_pbr_library_dir_state(d)
                self.log("PBR材质库目录：{}（已记录，下次启动/升级仍会保留）".format(d))
        except Exception:
            self.log(status_text_for_exception("选择PBR材质库目录失败"))

    def open_pbr_library_folder(self):
        d = self.pbr_library_dir.text().strip()
        if not d:
            self.log("请先设置PBR材质库目录")
            return
        try:
            os.makedirs(d, exist_ok=True)
        except Exception:
            pass
        if open_folder_in_os(d):
            self.log("已打开PBR材质库目录：{}".format(d))
        else:
            self.log("打开PBR材质库目录失败：{}".format(d))

    def set_pbrset_folder_from_library(self):
        d = self.pbr_library_dir.text().strip()
        if not d:
            self.log("请先设置PBR材质库目录")
            return
        self.pbrset_folder.setText(d)
        self.log("已把PBR套装扫描目录设为材质库：{}".format(d))
        try:
            self.tabs.setCurrentWidget(self.pbrset_tab)
        except Exception:
            pass

    def add_pbr_site_dialog(self):
        try:
            name, ok = QtWidgets.QInputDialog.getText(self, "添加PBR网站", "网站名称：")
            if not ok or not name:
                return
            url, ok = QtWidgets.QInputDialog.getText(self, "添加PBR网站", "网站地址 URL：")
            if not ok or not url:
                return
            lic, ok = QtWidgets.QInputDialog.getText(self, "添加PBR网站", "许可/备注，例如 Free / CC0：", text="Free")
            if not ok:
                lic = "Free"
            note, ok = QtWidgets.QInputDialog.getText(self, "添加PBR网站", "说明：", text="用户自定义PBR资源网站")
            if not ok:
                note = ""

            item = QtWidgets.QTreeWidgetItem()
            item.setText(0, name)
            item.setText(1, lic)
            item.setText(2, note)
            item.setText(3, url)
            self.pbr_site_tree.addTopLevelItem(item)
            self.save_pbr_download_sites()
            self.log("已添加PBR网站：{}".format(name))
        except Exception:
            self.log(status_text_for_exception("添加PBR网站失败"))

    def remove_selected_pbr_sites(self):
        items = self.pbr_site_tree.selectedItems()
        if not items:
            self.log("没有选择要移除的网站")
            return
        for item in items:
            idx = self.pbr_site_tree.indexOfTopLevelItem(item)
            if idx >= 0:
                self.pbr_site_tree.takeTopLevelItem(idx)
        self.save_pbr_download_sites()
        self.log("已移除选择的网站：{} 个".format(len(items)))

    def open_selected_pbr_site_chrome(self):
        item = self.current_or_first_pbr_site_item() if hasattr(self, "current_or_first_pbr_site_item") else None
        if not item:
            self.log("没有可打开的PBR网站，站点列表为空")
            return
        url = item.text(3)
        if not url:
            self.log("该网站没有URL：{}".format(item.text(0)))
            return
        if open_url_in_chrome_or_browser(url):
            self.log("已用外部浏览器打开网站：{}".format(url))
            self.log("提示：复制网页里的下载链接后，若开启\"监听剪贴板\"，插件会自动识别并加入队列。")
        else:
            self.log("外部浏览器打开失败：{}".format(url))

    def check_pbr_clipboard_links(self):
        try:
            if not hasattr(self, "chk_pbr_watch_clipboard") or not self.chk_pbr_watch_clipboard.isChecked():
                return
            clip = QtWidgets.QApplication.clipboard()
            clip_text = safe_str(clip.text(), "")
            if not clip_text or clip_text == getattr(self, "pbr_clipboard_last_text", ""):
                return
            self.pbr_clipboard_last_text = clip_text
            urls = pbr_extract_urls_from_text(clip_text)
            if not urls:
                return
            added = 0
            blocked = 0
            for url in urls:
                if self.add_pbr_download_entry(url, source="剪贴板"):
                    added += 1
                else:
                    blocked += 1
            if added:
                self.log("剪贴板识别到下载链接，已加入队列：{} 条".format(added))
            elif blocked:
                self.log("剪贴板有链接，但没有符合当前勾选类型的下载链接。当前允许：{}".format(self.pbr_selected_type_text()))
        except Exception:
            pass

    def clear_selected_pbr_download_queue(self):
        items = self.pbr_download_tree.selectedItems()
        if not items:
            self.log("下载队列没有高亮选择")
            return
        rows = sorted([self.pbr_download_tree.indexOfTopLevelItem(i) for i in items if self.pbr_download_tree.indexOfTopLevelItem(i) >= 0], reverse=True)
        removed = 0
        for row in rows:
            if 0 <= row < len(self.pbr_download_queue):
                self.pbr_download_queue.pop(row)
                removed += 1
        self.refresh_pbr_download_tree()
        self.log("已从下载队列清除所选：{} 项".format(removed))

    def check_selected_pbr_queue_redundant_folders(self):
        entries = self.selected_pbr_download_entries()
        if not entries:
            entries = list(self.pbr_download_queue)
        if not entries:
            self.log("下载队列为空，无法检查冗余文件夹")
            return
        found = 0
        for e in entries:
            target = e.get("target", "")
            info = pbr_redundant_folder_info(target)
            if info.get("redundant"):
                found += 1
                self.log("可能冗余文件夹：{} -> {}".format(target, info.get("inner", "")))
        if found == 0:
            self.log("未发现明显单层冗余文件夹。")
        else:
            self.log("冗余文件夹检查完成：发现 {} 项。".format(found))

    def open_selected_pbr_site(self):
        item = self.current_or_first_pbr_site_item() if hasattr(self, "current_or_first_pbr_site_item") else None
        if not item:
            self.log("没有可打开的PBR网站，站点列表为空")
            return
        url = item.text(3)
        if not url:
            self.log("该网站没有URL：{}".format(item.text(0)))
            return
        try:
            webbrowser.open(url)
            self.log("已用外部浏览器打开网站：{}".format(url))
        except Exception:
            self.log(status_text_for_exception("打开网站失败"))

    def selected_pbr_download_entries(self):
        entries = []
        for item in self.pbr_download_tree.selectedItems():
            row = self.pbr_download_tree.indexOfTopLevelItem(item)
            if row >= 0 and row < len(self.pbr_download_queue):
                entries.append(self.pbr_download_queue[row])
        return entries

    def refresh_pbr_download_tree(self):
        self.pbr_download_tree.clear()
        for e in self.pbr_download_queue:
            item = QtWidgets.QTreeWidgetItem()
            analysis = e.get("pbr_analysis", {}) or {}
            pbr_text = analysis.get("message", "")
            if analysis:
                pbr_text = ("完整" if analysis.get("complete") else "可识别" if analysis.get("ok") else "未识别") + " / " + pbr_text
            item.setText(0, e.get("status", "等待"))
            item.setText(1, e.get("name", ""))
            item.setText(2, e.get("url", ""))
            item.setText(3, e.get("target", ""))
            item.setText(4, pbr_text)
            self.pbr_download_tree.addTopLevelItem(item)
        for i in range(5):
            self.pbr_download_tree.resizeColumnToContents(i)

    def pbr_start_push_server(self, force_restart=False):
        try:
            port = int(self.pbr_push_port_spin.value()) if hasattr(self, "pbr_push_port_spin") else 19527
        except Exception:
            port = 19527
        ok_port, msg_port = validate_local_service_port(port)
        if not ok_port:
            self.log("本地桥接服务：{}".format(msg_port))
            try:
                self.lbl_pbr_push_server_status.setText("本地桥接服务：{}".format(msg_port))
            except Exception:
                pass
            return
        free_ok, free_msg = is_local_port_available(port)
        if force_restart and _pbr_push_server_instance is not None:
            try:
                stop_pbr_push_server()
            except Exception:
                pass
            free_ok, free_msg = is_local_port_available(port)
        if not free_ok and _pbr_push_server_instance is None:
            msg = "端口 {} 已被占用，请改成别的端口，并同步修改浏览器插件里的端口。{}".format(port, " 原因：" + free_msg if free_msg else "")
            self.log("本地桥接服务：{}".format(msg))
            try:
                self.lbl_pbr_push_server_status.setText("本地桥接服务：{}".format(msg))
            except Exception:
                pass
            return
        try:
            self.save_config_silent()
        except Exception:
            pass

        def _cb(url_list, auto_start_download=False):
            try:
                self.enqueue_pbr_push_urls(url_list, auto_start_download=auto_start_download)
            except Exception:
                pass

        ok, msg = start_pbr_push_server(port=port, callback=_cb)
        self.log("本地桥接服务：{}".format(msg))
        try:
            self.lbl_pbr_push_server_status.setText("本地桥接服务：{}".format(msg))
            self.btn_pbr_push_server_start.setEnabled(not ok)
            self.btn_pbr_push_server_stop.setEnabled(ok)
        except Exception:
            pass

    def pbr_stop_push_server(self):
        ok, msg = stop_pbr_push_server()
        self.log("本地桥接服务：{}".format(msg))
        try:
            self.lbl_pbr_push_server_status.setText("本地桥接服务：{}".format(msg))
            self.btn_pbr_push_server_start.setEnabled(True)
            self.btn_pbr_push_server_stop.setEnabled(False)
        except Exception:
            pass

    def pbr_check_push_server_port(self):
        try:
            port = int(self.pbr_push_port_spin.value()) if hasattr(self, "pbr_push_port_spin") else 19527
        except Exception:
            port = 19527
        try:
            self.save_config_silent()
        except Exception:
            pass
        ok, msg = validate_local_service_port(port)
        if not ok:
            text = "端口检查失败：{}".format(msg)
            self.log(text)
            try:
                self.lbl_pbr_push_server_status.setText("本地桥接服务：{}".format(text))
            except Exception:
                pass
            return
        free_ok, free_msg = is_local_port_available(port)
        if _pbr_push_server_instance is not None and int(_pbr_push_server_port or 0) == int(port):
            text = "端口 {} 当前正被本插件使用。浏览器插件端口也要改成 {}。".format(port, port)
        elif free_ok:
            text = "端口 {} 可用。修改浏览器插件时请填同一个端口。".format(port)
        else:
            text = "端口 {} 已被其他程序占用，请换一个端口，并同步修改浏览器插件。{}".format(port, " 原因：" + free_msg if free_msg else "")
        self.log(text)
        try:
            self.lbl_pbr_push_server_status.setText("本地桥接服务：{}".format(text))
        except Exception:
            pass

    def pbr_chrome_extension_dir(self):
        try:
            return os.path.abspath(os.path.join(os.path.dirname(__file__), "chrome_extension"))
        except Exception:
            return ""

    def pbr_open_chrome_extension_dir(self):
        path = self.pbr_chrome_extension_dir()
        if not path or not os.path.isdir(path):
            msg = "没有找到 Chrome 扩展目录：{}".format(path or "(空)")
            self.log(msg)
            try:
                QtWidgets.QMessageBox.warning(self, "Chrome扩展目录", msg)
            except Exception:
                pass
            return
        if open_folder_in_os(path):
            self.log("已打开 Chrome 扩展目录：{}".format(path))
        else:
            self.log("打开 Chrome 扩展目录失败：{}".format(path))

    def pbr_open_chrome_extensions_page(self):
        url = "chrome://extensions"
        if not open_url_in_chrome_or_browser(url):
            try:
                webbrowser.open(url)
            except Exception:
                pass
        path = self.pbr_chrome_extension_dir()
        msg = (
            "Chrome扩展安装/更新步骤：\n"
            "1. 在 Chrome 扩展页打开“开发者模式”。\n"
            "2. 点“加载已解压的扩展程序”。\n"
            "3. 选择目录：\n{}\n\n"
            "更新本插件后，请在扩展页点此扩展的“重新加载”。"
        ).format(path)
        self.log("已打开 Chrome 扩展页。扩展目录：{}".format(path))
        try:
            QtWidgets.QMessageBox.information(self, "Chrome扩展安装/更新", msg)
        except Exception:
            self.log(msg)

    def enqueue_pbr_push_urls(self, url_list, auto_start_download=False):
        if not url_list:
            return
        try:
            if self._pbr_push_pending_lock is None:
                import threading as _threading
                self._pbr_push_pending_lock = _threading.Lock()
            lock = self._pbr_push_pending_lock
        except Exception:
            lock = None
        normalized = []
        for url in url_list:
            u = safe_str(url, "").strip()
            if u:
                normalized.append(dict(url=u, auto_start_download=bool(auto_start_download)))
        if not normalized:
            return
        if lock is None:
            self._pbr_push_pending_urls.extend(normalized)
            return
        try:
            with lock:
                self._pbr_push_pending_urls.extend(normalized)
        except Exception:
            pass

    def process_pbr_push_queue(self):
        pending = []
        try:
            lock = self._pbr_push_pending_lock
        except Exception:
            lock = None
        if lock is None:
            if not getattr(self, "_pbr_push_pending_urls", None):
                return
            pending = list(self._pbr_push_pending_urls)
            self._pbr_push_pending_urls = []
        else:
            try:
                with lock:
                    if not self._pbr_push_pending_urls:
                        return
                    pending = list(self._pbr_push_pending_urls)
                    self._pbr_push_pending_urls = []
            except Exception:
                return
        if pending:
            self._pbr_push_urls_from_chrome(pending)

    def _pbr_push_urls_from_chrome(self, url_list):
        if not url_list:
            return
        added = 0
        auto_entries = []
        for item in url_list:
            if isinstance(item, dict):
                url = safe_str(item.get("url", ""), "").strip()
                auto_now = bool(item.get("auto_start_download", False))
            else:
                url = safe_str(item, "").strip()
                auto_now = False
            if url and self.add_pbr_download_entry(url, source="Chrome扩展"):
                added += 1
                if auto_now and self.pbr_download_queue:
                    try:
                        auto_entries.append(self.pbr_download_queue[-1])
                    except Exception:
                        pass
        if added:
            self.log("Chrome扩展推送了 {} 条链接，已加入下载队列。".format(added))
            try:
                self.pbr_download_pages.setCurrentIndex(1)  # 切到下载队列标签
            except Exception:
                pass
        for entry in auto_entries:
            try:
                entry["status"] = "等待立即下载"
                self.download_one_pbr_entry(entry)
            except Exception:
                pass

    def add_pbr_download_entry(self, url, name="", filename="", source="手动"):
        url = safe_str(url, "").strip()
        if not url:
            self.log("下载链接为空，不能加入队列")
            return False

        if not self.pbr_url_allowed_by_selected_types(url, filename=filename):
            ext = pbr_detect_extension(url, filename=filename) or "未知类型"
            self.log("已拦截入队：{} 不在允许类型内。当前允许：{}".format(ext, self.pbr_selected_type_text()))
            return False

        lib = self.pbr_library_dir.text().strip() or pbr_default_library_dir()

        download_filename = filename or pbr_download_filename_from_url(url)
        auto_name = pbr_material_name_from_url_or_file(url, download_filename)
        if not name:
            name = auto_name
        else:
            name = clean_name_part(name, auto_name or "PBR_Material")

        target_dir = os.path.join(lib, clean_name_part(name, auto_name or "PBR_Material"))

        # 避免同一URL重复入队
        for e in self.pbr_download_queue:
            if safe_str(e.get("url", "")).strip().lower() == url.lower():
                self.log("下载队列中已存在：{}".format(url))
                return False

        self.pbr_download_queue.append(dict(
            status="等待",
            name=name,
            url=url,
            target=target_dir,
            filename=download_filename,
            source=source
        ))
        self.refresh_pbr_download_tree()
        self.log("已加入PBR下载队列：{} / {}".format(name, source))
        return True

    def add_local_pbr_file_to_queue(self, file_path, name="", source="本地拖入"):
        file_path = safe_abs_texture_path(file_path)
        if not pbr_is_supported_local_asset_file(file_path):
            self.log("已拦截拖入文件：不支持的类型 {}".format(file_path))
            return False
        filename = os.path.basename(file_path)
        if not name:
            name = pbr_material_name_from_filename(filename)
        lib = self.pbr_library_dir.text().strip() or pbr_default_library_dir()
        target_dir = os.path.join(lib, clean_name_part(name, "PBR_Material"))

        for e in self.pbr_download_queue:
            existing_local = safe_abs_texture_path(e.get("local_file", ""))
            if existing_local and existing_local.lower() == file_path.lower():
                self.log("下载队列中已存在本地文件：{}".format(file_path))
                return False

        self.pbr_download_queue.append(dict(
            status="等待处理",
            name=name,
            url="file://" + file_path.replace("\\", "/"),
            target=target_dir,
            filename=filename,
            source=source,
            local_file=file_path,
            downloaded_file=file_path
        ))
        self.refresh_pbr_download_tree()
        self.log("已把本地文件加入PBR下载队列：{} / {}".format(filename, source))
        return True

    def pbr_download_tree_accepts_mime(self, mime):
        try:
            if mime is None or not mime.hasUrls():
                return False
            for qurl in mime.urls():
                try:
                    local_path = qurl.toLocalFile()
                except Exception:
                    local_path = ""
                if local_path and pbr_is_supported_local_asset_file(local_path):
                    return True
        except Exception:
            pass
        return False

    def pbr_download_tree_drag_enter_event(self, event):
        try:
            if self.pbr_download_tree_accepts_mime(event.mimeData()):
                event.acceptProposedAction()
            else:
                event.ignore()
        except Exception:
            event.ignore()

    def pbr_download_tree_drag_move_event(self, event):
        try:
            if self.pbr_download_tree_accepts_mime(event.mimeData()):
                event.acceptProposedAction()
            else:
                event.ignore()
        except Exception:
            event.ignore()

    def pbr_download_tree_drop_event(self, event):
        added = 0
        try:
            mime = event.mimeData()
            if not self.pbr_download_tree_accepts_mime(mime):
                event.ignore()
                return
            for qurl in mime.urls():
                try:
                    local_path = qurl.toLocalFile()
                except Exception:
                    local_path = ""
                if local_path and self.add_local_pbr_file_to_queue(local_path, source="本地拖入"):
                    added += 1
            event.acceptProposedAction()
            if added:
                self.log("本地文件拖入完成：{} 个".format(added))
                try:
                    self.pbr_download_pages.setCurrentIndex(1)
                except Exception:
                    pass
            else:
                self.log("拖入的文件没有成功加入队列")
        except Exception:
            try:
                event.ignore()
            except Exception:
                pass
            self.log(status_text_for_exception("处理本地拖入文件失败"))

    def import_local_pbr_files(self):
        try:
            start_dir = self.pbr_library_dir.text().strip() or user_documents_dir()
        except Exception:
            start_dir = user_documents_dir()
        try:
            files, _ = QtWidgets.QFileDialog.getOpenFileNames(
                self,
                "导入本地压缩包 / 贴图文件",
                start_dir,
                "PBR Files (*.zip *.rar *.7z *.jpg *.jpeg *.png *.tif *.tiff *.tga *.bmp *.webp *.exr *.hdr *.tx *.pdf *.sbsar);;All Files (*.*)"
            )
        except Exception:
            self.log(status_text_for_exception("打开本地文件选择框失败"))
            return
        if not files:
            return
        added = 0
        for path in files:
            if self.add_local_pbr_file_to_queue(path, source="本地导入"):
                added += 1
        if added:
            self.log("已导入本地文件到PBR下载队列：{} 个".format(added))
            try:
                self.pbr_download_pages.setCurrentIndex(1)
            except Exception:
                pass
        else:
            self.log("没有成功导入本地文件")

    def add_pbr_download_url_to_queue(self):
        url = self.pbr_download_url.text().strip()
        if not url:
            self.log("请先粘贴PBR下载直链")
            return
        name = self.pbr_download_name.text().strip()
        self.add_pbr_download_entry(url, name=name, source="手动直链")

    def current_pbr_download_row(self):
        try:
            item = self.pbr_download_tree.currentItem()
            if item:
                row = self.pbr_download_tree.indexOfTopLevelItem(item)
                if 0 <= row < len(self.pbr_download_queue):
                    return row
            items = self.pbr_download_tree.selectedItems()
            if items:
                row = self.pbr_download_tree.indexOfTopLevelItem(items[0])
                if 0 <= row < len(self.pbr_download_queue):
                    return row
        except Exception:
            pass
        return -1

    def highlight_pbr_download_entry(self, entry):
        try:
            if entry not in self.pbr_download_queue:
                return
            row = self.pbr_download_queue.index(entry)
            self.refresh_pbr_download_tree()
            item = self.pbr_download_tree.topLevelItem(row)
            if item:
                self.pbr_download_tree.setCurrentItem(item)
                item.setSelected(True)
                self.pbr_download_tree.scrollToItem(item)
            if hasattr(self, "pbr_download_pages"):
                self.pbr_download_pages.setCurrentIndex(1)
        except Exception:
            pass

    def rename_selected_pbr_download_entry(self):
        row = self.current_pbr_download_row()
        if row < 0:
            self.log("下载队列没有选择项")
            return
        entry = self.pbr_download_queue[row]
        old_name = safe_str(entry.get("name", ""), "")
        new_name, ok = QtWidgets.QInputDialog.getText(self, "修改材质名", "材质名：", text=old_name)
        if not ok or not new_name:
            return
        new_name = clean_name_part(new_name, old_name or "PBR_Material")
        lib = self.pbr_library_dir.text().strip() or pbr_default_library_dir()
        old_target = safe_str(entry.get("target", ""), "")
        new_target = os.path.join(lib, new_name)

        # 如果还没下载，只改队列目标；如果已下载且目录存在，尝试重命名/迁移目录。
        try:
            if old_target and os.path.isdir(old_target) and old_target != new_target:
                if os.path.exists(new_target):
                    new_target = ensure_unique_folder_path(new_target)
                try:
                    os.rename(old_target, new_target)
                except Exception:
                    pbr_move_folder_contents(old_target, new_target)
        except Exception:
            pass

        entry["name"] = new_name
        entry["target"] = new_target
        self.refresh_pbr_download_tree()
        self.log("已修改下载任务材质名：{} -> {}".format(old_name, new_name))

    def copy_failed_pbr_download_links(self):
        links = []
        entries = self.selected_pbr_download_entries()
        if not entries:
            entries = [e for e in self.pbr_download_queue if pbr_entry_is_failed(e)]
        for e in entries:
            if pbr_entry_is_failed(e):
                links.append(safe_str(e.get("url", ""), ""))
        links = [u for u in links if u]
        if not links:
            self.log("没有可复制的失败链接")
            return
        try:
            QtWidgets.QApplication.clipboard().setText("\\n".join(links))
            self.log("已复制失败链接到剪贴板：{} 条".format(len(links)))
        except Exception:
            self.log(status_text_for_exception("复制失败链接失败"))

    def retry_selected_pbr_downloads(self):
        entries = self.selected_pbr_download_entries()
        if not entries:
            entries = [e for e in self.pbr_download_queue if pbr_entry_is_failed(e)]
        if not entries:
            self.log("没有选择要重试的下载任务，也没有失败任务")
            return
        ok = 0
        for e in entries:
            e["status"] = "等待重试"
            if self.download_one_pbr_entry(e):
                ok += 1
        self.log("重试完成：成功/跳过 {} 个，共 {} 个".format(ok, len(entries)))

    def open_selected_pbr_download_target(self):
        entries = self.selected_pbr_download_entries()
        if not entries:
            self.log("没有选择下载任务")
            return
        target = safe_str(entries[0].get("target", ""), "")
        if not target:
            self.log("该任务没有目标目录")
            return
        try:
            os.makedirs(target, exist_ok=True)
        except Exception:
            pass
        if open_folder_in_os(target):
            self.log("已打开目标文件夹：{}".format(target))
        else:
            self.log("打开目标文件夹失败：{}".format(target))

    def set_selected_pbr_download_as_pbrset_folder(self):
        entries = self.selected_pbr_download_entries()
        if not entries:
            self.log("没有选择下载任务")
            return
        target = safe_str(entries[0].get("target", ""), "")
        if not target or not os.path.isdir(target):
            self.log("目标目录不存在：{}".format(target))
            return
        try:
            self.pbrset_folder.setText(target)
            self.tabs.setCurrentWidget(self.pbrset_tab)
            self.log("已把当前下载目标设为PBR套装扫描目录：{}".format(target))
        except Exception:
            self.log(status_text_for_exception("设置PBR套装目录失败"))

    def scan_selected_pbr_download_as_pbrset(self):
        entries = self.selected_pbr_download_entries()
        if not entries:
            self.log("没有选择下载任务")
            return
        target = safe_str(entries[0].get("target", ""), "")
        if not target or not os.path.isdir(target):
            self.log("目标目录不存在：{}".format(target))
            return
        try:
            self.pbrset_folder.setText(target)
            self.tabs.setCurrentWidget(self.pbrset_tab)
            self.scan_pbrset_folder()
            self.log("已扫描当前下载材质目录：{}".format(target))
        except Exception:
            self.log(status_text_for_exception("扫描当前下载材质目录失败"))

    def clear_pbr_download_queue(self):
        self.pbr_download_queue = []
        self.refresh_pbr_download_tree()
        self.log("PBR下载队列已清空")

    def download_one_pbr_entry(self, entry):
        url = entry.get("url", "")
        target_dir = entry.get("target", "")
        name = entry.get("name", "PBR_Material")
        if not url or not target_dir:
            entry["status"] = "失败：URL或目录为空"
            return False

        os.makedirs(target_dir, exist_ok=True)
        filename = safe_str(entry.get("filename", ""), "")
        if not filename:
            filename = pbr_download_filename_from_url(url)

        local_file = safe_abs_texture_path(entry.get("local_file", ""))
        if local_file:
            if not os.path.isfile(local_file):
                entry["status"] = "失败：本地文件不存在"
                self.refresh_pbr_download_tree()
                return False
            try:
                out_path = os.path.join(target_dir, filename or os.path.basename(local_file))
                src_abs = os.path.abspath(local_file)
                dst_abs = os.path.abspath(out_path)
                if src_abs.lower() != dst_abs.lower():
                    if os.path.exists(out_path) and self.chk_pbr_download_no_overwrite.isChecked():
                        entry["status"] = "已存在，跳过：{}".format(os.path.basename(out_path))
                    else:
                        if os.path.exists(out_path) and not self.chk_pbr_download_no_overwrite.isChecked():
                            out_path = ensure_unique_path(out_path)
                        shutil.copy2(local_file, out_path)
                        entry["downloaded_file"] = out_path
                        entry["status"] = "完成：已导入本地文件"
                else:
                    entry["downloaded_file"] = local_file
                    out_path = local_file
                    entry["status"] = "完成：使用本地文件"

                if pbr_is_sbsar_file(out_path):
                    entry["pbr_analysis"] = dict(
                        ok=False,
                        complete=False,
                        message="SBSAR 程序化材质源文件；需用 Substance 工具或 3ds Max Substance 插件导出贴图后再做 PBR 检测。"
                    )
                    entry["status"] = "完成：已导入 SBSAR 源文件；不自动解压"
                    self.bar.setValue(0)
                    self.highlight_pbr_download_entry(entry)
                    self.refresh_pbr_download_tree()
                    return True

                if self.chk_pbr_download_extract_zip.isChecked() and out_path.lower().endswith(".zip"):
                    entry["status"] = "解压中 0%"
                    self.refresh_pbr_download_tree()
                    QtWidgets.QApplication.processEvents()
                    with zipfile.ZipFile(out_path, "r") as z:
                        members = z.infolist()
                        total_members = max(1, len(members))
                        for idx, member in enumerate(members, 1):
                            if self.check_operation_cancelled():
                                entry["status"] = "已停止：解压未完成 {}/{}".format(idx - 1, total_members)
                                return False
                            z.extract(member, target_dir)
                            pct = int(idx * 100 / total_members)
                            entry["status"] = "解压中 {}%：{}/{}".format(pct, idx, total_members)
                            self.bar.setValue(max(0, min(100, pct)))
                            self.set_status("PBR解压中：{} {}% ({}/{})".format(name, pct, idx, total_members))
                            if idx == 1 or idx == total_members or idx % 3 == 0:
                                self.refresh_pbr_download_tree()
                            QtWidgets.QApplication.processEvents()

                    notes = []
                    if hasattr(self, "chk_pbr_flatten_redundant_folder") and self.chk_pbr_flatten_redundant_folder.isChecked():
                        ok_flat, msg_flat = pbr_flatten_single_redundant_folder(target_dir)
                        if msg_flat:
                            notes.append(msg_flat)
                    else:
                        info = pbr_redundant_folder_info(target_dir)
                        if info.get("redundant"):
                            notes.append("发现可能冗余文件夹：{}".format(info.get("inner", "")))

                    analysis = pbr_analyze_material_folder(target_dir)
                    entry["pbr_analysis"] = analysis
                    if analysis.get("ok"):
                        notes.append(("PBR可识别" if analysis.get("complete") else "PBR可识别但可能不完整") + "：" + analysis.get("message", ""))
                    else:
                        notes.append("PBR检测：" + analysis.get("message", ""))
                    entry["status"] = "完成：已导入并解压" + ("；" + "；".join(notes) if notes else "")
                else:
                    analysis = pbr_analyze_material_folder(target_dir)
                    entry["pbr_analysis"] = analysis
                    if analysis.get("ok"):
                        entry["status"] += "；" + ("PBR可识别" if analysis.get("complete") else "PBR可识别但可能不完整")
                    else:
                        entry["status"] += "；PBR检测：" + analysis.get("message", "")

                self.bar.setValue(0)
                self.highlight_pbr_download_entry(entry)
                self.refresh_pbr_download_tree()
                return True
            except Exception as e:
                entry["status"] = "失败：处理本地文件出错 {}".format(e)
                self.bar.setValue(0)
                self.refresh_pbr_download_tree()
                return False

        try:
            resolved_url, resolved_filename, resolve_note = pbr_resolve_polyhaven_download_url(url, filename=filename)
            if resolved_url and resolved_url != url:
                entry["url"] = resolved_url
                url = resolved_url
            if resolved_filename:
                filename = resolved_filename
                entry["filename"] = resolved_filename
            if resolve_note:
                self.log("Poly Haven 链接刷新：{}".format(resolve_note))
        except Exception:
            pass

        out_path = os.path.join(target_dir, filename)
        if os.path.exists(out_path) and self.chk_pbr_download_no_overwrite.isChecked():
            entry["status"] = "已存在，跳过：{}".format(filename)
            return True
        if os.path.exists(out_path) and not self.chk_pbr_download_no_overwrite.isChecked():
            out_path = ensure_unique_path(out_path)

        entry["status"] = "下载中"
        self.refresh_pbr_download_tree()
        QtWidgets.QApplication.processEvents()

        last_error = ""
        for attempt in range(1, 3):
            try:
                req = urllib.request.Request(
                    url,
                    headers={
                        "User-Agent": PBR_BROWSER_USER_AGENT,
                        "Accept": "*/*",
                        "Connection": "close",
                    }
                )
                with urllib.request.urlopen(req, timeout=60) as resp:
                    # 如果服务器给了更准确的文件名，优先使用
                    cd_name = pbr_filename_from_content_disposition(resp.headers.get("Content-Disposition", ""))
                    if cd_name and not safe_str(entry.get("filename", ""), ""):
                        old_target_dir = target_dir
                        filename = cd_name

                        # V70：服务器返回真实文件名时，同步材质名和目标目录。
                        real_name = pbr_material_name_from_download_file(filename)
                        if real_name and real_name != name:
                            name = real_name
                            entry["name"] = real_name
                            lib = self.pbr_library_dir.text().strip() or pbr_default_library_dir()
                            target_dir = os.path.join(lib, clean_name_part(real_name, "PBR_Material"))
                            if old_target_dir != target_dir and os.path.isdir(old_target_dir):
                                # 如果旧目录刚创建且为空，删掉；如果已有内容，尽量迁移。
                                try:
                                    if not os.listdir(old_target_dir):
                                        os.rmdir(old_target_dir)
                                    else:
                                        target_dir = ensure_unique_folder_path(target_dir) if os.path.exists(target_dir) else target_dir
                                        pbr_move_folder_contents(old_target_dir, target_dir)
                                except Exception:
                                    pass
                            os.makedirs(target_dir, exist_ok=True)
                            entry["target"] = target_dir
                            self.refresh_pbr_download_tree()

                        entry["filename"] = filename
                        out_path = os.path.join(target_dir, filename)
                        if os.path.exists(out_path) and self.chk_pbr_download_no_overwrite.isChecked():
                            entry["status"] = "已存在，跳过：{}".format(filename)
                            return True
                        if os.path.exists(out_path) and not self.chk_pbr_download_no_overwrite.isChecked():
                            out_path = ensure_unique_path(out_path)

                    total = int(resp.headers.get("Content-Length", "0") or 0)
                    done = 0
                    chunk_size = 1024 * 512
                    with open(out_path, "wb") as f:
                        while True:
                            if self.check_operation_cancelled():
                                entry["status"] = "已停止：下载未完成"
                                return False
                            chunk = resp.read(chunk_size)
                            if not chunk:
                                break
                            f.write(chunk)
                            done += len(chunk)
                            if total > 0:
                                pct = int(done * 100 / total)
                                self.bar.setValue(max(0, min(100, pct)))
                                self.set_status("PBR下载中：{} {}%".format(name, pct))
                            else:
                                self.set_status("PBR下载中：{} 已下载 {:.1f} MB".format(name, done / 1024.0 / 1024.0))
                            QtWidgets.QApplication.processEvents()

                entry["downloaded_file"] = out_path

                if pbr_is_sbsar_file(out_path):
                    entry["pbr_analysis"] = dict(
                        ok=False,
                        complete=False,
                        message="SBSAR 程序化材质源文件；需用 Substance 工具或 3ds Max Substance 插件导出贴图后再做 PBR 检测。"
                    )
                    entry["status"] = "完成：已下载 SBSAR 源文件；不自动解压"
                    self.bar.setValue(0)
                    self.highlight_pbr_download_entry(entry)
                    return True

                if self.chk_pbr_download_extract_zip.isChecked() and out_path.lower().endswith(".zip"):
                    entry["status"] = "解压中 0%"
                    self.refresh_pbr_download_tree()
                    QtWidgets.QApplication.processEvents()
                    try:
                        with zipfile.ZipFile(out_path, "r") as z:
                            members = z.infolist()
                            total_members = max(1, len(members))
                            for idx, member in enumerate(members, 1):
                                if self.check_operation_cancelled():
                                    entry["status"] = "已停止：解压未完成 {}/{}".format(idx - 1, total_members)
                                    return False
                                z.extract(member, target_dir)
                                pct = int(idx * 100 / total_members)
                                entry["status"] = "解压中 {}%：{}/{}".format(pct, idx, total_members)
                                self.bar.setValue(max(0, min(100, pct)))
                                self.set_status("PBR解压中：{} {}% ({}/{})".format(name, pct, idx, total_members))
                                if idx == 1 or idx == total_members or idx % 3 == 0:
                                    self.refresh_pbr_download_tree()
                                QtWidgets.QApplication.processEvents()

                        notes = []
                        if hasattr(self, "chk_pbr_flatten_redundant_folder") and self.chk_pbr_flatten_redundant_folder.isChecked():
                            ok_flat, msg_flat = pbr_flatten_single_redundant_folder(target_dir)
                            if msg_flat:
                                notes.append(msg_flat)
                        else:
                            info = pbr_redundant_folder_info(target_dir)
                            if info.get("redundant"):
                                notes.append("发现可能冗余文件夹：{}".format(info.get("inner", "")))

                        if hasattr(self, "chk_pbr_delete_archive_after_extract") and self.chk_pbr_delete_archive_after_extract.isChecked():
                            try:
                                os.remove(out_path)
                                notes.append("已删除原压缩包")
                            except Exception as de:
                                notes.append("删除压缩包失败：{}".format(de))

                        analysis = pbr_analyze_material_folder(target_dir)
                        entry["pbr_analysis"] = analysis
                        if analysis.get("ok"):
                            notes.append(("PBR可识别" if analysis.get("complete") else "PBR可识别但可能不完整") + "：" + analysis.get("message", ""))
                        else:
                            notes.append("PBR检测：" + analysis.get("message", ""))

                        entry["status"] = "完成：已下载并解压" + ("；" + "；".join(notes) if notes else "")
                    except Exception as e:
                        entry["status"] = "下载完成，解压失败：{}".format(e)
                        return False
                else:
                    entry["status"] = "完成：已下载"
                    info = pbr_redundant_folder_info(target_dir)
                    if info.get("redundant"):
                        entry["status"] += "；可能冗余文件夹"
                    analysis = pbr_analyze_material_folder(target_dir)
                    entry["pbr_analysis"] = analysis
                    if analysis.get("ok"):
                        entry["status"] += "；" + ("PBR可识别" if analysis.get("complete") else "PBR可识别但可能不完整")
                    else:
                        entry["status"] += "；PBR检测：" + analysis.get("message", "")

                self.bar.setValue(0)
                self.highlight_pbr_download_entry(entry)
                return True
            except Exception as e:
                last_error = str(e)
                entry["status"] = "重试中 {}/2：{}".format(attempt, last_error)
                self.refresh_pbr_download_tree()
                QtWidgets.QApplication.processEvents()

        entry["status"] = "失败：{}".format(last_error)
        self.bar.setValue(0)
        self.refresh_pbr_download_tree()
        return False

    def download_selected_pbr_queue(self):
        entries = self.selected_pbr_download_entries()
        if not entries:
            self.log("没有高亮选择下载任务")
            return
        ok = 0
        cancelled = False
        self.begin_operation("PBR下载选择", len(entries), cancellable=True)
        try:
            for i, e in enumerate(entries, 1):
                if self.safe_ui_step(i - 1, len(entries), "准备：{}".format(e.get("name", ""))):
                    cancelled = True
                    break
                if self.download_one_pbr_entry(e):
                    ok += 1
                self.update_operation(i, len(entries), "完成：{}".format(e.get("name", "")))
                if self.check_operation_cancelled():
                    cancelled = True
                    break
        finally:
            self.finish_operation("PBR下载结束：成功/跳过 {} 个，共 {} 个".format(ok, len(entries)), cancelled=cancelled)
        self.log("PBR下载完成：成功/跳过 {} 个，共 {} 个{}".format(ok, len(entries), "（已停止）" if cancelled else ""))

    def download_all_pbr_queue(self):
        if not self.pbr_download_queue:
            self.log("PBR下载队列为空")
            return
        ok = 0
        cancelled = False
        entries = list(self.pbr_download_queue)
        self.begin_operation("PBR下载全部", len(entries), cancellable=True)
        try:
            for i, e in enumerate(entries, 1):
                if self.safe_ui_step(i - 1, len(entries), "准备：{}".format(e.get("name", ""))):
                    cancelled = True
                    break
                if self.download_one_pbr_entry(e):
                    ok += 1
                self.update_operation(i, len(entries), "完成：{}".format(e.get("name", "")))
                if self.check_operation_cancelled():
                    cancelled = True
                    break
        finally:
            self.finish_operation("PBR下载结束：成功/跳过 {} 个，共 {} 个".format(ok, len(entries)), cancelled=cancelled)
        self.log("PBR下载完成：成功/跳过 {} 个，共 {} 个{}".format(ok, len(entries), "（已停止）" if cancelled else ""))

    def open_selected_pbr_site_popup(self):
        item = self.current_or_first_pbr_site_item()
        if not item:
            self.log("没有可打开的PBR网站，站点列表为空")
            try:
                QtWidgets.QMessageBox.information(self, "PBR网站", "站点列表为空，请先添加网站。")
            except Exception:
                pass
            return
        url = item.text(3)
        if not url:
            self.log("该网站没有URL：{}".format(item.text(0)))
            return
        self.open_pbr_browser_popup(url)

    def pbr_selected_download_extensions(self):
        if not hasattr(self, "pbr_download_type_checks"):
            return [".zip", ".jpg", ".png", ".exr", ".hdr"], False
        exts = []
        allow_unknown = False
        for key, chk in self.pbr_download_type_checks.items():
            try:
                if not chk.isChecked():
                    continue
                if key == "unknown":
                    allow_unknown = True
                else:
                    exts.extend(key.split("|"))
            except Exception:
                pass
        return [pbr_normalize_extension(e) for e in exts], allow_unknown

    def pbr_url_allowed_by_selected_types(self, url, filename=""):
        exts, allow_unknown = self.pbr_selected_download_extensions()
        return pbr_url_matches_extensions(url, exts, filename=filename, allow_unknown=allow_unknown)

    def pbr_selected_type_text(self):
        exts, allow_unknown = self.pbr_selected_download_extensions()
        labels = [e.replace(".", "").upper() for e in exts]
        if allow_unknown:
            labels.append("未知直链")
        return " / ".join(labels) if labels else "未选择任何类型"

    def setup_pbr_webengine_view(self, web):
        """
        尽量让 QtWebEngine 以桌面 Chrome 方式加载网页。
        注意：3ds Max 自带 QtWebEngine 版本可能偏旧，所以仍不能保证和外部最新版 Chrome 100%一致。
        """
        if not web:
            return
        try:
            profile = web.page().profile()
            try:
                profile.setHttpUserAgent(PBR_BROWSER_USER_AGENT)
            except Exception:
                pass
            try:
                profile.setHttpAcceptLanguage("zh-CN,zh;q=0.9,en-US;q=0.8,en;q=0.7")
            except Exception:
                pass
        except Exception:
            pass

        try:
            settings = web.settings()
            cls = QtWebEngineWidgets.QWebEngineSettings
            for attr_name in [
                "JavascriptEnabled", "LocalStorageEnabled", "PluginsEnabled",
                "FullScreenSupportEnabled", "WebGLEnabled", "Accelerated2dCanvasEnabled",
                "AutoLoadImages", "JavascriptCanOpenWindows", "JavascriptCanAccessClipboard"
            ]:
                try:
                    settings.setAttribute(getattr(cls, attr_name), True)
                except Exception:
                    pass
            try:
                web.setZoomFactor(1.0)
            except Exception:
                pass
        except Exception:
            pass

    def open_pbr_browser_popup(self, url=None):
        """
        V67：Chrome式弹窗浏览器。
        主界面只保留一个弹窗入口；弹窗里顶部导航栏 + 大网页区域，
        下载链接面板默认隐藏，避免界面像工具面板一样拥挤。
        """
        if not self.pbr_browser_available():
            msg = "当前 3ds Max Python 环境没有 QtWebEngine，无法使用弹窗内置浏览器。\n\n可以继续粘贴下载直链到队列。\n\n技术信息：{}".format(QTWEBENGINE_ERROR or "QtWebEngineWidgets unavailable")
            self.log("弹窗浏览器不可用：{}".format(QTWEBENGINE_ERROR or "QtWebEngineWidgets unavailable"))
            try:
                QtWidgets.QMessageBox.information(self, "弹窗浏览器不可用", msg)
            except Exception:
                pass
            return

        try:
            url = safe_str(url, "").strip()
            if not url:
                item = self.current_or_first_pbr_site_item()
                if item:
                    url = item.text(3)
            if not url:
                self.log("弹窗浏览器URL为空")
                return
            if not re.match(r"^https?://", url, re.I):
                url = "https://" + url

            dlg = QtWidgets.QDialog(self)
            dlg.setWindowTitle("PBR 浏览器 - 室内场景助手 Pro")
            dlg.resize(1420, 920)
            try:
                apply_installed_window_icon(dlg)
            except Exception:
                pass

            main = QtWidgets.QVBoxLayout(dlg)
            main.setContentsMargins(8, 8, 8, 8)
            main.setSpacing(6)

            # Chrome式顶部栏
            chrome = QtWidgets.QFrame()
            chrome.setObjectName("browserChromeBar")
            chrome_lay = QtWidgets.QHBoxLayout(chrome)
            chrome_lay.setContentsMargins(8, 6, 8, 6)
            chrome_lay.setSpacing(6)

            btn_back = QtWidgets.QPushButton("←")
            btn_forward = QtWidgets.QPushButton("→")
            btn_reload = QtWidgets.QPushButton("↻")
            btn_home = QtWidgets.QPushButton("⌂")
            for b in [btn_back, btn_forward, btn_reload, btn_home]:
                try:
                    b.setFixedWidth(42)
                    b.setMinimumWidth(42)
                    b.setToolTip({"←":"后退","→":"前进","↻":"刷新","⌂":"回到当前网站首页"}.get(b.text(), ""))
                except Exception:
                    pass

            url_edit = QtWidgets.QLineEdit(url)
            url_edit.setPlaceholderText("输入网址，或从PBR网站列表打开")
            btn_go = QtWidgets.QPushButton("打开")
            btn_external = QtWidgets.QPushButton("外部")
            try:
                btn_go.setFixedWidth(72)
                btn_external.setFixedWidth(72)
            except Exception:
                pass

            chrome_lay.addWidget(btn_back)
            chrome_lay.addWidget(btn_forward)
            chrome_lay.addWidget(btn_reload)
            chrome_lay.addWidget(btn_home)
            chrome_lay.addWidget(url_edit, 1)
            chrome_lay.addWidget(btn_go)
            chrome_lay.addWidget(btn_external)
            main.addWidget(chrome)

            # 工具条：默认很窄，不挤占网页
            tools = QtWidgets.QFrame()
            tools.setObjectName("browserToolBar")
            tools_lay = QtWidgets.QHBoxLayout(tools)
            tools_lay.setContentsMargins(8, 0, 8, 2)
            tools_lay.setSpacing(8)
            status = QtWidgets.QLabel("准备打开：{}    允许入队类型：{}".format(url, self.pbr_selected_type_text()))
            status.setObjectName("hintLabel")
            status.setWordWrap(False)
            btn_current = QtWidgets.QPushButton("当前URL入队")
            btn_grab = QtWidgets.QPushButton("抓取下载链接")
            btn_show_links = QtWidgets.QPushButton("显示链接面板")
            btn_close = QtWidgets.QPushButton("关闭")
            tools_lay.addWidget(status, 1)
            tools_lay.addWidget(btn_current)
            tools_lay.addWidget(btn_grab)
            tools_lay.addWidget(btn_show_links)
            tools_lay.addWidget(btn_close)
            main.addWidget(tools)

            web = QtWebEngineWidgets.QWebEngineView()
            try:
                page = PBRBrowserPage(web)
                page.directUrlDetected.connect(self.on_pbr_browser_direct_url)
                web.setPage(page)
            except Exception:
                pass
            self.setup_pbr_webengine_view(web)
            try:
                web.setMinimumHeight(720)
                web.setSizePolicy(QtWidgets.QSizePolicy.Expanding, QtWidgets.QSizePolicy.Expanding)
            except Exception:
                pass
            main.addWidget(web, 1)

            # 下载链接抽屉，默认隐藏
            drawer = QtWidgets.QFrame()
            drawer.setObjectName("browserLinkDrawer")
            drawer.setVisible(False)
            drawer_lay = QtWidgets.QGridLayout(drawer)
            drawer_lay.setContentsMargins(8, 8, 8, 8)
            drawer_lay.setHorizontalSpacing(8)
            drawer_lay.setVerticalSpacing(6)

            link_tree = QtWidgets.QTreeWidget()
            link_tree.setColumnCount(3)
            link_tree.setHeaderLabels(["类型", "文字", "链接"])
            self.prepare_tree(link_tree)
            link_tree.setMinimumHeight(190)
            popup_links = []

            btn_add_sel = QtWidgets.QPushButton("选择链接入队")
            btn_add_all = QtWidgets.QPushButton("全部链接入队")
            btn_hide_links = QtWidgets.QPushButton("隐藏面板")

            drawer_lay.addWidget(link_tree, 0, 0, 1, 6)
            drawer_lay.addWidget(btn_add_sel, 1, 3)
            drawer_lay.addWidget(btn_add_all, 1, 4)
            drawer_lay.addWidget(btn_hide_links, 1, 5)
            main.addWidget(drawer)

            def set_links(links):
                link_tree.clear()
                popup_links[:] = []
                for link in links:
                    u = safe_str(link.get("url", ""), "")
                    t = safe_str(link.get("text", ""), "")
                    if not u:
                        continue
                    item = QtWidgets.QTreeWidgetItem()
                    item.setText(0, "下载链接" if pbr_is_probably_download_url(u) else "普通链接")
                    item.setText(1, t[:120])
                    item.setText(2, u)
                    link_tree.addTopLevelItem(item)
                    popup_links.append(dict(url=u, text=t))
                for i in range(3):
                    link_tree.resizeColumnToContents(i)

            def show_drawer(show=True):
                drawer.setVisible(bool(show))
                btn_show_links.setText("隐藏链接面板" if show else "显示链接面板")
                if show:
                    try:
                        dlg.resize(max(dlg.width(), 1420), max(dlg.height(), 980))
                    except Exception:
                        pass

            def toggle_drawer():
                show_drawer(not drawer.isVisible())

            def grab_links():
                js = r"""
                (function(){
                    const out = [];
                    const seen = new Set();
                    function add(url, text){
                        if(!url || seen.has(url)) return;
                        seen.add(url);
                        out.push({url:url, text:(text||'').trim()});
                    }
                    document.querySelectorAll('a[href]').forEach(a => add(a.href, a.innerText || a.title || a.getAttribute('download') || ''));
                    document.querySelectorAll('[data-href],[data-url],[data-download],[href]').forEach(el => {
                        add(el.getAttribute('data-href') || el.getAttribute('data-url') || el.getAttribute('data-download') || el.getAttribute('href'), el.innerText || el.title || '');
                    });
                    return out;
                })();
                """
                def cb(result):
                    try:
                        base_url = url_edit.text().strip()
                        links = []
                        for r in result or []:
                            try:
                                u = safe_str(r.get("url", ""), "")
                                t = safe_str(r.get("text", ""), "")
                            except Exception:
                                continue
                            u = pbr_url_join(base_url, u)
                            if self.pbr_url_allowed_by_selected_types(u):
                                links.append(dict(url=u, text=t))
                        set_links(links)
                        show_drawer(True)
                        status.setText("抓取到可能下载链接：{} 条".format(len(links)))
                        self.log("弹窗浏览器抓取到可能下载链接：{} 条".format(len(links)))
                    except Exception:
                        self.log(status_text_for_exception("弹窗浏览器处理网页链接失败"))
                try:
                    web.page().runJavaScript(js, cb)
                except Exception:
                    self.log(status_text_for_exception("弹窗浏览器抓取网页链接失败"))

            def selected_links():
                result = []
                for item in link_tree.selectedItems():
                    row = link_tree.indexOfTopLevelItem(item)
                    if row >= 0 and row < len(popup_links):
                        result.append(popup_links[row])
                return result

            def add_links(links):
                if not links:
                    self.log("没有可加入的网页链接")
                    return
                added = 0
                for l in links:
                    u = l.get("url", "")
                    t = l.get("text", "")
                    name = pbr_safe_material_folder_name(u, t[:60] if t else "")
                    if self.add_pbr_download_entry(u, name=name, source="弹窗浏览器"):
                        added += 1
                status.setText("已加入下载队列：{} 条".format(added))

            def normalize_url(u):
                u = safe_str(u, "").strip()
                if u and not re.match(r"^https?://", u, re.I):
                    u = "https://" + u
                return u

            def go_to(u=None):
                u = normalize_url(u or url_edit.text())
                if not u:
                    return
                url_edit.setText(u)
                status.setText("正在加载：{}".format(u))
                web.load(QtCore.QUrl(u))

            def add_current():
                self.add_pbr_download_entry(url_edit.text().strip(), source="弹窗当前URL")

            def open_external():
                try:
                    webbrowser.open(url_edit.text().strip())
                except Exception:
                    pass

            btn_back.clicked.connect(web.back)
            btn_forward.clicked.connect(web.forward)
            btn_reload.clicked.connect(web.reload)
            btn_home.clicked.connect(lambda: go_to(url))
            btn_go.clicked.connect(lambda: go_to())
            url_edit.returnPressed.connect(lambda: go_to())
            btn_external.clicked.connect(open_external)
            btn_current.clicked.connect(add_current)
            btn_grab.clicked.connect(grab_links)
            btn_show_links.clicked.connect(toggle_drawer)
            btn_hide_links.clicked.connect(lambda: show_drawer(False))
            btn_add_sel.clicked.connect(lambda: add_links(selected_links()))
            btn_add_all.clicked.connect(lambda: add_links(list(popup_links)))
            btn_close.clicked.connect(dlg.close)

            try:
                web.urlChanged.connect(lambda qurl: url_edit.setText(qurl.toString()))
                web.loadStarted.connect(lambda: status.setText("正在加载网页……"))
                web.loadFinished.connect(lambda ok: status.setText(("加载完成：" if ok else "加载失败：") + url_edit.text().strip()))
                web.page().profile().downloadRequested.connect(self.on_pbr_browser_download_requested)
            except Exception:
                pass

            self.pbr_popup_dialog = dlg
            self.pbr_popup_web = web
            self.pbr_popup_link_tree = link_tree

            dlg.show()
            try:
                dlg.raise_()
                dlg.activateWindow()
            except Exception:
                pass

            go_to(url)
            self.log("Chrome式弹窗浏览器打开：{}".format(url))
        except Exception:
            self.log(status_text_for_exception("打开弹窗浏览器失败"))

    def pbr_browser_available(self):
        return bool(HAS_QTWEBENGINE)

    def load_pbr_browser_url(self, url=None):
        if not self.pbr_browser_available():
            msg = "当前 3ds Max Python 环境没有 QtWebEngine，无法使用内置浏览器。\n\n可以继续用\"外部浏览器\"打开网站，或者粘贴下载直链。\n\n技术信息：{}".format(QTWEBENGINE_ERROR or "QtWebEngineWidgets unavailable")
            self.log("内置浏览器不可用：{}".format(QTWEBENGINE_ERROR or "QtWebEngineWidgets unavailable"))
            try:
                QtWidgets.QMessageBox.information(self, "内置浏览器不可用", msg)
            except Exception:
                pass
            return
        try:
            url = url or self.pbr_browser_url.text().strip()
            if not url:
                self.log("内置浏览器URL为空，请选择上方网站或输入网址")
                return
            if not re.match(r"^https?://", url, re.I):
                url = "https://" + url
            self.pbr_browser_url.setText(url)
            try:
                self.pbr_browser_status.setText("正在加载：{}".format(url))
            except Exception:
                pass
            self.pbr_browser.load(QtCore.QUrl(url))
            self.log("内置浏览器打开：{}".format(url))
        except Exception:
            self.log(status_text_for_exception("内置浏览器打开失败"))

    def current_or_first_pbr_site_item(self):
        try:
            item = self.pbr_site_tree.currentItem()
            if item:
                return item
            items = self.pbr_site_tree.selectedItems()
            if items:
                return items[0]
            if self.pbr_site_tree.topLevelItemCount() > 0:
                item = self.pbr_site_tree.topLevelItem(0)
                self.pbr_site_tree.setCurrentItem(item)
                item.setSelected(True)
                return item
        except Exception:
            pass
        return None

    def open_selected_pbr_site_in_browser(self):
        item = self.current_or_first_pbr_site_item()
        if not item:
            self.log("没有可打开的PBR网站，站点列表为空")
            try:
                QtWidgets.QMessageBox.information(self, "PBR网站", "站点列表为空，请先添加网站。")
            except Exception:
                pass
            return
        url = item.text(3)
        if not url:
            self.log("该网站没有URL：{}".format(item.text(0)))
            return
        try:
            self.pbr_browser_url.setText(url)
        except Exception:
            pass
        if self.pbr_browser_available():
            try:
                self.load_pbr_browser_url(url)
            except Exception:
                self.log(status_text_for_exception("内置浏览器打开网站失败"))
        else:
            self.load_pbr_browser_url(url)  # 这里会弹出"内置浏览器不可用"的说明

    def on_pbr_browser_load_started(self):
        try:
            self.pbr_browser_status.setText("正在加载网页……")
        except Exception:
            pass

    def on_pbr_browser_load_finished(self, ok):
        try:
            url = self.pbr_browser_url.text().strip()
            self.pbr_browser_status.setText(("加载完成：" if ok else "加载失败：") + url)
        except Exception:
            pass
        try:
            self.log(("内置浏览器加载完成：" if ok else "内置浏览器加载失败：") + self.pbr_browser_url.text().strip())
        except Exception:
            pass

    def on_pbr_browser_url_changed(self, qurl):
        try:
            self.pbr_browser_url.setText(qurl.toString())
        except Exception:
            pass

    def on_pbr_browser_download_requested(self, download):
        try:
            url = ""
            filename = ""
            try:
                url = download.url().toString()
            except Exception:
                pass
            for attr in ["suggestedFileName", "downloadFileName"]:
                try:
                    v = getattr(download, attr)
                    filename = v() if callable(v) else v
                    if filename:
                        break
                except Exception:
                    pass

            if not url:
                return
            if not self.pbr_url_allowed_by_selected_types(url, filename=filename):
                self.log("已拦截浏览器下载请求：{} 不在允许类型内。当前允许：{}".format(filename or url, self.pbr_selected_type_text()))
                try:
                    download.cancel()
                except Exception:
                    pass
                return
            name = pbr_material_name_from_filename(filename) if filename else pbr_safe_material_folder_name(url)
            self.add_pbr_download_entry(url, name=name, filename=filename, source="浏览器捕获")
            try:
                download.cancel()
            except Exception:
                pass
            self.log("已从浏览器下载请求加入队列：{}".format(filename or url))
        except Exception:
            self.log(status_text_for_exception("捕获浏览器下载失败"))

    def on_pbr_browser_direct_url(self, url):
        try:
            if not self.add_pbr_download_entry(url, source="浏览器点击"):
                self.log("浏览器点击链接未入队，原因可能是文件类型未勾选：{}".format(url))
        except Exception:
            self.log(status_text_for_exception("添加浏览器链接失败"))

    def pbr_browser_add_current_url(self):
        if not self.pbr_browser_available():
            self.log("当前没有内置浏览器")
            return
        url = self.pbr_browser_url.text().strip()
        if not url:
            self.log("当前浏览器URL为空")
            return
        self.add_pbr_download_entry(url, source="当前页面URL")

    def refresh_pbr_browser_link_tree(self, links):
        self.pbr_browser_link_tree.clear()
        self.pbr_browser_links = []
        for link in links:
            url = safe_str(link.get("url", ""), "")
            text = safe_str(link.get("text", ""), "")
            if not url:
                continue
            item = QtWidgets.QTreeWidgetItem()
            item.setText(0, "下载链接" if pbr_is_probably_download_url(url) else "普通链接")
            item.setText(1, text[:120])
            item.setText(2, url)
            self.pbr_browser_link_tree.addTopLevelItem(item)
            self.pbr_browser_links.append(dict(url=url, text=text))
        for i in range(3):
            self.pbr_browser_link_tree.resizeColumnToContents(i)

    def grab_pbr_browser_links(self):
        if not self.pbr_browser_available():
            self.log("当前 3ds Max Python 没有 QtWebEngine，无法抓取网页链接。")
            return
        js = r"""
        (function(){
            const out = [];
            const seen = new Set();
            function add(url, text){
                if(!url || seen.has(url)) return;
                seen.add(url);
                out.push({url:url, text:(text||'').trim()});
            }
            document.querySelectorAll('a[href]').forEach(a => add(a.href, a.innerText || a.title || a.getAttribute('download') || ''));
            document.querySelectorAll('[data-href],[data-url],[data-download],[href]').forEach(el => {
                add(el.getAttribute('data-href') || el.getAttribute('data-url') || el.getAttribute('data-download') || el.getAttribute('href'), el.innerText || el.title || '');
            });
            return out;
        })();
        """
        try:
            self.pbr_browser.page().runJavaScript(js, self.on_pbr_browser_links_grabbed)
        except Exception:
            self.log(status_text_for_exception("抓取网页链接失败"))

    def on_pbr_browser_links_grabbed(self, result):
        try:
            base_url = self.pbr_browser_url.text().strip()
            links = []
            for r in result or []:
                try:
                    url = safe_str(r.get("url", ""), "")
                    txt = safe_str(r.get("text", ""), "")
                except Exception:
                    continue
                url = pbr_url_join(base_url, url)
                # 默认只显示像文件的链接，避免普通网页链接太多。
                if pbr_is_probably_download_url(url) or any(k in txt.lower() for k in ["download", "zip", "2k", "4k", "8k"]):
                    links.append(dict(url=url, text=txt))
            self.refresh_pbr_browser_link_tree(links)
            self.log("当前网页抓取到可能下载链接：{} 条".format(len(links)))
        except Exception:
            self.log(status_text_for_exception("处理网页链接失败"))

    def selected_pbr_browser_links(self):
        result = []
        for item in self.pbr_browser_link_tree.selectedItems():
            row = self.pbr_browser_link_tree.indexOfTopLevelItem(item)
            if row >= 0 and row < len(self.pbr_browser_links):
                result.append(self.pbr_browser_links[row])
        return result

    def add_selected_browser_links_to_queue(self):
        links = self.selected_pbr_browser_links()
        if not links:
            self.log("没有高亮选择网页链接")
            return
        added = 0
        for l in links:
            url = l.get("url", "")
            txt = l.get("text", "")
            name = pbr_safe_material_folder_name(url, txt[:60] if txt else "")
            if self.add_pbr_download_entry(url, name=name, source="网页抓取"):
                added += 1
        self.log("已把网页链接加入队列：{} 条".format(added))

    def add_all_browser_links_to_queue(self):
        links = list(getattr(self, "pbr_browser_links", []))
        if not links:
            self.log("当前没有可加入的网页链接，请先抓取本页链接")
            return
        added = 0
        for l in links:
            url = l.get("url", "")
            txt = l.get("text", "")
            name = pbr_safe_material_folder_name(url, txt[:60] if txt else "")
            if self.add_pbr_download_entry(url, name=name, source="网页抓取"):
                added += 1
        self.log("已把全部网页链接加入队列：{} 条".format(added))

    def build_pbr_download_tab(self):
        """
        V69：PBR下载库重新排版。
        不再左右分栏；改成"顶部材质库 + 下方分页卡片"，更清爽。
        主流程：外部浏览器打开网站 -> 复制下载链接 -> 剪贴板监听自动入队。
        """
        main = QtWidgets.QVBoxLayout(self.pbr_download_tab)
        main.setContentsMargins(12, 12, 12, 12)
        main.setSpacing(10)

        lib_box = self.card("PBR材质库")
        lib_lay = QtWidgets.QGridLayout(lib_box)
        lib_lay.setContentsMargins(12, 16, 12, 12)
        lib_lay.setHorizontalSpacing(8)
        lib_lay.setVerticalSpacing(8)

        self.pbr_library_dir = QtWidgets.QLineEdit(load_saved_pbr_library_dir())
        self.pbr_library_dir.setMinimumWidth(520)
        self.btn_choose_pbr_library = QtWidgets.QPushButton("选择材质库")
        self.btn_choose_pbr_library.clicked.connect(self.choose_pbr_library_folder)
        self.btn_open_pbr_library = QtWidgets.QPushButton("打开材质库")
        self.btn_open_pbr_library.clicked.connect(self.open_pbr_library_folder)
        self.btn_use_library_for_pbrset = QtWidgets.QPushButton("设为PBR套装目录")
        self.btn_use_library_for_pbrset.setObjectName("primaryButton")
        self.btn_use_library_for_pbrset.clicked.connect(self.set_pbrset_folder_from_library)

        lib_lay.addWidget(QtWidgets.QLabel("文件夹"), 0, 0)
        lib_lay.addWidget(self.pbr_library_dir, 0, 1, 1, 6)
        lib_lay.addWidget(self.btn_choose_pbr_library, 1, 1)
        lib_lay.addWidget(self.btn_open_pbr_library, 1, 2)
        lib_lay.addWidget(self.btn_use_library_for_pbrset, 1, 3, 1, 2)
        main.addWidget(lib_box)

        self.pbr_download_pages = QtWidgets.QTabWidget()
        main.addWidget(self.pbr_download_pages, 1)

        # 页面1：网站和剪贴板工作流
        site_page = QtWidgets.QWidget()
        site_lay = QtWidgets.QGridLayout(site_page)
        site_lay.setContentsMargins(10, 10, 10, 10)
        site_lay.setHorizontalSpacing(8)
        site_lay.setVerticalSpacing(8)

        self.pbr_site_tree = QtWidgets.QTreeWidget()
        self.pbr_site_tree.setColumnCount(4)
        self.pbr_site_tree.setHeaderLabels(["网站", "许可", "说明", "网址"])
        self.prepare_tree(self.pbr_site_tree)
        self.pbr_site_tree.setMinimumHeight(360)
        self.pbr_site_tree.itemDoubleClicked.connect(lambda *_: self.open_selected_pbr_site_chrome())

        self.btn_open_pbr_site = QtWidgets.QPushButton("Chrome打开网站")
        self.btn_open_pbr_site.setObjectName("primaryButton")
        self.btn_open_pbr_site.setToolTip("优先调用Google Chrome；找不到Chrome时使用系统默认浏览器。")
        self.btn_open_pbr_site.clicked.connect(self.open_selected_pbr_site_chrome)
        self.btn_open_pbr_site_external = QtWidgets.QPushButton("默认浏览器")
        self.btn_open_pbr_site_external.clicked.connect(self.open_selected_pbr_site)
        self.btn_add_pbr_site = QtWidgets.QPushButton("添加网站")
        self.btn_add_pbr_site.clicked.connect(self.add_pbr_site_dialog)
        self.btn_remove_pbr_site = QtWidgets.QPushButton("清除选择")
        self.btn_remove_pbr_site.setObjectName("dangerButton")
        self.btn_remove_pbr_site.clicked.connect(self.remove_selected_pbr_sites)
        self.btn_save_pbr_sites = QtWidgets.QPushButton("保存站点")
        self.btn_save_pbr_sites.clicked.connect(self.save_pbr_download_sites)

        self.chk_pbr_watch_clipboard = QtWidgets.QCheckBox("监听剪贴板下载链接")
        self.chk_pbr_watch_clipboard.setChecked(True)
        self.chk_pbr_watch_clipboard.setToolTip("复制网页里的下载链接后，符合允许类型就自动加入下载队列。")
        self.btn_check_pbr_clipboard_now = QtWidgets.QPushButton("立即识别剪贴板")
        self.btn_check_pbr_clipboard_now.clicked.connect(lambda: (setattr(self, "pbr_clipboard_last_text", ""), self.check_pbr_clipboard_links()))

        workflow = QtWidgets.QLabel("工作流：Chrome打开网站 → 浏览器插件/剪贴板把下载链接推到这里 → 下载队列处理文件。")
        workflow.setObjectName("hintLabel")
        workflow.setWordWrap(True)

        self.pbr_push_port_spin = QtWidgets.QSpinBox()
        self.pbr_push_port_spin.setRange(1025, 65535)
        self.pbr_push_port_spin.setValue(19527)
        self.pbr_push_port_spin.setToolTip("本地桥接服务端口，供 Chrome 扩展推送 PBR 链接和 Web AI 面板连接 3ds Max。建议 1025-65535，避免 80、443、3306、3389 等常用端口。")
        self.pbr_push_port_spin.valueChanged.connect(lambda _v: self.ai_sync_browser_port_controls())
        self.btn_pbr_push_port_check = QtWidgets.QPushButton("检查端口")
        self.btn_pbr_push_port_check.clicked.connect(self.pbr_check_push_server_port)
        self.pbr_push_port_hint = QtWidgets.QLabel("本地桥接端口。Chrome 扩展、PBR 推送和 Web AI 都使用同一个端口。")
        self.pbr_push_port_hint.setObjectName("hintLabel")
        self.pbr_push_port_hint.setWordWrap(True)

        self.btn_pbr_push_server_start = QtWidgets.QPushButton("启动本地桥接服务")
        self.btn_pbr_push_server_start.setObjectName("primaryButton")
        self.btn_pbr_push_server_start.setToolTip("启动本地 HTTP 服务，让 Chrome 扩展推送下载链接，并让浏览器 Web AI 连接 3ds Max。端口请和浏览器插件配置保持一致。")
        self.btn_pbr_push_server_start.clicked.connect(self.pbr_start_push_server)
        self.btn_pbr_push_server_stop = QtWidgets.QPushButton("停止")
        self.btn_pbr_push_server_stop.setObjectName("dangerButton")
        self.btn_pbr_push_server_stop.setEnabled(False)
        self.btn_pbr_push_server_stop.clicked.connect(self.pbr_stop_push_server)
        self.lbl_pbr_push_server_status = QtWidgets.QLabel("本地桥接服务：未启动")
        self.lbl_pbr_push_server_status.setObjectName("hintLabel")

        self.btn_pbr_open_ext_dir = QtWidgets.QPushButton("打开扩展目录")
        self.btn_pbr_open_ext_dir.setToolTip("打开 chrome_extension 文件夹，用于加载已解压的 Chrome 扩展。")
        self.btn_pbr_open_ext_dir.clicked.connect(self.pbr_open_chrome_extension_dir)
        self.btn_pbr_open_ext_page = QtWidgets.QPushButton("Chrome扩展页")
        self.btn_pbr_open_ext_page.setObjectName("primaryButton")
        self.btn_pbr_open_ext_page.setToolTip("打开 chrome://extensions，并显示加载/更新扩展的步骤。")
        self.btn_pbr_open_ext_page.clicked.connect(self.pbr_open_chrome_extensions_page)
        self.pbr_extension_install_hint = QtWidgets.QLabel("扩展安装/更新：打开 Chrome 扩展页 → 开发者模式 → 加载已解压扩展 → 选择 chrome_extension。更新后点“重新加载”。")
        self.pbr_extension_install_hint.setObjectName("hintLabel")
        self.pbr_extension_install_hint.setWordWrap(True)

        site_lay.addWidget(workflow, 0, 0, 1, 5)
        site_lay.addWidget(self.pbr_site_tree, 1, 0, 1, 5)
        site_lay.addWidget(self.btn_open_pbr_site, 2, 0, 1, 2)
        site_lay.addWidget(self.btn_open_pbr_site_external, 2, 2)
        site_lay.addWidget(self.btn_add_pbr_site, 3, 0)
        site_lay.addWidget(self.btn_remove_pbr_site, 3, 1)
        site_lay.addWidget(self.btn_save_pbr_sites, 3, 2)
        site_lay.addWidget(self.chk_pbr_watch_clipboard, 4, 0, 1, 2)
        site_lay.addWidget(self.btn_check_pbr_clipboard_now, 4, 2)
        site_lay.addWidget(QtWidgets.QLabel("通信端口"), 5, 0)
        site_lay.addWidget(self.pbr_push_port_spin, 5, 1)
        site_lay.addWidget(self.btn_pbr_push_port_check, 5, 2)
        site_lay.addWidget(self.pbr_push_port_hint, 5, 3, 1, 2)
        site_lay.addWidget(self.btn_pbr_push_server_start, 6, 0, 1, 2)
        site_lay.addWidget(self.btn_pbr_push_server_stop, 6, 2)
        site_lay.addWidget(self.lbl_pbr_push_server_status, 6, 3, 1, 2)
        site_lay.addWidget(self.btn_pbr_open_ext_dir, 7, 0, 1, 2)
        site_lay.addWidget(self.btn_pbr_open_ext_page, 7, 2)
        site_lay.addWidget(self.pbr_extension_install_hint, 7, 3, 1, 2)
        self.pbr_download_pages.addTab(site_page, "网站 / 剪贴板")

        # 页面2：下载队列
        queue_page = QtWidgets.QWidget()
        dl_lay = QtWidgets.QGridLayout(queue_page)
        dl_lay.setContentsMargins(10, 10, 10, 10)
        dl_lay.setHorizontalSpacing(8)
        dl_lay.setVerticalSpacing(8)

        self.pbr_download_url = QtWidgets.QLineEdit()
        self.pbr_download_url.setPlaceholderText("也可以手动粘贴直链：.zip / .jpg / .png / .exr / .hdr 等")
        self.pbr_download_name = QtWidgets.QLineEdit()
        self.pbr_download_name.setPlaceholderText("材质名，可留空")
        self.chk_pbr_download_extract_zip = QtWidgets.QCheckBox("ZIP自动解压")
        self.chk_pbr_download_extract_zip.setChecked(True)
        self.chk_pbr_delete_archive_after_extract = QtWidgets.QCheckBox("解压后删除原压缩包")
        self.chk_pbr_delete_archive_after_extract.setChecked(True)
        self.chk_pbr_flatten_redundant_folder = QtWidgets.QCheckBox("自动整理单层冗余文件夹")
        self.chk_pbr_flatten_redundant_folder.setChecked(True)
        self.chk_pbr_download_no_overwrite = QtWidgets.QCheckBox("已有不覆盖")
        self.chk_pbr_download_no_overwrite.setChecked(True)

        self.pbr_download_type_checks = {}
        type_row = QtWidgets.QFrame()
        type_row.setObjectName("compactRenameRow")
        type_lay = QtWidgets.QHBoxLayout(type_row)
        type_lay.setContentsMargins(0, 0, 0, 0)
        type_lay.setSpacing(8)
        type_lay.addWidget(QtWidgets.QLabel("允许入队类型"))
        for key, label, default in [
            (".zip", "ZIP", True),
            (".jpg|.jpeg", "JPG", True),
            (".png", "PNG", True),
            (".tif|.tiff", "TIF", False),
            (".exr", "EXR", True),
            (".hdr", "HDR", True),
            (".tga", "TGA", False),
            (".webp", "WEBP", False),
            (".rar|.7z", "RAR/7Z", False),
            ("unknown", "未知直链", False),
        ]:
            chk = QtWidgets.QCheckBox(label)
            chk.setChecked(default)
            self.pbr_download_type_checks[key] = chk
            type_lay.addWidget(chk)
        type_lay.addStretch()

        self.btn_add_pbr_download = QtWidgets.QPushButton("加入队列")
        self.btn_add_pbr_download.clicked.connect(self.add_pbr_download_url_to_queue)
        self.btn_import_local_pbr = QtWidgets.QPushButton("导入本地文件")
        self.btn_import_local_pbr.setObjectName("primaryButton")
        self.btn_import_local_pbr.setMinimumWidth(128)
        self.btn_import_local_pbr.setMinimumHeight(36)
        self.btn_import_local_pbr.clicked.connect(self.import_local_pbr_files)
        self.btn_download_selected_pbr = QtWidgets.QPushButton("下载选择")
        self.btn_download_selected_pbr.setObjectName("primaryButton")
        self.btn_download_selected_pbr.clicked.connect(self.download_selected_pbr_queue)
        self.btn_download_all_pbr = QtWidgets.QPushButton("下载全部")
        self.btn_download_all_pbr.setObjectName("primaryButton")
        self.btn_download_all_pbr.clicked.connect(self.download_all_pbr_queue)
        self.btn_clear_selected_pbr_download_queue = QtWidgets.QPushButton("清除所选")
        self.btn_clear_selected_pbr_download_queue.setObjectName("dangerButton")
        self.btn_clear_selected_pbr_download_queue.clicked.connect(self.clear_selected_pbr_download_queue)
        self.btn_clear_pbr_download_queue = QtWidgets.QPushButton("清空队列")
        self.btn_clear_pbr_download_queue.setObjectName("dangerButton")
        self.btn_clear_pbr_download_queue.clicked.connect(self.clear_pbr_download_queue)
        self.btn_check_pbr_redundant = QtWidgets.QPushButton("检查冗余文件夹")
        self.btn_check_pbr_redundant.clicked.connect(self.check_selected_pbr_queue_redundant_folders)

        self.btn_rename_pbr_download = QtWidgets.QPushButton("改材质名")
        self.btn_rename_pbr_download.clicked.connect(self.rename_selected_pbr_download_entry)
        self.btn_retry_pbr_download = QtWidgets.QPushButton("重试")
        self.btn_retry_pbr_download.clicked.connect(self.retry_selected_pbr_downloads)
        self.btn_copy_failed_pbr_links = QtWidgets.QPushButton("复制失败链接")
        self.btn_copy_failed_pbr_links.clicked.connect(self.copy_failed_pbr_download_links)
        self.btn_open_pbr_download_target = QtWidgets.QPushButton("打开目标文件夹")
        self.btn_open_pbr_download_target.clicked.connect(self.open_selected_pbr_download_target)
        self.btn_set_download_as_pbrset = QtWidgets.QPushButton("设为PBR套装目录")
        self.btn_set_download_as_pbrset.clicked.connect(self.set_selected_pbr_download_as_pbrset_folder)
        self.btn_scan_download_pbrset = QtWidgets.QPushButton("扫描当前材质")
        self.btn_scan_download_pbrset.setObjectName("primaryButton")
        self.btn_scan_download_pbrset.clicked.connect(self.scan_selected_pbr_download_as_pbrset)

        self.pbr_download_tree = QtWidgets.QTreeWidget()
        self.pbr_download_tree.setColumnCount(5)
        self.pbr_download_tree.setHeaderLabels(["状态", "材质名", "下载链接", "目标目录", "PBR检测"])
        self.prepare_tree(self.pbr_download_tree)
        self.pbr_download_tree.setMinimumHeight(380)
        try:
            self.pbr_download_tree.setAcceptDrops(True)
            self.pbr_download_tree.dragEnterEvent = self.pbr_download_tree_drag_enter_event
            self.pbr_download_tree.dragMoveEvent = self.pbr_download_tree_drag_move_event
            self.pbr_download_tree.dropEvent = self.pbr_download_tree_drop_event
        except Exception:
            pass

        dl_lay.addWidget(QtWidgets.QLabel("直链"), 0, 0)
        dl_lay.addWidget(self.pbr_download_url, 0, 1, 1, 5)
        dl_lay.addWidget(self.btn_add_pbr_download, 0, 6)
        dl_lay.addWidget(self.btn_import_local_pbr, 0, 7)
        dl_lay.addWidget(QtWidgets.QLabel("材质名"), 1, 0)
        dl_lay.addWidget(self.pbr_download_name, 1, 1, 1, 2)
        dl_lay.addWidget(self.chk_pbr_download_extract_zip, 1, 3)
        dl_lay.addWidget(self.chk_pbr_delete_archive_after_extract, 1, 4)
        dl_lay.addWidget(self.chk_pbr_flatten_redundant_folder, 1, 5, 1, 3)
        dl_lay.addWidget(self.chk_pbr_download_no_overwrite, 2, 1)
        dl_lay.addWidget(type_row, 3, 0, 1, 8)
        dl_lay.addWidget(self.btn_download_selected_pbr, 4, 0)
        dl_lay.addWidget(self.btn_download_all_pbr, 4, 1)
        dl_lay.addWidget(self.btn_clear_selected_pbr_download_queue, 4, 2)
        dl_lay.addWidget(self.btn_clear_pbr_download_queue, 4, 3)
        dl_lay.addWidget(self.btn_check_pbr_redundant, 4, 4)
        dl_lay.addWidget(self.btn_rename_pbr_download, 5, 0)
        dl_lay.addWidget(self.btn_retry_pbr_download, 5, 1)
        dl_lay.addWidget(self.btn_copy_failed_pbr_links, 5, 2)
        dl_lay.addWidget(self.btn_open_pbr_download_target, 5, 3)
        dl_lay.addWidget(self.btn_set_download_as_pbrset, 5, 4)
        dl_lay.addWidget(self.btn_scan_download_pbrset, 5, 5)
        dl_lay.addWidget(self.pbr_download_tree, 6, 0, 1, 7)

        note = QtWidgets.QLabel("说明：材质名默认使用下载文件名去后缀，例如 Onyx001_2K.zip → Onyx001_2K。支持把本地 ZIP / RAR / 7Z / JPG / PNG / EXR / HDR 等文件直接拖进下方下载队列，按“已完成下载”继续解压、整理和PBR检测。")
        note.setObjectName("hintLabel")
        note.setWordWrap(True)
        dl_lay.addWidget(note, 7, 0, 1, 7)
        self.pbr_download_pages.addTab(queue_page, "下载队列")

        self.pbr_browser_links = []
        self.pbr_download_queue = []
        self.refresh_pbr_site_tree()

    # ---------- AI 小助手 ----------
    def ai_current_provider_info(self, provider=None):
        try:
            provider = provider or self.ai_provider_combo.currentText()
            provider = ai_provider_key_from_name(provider)
            return ai_provider_presets().get(provider, {})
        except Exception:
            return {}

    def ai_save_current_provider_config(self, provider=None):
        """
        V78：每个服务商保存独立配置。切换服务商时不再互相覆盖Key/模型名。
        """
        try:
            if not hasattr(self, "ai_provider_configs"):
                self.ai_provider_configs = {}
            provider = provider or getattr(self, "ai_active_provider_name", "")
            if not provider:
                provider = self.ai_provider_combo.currentText() if hasattr(self, "ai_provider_combo") else ""
            provider = ai_provider_key_from_name(provider)
            if not provider:
                return
            self.ai_provider_configs[provider] = dict(
                api_type=self.ai_api_type_combo.currentText(),
                base_url=self.ai_base_url.text(),
                model=self.ai_model.text(),
                save_key=self.ai_save_key_chk.isChecked(),
                api_key=self.ai_api_key.text() if self.ai_save_key_chk.isChecked() else "",
                temperature=self.ai_temperature.value(),
                history=self.ai_history_spin.value()
            )
        except Exception:
            pass

    def _on_ai_robot_name_changed(self, text):
        name = text.strip() or "AI小助手"
        self._ai_robot_name = name
        try:
            self.ai_render_chat()
        except Exception:
            pass
        try:
            self.save_config_silent()
        except Exception:
            pass

    def _on_ai_user_name_changed(self, text):
        name = text.strip() or "用户"
        self._ai_user_name = name
        try:
            self.ai_render_chat()
        except Exception:
            pass
        try:
            self.save_config_silent()
        except Exception:
            pass

    def ai_on_provider_changed(self):
        try:
            old_provider = getattr(self, "ai_active_provider_name", "")
            if old_provider:
                self.ai_save_current_provider_config(old_provider)
            self.ai_active_provider_name = ai_provider_key_from_name(self.ai_provider_combo.currentText())
            self.ai_apply_preset()
        except Exception:
            self.ai_apply_preset()

    def ai_apply_preset(self):
        try:
            provider = ai_provider_key_from_name(self.ai_provider_combo.currentText())
            info = self.ai_current_provider_info(provider)
            if not info:
                return

            saved = {}
            try:
                saved = getattr(self, "ai_provider_configs", {}).get(provider, {}) or {}
            except Exception:
                saved = {}

            api_type = saved.get("api_type", info.get("api_type", "OpenAI兼容"))
            idx = self.ai_api_type_combo.findText(api_type)
            if idx >= 0:
                self.ai_api_type_combo.setCurrentIndex(idx)

            self.ai_base_url.setText(saved.get("base_url", info.get("base_url", "")))
            self.ai_model.setText(saved.get("model", info.get("model", "")))

            save_key = bool(saved.get("save_key", False))
            self.ai_save_key_chk.setChecked(save_key)
            self.ai_api_key.setText(saved.get("api_key", "") if save_key else "")

            try:
                self.ai_temperature.setValue(float(saved.get("temperature", self.ai_temperature.value())))
                self.ai_history_spin.setValue(int(saved.get("history", self.ai_history_spin.value())))
            except Exception:
                pass

            try:
                self.ai_cost_type_label.setText(info.get("cost_type", ""))
                show_name = info.get("display_name", provider)
                capability_text = ai_provider_capability_text(info)
                self.ai_provider_note.setText("当前方案：{}。能力：{}。费用类型：{}。{}".format(show_name, capability_text, info.get("cost_type", ""), info.get("note", "")))
            except Exception:
                pass

            self.log("AI预设已切换：{}".format(info.get("display_name", provider)))
            self.ai_refresh_config_summary()
        except Exception:
            self.log(status_text_for_exception("切换AI预设失败"))

    def ai_open_key_page(self):
        try:
            info = self.ai_current_provider_info()
            url = info.get("key_url", "")
            if not url:
                self.log("当前方案没有API Key获取地址")
                return
            if open_url_in_chrome_or_browser(url):
                self.log("已打开API Key获取页面：{}".format(url))
            else:
                webbrowser.open(url)
        except Exception:
            self.log(status_text_for_exception("打开API Key页面失败"))

    def ai_open_provider_docs(self):
        try:
            info = self.ai_current_provider_info()
            url = info.get("doc_url", "")
            if not url:
                self.log("当前方案没有文档地址")
                return
            if open_url_in_chrome_or_browser(url):
                self.log("已打开AI接口文档：{}".format(url))
            else:
                webbrowser.open(url)
        except Exception:
            self.log(status_text_for_exception("打开AI接口文档失败"))

    def ai_open_provider_billing(self):
        try:
            info = self.ai_current_provider_info()
            url = info.get("billing_url", "") or info.get("usage_url", "") or info.get("key_url", "")
            if not url:
                self.log("当前方案没有账单/用量地址")
                return
            if open_url_in_chrome_or_browser(url):
                self.log("已打开账单/用量页面：{}".format(url))
            else:
                webbrowser.open(url)
        except Exception:
            self.log(status_text_for_exception("打开账单/用量页面失败"))

    def ai_activate_provider(self):
        try:
            provider = ai_provider_key_from_name(self.ai_provider_combo.currentText())
            info = self.ai_current_provider_info()
            if info.get("need_key", True) and not self.ai_api_key.text().strip():
                QtWidgets.QMessageBox.information(self, "AI激活", "当前方案需要API Key。\n\n请先点击\"获取API Key\"，复制后填入API Key。")
                return
            self.ai_save_current_provider_config(provider)
            self.save_config()
            self.log("AI方案已保存：{}".format(info.get("display_name", provider)))
            self.ai_test_connection()
        except Exception:
            self.log(status_text_for_exception("激活AI方案失败"))

    def ai_chat_font_size_value(self):
        try:
            return int(self.ai_font_size_spin.value())
        except Exception:
            return 8

    def ai_change_font_size(self, delta):
        try:
            v = int(self.ai_font_size_spin.value()) + int(delta)
            v = max(int(self.ai_font_size_spin.minimum()), min(int(self.ai_font_size_spin.maximum()), v))
            self.ai_font_size_spin.setValue(v)
            self.ai_update_chat_style()
            self.save_config_silent()
            self.log("AI聊天字号：{}".format(v))
        except Exception:
            self.log(status_text_for_exception("调整AI聊天字号失败"))

    def ai_update_chat_style(self):
        """
        V80：强制用 setFont + styleSheet 两套方式更新字号，解决部分 Max 皮肤下字号不生效。
        """
        try:
            size = self.ai_chat_font_size_value()
            p = getattr(self, "_ui_palette", {}) or {}
            field = p.get("field", "#191714")
            text = p.get("text", "#F4EEE7")
            line = p.get("line", "#4B4036")
            selection = p.get("selection", "#5B4027")
            selection_text = p.get("selection_text", "#FFF8EF")
            qss = (
                "QPlainTextEdit, QListWidget {"
                "background: %s;"
                "color: %s;"
                "border: 1px solid %s;"
                "font-family: 'Microsoft YaHei UI', 'Microsoft YaHei', 'DengXian', 'Segoe UI';"
                "font-size: %dpt;"
                "line-height: 130%%;"
                "padding: 8px;"
                "selection-background-color: %s;"
                "selection-color: %s;"
                "}"
            ) % (field, text, line, size, selection, selection_text)
            for w in [
                getattr(self, "ai_chat_view", None),
                getattr(self, "ai_input", None),
                getattr(self, "ai_diag_view", None),
                getattr(self, "ai_popup_chat_view", None),
                getattr(self, "ai_popup_input", None),
            ]:
                if not w:
                    continue
                font = w.font()
                font.setPointSize(size)
                try:
                    font.setFamily("Microsoft YaHei UI")
                except Exception:
                    pass
                w.setFont(font)
                try:
                    w.setStyleSheet(qss)
                except Exception:
                    pass
            self.ai_render_chat()
        except Exception:
            pass

    def ai_format_message_plain(self, role, content, images=None):
        role = safe_str(role, "")
        content = safe_str(content, "")
        if role == "user":
            title = "【{}】".format(safe_str(getattr(self, "_ai_user_name", "用户"), "用户"))
        elif role == "assistant":
            title = "【{}】".format(getattr(self, "_ai_robot_name", "AI小助手"))
        elif role == "system":
            title = "【系统】"
        elif role == "script_result":
            title = "【脚本结果】"
        elif role == "script_running":
            title = "【执行中】"
        else:
            title = "【{}】".format(role or "消息")
        if images:
            content = (content + "\n\n[图片]\n" + "\n".join(images)).strip()
        return "{}\n{}\n".format(title, content)

    def ai_role_display_name(self, role):
        role = safe_str(role, "")
        if role == "user":
            return safe_str(getattr(self, "_ai_user_name", "用户"), "用户") + ":"
        if role == "assistant":
            return safe_str(getattr(self, "_ai_robot_name", "AI小助手"), "AI小助手") + ":"
        if role == "system":
            return "提示"
        if role == "ai_thinking":
            return ""
        if role == "script_result":
            return "执行结果"
        if role == "script_running":
            return "处理中"
        return role or "消息"

    def ai_message_alignment(self, role):
        if role in ("system", "script_running", "ai_thinking"):
            return "center"
        return "right" if role == "user" else "left"

    def ai_message_colors(self, role, message_index=None):
        user_palettes = [
            ("#D9ECFF", "#17324D", "#A8CAE9"),
            ("#DDF7E8", "#183A2B", "#B7DEC8"),
            ("#FFF0DB", "#4A3215", "#E9CDA5"),
            ("#F3E8FF", "#43245D", "#D7BFEA"),
            ("#FFE4EA", "#5A2430", "#E9BCC6"),
        ]
        assistant_palettes = [
            ("#F7F7F8", "#1F252C", "#D7DADF"),
            ("#EAF3FF", "#1E2E45", "#C8D8EE"),
            ("#EFF8F1", "#203626", "#CDE1D0"),
            ("#FFF7EA", "#45361D", "#E7D5AE"),
            ("#F6F0FF", "#36224A", "#D7CAE9"),
        ]
        try:
            idx = int(message_index)
        except Exception:
            idx = 0
        if role == "user":
            return user_palettes[idx % len(user_palettes)]
        if role == "assistant":
            return assistant_palettes[idx % len(assistant_palettes)]
        if role == "tool":
            return ("#EEF5FF", "#233A56", "#BED0EA")
        if role == "script_result":
            return ("#EEF8EE", "#16351E", "#8BBE92")
        if role in ("script_running", "ai_thinking"):
            return ("#FFF6DE", "#47350A", "#D9BB63")
        if role == "system":
            return ("#F6F7F9", "#4A5560", "#D4D9E1")
        return ("#ECECEC", "#202020", "#9A9A9A")

    def ai_link_color(self, role):
        if role == "user":
            return "#EAF2FF"
        if role == "script_result":
            return "#2B6C34"
        return "#2D6CDF"

    def ai_image_thumbnail_data_uri(self, path, max_edge=128):
        try:
            if not safe_str(path, "").strip():
                return ""
            pix = QtGui.QPixmap(path)
            if pix.isNull():
                return ""
            pix = pix.scaled(max_edge, max_edge, QtCore.Qt.KeepAspectRatio, QtCore.Qt.SmoothTransformation)
            buf = QtCore.QBuffer()
            buf.open(QtCore.QIODevice.WriteOnly)
            pix.save(buf, "PNG")
            data = bytes(buf.data().data())
            return "data:image/png;base64," + base64.b64encode(data).decode("ascii")
        except Exception:
            return ""

    def ai_chat_plain_text(self):
        lines = []
        for m in getattr(self, "ai_messages", []):
            lines.append(self.ai_format_message_plain(m.get("role", ""), m.get("content", ""), m.get("images", [])).rstrip())
            lines.append("")
        return "\n".join(lines).rstrip() + "\n"

    def ai_append_chat_message(self, role, content):
        try:
            self.ai_messages.append(dict(role=role, content=content))
            self.ai_render_chat()
        except Exception:
            pass

    def ai_remove_pending_thinking_message(self):
        try:
            if getattr(self, "ai_messages", None) and self.ai_messages[-1].get("role") == "ai_thinking":
                self.ai_messages.pop()
                return True
        except Exception:
            pass
        return False

    def ai_list_widgets(self):
        return [
            getattr(self, "ai_chat_view", None),
            getattr(self, "ai_popup_chat_view", None),
        ]

    def ai_message_bubble_width(self, role):
        try:
            view = getattr(self, "ai_popup_chat_view", None) if getattr(self, "ai_popup_chat_view", None) and getattr(self, "ai_popup_chat_view", None).isVisible() else getattr(self, "ai_chat_view", None)
            view_w = max(520, int(view.viewport().width())) if view else 900
        except Exception:
            view_w = 900
        if role in ("system", "script_running", "ai_thinking"):
            return int(view_w * 0.74)
        if role == "script_result":
            return int(view_w * 0.82)
        return int(view_w * 0.72)

    def ai_make_chat_action_button(self, text, func):
        btn = QtWidgets.QPushButton(text)
        btn.setCursor(QtCore.Qt.PointingHandCursor)
        btn.setFlat(True)
        btn.setStyleSheet(
            "QPushButton { border:none; color:#2D6CDF; text-decoration:underline; padding:0 6px 0 0; background:transparent; }"
            "QPushButton:hover { color:#1F56BC; }"
        )
        btn.clicked.connect(func)
        return btn

    def ai_build_message_widget(self, message, message_index=0):
        role = safe_str((message or {}).get("role", ""), "")
        content = safe_str((message or {}).get("content", ""), "")
        images = list((message or {}).get("images", []) or [])
        title = self.ai_role_display_name(role)
        bg, fg, border = self.ai_message_colors(role, message_index)
        font_size = self.ai_chat_font_size_value()

        root = QtWidgets.QWidget()
        root_lay = QtWidgets.QHBoxLayout(root)
        root_lay.setContentsMargins(12, 6, 12, 6)
        root_lay.setSpacing(0)

        if role == "user":
            root_lay.addStretch()

        bubble = QtWidgets.QFrame()
        bubble.setObjectName("aiBubble")
        bubble.setAutoFillBackground(False)
        bubble.setMaximumWidth(self.ai_message_bubble_width(role))
        bubble_bg = color_to_rgba_css(bg, 0.56)
        bubble_border = color_to_rgba_css(border, 0.82)
        bubble.setStyleSheet(
            "QFrame#aiBubble { background-color:%s; border:1px solid %s; border-radius:16px; }" % (bubble_bg, bubble_border)
        )
        bubble_lay = QtWidgets.QVBoxLayout(bubble)
        bubble_lay.setContentsMargins(12, 9, 12, 9)
        bubble_lay.setSpacing(6)

        title_lbl = QtWidgets.QLabel(title)
        title_lbl.setStyleSheet("color:%s; font-weight:600; background:transparent;" % fg)
        title_font = title_lbl.font()
        title_font.setFamily("Microsoft YaHei UI")
        title_font.setPointSize(max(8, font_size - 1))
        title_lbl.setFont(title_font)
        bubble_lay.addWidget(title_lbl)

        if role == "script_result":
            tag = QtWidgets.QLabel("脚本执行返回")
            tag.setStyleSheet("color:#2B6C34; background:rgba(43,108,52,0.08); border:none; padding:4px 8px;")
            bubble_lay.addWidget(tag)

        body_lbl = QtWidgets.QLabel(content or " ")
        body_lbl.setWordWrap(True)
        body_lbl.setTextInteractionFlags(QtCore.Qt.TextSelectableByMouse)
        body_lbl.setStyleSheet("color:%s; background:transparent;" % fg)
        body_font = body_lbl.font()
        body_font.setFamily("Microsoft YaHei UI")
        body_font.setPointSize(font_size)
        body_lbl.setFont(body_font)
        bubble_lay.addWidget(body_lbl)

        for path in images:
            abs_path = safe_str(path, "").strip()
            if not abs_path:
                continue
            box = QtWidgets.QWidget()
            box_lay = QtWidgets.QVBoxLayout(box)
            box_lay.setContentsMargins(0, 2, 0, 0)
            box_lay.setSpacing(4)

            preview_btn = QtWidgets.QPushButton()
            preview_btn.setCursor(QtCore.Qt.PointingHandCursor)
            preview_btn.setFixedSize(120, 120)
            preview_btn.setStyleSheet(
                "QPushButton { background:#FFFFFF; border:1px solid %s; border-radius:12px; padding:4px; }" % border
            )
            pix = QtGui.QPixmap(abs_path)
            if not pix.isNull():
                preview_btn.setIcon(QtGui.QIcon(pix))
                preview_btn.setIconSize(QtCore.QSize(104, 104))
            else:
                preview_btn.setText("图片预览")
            preview_btn.clicked.connect(lambda _=False, p=abs_path: open_file_in_os(p))
            box_lay.addWidget(preview_btn, 0, QtCore.Qt.AlignLeft)

            name_lbl = QtWidgets.QLabel(os.path.basename(abs_path))
            name_lbl.setWordWrap(True)
            name_lbl.setTextInteractionFlags(QtCore.Qt.TextSelectableByMouse)
            name_lbl.setStyleSheet("color:%s; background:transparent;" % fg)
            name_font = name_lbl.font()
            name_font.setFamily("Microsoft YaHei UI")
            name_font.setPointSize(max(8, font_size - 1))
            name_lbl.setFont(name_font)
            box_lay.addWidget(name_lbl)

            action_row = QtWidgets.QHBoxLayout()
            action_row.setContentsMargins(0, 0, 0, 0)
            action_row.setSpacing(6)
            action_row.addWidget(self.ai_make_chat_action_button("打开原图", lambda _=False, p=abs_path: open_file_in_os(p)))
            action_row.addWidget(self.ai_make_chat_action_button("打开文件夹", lambda _=False, p=abs_path: open_folder_in_os(os.path.dirname(p))))
            action_row.addStretch()
            box_lay.addLayout(action_row)
            bubble_lay.addWidget(box)

        if role == "script_result":
            action_row = QtWidgets.QHBoxLayout()
            action_row.setContentsMargins(0, 2, 0, 0)
            action_row.setSpacing(6)
            action_row.addWidget(self.ai_make_chat_action_button("复制结果", self.ai_copy_last_script_result))
            action_row.addWidget(self.ai_make_chat_action_button("让AI再修一次", self.ai_retry_last_script_fix))
            action_row.addWidget(self.ai_make_chat_action_button("再执行一次代码", self.ai_run_last_code_block))
            action_row.addStretch()
            bubble_lay.addLayout(action_row)

        root_lay.addWidget(bubble, 0, QtCore.Qt.AlignLeft if role != "user" else QtCore.Qt.AlignRight)
        if role != "user":
            root_lay.addStretch()
        return root

    def ai_render_chat(self):
        try:
            for w in self.ai_list_widgets():
                if not w:
                    continue
                w.setUpdatesEnabled(False)
                w.clear()
                for idx, m in enumerate(getattr(self, "ai_messages", [])):
                    item = QtWidgets.QListWidgetItem()
                    widget = self.ai_build_message_widget(m, idx)
                    item.setFlags(QtCore.Qt.ItemIsEnabled | QtCore.Qt.ItemIsSelectable)
                    item.setSizeHint(widget.sizeHint())
                    w.addItem(item)
                    w.setItemWidget(item, widget)
                w.setUpdatesEnabled(True)
                try:
                    w.scrollToBottom()
                except Exception:
                    pass
            QtWidgets.QApplication.processEvents()
        except Exception:
            pass

    def ai_open_chat_link(self, url):
        try:
            u = safe_str(url.toString() if hasattr(url, "toString") else url, "").strip()
            if not u:
                return
            if u.lower().startswith("action:///"):
                action = safe_str(urllib.parse.unquote(u[10:]), "").strip().lower()
                if action == "copy_last_script_result":
                    self.ai_copy_last_script_result()
                elif action == "retry_last_script_fix":
                    self.ai_retry_last_script_fix()
                elif action == "run_last_code_block":
                    self.ai_run_last_code_block()
                return
            if u.lower().startswith("folder:///"):
                path = urllib.parse.unquote(u[10:]).replace("/", "\\")
                if not open_folder_in_os(path):
                    self.log("无法打开文件夹：{}".format(path))
                return
            if u.lower().startswith("file:///"):
                path = urllib.parse.unquote(u[8:]).replace("/", "\\")
                if not open_file_in_os(path):
                    self.log("无法打开图片：{}".format(path))
                return
            if not open_url_in_chrome_or_browser(u):
                webbrowser.open(u)
        except Exception:
            self.log(status_text_for_exception("打开聊天链接失败"))

    def ai_selected_image_paths(self):
        return list(getattr(self, "_ai_pending_images", []) or [])

    def ai_refresh_image_preview(self):
        try:
            imgs = self.ai_selected_image_paths()
            if hasattr(self, "ai_image_preview_label"):
                if imgs:
                    self.ai_image_preview_label.setText("已附加图片：{}".format("；".join(os.path.basename(p) for p in imgs[:3]) + (" 等" if len(imgs) > 3 else "")))
                else:
                    self.ai_image_preview_label.setText("未附加图片")
        except Exception:
            pass
        try:
            if hasattr(self, "ai_popup_image_preview_label"):
                if imgs:
                    self.ai_popup_image_preview_label.setText("已附加图片：{}".format("；".join(os.path.basename(p) for p in imgs[:3]) + (" 等" if len(imgs) > 3 else "")))
                else:
                    self.ai_popup_image_preview_label.setText("未附加图片")
        except Exception:
            pass

    def ai_choose_images(self):
        try:
            files, _ = QtWidgets.QFileDialog.getOpenFileNames(
                self,
                "选择图片",
                current_scene_folder(),
                "Images (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.webp *.gif *.exr *.hdr);;All Files (*.*)"
            )
            files = [safe_str(f, "").strip() for f in files if safe_str(f, "").strip()]
            if not files:
                return
            self._ai_pending_images = files
            self.ai_refresh_image_preview()
            support_state, support_text = self.ai_image_support_state()
            if support_state is False:
                self.log("AI已附加图片：{} 张。{}".format(len(files), support_text))
            else:
                self.log("AI已附加图片：{} 张".format(len(files)))
        except Exception:
            self.log(status_text_for_exception("附加图片失败"))

    def ai_clear_images(self):
        self._ai_pending_images = []
        self.ai_refresh_image_preview()

    def ai_image_support_state(self):
        try:
            api_type = self.ai_api_type_combo.currentText() if hasattr(self, "ai_api_type_combo") else ""
        except Exception:
            api_type = ""
        try:
            provider = self.ai_provider_combo.currentText() if hasattr(self, "ai_provider_combo") else ""
        except Exception:
            provider = ""
        try:
            model = safe_str(self.ai_model.text(), "").strip().lower() if hasattr(self, "ai_model") else ""
        except Exception:
            model = ""
        try:
            base_url = safe_str(self.ai_base_url.text(), "").strip() if hasattr(self, "ai_base_url") else ""
        except Exception:
            base_url = ""
        try:
            info = self.ai_current_provider_info() if hasattr(self, "ai_current_provider_info") else {}
        except Exception:
            info = {}

        vision_keys = [
            "gpt-4o", "gpt-4.1", "o4", "o3", "vision", "vl", "llava", "bakllava",
            "qwen2-vl", "qwen2.5-vl", "qvq", "minicpm-v", "internvl", "gemma3",
            "llama3.2-vision", "llama-4-scout", "llama-4-maverick", "moondream", "pixtral", "gemini", "claude-3"
        ]
        looks_like_vision = any(k in model for k in vision_keys)
        can_edit = bool(info.get("supports_image_editing", False))
        edit_tail = "；注意：当前这里只代表支持发图看图，不等于当前插件已经走通直接改图/返图专用接口。"
        if can_edit:
            edit_tail = "；该方案对应生态里可能存在直接改图能力，但通常需要专用图片模型或单独图片接口。"

        if safe_str(api_type, "").startswith("Ollama"):
            if looks_like_vision:
                return True, "当前模型看起来支持图片输入，会直接把图片发给 AI{}".format(edit_tail)
            return False, "当前 Ollama 模型名看起来不是视觉模型；发送图片时会自动退回成文字+图片路径说明。"

        if "groq" in safe_str(provider, "").lower():
            if looks_like_vision:
                return True, "当前 Groq 模型名看起来支持图片输入，会直接把图片发给 AI；Groq 当前这条方案主要是看图分析，不是直接改图模型。"
            return False, "当前 Groq 模型不是视觉模型。像 llama-3.3-70b-versatile 这类文本模型不会看图；发送图片时会自动退回成文字说明。"

        if looks_like_vision:
            return True, "当前模型看起来支持图片输入，会直接把图片发给 AI{}".format(edit_tail)

        if ai_is_local_endpoint(base_url):
            return False, "当前本地模型名看起来不是视觉模型；发送图片时会自动退回成文字+图片路径说明。"

        return None, "当前接口是否支持图片取决于服务商和模型；如果不支持，会自动退回成文字+图片路径说明。看图能力和直接改图能力不是一回事。"

    def ai_refresh_image_capability_hint(self):
        try:
            _state, text = self.ai_image_support_state()
        except Exception:
            text = "图片支持状态未知。"
        for name in ("ai_image_capability_label", "ai_popup_image_capability_label"):
            try:
                w = getattr(self, name, None)
                if w:
                    w.setText(text)
            except Exception:
                pass

    def ai_bind_image_capability_refreshers(self):
        try:
            self.ai_provider_combo.currentIndexChanged.connect(lambda _=0: self.ai_refresh_image_capability_hint())
        except Exception:
            pass
        try:
            self.ai_api_type_combo.currentIndexChanged.connect(lambda _=0: self.ai_refresh_image_capability_hint())
        except Exception:
            pass
        try:
            self.ai_base_url.textChanged.connect(lambda _="": self.ai_refresh_image_capability_hint())
        except Exception:
            pass
        try:
            self.ai_model.textChanged.connect(lambda _="": self.ai_refresh_image_capability_hint())
        except Exception:
            pass

    def ai_guess_image_mime(self, path):
        ext = os.path.splitext(safe_str(path, ""))[1].lower()[1:]
        mapping = {
            "jpg": "image/jpeg",
            "jpeg": "image/jpeg",
            "png": "image/png",
            "bmp": "image/bmp",
            "gif": "image/gif",
            "webp": "image/webp",
            "tif": "image/tiff",
            "tiff": "image/tiff",
        }
        return mapping.get(ext, "image/png")

    def ai_prepare_image_for_transport(self, path, max_edge=1600, max_bytes=4 * 1024 * 1024, jpeg_quality=86):
        abs_path = safe_str(path, "").strip()
        if not abs_path or (not os.path.isfile(abs_path)):
            raise RuntimeError("图片不存在：{}".format(abs_path))
        ext = os.path.splitext(abs_path)[1].lower()
        common_raster_exts = set([".jpg", ".jpeg", ".png", ".bmp", ".webp", ".gif", ".tif", ".tiff"])
        special_keep_exts = set([".exr", ".hdr"])

        try:
            self.set_status("正在压缩图片：{}".format(os.path.basename(abs_path)))
            QtWidgets.QApplication.processEvents()
        except Exception:
            pass

        raw_size = 0
        try:
            raw_size = int(os.path.getsize(abs_path))
        except Exception:
            raw_size = 0

        if ext in special_keep_exts:
            with open(abs_path, "rb") as f:
                raw = f.read()
            return self.ai_guess_image_mime(abs_path), raw, dict(
                source_path=abs_path,
                source_bytes=len(raw),
                output_bytes=len(raw),
                output_format=ext.replace(".", "").upper() or "RAW",
                width=0,
                height=0,
                compressed=False,
                method="RAW-special"
            )

        try:
            from PIL import Image
            with Image.open(abs_path) as im:
                try:
                    im = im.convert("RGB")
                except Exception:
                    pass
                w, h = im.size
                scale = min(float(max_edge) / float(max(w, 1)), float(max_edge) / float(max(h, 1)), 1.0)
                if scale < 1.0:
                    tw = max(1, int(w * scale))
                    th = max(1, int(h * scale))
                    resample = getattr(Image, "LANCZOS", getattr(Image, "BICUBIC", 3))
                    im = im.resize((tw, th), resample)

                out = io.BytesIO()
                save_quality = int(jpeg_quality)
                im.save(out, format="JPEG", quality=save_quality, optimize=True)
                data = out.getvalue()

                while len(data) > int(max_bytes) and save_quality > 55:
                    out = io.BytesIO()
                    save_quality -= 8
                    im.save(out, format="JPEG", quality=save_quality, optimize=True)
                    data = out.getvalue()

                if len(data) > int(max_bytes):
                    while max(im.size) > 960 and len(data) > int(max_bytes):
                        tw = max(1, int(im.size[0] * 0.85))
                        th = max(1, int(im.size[1] * 0.85))
                        resample = getattr(Image, "LANCZOS", getattr(Image, "BICUBIC", 3))
                        im = im.resize((tw, th), resample)
                        out = io.BytesIO()
                        im.save(out, format="JPEG", quality=max(55, save_quality), optimize=True)
                        data = out.getvalue()

                return "image/jpeg", data, dict(
                    source_path=abs_path,
                    source_bytes=raw_size,
                    output_bytes=len(data),
                    output_format="JPEG",
                    width=int(im.size[0]),
                    height=int(im.size[1]),
                    compressed=(len(data) != raw_size),
                    method="Pillow"
                )
        except Exception:
            pass

        if ext in common_raster_exts:
            try:
                reader = QtGui.QImageReader(abs_path)
                img = reader.read()
                if not img.isNull():
                    w = int(img.width())
                    h = int(img.height())
                    scale = min(float(max_edge) / float(max(w, 1)), float(max_edge) / float(max(h, 1)), 1.0)
                    if scale < 1.0:
                        tw = max(1, int(w * scale))
                        th = max(1, int(h * scale))
                        img = img.scaled(tw, th, QtCore.Qt.KeepAspectRatio, QtCore.Qt.SmoothTransformation)
                    quality = int(jpeg_quality)
                    out_arr = QtCore.QByteArray()
                    out_buf = QtCore.QBuffer(out_arr)
                    out_buf.open(QtCore.QIODevice.WriteOnly)
                    img.save(out_buf, "JPEG", quality)
                    data = bytes(out_arr.data())

                    while len(data) > int(max_bytes) and quality > 55:
                        out_arr = QtCore.QByteArray()
                        out_buf = QtCore.QBuffer(out_arr)
                        out_buf.open(QtCore.QIODevice.WriteOnly)
                        quality -= 8
                        img.save(out_buf, "JPEG", quality)
                        data = bytes(out_arr.data())

                    while len(data) > int(max_bytes) and max(int(img.width()), int(img.height())) > 960:
                        tw = max(1, int(img.width() * 0.85))
                        th = max(1, int(img.height() * 0.85))
                        img = img.scaled(tw, th, QtCore.Qt.KeepAspectRatio, QtCore.Qt.SmoothTransformation)
                        out_arr = QtCore.QByteArray()
                        out_buf = QtCore.QBuffer(out_arr)
                        out_buf.open(QtCore.QIODevice.WriteOnly)
                        img.save(out_buf, "JPEG", max(55, quality))
                        data = bytes(out_arr.data())

                    return "image/jpeg", data, dict(
                        source_path=abs_path,
                        source_bytes=raw_size,
                        output_bytes=len(data),
                        output_format="JPEG",
                        width=int(img.width()),
                        height=int(img.height()),
                        compressed=(len(data) != raw_size or int(img.width()) != w or int(img.height()) != h),
                        method="Qt"
                    )
            except Exception:
                pass

        if ext not in common_raster_exts and ext not in special_keep_exts:
            try:
                self.log("AI图片发送提示：{} 不是常见位图格式，本次按原图直传。".format(os.path.basename(abs_path)))
            except Exception:
                pass

        with open(abs_path, "rb") as f:
            raw = f.read()
        return self.ai_guess_image_mime(abs_path), raw, dict(
            source_path=abs_path,
            source_bytes=len(raw),
            output_bytes=len(raw),
            output_format="RAW",
            width=0,
            height=0,
            compressed=False,
            method="RAW"
        )

    def ai_encode_image_for_transport(self, path):
        abs_path = safe_str(path, "").strip()
        if not abs_path or (not os.path.isfile(abs_path)):
            raise RuntimeError("图片不存在：{}".format(abs_path))
        try:
            self.set_status("正在编码图片：{}".format(os.path.basename(abs_path)))
            QtWidgets.QApplication.processEvents()
        except Exception:
            pass
        mime, raw, info = self.ai_prepare_image_for_transport(abs_path)
        self._ai_last_image_prepare_info = dict(info or {})
        b64 = base64.b64encode(raw).decode("ascii")
        return mime, b64

    def ai_build_api_message_with_report(self, role, content, images, api_type):
        role = safe_str(role, "")
        content = safe_str(content, "")
        images = [p for p in list(images or []) if safe_str(p, "").strip()]
        report = dict(
            requested=len(images),
            attached=0,
            transport="text",
            sent=False,
            note="未附加图片",
            errors=[]
        )
        if not images:
            return dict(role=role, content=content), report

        text = content or "请结合附加图片回答。"
        support_state, _support_text = self.ai_image_support_state()
        if support_state is False:
            fallback_text = (
                text
                + "\n\n[说明]\n用户这次确实附加了真实图片，但当前模型可能不支持直接看图。"
                + "不要假装你已经看到了图片内容，也不要回答“我无法访问你电脑上的本地文件路径”。"
                + "请明确说明当前模型可能不支持图片分析，并给出下一步建议。"
            )
            report["transport"] = "fallback_text"
            report["note"] = "当前模型看起来不支持图片直传，本次只发送了文字说明。"
            return dict(role=role, content=fallback_text), report

        if safe_str(api_type, "").startswith("Ollama"):
            encoded = []
            for path in images:
                try:
                    _mime, b64 = self.ai_encode_image_for_transport(path)
                    encoded.append(b64)
                except Exception as e:
                    report["errors"].append("{} -> {}".format(path, e))
                    self.log("图片编码失败，已跳过：{} / {}".format(path, e))
            report["attached"] = len(encoded)
            if encoded:
                report["transport"] = "ollama_images"
                report["sent"] = True
                report["note"] = "已把 {} 张图片随 Ollama 请求一并发出。".format(len(encoded))
                return dict(
                    role=role,
                    content=text + "\n\n[系统说明] 用户已通过插件附加真实图片，请直接基于图片内容回答，不要说无法访问本地路径。",
                    images=encoded
                ), report
            report["transport"] = "encode_failed"
            report["note"] = "图片编码失败，本次没有真正把图片发给模型。"
            return dict(role=role, content=text + "\n\n[说明] 图片附加失败。不要假装看到了图片，请直接说明本次未成功上传图片。"), report

        parts = [dict(type="text", text=text + "\n\n[系统说明] 用户已通过插件附加真实图片，请直接基于图片内容回答，不要说无法访问本地路径。")]
        added = 0
        for path in images:
            try:
                mime, b64 = self.ai_encode_image_for_transport(path)
                parts.append(dict(type="image_url", image_url=dict(url="data:{};base64,{}".format(mime, b64))))
                added += 1
            except Exception as e:
                report["errors"].append("{} -> {}".format(path, e))
                self.log("图片编码失败，已跳过：{} / {}".format(path, e))
        report["attached"] = added
        if added:
            report["transport"] = "openai_image_url"
            report["sent"] = True
            report["note"] = "已把 {} 张图片编码后随多模态请求发出。".format(added)
            return dict(role=role, content=parts), report
        report["transport"] = "encode_failed"
        report["note"] = "图片编码失败，本次没有真正把图片发给模型。"
        return dict(role=role, content=text + "\n\n[说明] 图片附加失败。不要假装看到了图片，请直接说明本次未成功上传图片。"), report

    def ai_build_api_message(self, role, content, images, api_type):
        msg, _report = self.ai_build_api_message_with_report(role, content, images, api_type)
        return msg

    def ai_describe_image_send_report(self, report):
        report = dict(report or {})
        requested = int(report.get("requested", 0) or 0)
        attached = int(report.get("attached", 0) or 0)
        transport = safe_str(report.get("transport", ""), "")
        note = safe_str(report.get("note", ""), "")
        if requested <= 0:
            return ""
        if transport == "openai_image_url":
            return "本次图片发送：已成功发出 {}/{} 张图片。{}".format(attached, requested, note)
        if transport == "ollama_images":
            return "本次图片发送：已成功发出 {}/{} 张图片。{}".format(attached, requested, note)
        if transport == "fallback_text":
            return "本次图片发送：没有真正发图，只发了文字说明。{}".format(note)
        if transport == "encode_failed":
            extra = ""
            errs = list(report.get("errors", []) or [])
            if errs:
                extra = " 编码错误：{}".format(" | ".join(errs[:2]))
            return "本次图片发送：图片编码失败，未真正发给模型。{}{}".format(note, extra)
        return "本次图片发送状态未知：请求 {} 张，实际附加 {} 张。{}".format(requested, attached, note)

    def ai_collect_request_debug_lines(self, messages, send_report=None):
        lines = []
        try:
            api_type = self.ai_api_type_combo.currentText() if hasattr(self, "ai_api_type_combo") else ""
        except Exception:
            api_type = ""
        try:
            provider = self.ai_provider_combo.currentText() if hasattr(self, "ai_provider_combo") else ""
        except Exception:
            provider = ""
        try:
            model = self.ai_model.text().strip() if hasattr(self, "ai_model") else ""
        except Exception:
            model = ""

        lines.append("AI请求结构诊断")
        lines.append("- 方案：{}".format(provider or "未知"))
        lines.append("- 接口类型：{}".format(api_type or "未知"))
        lines.append("- 模型名：{}".format(model or "空"))
        lines.append("- 消息数：{}".format(len(list(messages or []))))

        image_parts = 0
        ollama_images = 0
        last_user_shape = "未知"
        try:
            for m in list(messages or []):
                role = safe_str(m.get("role", ""), "")
                content = m.get("content", "")
                if isinstance(content, list):
                    txt_parts = 0
                    img_parts = 0
                    for part in content:
                        if not isinstance(part, dict):
                            continue
                        if part.get("type") == "text":
                            txt_parts += 1
                        elif part.get("type") == "image_url":
                            img_parts += 1
                    image_parts += img_parts
                    if role == "user":
                        last_user_shape = "OpenAI兼容 content-list: text={} image_url={}".format(txt_parts, img_parts)
                imgs = m.get("images", None)
                if isinstance(imgs, list) and imgs:
                    ollama_images += len(imgs)
                    if role == "user":
                        last_user_shape = "Ollama images: {}".format(len(imgs))
                if role == "user" and isinstance(content, str) and "[说明]" in content and "当前模型可能不支持直接看图" in content:
                    last_user_shape = "纯文字降级：模型不支持图片直传"
        except Exception:
            pass

        lines.append("- OpenAI image_url 数量：{}".format(image_parts))
        lines.append("- Ollama images 数量：{}".format(ollama_images))
        lines.append("- 最后一条用户消息结构：{}".format(last_user_shape))
        try:
            prep = dict(getattr(self, "_ai_last_image_prepare_info", {}) or {})
            if prep:
                lines.append("- 图片预处理：{} / {} {}x{} {} -> {} bytes".format(
                    prep.get("method", "RAW"),
                    prep.get("output_format", "RAW"),
                    prep.get("width", 0),
                    prep.get("height", 0),
                    prep.get("source_bytes", 0),
                    prep.get("output_bytes", 0)
                ))
        except Exception:
            pass

        report_text = self.ai_describe_image_send_report(send_report)
        if report_text:
            lines.append("- {}".format(report_text))
        return lines

    def ai_sync_input_from_popup(self):
        try:
            if hasattr(self, "ai_popup_input") and self.ai_popup_input:
                self.ai_input.setPlainText(self.ai_popup_input.toPlainText())
        except Exception:
            pass

    def ai_sync_input_to_popup(self):
        try:
            if hasattr(self, "ai_popup_input") and self.ai_popup_input:
                self.ai_popup_input.setPlainText(self.ai_input.toPlainText())
        except Exception:
            pass

    def ai_send_message_from_popup(self):
        try:
            self.ai_sync_input_from_popup()
            self.ai_send_message()
            self.ai_sync_input_to_popup()
        except Exception:
            self.ai_send_message()

    def ai_image_edit_mode(self):
        return bool(getattr(self, "_ai_image_edit_mode", False))

    def ai_image_edit_mode_prefix(self):
        if not self.ai_image_edit_mode():
            return ""
        return (
            "【图片编辑模式】\n"
            "本轮按图片编辑/改图需求回答。\n"
            "优先结合用户附加图片，回答如何改图、怎么合成、人物/物体放哪里、提示词怎么写、需要什么素材。\n"
            "不要输出3ds Max脚本、Python脚本或MAXScript，除非用户明确要求写脚本。\n"
            "如果当前模型不能直接改图，请明确说明，并给出最接近的替代方案。\n\n"
        )

    def ai_set_image_edit_mode(self, enabled):
        self._ai_image_edit_mode = bool(enabled)
        self.ai_refresh_image_edit_mode_ui()
        try:
            self.save_config_silent()
        except Exception:
            pass

    def ai_toggle_image_edit_mode(self):
        self.ai_set_image_edit_mode(not self.ai_image_edit_mode())
        self.log("图片编辑模式已{}".format("开启" if self.ai_image_edit_mode() else "关闭"))

    def ai_refresh_image_edit_mode_ui(self):
        enabled = self.ai_image_edit_mode()
        text = "图片编辑模式：开" if enabled else "图片编辑模式：关"
        tip = (
            "当前优先按看图/改图需求回答，不自动偏向脚本。"
            if enabled else
            "当前按普通聊天模式处理；如附图且问题明显是改图，也会尽量拦截脚本跑偏。"
        )
        for name in ("btn_ai_image_edit_mode", "btn_ai_popup_image_edit_mode"):
            try:
                btn = getattr(self, name, None)
                if btn:
                    btn.setText(text)
                    btn.setToolTip(tip)
                    btn.setProperty("activeToggle", enabled)
                    btn.style().unpolish(btn)
                    btn.style().polish(btn)
                    btn.update()
            except Exception:
                pass
        for name in ("ai_image_edit_mode_label", "ai_popup_image_edit_mode_label"):
            try:
                lab = getattr(self, name, None)
                if lab:
                    lab.setText(tip)
            except Exception:
                pass

    def ai_append_diagnosis_line(self, text):
        try:
            line = safe_str(text, "").strip()
            if not line or not hasattr(self, "ai_diag_view") or not self.ai_diag_view:
                return
            cur = safe_str(self.ai_diag_view.toPlainText(), "")
            if cur:
                cur += "\n"
            self.ai_diag_view.setPlainText(cur + line)
            try:
                self.ai_diag_view.moveCursor(QtGui.QTextCursor.End)
            except Exception:
                pass
        except Exception:
            pass

    def ai_clear_diagnosis_log(self):
        try:
            if hasattr(self, "ai_diag_view") and self.ai_diag_view:
                self.ai_diag_view.setPlainText("")
            if hasattr(self, "ai_diag_popup_view") and self.ai_diag_popup_view:
                self.ai_diag_popup_view.setPlainText("")
            self.log("AI诊断日志已清空")
        except Exception:
            self.log(status_text_for_exception("清空AI诊断日志失败"))

    def ai_open_diagnosis_log(self):
        try:
            if hasattr(self, "ai_diag_dialog") and self.ai_diag_dialog:
                self.ai_diag_popup_view.setPlainText(self.ai_diag_view.toPlainText() if hasattr(self, "ai_diag_view") else "")
                self.ai_diag_dialog.show()
                self.ai_diag_dialog.raise_()
                self.ai_diag_dialog.activateWindow()
                return
            dlg = QtWidgets.QDialog(self)
            dlg.setWindowTitle("AI请求诊断日志")
            dlg.resize(860, 560)
            lay = QtWidgets.QVBoxLayout(dlg)
            lay.setContentsMargins(10, 10, 10, 10)
            lay.setSpacing(8)
            top = QtWidgets.QHBoxLayout()
            top.addStretch()
            btn_clear = QtWidgets.QPushButton("清除日志")
            btn_clear.setObjectName("dangerButton")
            btn_clear.clicked.connect(self.ai_clear_diagnosis_log)
            btn_close = QtWidgets.QPushButton("关闭")
            btn_close.clicked.connect(dlg.close)
            top.addWidget(btn_clear)
            top.addWidget(btn_close)
            lay.addLayout(top)
            self.ai_diag_popup_view = QtWidgets.QPlainTextEdit()
            self.ai_diag_popup_view.setReadOnly(True)
            self.ai_diag_popup_view.setPlainText(self.ai_diag_view.toPlainText() if hasattr(self, "ai_diag_view") else "")
            lay.addWidget(self.ai_diag_popup_view, 1)
            self.ai_diag_dialog = dlg
            dlg.show()
            dlg.raise_()
            dlg.activateWindow()
        except Exception:
            self.log(status_text_for_exception("打开AI诊断日志失败"))

    def ai_set_config_collapsed(self, collapsed):
        self._ai_config_collapsed = bool(collapsed)
        self.ai_refresh_config_collapse_ui()
        try:
            self.save_config_silent()
        except Exception:
            pass

    def ai_toggle_config_collapsed(self):
        self.ai_set_config_collapsed(not bool(getattr(self, "_ai_config_collapsed", False)))

    def ai_refresh_config_collapse_ui(self):
        collapsed = bool(getattr(self, "_ai_config_collapsed", False))
        try:
            if hasattr(self, "ai_config_card") and self.ai_config_card:
                self.ai_config_card.setVisible(not collapsed)
        except Exception:
            pass
        try:
            if hasattr(self, "ai_cfg_body") and self.ai_cfg_body:
                self.ai_cfg_body.setVisible(not collapsed)
        except Exception:
            pass
        try:
            if hasattr(self, "btn_ai_toggle_config") and self.btn_ai_toggle_config:
                self.btn_ai_toggle_config.setText("展开AI配置" if collapsed else "收起AI配置")
        except Exception:
            pass
        self.ai_refresh_config_summary()

    def ai_config_summary_text(self):
        try:
            provider = self.ai_provider_combo.currentText().strip() if hasattr(self, "ai_provider_combo") else "未选择方案"
            api_type = self.ai_api_type_combo.currentText().strip() if hasattr(self, "ai_api_type_combo") else "未设置接口"
            base_url = self.ai_base_url.text().strip() if hasattr(self, "ai_base_url") else ""
            model = self.ai_model.text().strip() if hasattr(self, "ai_model") else ""
            api_key = self.ai_api_key.text().strip() if hasattr(self, "ai_api_key") else ""
            save_key = self.ai_save_key_chk.isChecked() if hasattr(self, "ai_save_key_chk") else False
            info = self.ai_current_provider_info(provider) if hasattr(self, "ai_current_provider_info") else {}
            need_key = bool(info.get("need_key", True))
            missing = []
            if not base_url:
                missing.append("Base URL")
            if not model:
                missing.append("模型")
            if need_key and not api_key:
                missing.append("Key")
            key_state = "Key已填{}".format("并保存" if save_key and api_key else "") if api_key else ("本地/免Key" if not need_key else "Key未填")
            ready_state = "可测试" if not missing else "缺少" + "/".join(missing)
            return "{} | {} | {} | {} | {}".format(provider or "未选择方案", api_type or "未设置接口", model or "模型未填", key_state, ready_state)
        except Exception:
            return "AI配置摘要不可用"

    def ai_refresh_config_summary(self):
        try:
            if hasattr(self, "ai_config_summary_label") and self.ai_config_summary_label:
                self.ai_config_summary_label.setText(self.ai_config_summary_text())
        except Exception:
            pass

    def ai_bind_config_summary_refreshers(self):
        pairs = [
            ("ai_provider_combo", "currentIndexChanged"),
            ("ai_api_type_combo", "currentIndexChanged"),
            ("ai_base_url", "textChanged"),
            ("ai_model", "textChanged"),
            ("ai_api_key", "textChanged"),
            ("ai_save_key_chk", "stateChanged"),
        ]
        for attr, signal_name in pairs:
            try:
                widget = getattr(self, attr, None)
                if not widget:
                    continue
                signal = getattr(widget, signal_name)
                signal.connect(lambda *_args: self.ai_refresh_config_summary())
            except Exception:
                pass

    def ai_web_state_payload(self):
        try:
            pending = list(getattr(self, "_ai_pending_images", []) or [])
        except Exception:
            pending = []
        try:
            port = int(self.pbr_push_port_spin.value()) if hasattr(self, "pbr_push_port_spin") else int(_pbr_push_server_port or 19527)
        except Exception:
            port = int(_pbr_push_server_port or 19527)
        messages = []
        for m in list(getattr(self, "ai_messages", []) or []):
            entry = dict(role=safe_str(m.get("role", ""), ""), content=safe_str(m.get("content", ""), ""))
            imgs = []
            for path in list(m.get("images", []) or []):
                path = _safe_local_path(path)
                if not path or not os.path.isfile(path):
                    continue
                imgs.append(dict(
                    path=path,
                    url="file:///" + path.replace("\\", "/"),
                    thumb=self.ai_image_thumbnail_data_uri(path, max_edge=128)
                ))
            if imgs:
                entry["images"] = imgs
            messages.append(entry)
        return {
            "ok": True,
            "online": _pbr_push_server_instance is not None,
            "port": port,
            "provider_names": list(ai_provider_names()) if callable(ai_provider_names) else [],
            "sending": bool(getattr(self, "_ai_web_busy", False)),
            "last_error": safe_str(getattr(self, "_ai_web_last_error", ""), ""),
            "config": {
                "provider": self.ai_provider_combo.currentText() if hasattr(self, "ai_provider_combo") else "",
                "api_type": self.ai_api_type_combo.currentText() if hasattr(self, "ai_api_type_combo") else "",
                "base_url": self.ai_base_url.text().strip() if hasattr(self, "ai_base_url") else "",
                "model": self.ai_model.text().strip() if hasattr(self, "ai_model") else "",
                "temperature": float(self.ai_temperature.value()) if hasattr(self, "ai_temperature") else 0.3,
                "history": int(self.ai_history_spin.value()) if hasattr(self, "ai_history_spin") else 8,
                "template": self.ai_template_combo.currentText() if hasattr(self, "ai_template_combo") else "",
                "display_font_size": self.ai_chat_font_size_value() if hasattr(self, "ai_chat_font_size_value") else 8,
                "robot_name": safe_str(getattr(self, "_ai_robot_name", "AI小助手"), "AI小助手"),
                "user_name": safe_str(getattr(self, "_ai_user_name", "用户"), "用户"),
                "image_edit_mode": bool(getattr(self, "_ai_image_edit_mode", False))
            },
            "messages": messages,
            "scene_summary_short": self.ai_collect_scene_summary().splitlines()[0] if self.ai_collect_scene_summary() else "",
            "pending_image_text": ("已附加图片：" + "；".join(os.path.basename(p) for p in pending[:6])) if pending else "未附加图片",
            "diagnosis": self.ai_diag_view.toPlainText() if hasattr(self, "ai_diag_view") else ""
        }

    def ai_web_update_config(self, data):
        data = data or {}
        provider = safe_str(data.get("provider", ""), "").strip()
        if provider and hasattr(self, "ai_provider_combo"):
            idx = self.ai_provider_combo.findText(provider)
            if idx >= 0:
                self.ai_provider_combo.setCurrentIndex(idx)
        api_type = safe_str(data.get("api_type", ""), "").strip()
        if api_type and hasattr(self, "ai_api_type_combo"):
            idx = self.ai_api_type_combo.findText(api_type)
            if idx >= 0:
                self.ai_api_type_combo.setCurrentIndex(idx)
        if "base_url" in data and hasattr(self, "ai_base_url"):
            self.ai_base_url.setText(safe_str(data.get("base_url", ""), ""))
        if "model" in data and hasattr(self, "ai_model"):
            self.ai_model.setText(safe_str(data.get("model", ""), ""))
        if safe_str(data.get("api_key", ""), "") and hasattr(self, "ai_api_key"):
            self.ai_api_key.setText(safe_str(data.get("api_key", ""), ""))
        if "temperature" in data and hasattr(self, "ai_temperature"):
            try:
                self.ai_temperature.setValue(float(data.get("temperature", self.ai_temperature.value())))
            except Exception:
                pass
        if "history" in data and hasattr(self, "ai_history_spin"):
            try:
                self.ai_history_spin.setValue(int(data.get("history", self.ai_history_spin.value())))
            except Exception:
                pass
        if "template" in data and hasattr(self, "ai_template_combo"):
            txt = safe_str(data.get("template", ""), "").strip()
            idx = self.ai_template_combo.findText(txt)
            if idx >= 0:
                self.ai_template_combo.setCurrentIndex(idx)
        if "robot_name" in data and hasattr(self, "ai_robot_name_edit"):
            self.ai_robot_name_edit.setText(safe_str(data.get("robot_name", ""), "") or "AI小助手")
        if "user_name" in data and hasattr(self, "ai_user_name_edit"):
            self.ai_user_name_edit.setText(safe_str(data.get("user_name", ""), "") or "用户")
        try:
            self.ai_save_current_provider_config()
        except Exception:
            pass
        try:
            self.ai_refresh_config_summary()
        except Exception:
            pass
        self.save_config_silent()
        return self.ai_web_state_payload()

    def ai_web_run_test(self):
        self.ai_test_connection()
        return self.ai_web_state_payload()

    def ai_web_run_diagnose(self):
        self.ai_diagnose_connection()
        return self.ai_web_state_payload()

    def ai_web_insert_scene_summary(self):
        self.ai_insert_scene_summary()
        return self.ai_web_state_payload()

    def ai_web_insert_recent_log(self):
        self.ai_insert_recent_log()
        return self.ai_web_state_payload()

    def ai_web_toggle_image_edit(self):
        self.ai_toggle_image_edit_mode()
        return self.ai_web_state_payload()

    def ai_web_clear_chat(self):
        self.ai_clear_chat()
        return self.ai_web_state_payload()

    def ai_sync_shared_state(self, announce=True):
        try:
            self.save_config_silent()
        except Exception:
            pass
        try:
            self.ai_sync_browser_port_controls()
        except Exception:
            pass
        try:
            self.ai_refresh_image_preview()
        except Exception:
            pass
        try:
            self.ai_render_chat()
        except Exception:
            pass
        if announce:
            try:
                self.log("AI消息与配置状态已同步")
            except Exception:
                pass

    def ai_web_schedule_ui_refresh(self):
        try:
            QtCore.QTimer.singleShot(0, self.ai_render_chat)
        except Exception:
            pass

    def ai_web_log_line(self, text):
        try:
            QtCore.QTimer.singleShot(0, lambda t=safe_str(text, ""): self.log(t))
        except Exception:
            pass

    def ai_web_append_diag_lines(self, lines):
        for line in list(lines or []):
            try:
                self.ai_append_diagnosis_line(line)
            except Exception:
                pass

    def ai_web_send_message(self, data):
        import threading as _threading
        payload = dict(data or {})
        text = safe_str(payload.get("text", ""), "").strip()
        if not text:
            raise RuntimeError("问题为空")
        if bool(getattr(self, "_ai_web_busy", False)):
            raise RuntimeError("上一条问题还在处理中，请先等待完成")
        images_payload = list(payload.get("images", []) or [])
        images = []
        for item in images_payload:
            if not isinstance(item, dict):
                continue
            saved = ai_save_data_url_image(safe_str(item.get("data_url", ""), ""))
            if saved:
                images.append(saved)

        prefix = self.ai_current_template_text()
        mode_prefix = self.ai_image_edit_mode_prefix()
        user_text = (mode_prefix + prefix + text).strip()
        if not hasattr(self, "ai_messages"):
            self.ai_messages = []
        self.ai_messages.append(dict(role="user", content=user_text, images=images))
        self.ai_messages.append(dict(role="ai_thinking", content="正在理解图片并思考…" if images else "正在思考…"))
        self._ai_pending_images = list(images)

        keep = int(self.ai_history_spin.value()) if hasattr(self, "ai_history_spin") else 6
        api_type = self.ai_api_type_combo.currentText() if hasattr(self, "ai_api_type_combo") else "OpenAI兼容"
        messages = [dict(role="system", content=self.ai_system_prompt())]
        history = list(getattr(self, "ai_messages", []))[-max(0, keep):]
        send_report = None
        for idx, m in enumerate(history):
            role = m.get("role", "")
            content = m.get("content", "")
            msg_images = m.get("images", [])
            is_last_user = (idx == len(history) - 1 and role == "user")
            if role in ("user", "assistant"):
                if is_last_user:
                    msg, send_report = self.ai_build_api_message_with_report(role, content, msg_images, api_type)
                    messages.append(msg)
                else:
                    messages.append(self.ai_build_api_message(role, content, msg_images, api_type))
            elif role == "script_result":
                messages.append(dict(role="user", content="[脚本执行结果]\n{}".format(content)))

        cfg = dict(
            api_type=self.ai_api_type_combo.currentText() if hasattr(self, "ai_api_type_combo") else "OpenAI兼容",
            base_url=self.ai_base_url.text().strip() if hasattr(self, "ai_base_url") else "",
            api_key=self.ai_api_key.text().strip() if hasattr(self, "ai_api_key") else "",
            model=self.ai_model.text().strip() if hasattr(self, "ai_model") else "",
            temperature=float(self.ai_temperature.value()) if hasattr(self, "ai_temperature") else 0.3,
            provider=self.ai_provider_combo.currentText() if hasattr(self, "ai_provider_combo") else "OpenAI兼容接口"
        )
        self._ai_web_busy = True
        self._ai_web_last_error = ""
        self.ai_web_append_diag_lines(self.ai_collect_request_debug_lines(messages, send_report=send_report))
        self.ai_web_schedule_ui_refresh()

        def _worker():
            try:
                result = self.ai_request_with_config(messages, cfg, test=False)
                answer = safe_str(result.get("text", ""), "").strip() or "AI没有返回内容"
                response_images = list(result.get("images", []) or [])
                self.ai_remove_pending_thinking_message()
                self.ai_messages.append(dict(role="assistant", content=answer, images=response_images))
                self._ai_pending_images = []
                report_text = self.ai_describe_image_send_report(send_report)
                if report_text:
                    self.ai_web_log_line(report_text)
                    try:
                        self.ai_append_diagnosis_line(report_text)
                    except Exception:
                        pass
            except Exception as e:
                msg = safe_str(e, "")
                self._ai_web_last_error = msg
                self.ai_remove_pending_thinking_message()
                self.ai_messages.append(dict(role="system", content="AI请求失败：\n{}".format(msg)))
                self.ai_web_log_line("AI请求失败：{}".format(msg))
            finally:
                self._ai_web_busy = False
                self.ai_web_schedule_ui_refresh()

        t = _threading.Thread(target=_worker, daemon=True)
        t.start()
        return self.ai_web_state_payload()

    def ai_sync_browser_port_controls(self):
        try:
            port = int(self.pbr_push_port_spin.value()) if hasattr(self, "pbr_push_port_spin") else 19527
        except Exception:
            port = 19527
        try:
            if hasattr(self, "ai_browser_port_spin") and self.ai_browser_port_spin and int(self.ai_browser_port_spin.value()) != int(port):
                old = self.ai_browser_port_spin.blockSignals(True)
                self.ai_browser_port_spin.setValue(int(port))
                self.ai_browser_port_spin.blockSignals(old)
        except Exception:
            pass

    def ai_set_browser_port_from_toolbar(self):
        try:
            if hasattr(self, "ai_browser_port_spin") and hasattr(self, "pbr_push_port_spin"):
                port = int(self.ai_browser_port_spin.value())
                if int(self.pbr_push_port_spin.value()) != int(port):
                    self.pbr_push_port_spin.setValue(port)
                self.save_config_silent()
        except Exception:
            self.log(status_text_for_exception("同步浏览器端口失败"))

    def ai_open_web_chat(self):
        try:
            self.ai_set_browser_port_from_toolbar()
        except Exception:
            pass
        try:
            # 强制重启本地服务，避免脚本热重载后还挂着旧 handler，导致 /ai 返回 not found。
            self.pbr_start_push_server(force_restart=True)
        except Exception:
            pass
        try:
            port = int(self.pbr_push_port_spin.value()) if hasattr(self, "pbr_push_port_spin") else int(_pbr_push_server_port or 19527)
        except Exception:
            port = int(_pbr_push_server_port or 19527 or 19527)
        url = "http://127.0.0.1:{}/ai".format(port)
        if not open_url_in_chrome_or_browser(url):
            webbrowser.open(url)
        self.log("已打开浏览器 AI 面板（端口 {}）：{}".format(port, url))

    def ai_open_popout_chat(self):
        """
        独立AI聊天窗口：可拉大拉小，不影响主插件窗口。
        """
        try:
            if hasattr(self, "ai_popout_dialog") and self.ai_popout_dialog:
                try:
                    self.ai_popout_dialog.show()
                    self.ai_popout_dialog.raise_()
                    self.ai_popout_dialog.activateWindow()
                    self.ai_render_chat()
                    self.ai_sync_input_to_popup()
                    return
                except Exception:
                    pass

            dlg = QtWidgets.QDialog(self)
            dlg.setWindowTitle("AI小助手 - 独立聊天窗口")
            dlg.resize(980, 720)
            try:
                apply_installed_window_icon(dlg)
            except Exception:
                pass

            lay = QtWidgets.QVBoxLayout(dlg)
            lay.setContentsMargins(10, 10, 10, 10)
            lay.setSpacing(8)

            top = QtWidgets.QHBoxLayout()
            top.addWidget(QtWidgets.QLabel("聊天字号"))
            btn_pop_font_minus = QtWidgets.QPushButton("A-")
            btn_pop_font_minus.setFixedSize(48, 32)
            btn_pop_font_minus.clicked.connect(lambda: self.ai_change_font_size(-1))
            top.addWidget(btn_pop_font_minus)
            pop_font_spin = QtWidgets.QSpinBox()
            pop_font_spin.setRange(8, 24)
            pop_font_spin.setValue(self.ai_chat_font_size_value())
            pop_font_spin.setFixedSize(104, 32)
            pop_font_spin.valueChanged.connect(lambda v: (self.ai_font_size_spin.setValue(v), self.ai_update_chat_style()))
            top.addWidget(pop_font_spin)
            btn_pop_font_plus = QtWidgets.QPushButton("A+")
            btn_pop_font_plus.setFixedSize(48, 32)
            btn_pop_font_plus.clicked.connect(lambda: self.ai_change_font_size(1))
            top.addWidget(btn_pop_font_plus)
            top.addStretch()
            btn_copy = QtWidgets.QPushButton("复制最后回答")
            btn_copy.clicked.connect(self.ai_copy_last_answer)
            btn_clear = QtWidgets.QPushButton("清空对话")
            btn_clear.setObjectName("dangerButton")
            btn_clear.clicked.connect(self.ai_clear_chat)
            btn_close = QtWidgets.QPushButton("关闭")
            btn_close.clicked.connect(dlg.close)
            top.addWidget(btn_copy)
            top.addWidget(btn_clear)
            top.addWidget(btn_close)
            lay.addLayout(top)

            self.ai_popup_chat_view = QtWidgets.QListWidget()
            self.ai_popup_chat_view.setSelectionMode(QtWidgets.QAbstractItemView.SingleSelection)
            self.ai_popup_chat_view.setVerticalScrollMode(QtWidgets.QAbstractItemView.ScrollPerPixel)
            self.ai_popup_chat_view.setHorizontalScrollBarPolicy(QtCore.Qt.ScrollBarAlwaysOff)
            self.ai_popup_chat_view.setResizeMode(QtWidgets.QListView.Adjust)
            self.ai_popup_chat_view.setSpacing(2)
            self.ai_popup_chat_view.setMinimumHeight(420)
            lay.addWidget(self.ai_popup_chat_view, 1)

            self.ai_popup_input = QtWidgets.QPlainTextEdit()
            self.ai_popup_input.setPlaceholderText("输入问题：Enter发送，Shift+Enter换行")
            self.ai_popup_input.setMinimumHeight(110)
            self.ai_popup_input.installEventFilter(self)
            lay.addWidget(self.ai_popup_input)
            self.ai_popup_image_preview_label = QtWidgets.QLabel("未附加图片")
            self.ai_popup_image_preview_label.setObjectName("hintLabel")
            lay.addWidget(self.ai_popup_image_preview_label)
            self.ai_popup_image_capability_label = QtWidgets.QLabel("")
            self.ai_popup_image_capability_label.setObjectName("hintLabel")
            self.ai_popup_image_capability_label.setWordWrap(True)
            lay.addWidget(self.ai_popup_image_capability_label)

            bottom = QtWidgets.QHBoxLayout()
            btn_send = QtWidgets.QPushButton("发送问题")
            btn_send.setObjectName("primaryButton")
            btn_send.clicked.connect(self.ai_send_message_from_popup)
            btn_scene = QtWidgets.QPushButton("附加场景摘要")
            btn_scene.clicked.connect(lambda: (self.ai_sync_input_from_popup(), self.ai_insert_scene_summary(), self.ai_sync_input_to_popup()))
            btn_log = QtWidgets.QPushButton("附加最近日志")
            btn_log.clicked.connect(lambda: (self.ai_sync_input_from_popup(), self.ai_insert_recent_log(), self.ai_sync_input_to_popup()))
            btn_img = QtWidgets.QPushButton("发送图片")
            btn_img.clicked.connect(self.ai_choose_images)
            btn_img_clear = QtWidgets.QPushButton("清空图片")
            btn_img_clear.clicked.connect(self.ai_clear_images)
            self.btn_ai_popup_image_edit_mode = QtWidgets.QPushButton("")
            self.btn_ai_popup_image_edit_mode.clicked.connect(self.ai_toggle_image_edit_mode)
            btn_detect = QtWidgets.QPushButton("识别推荐操作")
            btn_detect.clicked.connect(self.ai_rebuild_smart_actions)
            btn_run = QtWidgets.QPushButton("执行推荐操作")
            btn_run.clicked.connect(self.ai_run_smart_action)
            bottom.addWidget(btn_send)
            bottom.addWidget(btn_scene)
            bottom.addWidget(btn_log)
            bottom.addWidget(btn_img)
            bottom.addWidget(btn_img_clear)
            bottom.addWidget(self.btn_ai_popup_image_edit_mode)
            bottom.addWidget(btn_detect)
            bottom.addWidget(btn_run)
            bottom.addStretch()
            lay.addLayout(bottom)
            self.ai_popup_image_edit_mode_label = QtWidgets.QLabel("")
            self.ai_popup_image_edit_mode_label.setObjectName("hintLabel")
            self.ai_popup_image_edit_mode_label.setWordWrap(True)
            lay.addWidget(self.ai_popup_image_edit_mode_label)

            self.ai_popout_dialog = dlg
            self.ai_render_chat()
            self.ai_sync_input_to_popup()
            self.ai_refresh_image_preview()
            self.ai_refresh_image_capability_hint()
            self.ai_refresh_image_edit_mode_ui()
            self.ai_update_chat_style()

            dlg.show()
            dlg.raise_()
            dlg.activateWindow()
            self.log("AI独立聊天窗口已打开")
        except Exception:
            self.log(status_text_for_exception("打开AI独立聊天窗口失败"))

    def eventFilter(self, obj, event):
        """
        AI输入框：Enter发送，Shift+Enter换行。
        """
        try:
            if obj in [getattr(self, "ai_input", None), getattr(self, "ai_popup_input", None)]:
                if event.type() == QtCore.QEvent.KeyPress:
                    key = event.key()
                    mods = event.modifiers()
                    if key in (QT_KEY_RETURN, QT_KEY_ENTER):
                        if mods & QT_SHIFT_MODIFIER:
                            return False
                        if obj is getattr(self, "ai_popup_input", None):
                            self.ai_send_message_from_popup()
                        else:
                            self.ai_send_message()
                        return True
        except Exception:
            pass
        try:
            return super(SceneHealthDialog, self).eventFilter(obj, event)
        except Exception:
            return False

    def ai_current_visible_chat_text(self):
        try:
            selected = []
            for w in self.ai_list_widgets():
                if not w:
                    continue
                for item in w.selectedItems():
                    row = w.row(item)
                    msgs = getattr(self, "ai_messages", [])
                    if 0 <= row < len(msgs):
                        selected.append(self.ai_format_message_plain(
                            msgs[row].get("role", ""),
                            msgs[row].get("content", ""),
                            msgs[row].get("images", [])
                        ).strip())
                if selected:
                    return "\n\n".join(selected).strip()
        except Exception:
            pass
        try:
            return self.ai_chat_plain_text()
        except Exception:
            return ""

    def ai_last_answer_text(self):
        try:
            for m in reversed(getattr(self, "ai_messages", [])):
                if m.get("role") == "assistant":
                    return safe_str(m.get("content", ""), "")
        except Exception:
            pass
        return ""

    def ai_rebuild_smart_actions(self):
        """
        从选中文字/聊天全文/最后回答里识别：
        - URL：外部浏览器打开
        - Windows路径：打开文件夹
        - 关键词：推荐插件内操作
        """
        try:
            base_text = self.ai_current_visible_chat_text()
            if not base_text.strip():
                base_text = self.ai_last_answer_text()
            actions = []

            urls = pbr_extract_urls_from_text(base_text)
            for u in urls[:20]:
                actions.append(dict(type="url", label="打开网址：{}".format(u[:90]), value=u))

            paths = ai_extract_windows_paths_from_text(base_text)
            for p in paths[:20]:
                actions.append(dict(type="path", label="打开路径/所在文件夹：{}".format(p[:90]), value=p))

            low = base_text.lower()
            # 根据常见AI回答关键词推荐插件操作
            keyword_actions = [
                ("完整诊断", "plugin:ai_diagnose", "执行AI完整诊断"),
                ("诊断", "plugin:ai_diagnose", "执行AI完整诊断"),
                ("账单", "plugin:ai_billing", "打开当前AI服务商账单/用量"),
                ("用量", "plugin:ai_billing", "打开当前AI服务商账单/用量"),
                ("api key", "plugin:ai_key", "打开当前AI服务商API Key页面"),
                ("apikey", "plugin:ai_key", "打开当前AI服务商API Key页面"),
                ("获取api", "plugin:ai_key", "打开当前AI服务商API Key页面"),
                ("接口文档", "plugin:ai_docs", "打开当前AI服务商接口文档"),
                ("模型列表", "plugin:ai_diagnose", "执行AI完整诊断并读取模型列表"),
                ("使用建议模型", "plugin:ai_use_model", "使用诊断建议模型"),
                ("附加最近日志", "plugin:ai_recent_log", "把最近插件日志附加到输入框"),
                ("最近日志", "plugin:ai_recent_log", "把最近插件日志附加到输入框"),
                ("附加场景摘要", "plugin:ai_scene", "把当前场景摘要附加到输入框"),
                ("场景摘要", "plugin:ai_scene", "把当前场景摘要附加到输入框"),
                ("pbr套装", "plugin:pbrset_tab", "切换到PBR贴图套装页面"),
                ("扫描当前材质", "plugin:scan_download_pbrset", "扫描下载队列中选择的当前材质"),
                ("贴图流送", "plugin:texture_tab", "切换到UE贴图流送页面"),
                ("ue贴图", "plugin:texture_tab", "切换到UE贴图流送页面"),
                ("复制失败链接", "plugin:copy_failed_links", "复制PBR下载失败链接"),
                ("重试", "plugin:retry_download", "重试PBR下载队列失败项/选择项"),
                ("打开目标文件夹", "plugin:open_download_target", "打开PBR下载目标文件夹"),
            ]
            seen_plugin = set()
            for key, value, label in keyword_actions:
                if key in low or key in base_text:
                    if value not in seen_plugin:
                        actions.append(dict(type="plugin", label=label, value=value))
                        seen_plugin.add(value)

            self.ai_smart_actions = actions
            if hasattr(self, "ai_action_combo"):
                self.ai_action_combo.clear()
                if actions:
                    self.ai_action_combo.addItems([a["label"] for a in actions])
                    self.ai_action_combo.setEnabled(True)
                    self.btn_ai_run_action.setEnabled(True)
                    self.btn_ai_open_url.setEnabled(any(a["type"] == "url" for a in actions))
                    self.btn_ai_copy_action_value.setEnabled(True)
                    self.log("AI已识别推荐操作：{} 项".format(len(actions)))
                else:
                    self.ai_action_combo.addItem("没有识别到可执行操作")
                    self.ai_action_combo.setEnabled(False)
                    self.btn_ai_run_action.setEnabled(False)
                    self.btn_ai_open_url.setEnabled(False)
                    self.btn_ai_copy_action_value.setEnabled(False)
                    self.log("AI聊天内容里没有识别到可执行操作")
        except Exception:
            self.log(status_text_for_exception("识别AI推荐操作失败"))

    def ai_selected_smart_action(self):
        try:
            idx = self.ai_action_combo.currentIndex()
            actions = getattr(self, "ai_smart_actions", [])
            if 0 <= idx < len(actions):
                return actions[idx]
        except Exception:
            pass
        return None

    def ai_open_first_url_action(self):
        try:
            for a in getattr(self, "ai_smart_actions", []):
                if a.get("type") == "url":
                    url = a.get("value", "")
                    if open_url_in_chrome_or_browser(url):
                        self.log("已打开AI识别网址：{}".format(url))
                    else:
                        self.log("打开网址失败：{}".format(url))
                    return
            self.log("没有可打开的网址")
        except Exception:
            self.log(status_text_for_exception("打开AI识别网址失败"))

    def ai_copy_action_value(self):
        try:
            a = self.ai_selected_smart_action()
            if not a:
                self.log("没有选择推荐操作")
                return
            QtWidgets.QApplication.clipboard().setText(safe_str(a.get("value", ""), ""))
            self.log("已复制推荐操作内容")
        except Exception:
            self.log(status_text_for_exception("复制推荐操作内容失败"))

    def ai_run_smart_action(self):
        try:
            a = self.ai_selected_smart_action()
            if not a:
                self.log("没有选择推荐操作")
                return
            typ = a.get("type", "")
            value = a.get("value", "")

            if typ == "url":
                if open_url_in_chrome_or_browser(value):
                    self.log("已打开网址：{}".format(value))
                else:
                    self.log("打开网址失败：{}".format(value))
                return

            if typ == "path":
                target = value
                if os.path.isfile(target):
                    target = os.path.dirname(target)
                elif not os.path.exists(target):
                    folder = os.path.dirname(target)
                    if folder and os.path.exists(folder):
                        target = folder
                if target and open_folder_in_os(target):
                    self.log("已打开路径：{}".format(target))
                else:
                    self.log("打开路径失败：{}".format(value))
                return

            if typ == "plugin":
                if value == "plugin:ai_diagnose":
                    self.ai_diagnose_connection()
                elif value == "plugin:ai_billing":
                    self.ai_open_provider_billing()
                elif value == "plugin:ai_key":
                    self.ai_open_key_page()
                elif value == "plugin:ai_docs":
                    self.ai_open_provider_docs()
                elif value == "plugin:ai_use_model":
                    self.ai_use_suggested_model()
                elif value == "plugin:ai_recent_log":
                    self.ai_insert_recent_log()
                elif value == "plugin:ai_scene":
                    self.ai_insert_scene_summary()
                elif value == "plugin:pbrset_tab":
                    self.tabs.setCurrentWidget(self.pbrset_tab)
                elif value == "plugin:texture_tab":
                    self.tabs.setCurrentWidget(self.texture_tab)
                elif value == "plugin:scan_download_pbrset":
                    self.scan_selected_pbr_download_as_pbrset()
                elif value == "plugin:copy_failed_links":
                    self.copy_failed_pbr_download_links()
                elif value == "plugin:retry_download":
                    self.retry_selected_pbr_downloads()
                elif value == "plugin:open_download_target":
                    self.open_selected_pbr_download_target()
                else:
                    self.log("未知插件推荐操作：{}".format(value))
                return

            self.log("未知推荐操作类型：{}".format(typ))
        except Exception:
            self.log(status_text_for_exception("执行AI推荐操作失败"))

    def ai_system_prompt(self):
        name = getattr(self, "_ai_robot_name", "AI小助手")
        base = (
            "你是{name}，一个专为3ds Max室内设计工作流服务的AI助手，同时也是场景管家。\n"
            "用中文回答。直接、准确。可以使用Markdown格式。\n\n"
            "【普通对话】直接回答，不要写任何脚本。\n\n"
            "【图片/修图/生成图相关】\n"
            "如果用户附加了图片，或者问题是在说改图片、修图、P图、加人、去人、换材质效果、"
            "替换背景、生成参考图、合成效果图、重绘局部、扩图、抠图、加美女、加植物、加摆件等，"
            "默认按图像理解与图像编辑需求处理，不要误写成3ds Max场景脚本。\n"
            "这类情况下应先结合图片内容回答：能怎么改、建议放在哪里、提示词怎么写、"
            "适合用哪类视觉模型或工作流；除非用户明确要求“给我3ds Max脚本/Max里落地命令”，否则不要写脚本。\n\n"
            "【脚本查询 — 仅在用户明确要求时使用】\n"
            "只有当用户明确要求查询场景数据（例如：'场景里有多少物体'、'列出所有灯光'、"
            "'帮我创建一个球'等明确的场景操作需求），才写脚本。\n"
            "闲聊、问候、提问软件用法等情况一律不写脚本，直接回答。\n\n"
            "【写脚本时的规则】\n"
            "优先用Python（print输出可以完整捕获）。通过 rt 访问场景：\n"
            "  rt.objects / rt.sceneMaterials / rt.lights / rt.cameras\n"
            "Python示例：\n"
            "```python\n"
            "count = rt.objects.count\n"
            "print('场景中有 ' + str(count) + ' 个物体')\n"
            "```\n"
            "MAXScript的print()无法捕获，如用MAXScript，最后一行必须是字符串表达式：\n"
            "```maxscript\n"
            "\"场景中有 \" + objects.count as string + \" 个物体\"\n"
            "```\n\n"
            "回答长度根据问题复杂度自行决定，不要截断。"
        ).format(name=name)
        script_rules = (
            "\n\n"
            "[SCRIPT QUALITY RULES]\n"
            "- Only output runnable code when the user explicitly asks for code or scene actions.\n"
            "- Prefer Python over MAXScript unless the user explicitly requests MAXScript.\n"
            "- Output exactly one code block when code is needed. Do not mix multiple alternative blocks.\n"
            "- Do not output pseudocode, placeholders, or incomplete snippets.\n"
            "- Use rt as the 3ds Max runtime object. Do not invent APIs.\n"
            "- In 3ds Max Python, do not use fake modules or APIs such as import maxscript, MaxPlus, rt.Objects.AddSphere, scene.add(), or bpy.\n"
            "- In 3ds Max Python, create objects with real runtime calls such as rt.sphere(radius=10) or other valid rt constructors/functions.\n"
            "- If using Python, make the code syntactically complete and use print(...) for user-visible results.\n"
            "- If using MAXScript, make the final expression return a readable string result.\n"
            "- Before writing code, minimize assumptions about object names, material classes, and selection state.\n"
            "- Never hard-code object names unless the user provided them or the code first checks they exist.\n"
            "- If the task depends on current selection, selection count, renderer, or material type, check that state in code first.\n"
            "- If critical information is missing, ask a concise question instead of guessing in code.\n"
            "- Do not include markdown explanation before or after the code block when code is requested.\n"
        )
        return base + script_rules

    def _ai_should_block_code_for_image_request(self, answer, user_text, images):
        answer = safe_str(answer, "")
        user_text = safe_str(user_text, "")
        images = list(images or [])
        low_user = user_text.lower()
        low_answer = answer.lower()
        image_intent_tokens = [
            "图片", "图像", "照片", "效果图", "修图", "p图", "改图", "看图", "识图",
            "加人", "加美女", "加植物", "加摆件", "去人", "去掉", "替换背景", "重绘",
            "扩图", "抠图", "局部重绘", "生成图", "出图", "参考图", "贴一张", "合成"
        ]
        explicit_script_tokens = [
            "maxscript", "python", "pymxs", "脚本", "代码", "3ds max里", "max里执行", "rt."
        ]
        has_image_intent = bool(images) or any(tok in user_text for tok in image_intent_tokens) or any(tok in low_user for tok in [x.lower() for x in image_intent_tokens])
        wants_script = any(tok in low_user for tok in [x.lower() for x in explicit_script_tokens])
        if not has_image_intent or wants_script:
            return False
        if "```" in answer:
            return True
        suspicious_answer_tokens = [
            "创建一个球", "create a sphere", "rt.sphere", "objects.addsphere",
            "import maxscript", "maxscript", "python脚本", "python script"
        ]
        return any(tok in low_answer for tok in suspicious_answer_tokens)

    def _ai_lang_matches_code(self, lang, body):
        lang = safe_str(lang, "").strip().lower()
        body = safe_str(body, "")
        low = body.lower()
        if lang == "python":
            mx_tokens = [
                "selection as array", "undefined", "format \"", "local ", "fn ",
                "for o in objects", " as string", "try(", "catch(", "$'"
            ]
            if any(tok in low for tok in mx_tokens):
                return False
        if lang == "maxscript":
            py_tokens = [
                "print(", "import ", "def ", "elif ", "except ", "rt.", "mxs.", "len(",
                "str(", "None", "True", "False", "__builtins__"
            ]
            if any(tok.lower() in low for tok in py_tokens):
                return False
        return True

    def ai_current_template_text(self):
        try:
            t = self.ai_template_combo.currentText()
            mapping = {
                "常规问题": "",
                "分析3ds Max报错": "请分析下面的3ds Max / MAXScript / Python报错，说明可能原因、影响范围、优先排查顺序和修复步骤：\n",
                "MAXScript报错分析": "请按MAXScript角度分析这个报错，指出可能是哪一行、哪个变量/命令/语法导致，并给出可复制的修复思路：\n",
                "Python/PySide报错分析": "请按3ds Max Python / PySide插件开发角度分析这个报错，说明原因和修复方案：\n",
                "插件使用指导": "请作为室内场景助手 Pro 使用顾问，告诉我这个操作应该怎么做，注意事项是什么：\n",
                "安装/工具栏/图标问题": "请分析3ds Max插件安装、工具栏、图标、帮助文件相关问题，给出排查步骤：\n",
                "PBR材质问题": "请分析这个PBR材质/贴图套装问题，说明通道、命名、法线DX/GL、粗糙度/光泽度等可能问题：\n",
                "PBR通道识别问题": "请分析为什么这些PBR贴图通道没有被正确识别或连接，给出命名、映射和手动修正建议：\n",
                "V-Ray材质问题": "请分析V-Ray材质创建/贴图槽连接问题，重点检查VRayMtl的diffuse、reflection、roughness/glossiness、bump/normal、displacement等槽位：\n",
                "Corona材质问题": "请分析Corona材质创建/贴图槽连接问题，重点检查CoronaPhysicalMtl/CoronaMtl的BaseColor、Roughness、Metalness、Bump/Normal等槽位：\n",
                "Physical/PBR材质问题": "请分析3ds Max Physical Material / PBR Material Metal/Rough的贴图连接问题，说明哪些通道应该连接到哪些槽：\n",
                "法线DX/GL判断": "请判断法线贴图应该使用DX还是GL，并说明在3ds Max、V-Ray、Corona、UE里需要注意什么：\n",
                "UE贴图流送问题": "请分析UE贴图流送相关问题，重点关注2幂尺寸、最大尺寸、贴图路径、输出目录和更新Max路径：\n",
                "UE导入前检查清单": "请给我一份3ds Max场景导入UE前检查清单，覆盖模型、命名、材质、贴图、灯光、相机、单位、路径：\n",
                "下载库问题": "请分析PBR下载库问题，重点关注下载链接、文件类型过滤、解压、冗余文件夹和材质库目录：\n",
                "贴图丢失/路径问题": "请分析3ds Max贴图丢失、路径失效、外部文件找不到的问题，给出修复和预防步骤：\n",
                "模型整理/重命名建议": "请给我一个室内场景模型整理和批量重命名建议，适合导入UE或交付团队：\n",
                "场景卡顿优化": "请分析3ds Max室内场景卡顿的常见原因，并给出从模型、材质、贴图、灯光、修改器、代理对象角度的优化步骤：\n",
                "写给客户/同事的说明": "请把下面的问题整理成一段给客户或同事看的说明，表达清楚、专业、不要太技术化：\n",
                "给我排查清单": "请给我一个按优先级排序的排查清单，每一步要能在3ds Max里执行：\n",
            }
            return mapping.get(t, "")
        except Exception:
            return ""

    def ai_collect_scene_summary(self):
        lines = []
        try:
            lines.append("Max文件：{}{}".format(safe_str(rt.maxFilePath, ""), safe_str(rt.maxFileName, "")))
        except Exception:
            pass
        try:
            lines.append("场景对象数：{}".format(len(list(rt.objects))))
        except Exception:
            pass
        try:
            lines.append("当前选择数：{}".format(len(list(rt.selection))))
        except Exception:
            pass
        try:
            lines.append("材质编辑器当前库/场景材质数量：{}".format(len(collect_scene_materials())))
        except Exception:
            pass
        try:
            lines.append("PBR套装列表数量：{}".format(len(getattr(self, "pbrset_cache", []) or [])))
        except Exception:
            pass
        try:
            lines.append("PBR下载队列数量：{}".format(len(getattr(self, "pbr_download_queue", []) or [])))
        except Exception:
            pass
        try:
            lines.append("UE贴图流送列表数量：{}".format(len(getattr(self, "texture_items", []) or [])))
        except Exception:
            pass
        try:
            lines.append("当前插件页：{}".format(self.tabs.tabText(self.tabs.currentIndex())))
        except Exception:
            pass
        return "\n".join(lines) if lines else "无法获取场景摘要"

    def ai_append_text_to_input(self, text):
        try:
            cur = self.ai_input.toPlainText().strip()
            if cur:
                cur += "\n\n"
            self.ai_input.setPlainText(cur + text)
            self.ai_input.moveCursor(QtGui.QTextCursor.End)
        except Exception:
            pass

    def ai_insert_scene_summary(self):
        self.ai_append_text_to_input("【当前场景摘要】\n" + self.ai_collect_scene_summary())

    def ai_insert_recent_log(self):
        try:
            log_text = self.log_edit.toPlainText()
            if len(log_text) > 5000:
                log_text = log_text[-5000:]
            self.ai_append_text_to_input("【最近插件日志】\n" + log_text)
        except Exception:
            self.ai_append_text_to_input("【最近插件日志】\n无法读取日志")

    def ai_clear_chat(self):
        self.ai_messages = []
        try:
            self.ai_render_chat()
        except Exception:
            pass
        self.log("AI对话已清空")

    def ai_build_messages(self, user_text):
        api_type = self.ai_api_type_combo.currentText() if hasattr(self, "ai_api_type_combo") else "OpenAI兼容"
        messages = [dict(role="system", content=self.ai_system_prompt())]
        try:
            keep = int(self.ai_history_spin.value())
        except Exception:
            keep = 6
        history = list(getattr(self, "ai_messages", []))[-max(0, keep):]
        for m in history:
            role = m.get("role", "")
            content = m.get("content", "")
            images = m.get("images", [])
            if role in ("user", "assistant"):
                messages.append(self.ai_build_api_message(role, content, images, api_type))
            elif role == "script_result":
                # Convert script results to user messages for API context
                messages.append(dict(role="user", content="[脚本执行结果]\n{}".format(content)))
            # skip display-only roles: system, script_running, etc.
        current_images = self.ai_selected_image_paths()
        last_history = history[-1] if history else {}
        last_role = safe_str(last_history.get("role", ""), "")
        last_content = safe_str(last_history.get("content", ""), "")
        last_images = list(last_history.get("images", []) or [])
        if not (last_role == "user" and last_content == safe_str(user_text, "") and last_images == current_images):
            messages.append(self.ai_build_api_message("user", user_text, current_images, api_type))
        return messages

    def ai_request(self, messages, test=False):
        cfg = dict(
            api_type=self.ai_api_type_combo.currentText() if hasattr(self, "ai_api_type_combo") else "OpenAI兼容",
            base_url=self.ai_base_url.text().strip() if hasattr(self, "ai_base_url") else "",
            api_key=self.ai_api_key.text().strip() if hasattr(self, "ai_api_key") else "",
            model=self.ai_model.text().strip() if hasattr(self, "ai_model") else "",
            temperature=float(self.ai_temperature.value()) if hasattr(self, "ai_temperature") else 0.3,
            provider=self.ai_provider_combo.currentText() if hasattr(self, "ai_provider_combo") else "OpenAI兼容接口"
        )
        result = self.ai_request_with_config(messages, cfg, test=test)
        try:
            self._ai_last_response_images = list(result.get("images", []) or [])
        except Exception:
            self._ai_last_response_images = []
        return result.get("text", "")

    def ai_request_with_config(self, messages, cfg, test=False):
        cfg = dict(cfg or {})
        api_type = safe_str(cfg.get("api_type", ""), "").strip()
        base_url = safe_str(cfg.get("base_url", ""), "").strip()
        api_key = safe_str(cfg.get("api_key", ""), "").strip()
        model = safe_str(cfg.get("model", ""), "").strip()
        provider = safe_str(cfg.get("provider", ""), "").strip() or "OpenAI兼容接口"
        temp = float(cfg.get("temperature", 0.3) or 0.3)
        if not model:
            raise RuntimeError("模型名为空")
        max_tokens = 8 if test else None
        if api_type.startswith("Ollama"):
            return dict(
                text=ai_ollama_request(base_url or "http://127.0.0.1:11434", model, messages, temperature=temp, timeout=120, max_tokens=max_tokens),
                images=[]
            )
        if not base_url:
            raise RuntimeError("Base URL为空")
        return ai_openai_compatible_request_full(
            base_url, api_key, model, messages,
            temperature=temp, timeout=120, max_tokens=max_tokens,
            provider=provider
        )

    def ai_set_diagnosis(self, lines):
        try:
            if isinstance(lines, list):
                text = "\n".join(lines)
            else:
                text = safe_str(lines, "")
            self.ai_diag_view.setPlainText(text)
        except Exception:
            self.log(safe_str(lines, ""))

    def ai_set_model_suggestions(self, model_ids):
        try:
            self.ai_last_model_suggestions = list(model_ids or [])
            if hasattr(self, "ai_model_suggest_combo"):
                self.ai_model_suggest_combo.clear()
                self.ai_model_suggest_combo.addItems(self.ai_last_model_suggestions)
                self.ai_model_suggest_combo.setEnabled(bool(self.ai_last_model_suggestions))
            if hasattr(self, "btn_ai_use_suggested_model"):
                self.btn_ai_use_suggested_model.setEnabled(bool(self.ai_last_model_suggestions))
        except Exception:
            pass

    def ai_fetch_model_list(self):
        try:
            api_type = self.ai_api_type_combo.currentText() if hasattr(self, "ai_api_type_combo") else "OpenAI兼容"
            base_url = self.ai_base_url.text().strip() if hasattr(self, "ai_base_url") else ""
            api_key = self.ai_api_key.text().strip() if hasattr(self, "ai_api_key") else ""
            if api_type.startswith("Ollama"):
                url = (base_url or "http://127.0.0.1:11434").rstrip("/") + "/api/tags"
                data, _raw = ai_http_get_json(url, "", timeout=20)
                ids = []
                for m in (data or {}).get("models", []):
                    if isinstance(m, dict) and (m.get("name") or m.get("model")):
                        ids.append(safe_str(m.get("name") or m.get("model"), ""))
            else:
                url = ai_openai_models_url(base_url)
                if not url:
                    raise RuntimeError("Base URL为空")
                data, _raw = ai_http_get_json(url, api_key, timeout=25)
                ids = ai_extract_model_ids_from_models_response(data or {})
            ids = sorted(list(dict.fromkeys([x for x in ids if x])))
            self.ai_set_model_suggestions(ids)
            if ids:
                try:
                    self.ai_model_suggest_combo.setCurrentIndex(0)
                except Exception:
                    pass
                self.log("已获取模型列表：{} 个。可在“可用模型”下拉框中切换。".format(len(ids)))
            else:
                self.log("没有从接口返回可用模型。")
        except Exception:
            self.log(status_text_for_exception("获取模型列表失败"))

    def ai_use_suggested_model(self):
        try:
            model = ""
            if hasattr(self, "ai_model_suggest_combo") and self.ai_model_suggest_combo.currentText():
                model = self.ai_model_suggest_combo.currentText()
            elif getattr(self, "ai_last_model_suggestions", []):
                model = self.ai_last_model_suggestions[0]
            if not model:
                self.log("没有可用的模型建议，请先点完整诊断")
                return
            self.ai_model.setText(model)
            self.ai_refresh_config_summary()
            self.log("已使用诊断建议模型：{}".format(model))
        except Exception:
            self.log(status_text_for_exception("使用建议模型失败"))

    def ai_on_suggested_model_changed(self, text):
        try:
            text = safe_str(text, "").strip()
            if not text:
                return
            if hasattr(self, "ai_model") and self.ai_model.text().strip() != text:
                self.ai_model.setText(text)
                self.ai_refresh_config_summary()
                self.log("已切换模型：{}".format(text))
        except Exception:
            pass

    def ai_diagnose_connection(self):
        """
        V75：完整诊断。
        目标不是只告诉"失败"，而是告诉用户到底卡在 Base URL、Key、模型名、限流还是接口格式。
        """
        lines = []
        try:
            provider = self.ai_provider_combo.currentText()
            api_type = self.ai_api_type_combo.currentText()
            base_url = self.ai_base_url.text().strip()
            api_key = self.ai_api_key.text().strip()
            model = self.ai_model.text().strip()
            info = self.ai_current_provider_info() if hasattr(self, "ai_current_provider_info") else {}

            lines.append("AI接口诊断")
            lines.append("=" * 42)
            lines.append("方案：{}".format(provider))
            lines.append("接口类型：{}".format(api_type))
            lines.append("Base URL：{}".format(base_url or "空"))
            lines.append("模型名：{}".format(model or "空"))
            lines.append("API Key：{}".format("已填写，长度{}，{}".format(len(api_key), ai_mask_key(api_key)) if api_key else "未填写"))
            lines.append("方案说明：{}".format(info.get("note", "")))
            lines.append("")

            if not model:
                lines.append("❌ 模型名为空。请填写模型名。")
                self.ai_set_diagnosis(lines)
                return
            if info.get("need_key", True) and not api_key and not api_type.startswith("Ollama"):
                lines.append("❌ 当前方案需要 API Key，但没有填写。")
                lines.append("处理：点\"获取API Key\"，复制后填入。")
                self.ai_set_diagnosis(lines)
                return

            self.set_status("AI完整诊断中……")
            QtWidgets.QApplication.processEvents()

            if api_type.startswith("Ollama"):
                base = base_url or "http://127.0.0.1:11434"
                tags_url = base.rstrip("/") + "/api/tags"
                chat_url = base.rstrip("/") + "/api/chat"
                lines.append("Ollama tags地址：{}".format(tags_url))
                lines.append("Ollama chat地址：{}".format(chat_url))
                lines.append("")

                try:
                    data, raw = ai_http_get_json(tags_url, "", timeout=20)
                    names = []
                    try:
                        for m in data.get("models", []):
                            if m.get("name"):
                                names.append(m.get("name"))
                    except Exception:
                        pass
                    lines.append("✅ Ollama服务可访问。已安装模型数：{}".format(len(names)))
                    if names:
                        lines.append("模型示例：{}".format(", ".join(names[:12])))
                        if model not in names:
                            lines.append("⚠ 当前模型名不在 /api/tags 列表里。请检查是否已运行：ollama pull {}".format(model))
                    else:
                        lines.append("⚠ 没有读取到模型列表，请确认Ollama里已安装模型。")
                except Exception as e:
                    lines.append("❌ Ollama服务不可访问或 /api/tags 失败：{}".format(e))
                    lines.append("处理：确认Ollama正在运行；浏览器打开 http://127.0.0.1:11434 应该有响应。")
                    self.ai_set_diagnosis(lines)
                    return

                try:
                    ans = ai_ollama_request(base, model, [
                        dict(role="system", content="你只需要回复 OK。"),
                        dict(role="user", content="请回复 OK")
                    ], temperature=0.0, timeout=60, max_tokens=8)
                    lines.append("")
                    lines.append("✅ Ollama聊天接口测试成功：{}".format(safe_str(ans, "").strip()[:300]))
                except Exception as e:
                    lines.append("")
                    lines.append("❌ Ollama聊天接口测试失败：{}".format(e))
                    lines.append("处理：检查模型是否已pull，模型名是否完全一致。")

                self.ai_set_diagnosis(lines)
                self.log("AI完整诊断完成")
                return

            # OpenAI兼容诊断
            chat_url = ai_openai_chat_url(base_url)
            models_url = ai_openai_models_url(base_url)
            lines.append("最终 Chat Completions 地址：{}".format(chat_url or "空"))
            lines.append("模型列表地址：{}".format(models_url or "空"))
            lines.append("")

            if not chat_url:
                lines.append("❌ Base URL为空。")
                self.ai_set_diagnosis(lines)
                return

            # Step 1: /models 检测。不是所有平台都开放 /models，所以失败不直接终止。
            model_list_ok = False
            try:
                data, raw = ai_http_get_json(models_url, api_key, timeout=35)
                ids = ai_extract_model_ids_from_models_response(data or {})
                model_list_ok = True
                self.ai_set_model_suggestions(ids)
                lines.append("✅ /models 可访问。模型数：{}".format(len(ids)))
                if ids:
                    self.ai_set_model_suggestions(ids)
                    lines.append("模型示例：{}".format(", ".join(ids[:20])))
                    if model in ids:
                        lines.append("✅ 当前模型名在模型列表中。")
                    else:
                        lines.append("⚠ 当前模型名没有出现在 /models 返回中。建议先改用列表中的模型。")
                        lines.append("可用建议模型：{}".format(", ".join(ids[:8])))
                        lines.append("处理：点\"使用建议模型\"，或手动把模型名改成上面任意一个。")
            except urllib.error.HTTPError as e:
                msg = ai_friendly_http_error(e, provider=provider, model=model)
                if getattr(e, "code", None) == 404:
                    lines.append("⚠ /models 返回404。部分OpenAI兼容平台不开放模型列表，可继续测试聊天接口。")
                else:
                    lines.append("⚠ /models 检测失败：")
                    lines.append(msg)
            except urllib.error.URLError as e:
                lines.append("❌ 无法连接模型列表接口：{}".format(ai_friendly_url_error(e)))
            except Exception as e:
                lines.append("⚠ /models 检测异常：{}".format(e))

            lines.append("")
            lines.append("开始测试聊天接口，使用 max_tokens=8，避免浪费额度……")
            QtWidgets.QApplication.processEvents()

            try:
                ans = ai_openai_compatible_request(base_url, api_key, model, [
                    dict(role="system", content="你只需要回复 OK。"),
                    dict(role="user", content="请回复 OK")
                ], temperature=0.0, timeout=60, max_tokens=8, provider=provider)
                lines.append("✅ 聊天接口测试成功：{}".format(safe_str(ans, "").strip()[:500]))
                lines.append("")
                lines.append("诊断结论：当前配置可用。")
            except Exception as e:
                lines.append("❌ 聊天接口测试失败：")
                lines.append(safe_str(e, ""))
                lines.append("")
                err_text = safe_str(e, "").lower()
                if "insufficient_quota" in err_text or "exceeded your current quota" in err_text or "配额/余额不足" in err_text:
                    lines.append("诊断结论：接口配置基本可达，但当前项目/API Key没有可用额度。请检查账单/用量或换一个有额度的Key。")
                else:
                    lines.append("诊断结论：配置暂不可用。优先检查：API Key、Base URL、模型名、额度/限流。")

            self.ai_set_diagnosis(lines)
            self.log("AI完整诊断完成")
        except Exception as e:
            msg = "AI完整诊断失败：{}".format(e)
            self.ai_set_diagnosis(msg)
            self.log(msg)
        finally:
            self.set_status("AI诊断完成")

    def ai_test_connection(self):
        try:
            self.set_status("AI连接测试中……")
            QtWidgets.QApplication.processEvents()
            messages = [
                dict(role="system", content="你只需要回复 OK。"),
                dict(role="user", content="请回复 OK")
            ]
            ans = self.ai_request(messages, test=True)
            self.log("AI连接测试成功：{}".format(safe_str(ans, "").strip()[:120]))
            try:
                QtWidgets.QMessageBox.information(self, "AI连接测试", "连接成功：\n{}".format(safe_str(ans, "").strip()[:500]))
            except Exception:
                pass
        except Exception as e:
            msg = safe_str(e, "")
            self.log("AI连接测试失败：{}".format(msg))
            try:
                QtWidgets.QMessageBox.warning(self, "AI连接测试失败", (msg + "\n\n建议：点击\"完整诊断\"，插件会检查最终URL、Key、模型名、/models接口和聊天接口。")[:1800])
            except Exception:
                pass
        finally:
            self.set_status("AI连接测试完成")

    def ai_send_message(self):
        try:
            raw = self.ai_input.toPlainText().strip()
            prefix = self.ai_current_template_text()
            mode_prefix = self.ai_image_edit_mode_prefix()
            user_text = (mode_prefix + prefix + raw).strip()
            if not user_text:
                self.log("AI问题为空")
                return

            if not hasattr(self, "ai_messages"):
                self.ai_messages = []

            self.ai_input.clear()
            if hasattr(self, "ai_popup_input") and self.ai_popup_input:
                self.ai_popup_input.clear()
            pending_images = self.ai_selected_image_paths()
            self.ai_messages.append(dict(role="user", content=user_text, images=pending_images))
            self.ai_messages.append(dict(role="ai_thinking", content="正在理解图片并思考…" if pending_images else "正在思考…"))
            self.ai_render_chat()
            self.set_status("AI正在生成回答……")
            QtWidgets.QApplication.processEvents()

            messages = [dict(role="system", content=self.ai_system_prompt())]
            try:
                keep = int(self.ai_history_spin.value())
            except Exception:
                keep = 6
            history = list(getattr(self, "ai_messages", []))[-max(0, keep):]
            send_report = None
            api_type = self.ai_api_type_combo.currentText() if hasattr(self, "ai_api_type_combo") else "OpenAI兼容"
            for idx, m in enumerate(history):
                role = m.get("role", "")
                content = m.get("content", "")
                images = m.get("images", [])
                is_last_user = (idx == len(history) - 1 and role == "user")
                if role in ("user", "assistant"):
                    if is_last_user:
                        msg, send_report = self.ai_build_api_message_with_report(role, content, images, api_type)
                        messages.append(msg)
                    else:
                        messages.append(self.ai_build_api_message(role, content, images, api_type))
                elif role == "script_result":
                    messages.append(dict(role="user", content="[脚本执行结果]\n{}".format(content)))
            debug_lines = self.ai_collect_request_debug_lines(messages, send_report=send_report)
            for line in debug_lines:
                self.ai_append_diagnosis_line(line)
            try:
                self.log("；".join(debug_lines[:4]))
            except Exception:
                pass
            try:
                if send_report and int(send_report.get("attached", 0) or 0) > 0:
                    self.set_status("正在上传图片并等待模型返回…")
                    QtWidgets.QApplication.processEvents()
            except Exception:
                pass
            try:
                self._ai_last_response_images = []
            except Exception:
                pass
            answer = self.ai_request(messages)
            answer = safe_str(answer, "").strip() or "AI没有返回内容"
            response_images = list(getattr(self, "_ai_last_response_images", []) or [])
            self.ai_remove_pending_thinking_message()
            self.ai_messages.append(dict(role="assistant", content=answer, images=response_images))
            self.ai_clear_images()
            self.ai_render_chat()
            QtWidgets.QApplication.processEvents()
            report_text = self.ai_describe_image_send_report(send_report)
            if report_text:
                self.log(report_text)
                self.set_status(report_text)
                self.ai_append_diagnosis_line(report_text)
            if response_images:
                self.log("AI返回了 {} 张图片，已接收到聊天窗口。".format(len(response_images)))
                self.ai_append_diagnosis_line("AI返图接收：已保存 {} 张图片到临时目录，并显示在聊天中。".format(len(response_images)))

            if self._ai_should_block_code_for_image_request(answer, user_text, pending_images):
                self.log("当前请求更像图片编辑/看图需求，已拦截脚本执行倾向。")
                self.ai_append_diagnosis_line("图片意图识别：本轮检测为图片编辑/图像理解需求，不自动走3ds Max脚本执行。")
                if "```" in answer:
                    repair = ""
                    try:
                        repair = self.ai_request([
                            dict(role="system", content=self.ai_system_prompt()),
                            dict(role="user", content=(
                                "用户这次是图片编辑/看图需求，不是让你写3ds Max脚本。\n"
                                "你上一条回答跑偏了，误写成了脚本或建模命令。\n"
                                "请重新回答，并严格遵守：\n"
                                "1. 不要输出任何代码块。\n"
                                "2. 不要建议创建球体、场景物体之类无关操作。\n"
                                "3. 结合用户图片内容，直接回答如何改图、怎么合成、人物应放哪里、提示词怎么写，或说明需要什么素材。\n"
                                "4. 如果用户其实想在 3ds Max 里落地，也先用中文说明思路，再问是否需要脚本。\n\n"
                                "用户原问题：\n{}"
                            ).format(user_text))
                        ])
                    except Exception:
                        repair = ""
                    repair = safe_str(repair, "").strip()
                    if repair:
                        self.ai_messages.append(dict(role="assistant", content=repair))
                        self.ai_render_chat()
                        QtWidgets.QApplication.processEvents()
                        self.log("AI已按图片编辑需求重新回答")
                return

            # Detect code block and auto-run the last runnable block.
            _blocks = self._ai_extract_code_blocks_from_text(answer)
            if _blocks:
                _code, _lang = _blocks[-1]
                if _code:
                    try:
                        self.btn_ai_run_script.setEnabled(False)
                    except Exception:
                        pass
                    if len(_blocks) > 1:
                        self.log("AI返回了多个代码块，已自动采用最后一个可执行代码块。")
                    else:
                        self.log("AI返回了可执行代码块，正在自动执行。")
                    if self.ai_image_edit_mode():
                        self.log("当前处于图片编辑模式，已禁止自动执行脚本。")
                        self.ai_append_diagnosis_line("图片编辑模式：已拦截本轮自动脚本执行。")
                        return
                    self._ai_auto_exec_and_interpret(_code, _lang)
                    return
            self.log("AI回答完成")
            try:
                self.btn_ai_run_script.setEnabled(False)
            except Exception:
                pass
        except Exception as e:
            msg = safe_str(e, "")
            self.log("AI请求失败：{}".format(msg))
            self.ai_remove_pending_thinking_message()
            try:
                self.ai_append_chat_message("system", "AI请求失败：\n{}".format(msg))
            except Exception:
                pass
        finally:
            self.set_status("等待操作")

    def ai_copy_last_answer(self):
        try:
            for m in reversed(getattr(self, "ai_messages", [])):
                if m.get("role") == "assistant":
                    QtWidgets.QApplication.clipboard().setText(m.get("content", ""))
                    self.log("已复制AI最后一条回答")
                    return
            self.log("没有可复制的AI回答")
        except Exception:
            self.log(status_text_for_exception("复制AI回答失败"))

    def ai_copy_last_script_result(self):
        try:
            for m in reversed(getattr(self, "ai_messages", [])):
                if m.get("role") == "script_result":
                    QtWidgets.QApplication.clipboard().setText(m.get("content", ""))
                    self.log("已复制最后一条脚本执行结果")
                    return
            self.log("没有可复制的脚本执行结果")
        except Exception:
            self.log(status_text_for_exception("复制脚本执行结果失败"))

    def _ai_extract_code_blocks_from_text(self, content):
        """Return a list of non-empty code blocks as (code, lang)."""
        import re
        blocks = []
        for lang, code in re.findall(r"```(\w*)\s*\n?(.*?)```", safe_str(content, ""), re.DOTALL):
            code = safe_str(code, "").strip()
            if not code:
                continue
            blocks.append((code, self._ai_guess_code_lang(lang, code)))
        return blocks

    def ai_extract_last_code_block(self):
        """Return (code, lang) from last AI message, or (None, None)."""
        for m in reversed(getattr(self, "ai_messages", [])):
            if m.get("role") == "assistant":
                blocks = self._ai_extract_code_blocks_from_text(m.get("content", ""))
                if blocks:
                    return blocks[-1]
        return None, None

    def _ai_guess_code_lang(self, lang, code):
        guessed = (lang or "").lower().strip()
        body = safe_str(code, "")
        if guessed in ("python", "py", "maxscript", "mxs", "ms", "max"):
            return "python" if guessed in ("python", "py") else "maxscript"
        probe = body.lower()
        if "import " in probe or "print(" in probe or "rt." in probe or "mxs." in probe:
            return "python"
        if " as string" in probe or "format " in probe or "fn " in probe:
            return "maxscript"
        return "python"

    def _ai_preflight_code(self, code, lang):
        body = safe_str(code, "").strip()
        if not body:
            return False, lang, ["Empty code block."]
        if "```" in body:
            return False, lang, ["Markdown fence was included inside the code block."]
        low = body.lower()
        risky_patterns = [
            ("while true", "Detected 'while True'. This may freeze 3ds Max."),
            ("while(true)", "Detected 'while(True)'. This may freeze 3ds Max."),
            ("while (true)", "Detected 'while (true)'. This may freeze 3ds Max."),
            ("for (;;)", "Detected 'for (;;)' infinite loop pattern."),
            ("time.sleep(", "Detected time.sleep(...). Long waits may make 3ds Max look frozen."),
            ("sleep(", "Detected sleep(...). Long waits may make 3ds Max look frozen."),
        ]
        for token, issue in risky_patterns:
            if token in low:
                return False, lang, [issue]
        if len(body) > 12000:
            return False, lang, ["Code block is too large for safe auto-execution."]

        normalized_lang = self._ai_guess_code_lang(lang, body)
        issues = []
        if not self._ai_lang_matches_code(normalized_lang, body):
            issues.append("Code content does not match declared/extracted language: {}".format(normalized_lang))
            return False, normalized_lang, issues

        invalid_python_tokens = [
            ("import maxscript", "Detected invalid 3ds Max Python import: import maxscript."),
            ("from maxscript", "Detected invalid 3ds Max Python import style."),
            ("maxplus", "Detected deprecated/unsupported MaxPlus style API."),
            ("rt.objects.", "Detected suspicious rt.objects attribute method call; this is usually not a valid constructor API."),
            ("objects.addsphere", "Detected invented constructor call: Objects.AddSphere."),
            ("addsphere(", "Detected invented AddSphere-style API."),
            ("scene.add(", "Detected non-3ds-Max scene.add(...) API."),
            ("bpy.", "Detected Blender API inside 3ds Max script."),
            ("cmds.", "Detected Maya cmds API inside 3ds Max script."),
        ]
        if normalized_lang == "python":
            low = body.lower()
            for token, issue in invalid_python_tokens:
                if token in low:
                    issues.append(issue)
            if issues:
                return False, normalized_lang, issues

        if normalized_lang == "python":
            try:
                compile(body, "<ai_preflight>", "exec")
            except Exception as e:
                issues.append("Python syntax check failed: {}".format(e))
        else:
            pairs = {"(": ")", "[": "]", "{": "}"}
            closing = {v: k for k, v in pairs.items()}
            stack = []
            in_string = False
            quote_char = ""
            escaped = False
            for ch in body:
                if in_string:
                    if escaped:
                        escaped = False
                    elif ch == "\\":
                        escaped = True
                    elif ch == quote_char:
                        in_string = False
                    continue
                if ch in ("'", '"'):
                    in_string = True
                    quote_char = ch
                    continue
                if ch in pairs:
                    stack.append(ch)
                elif ch in closing:
                    if not stack or stack[-1] != closing[ch]:
                        issues.append("MAXScript bracket balance check failed.")
                        break
                    stack.pop()
            if not issues and (in_string or stack):
                issues.append("MAXScript string or bracket balance check failed.")

        return len(issues) == 0, normalized_lang, issues

    def _ai_preflight_issue_text(self, issues):
        issues = [safe_str(x, "").strip() for x in list(issues or []) if safe_str(x, "").strip()]
        if not issues:
            return ""
        cn = []
        for issue in issues:
            low = issue.lower()
            if "does not match declared" in low:
                cn.append("脚本语言判断不对，当前代码内容和执行语言不匹配。")
            elif "invalid 3ds max python import" in low or "import style" in low:
                cn.append("这段 Python 里用了不对的 3ds Max 模块导入方式。应直接使用当前环境里的 rt，而不是 import maxscript 这类假写法。")
            elif "invented constructor" in low or "addsphere-style" in low or "suspicious rt.objects attribute method call" in low:
                cn.append("这段 Python 里用了编造出来的 3ds Max API。创建物体不能写成 Objects.AddSphere 这种形式。")
            elif "scene.add" in low:
                cn.append("这段代码混入了别的软件脚本接口，不是 3ds Max Python。")
            elif "blender api" in low:
                cn.append("这段代码混入了 Blender 的 bpy 接口，不是 3ds Max Python。")
            elif "maya cmds api" in low:
                cn.append("这段代码混入了 Maya 的 cmds 接口，不是 3ds Max Python。")
            elif "python syntax check failed" in low:
                cn.append("Python语法检查没有通过。")
            elif "maxscript bracket balance check failed" in low or "maxscript string or bracket balance check failed" in low:
                cn.append("MAXScript 括号或字符串结构不完整。")
            elif "while true" in low or "freeze" in low or "sleep" in low:
                cn.append("脚本里有可能让 3ds Max 卡住的循环或等待。")
            elif "too large" in low:
                cn.append("脚本过长，当前不适合直接自动执行。")
            else:
                cn.append(issue)
        return "\n".join(cn)

    def _wrap_mxs_capture(self, code):
        """Wrap MAXScript in a block that intercepts print() via StringStream."""
        # Shadow the built-in print with a local fn that writes to a StringStream.
        # If any output is captured, return it; otherwise let the block's natural
        # last-expression value fall through to the caller.
        return (
            "(\n"
            "local __out = StringStream \"\"\n"
            "local print = fn print x = (format \"%\\n\" (x as string) to:__out; true)\n"
            + code + "\n"
            "local __s = trimRight (__out as string)\n"
            "if (__s == \"\") then undefined else __s\n"
            ")"
        )

    def _ai_exec_code(self, code, lang):
        """Execute code string, return captured output or return value as str."""
        try:
            if lang in ("maxscript", "mxs", "ms", "max"):
                # 1st attempt: wrap to capture print() output
                try:
                    captured = rt.execute(self._wrap_mxs_capture(code))
                    if captured is not None:
                        text = str(captured).strip()
                        if text:
                            return text
                except Exception:
                    pass
                # 2nd attempt: direct execution — get last expression value
                ret = rt.execute(code)
                if ret is None:
                    return "（执行完成，无返回值）"
                if ret is True:
                    # True almost always means print() was used; output went to Listener only
                    return (
                        "（执行完成，但print输出只能在MAXScript Listener窗口看到。\n"
                        "提示：下次改用Python脚本并用print()输出，结果会在这里完整显示。）"
                    )
                return str(ret)
            else:
                # Python — redirect stdout to capture ALL print() output
                import io, sys as _sys
                buf = io.StringIO()
                old_out = _sys.stdout
                _sys.stdout = buf
                exec_globals = {
                    "rt": rt, "mxs": rt, "plugin": self,
                    "__builtins__": __builtins__,
                }
                try:
                    exec(compile(code, "<ai_script>", "exec"), exec_globals)
                finally:
                    _sys.stdout = old_out
                output = buf.getvalue()
                # Also collect any non-None local results
                result_val = exec_globals.get("_result", None)
                if output.strip():
                    return output.strip()
                if result_val is not None:
                    return str(result_val)
                return "（执行完成，无输出）"
        except Exception as e:
            return "[错误] {}".format(e)

    def _ai_request_script_repair(self, code, lang, failure_text):
        try:
            keep = 10
            try:
                keep = int(self.ai_history_spin.value())
            except Exception:
                pass
            api_msgs = [dict(role="system", content=self.ai_system_prompt())]
            for m in list(getattr(self, "ai_messages", []))[-max(0, keep):]:
                role = m.get("role", "")
                content = m.get("content", "")
                if role in ("user", "assistant"):
                    api_msgs.append(dict(role=role, content=content))
            api_msgs.append(dict(
                role="user",
                content=(
                    "[脚本执行失败]\n"
                    "语言：{lang}\n"
                    "原代码：\n```{fence}\n{code}\n```\n"
                    "失败信息：\n{failure}\n\n"
                    "请直接给出更稳妥的新方案：\n"
                    "1. 先用简洁中文说明失败原因。\n"
                    "2. 先判断这里该用 Python 还是 MAXScript；不要把两种语言混用。\n"
                    "3. 如果能修复，就只给一个可运行代码块。\n"
                    "4. Python 必须通过 rt 访问 3ds Max，不要编造 API；要用 print(...) 输出结果。\n"
                    "5. 不要使用 import maxscript、MaxPlus、rt.Objects.AddSphere、scene.add、bpy、cmds 等错误接口名。\n"
                    "6. MAXScript 最后一行必须返回可读字符串。\n"
                    "7. 如果关键信息缺失，就先提一个最关键的问题，不要瞎猜对象名或选择状态。"
                ).format(
                    lang=lang or "python",
                    fence="python" if (lang or "").lower().startswith("py") else "maxscript",
                    code=safe_str(code, "").strip(),
                    failure=safe_str(failure_text, "").strip()
                )
            ))
            return safe_str(self.ai_request(api_msgs), "").strip()
        except Exception as e:
            self.log("AI修正方案生成失败：{}".format(e))
            return ""

    def ai_retry_last_script_fix(self):
        try:
            failure_text = ""
            for m in reversed(getattr(self, "ai_messages", [])):
                if m.get("role") == "script_result":
                    failure_text = safe_str(m.get("content", ""), "").strip()
                    if failure_text:
                        break
            code, lang = self.ai_extract_last_code_block()
            if not code:
                self.log("没有可重试修复的代码块")
                return
            if not failure_text:
                failure_text = "用户要求基于上一轮代码继续优化或修复。"
            self.set_status("AI正在重新修正脚本…")
            QtWidgets.QApplication.processEvents()
            repair = self._ai_request_script_repair(code, lang, failure_text)
            if repair:
                self.ai_messages.append(dict(role="assistant", content=repair))
                self.ai_render_chat()
                QtWidgets.QApplication.processEvents()
                self.log("AI已基于最近一次结果重新给出修正方案")
            else:
                self.log("AI没有返回新的修正方案")
        except Exception:
            self.log(status_text_for_exception("重新请求AI修复失败"))
        finally:
            self.set_status("等待操作")

    def _ai_auto_exec_and_interpret(self, code, lang):
        """Run code block from AI, then send result back to AI for interpretation."""
        # Show executing placeholder
        self.ai_messages.append(dict(role="script_running", content="正在执行脚本…"))
        self.ai_render_chat()
        QtWidgets.QApplication.processEvents()

        ok, lang, issues = self._ai_preflight_code(code, lang)
        if not ok:
            failure_text = "[Preflight blocked execution]\n{}".format("\n".join(issues))
            if self.ai_messages and self.ai_messages[-1].get("role") == "script_running":
                self.ai_messages.pop()
            self.ai_render_chat()
            QtWidgets.QApplication.processEvents()
            repair = self._ai_request_script_repair(code, lang, failure_text)
            if repair:
                self.ai_messages.append(dict(role="assistant", content=repair))
                self.ai_render_chat()
                QtWidgets.QApplication.processEvents()
                self.log("AI脚本预检未通过，已自动请求更稳的新方案")
            else:
                self.ai_messages.append(dict(
                    role="script_result",
                    content="脚本预检未通过，已拦截执行：\n{}".format(self._ai_preflight_issue_text(issues) or "\n".join(issues))
                ))
                self.ai_render_chat()
                QtWidgets.QApplication.processEvents()
            return

        result = self._ai_exec_code(code, lang)

        # Replace placeholder with actual result
        if self.ai_messages and self.ai_messages[-1].get("role") == "script_running":
            self.ai_messages.pop()
        self.ai_messages.append(dict(role="script_result", content=result))
        self.ai_render_chat()
        QtWidgets.QApplication.processEvents()

        if safe_str(result, "").startswith("[错误]"):
            repair = self._ai_request_script_repair(code, lang, result)
            if repair:
                self.ai_messages.append(dict(role="assistant", content=repair))
                self.ai_render_chat()
                QtWidgets.QApplication.processEvents()
            self.log("AI脚本执行失败，已请求新方案")
            self.set_status("等待操作")
            return

        # Second AI call: interpret the result
        self.set_status("AI正在解读脚本结果…")
        QtWidgets.QApplication.processEvents()
        try:
            keep = 10
            try:
                keep = int(self.ai_history_spin.value())
            except Exception:
                pass
            api_msgs = [dict(role="system", content=self.ai_system_prompt())]
            for m in list(self.ai_messages)[-max(0, keep):]:
                role = m.get("role", "")
                content = m.get("content", "")
                if role in ("user", "assistant"):
                    api_msgs.append(dict(role=role, content=content))
                elif role == "script_result":
                    api_msgs.append(dict(role="user",
                        content="[脚本执行结果]\n{}\n\n请根据以上结果，用简洁的中文直接回答用户的问题。".format(content)))
            interp = self.ai_request(api_msgs)
            interp = safe_str(interp, "").strip() or "（AI无法解读结果）"
            self.ai_messages.append(dict(role="assistant", content=interp))
            self.ai_render_chat()
            QtWidgets.QApplication.processEvents()
        except Exception as e:
            self.log("AI解读失败：{}".format(e))
        self.log("AI脚本执行完成")
        self.set_status("等待操作")

    def ai_run_last_code_block(self):
        """Manually execute last code block from AI response (triggered by button)."""
        code, lang = self.ai_extract_last_code_block()
        if not code:
            self.ai_messages.append(dict(role="script_result", content="未找到可执行的代码块。"))
            self.ai_render_chat()
            QtWidgets.QApplication.processEvents()
            return
        ok, lang, issues = self._ai_preflight_code(code, lang)
        if not ok:
            self.ai_messages.append(dict(
                role="script_result",
                content="[Preflight blocked execution]\n{}".format("\n".join(issues))
            ))
            self.ai_render_chat()
            QtWidgets.QApplication.processEvents()
            try:
                self.btn_ai_run_script.setEnabled(True)
            except Exception:
                pass
            return
        try:
            self.btn_ai_run_script.setEnabled(False)
        except Exception:
            pass
        self._ai_auto_exec_and_interpret(code, lang)

    def ai_open_config_help(self):
        msg = (
            "AI小助手接入方式：\n\n"
            "1. Ollama本地：先在电脑安装并运行Ollama，Base URL通常是 http://127.0.0.1:11434，模型名例如 qwen2.5:7b。\n"
            "2. LM Studio本地：在LM Studio里开启Local Server，Base URL通常是 http://127.0.0.1:1234/v1，模型名按LM Studio显示填写。\n"
            "3. OpenAI兼容接口：填服务商提供的 Base URL、API Key、模型名。很多免费额度平台都提供OpenAI兼容格式。\n\n"
            "API Key如果勾选保存，会明文写入插件配置文件；不勾选则只在当前会话里使用。"
        )
        try:
            QtWidgets.QMessageBox.information(self, "AI接入说明", msg)
        except Exception:
            self.log(msg)

    def build_ai_tab(self):
        main = QtWidgets.QVBoxLayout(self.ai_tab)
        main.setContentsMargins(12, 12, 12, 12)
        main.setSpacing(10)

        # ---------------- AI 配置区：重新排版 ----------------
        cfg_box = self.card("AI接口配置 · 选择方案 → 获取Key → 激活使用")
        self.ai_config_card = cfg_box
        cfg_shell = QtWidgets.QVBoxLayout(cfg_box)
        cfg_shell.setContentsMargins(12, 12, 12, 12)
        cfg_shell.setSpacing(8)
        self.ai_cfg_body = QtWidgets.QWidget()
        cfg_shell.addWidget(self.ai_cfg_body)
        cfg = QtWidgets.QGridLayout(self.ai_cfg_body)
        cfg.setContentsMargins(0, 0, 0, 0)
        cfg.setHorizontalSpacing(10)
        cfg.setVerticalSpacing(8)

        self.ai_provider_configs = {}
        self.ai_active_provider_name = ""
        self.ai_provider_combo = QtWidgets.QComboBox()
        self.ai_provider_combo.addItems(ai_provider_names())
        self.ai_provider_combo.setMinimumWidth(230)
        self.ai_provider_combo.currentIndexChanged.connect(self.ai_on_provider_changed)

        self.ai_api_type_combo = QtWidgets.QComboBox()
        self.ai_api_type_combo.addItems(["OpenAI兼容", "Ollama /api/chat"])
        self.ai_api_type_combo.setMinimumWidth(130)

        self.ai_cost_type_label = QtWidgets.QLabel("")
        self.ai_cost_type_label.setObjectName("statusGood")
        self.ai_cost_type_label.setWordWrap(True)
        self.ai_cost_type_label.setMinimumWidth(260)

        self.ai_base_url = QtWidgets.QLineEdit("http://127.0.0.1:11434")
        self.ai_base_url.setPlaceholderText("例如 http://127.0.0.1:11434 / http://127.0.0.1:1234/v1 / 服务商URL")
        self.ai_model = QtWidgets.QLineEdit("qwen2.5:7b")
        self.ai_model.setPlaceholderText("模型名，例如 qwen2.5:7b / local-model / 服务商模型名")
        self.ai_api_key = QtWidgets.QLineEdit()
        self.ai_api_key.setEchoMode(QtWidgets.QLineEdit.Password)
        self.ai_api_key.setPlaceholderText("API Key；本地Ollama/LM Studio通常留空")
        self.ai_save_key_chk = QtWidgets.QCheckBox("保存Key到配置（明文，谨慎）")

        self.ai_temperature = QtWidgets.QDoubleSpinBox()
        self.ai_temperature.setRange(0.0, 2.0)
        self.ai_temperature.setSingleStep(0.1)
        self.ai_temperature.setValue(0.3)
        self.ai_temperature.setFixedWidth(76)
        self.ai_history_spin = QtWidgets.QSpinBox()
        self.ai_history_spin.setRange(0, 20)
        self.ai_history_spin.setValue(8)
        self.ai_history_spin.setFixedWidth(76)

        self.btn_ai_get_key = QtWidgets.QPushButton("获取API Key")
        self.btn_ai_get_key.setObjectName("primaryButton")
        self.btn_ai_get_key.clicked.connect(self.ai_open_key_page)
        self.btn_ai_activate = QtWidgets.QPushButton("激活使用")
        self.btn_ai_activate.setObjectName("primaryButton")
        self.btn_ai_activate.clicked.connect(self.ai_activate_provider)
        self.btn_ai_diagnose = QtWidgets.QPushButton("完整诊断")
        self.btn_ai_diagnose.setObjectName("primaryButton")
        self.btn_ai_diagnose.clicked.connect(self.ai_diagnose_connection)
        self.btn_ai_test = QtWidgets.QPushButton("测试连接")
        self.btn_ai_test.clicked.connect(self.ai_test_connection)
        self.btn_ai_fetch_models = QtWidgets.QPushButton("获取模型")
        self.btn_ai_fetch_models.clicked.connect(self.ai_fetch_model_list)
        self.btn_ai_save_config = QtWidgets.QPushButton("保存配置")
        self.btn_ai_save_config.clicked.connect(self.save_config)
        self.btn_ai_provider_docs = QtWidgets.QPushButton("接口文档")
        self.btn_ai_provider_docs.clicked.connect(self.ai_open_provider_docs)
        self.btn_ai_billing = QtWidgets.QPushButton("账单/用量")
        self.btn_ai_billing.clicked.connect(self.ai_open_provider_billing)
        self.btn_ai_help = QtWidgets.QPushButton("接入说明")
        self.btn_ai_help.clicked.connect(self.ai_open_config_help)

        self.ai_model_suggest_combo = QtWidgets.QComboBox()
        self.ai_model_suggest_combo.setEnabled(False)
        self.ai_model_suggest_combo.setMinimumWidth(260)
        self.ai_model_suggest_combo.setToolTip("获取模型或完整诊断返回的可用模型列表；选择后会自动写入模型名。")
        self.ai_model_suggest_combo.currentTextChanged.connect(self.ai_on_suggested_model_changed)
        self.btn_ai_use_suggested_model = QtWidgets.QPushButton("使用建议模型")
        self.btn_ai_use_suggested_model.setEnabled(False)
        self.btn_ai_use_suggested_model.clicked.connect(self.ai_use_suggested_model)
        self.ai_last_model_suggestions = []

        self.ai_provider_note = QtWidgets.QLabel("选择一个AI方案。")
        self.ai_provider_note.setObjectName("hintLabel")
        self.ai_provider_note.setWordWrap(True)

        row0 = QtWidgets.QHBoxLayout()
        row0.setSpacing(8)
        row0.addWidget(QtWidgets.QLabel("模型方案"))
        row0.addWidget(self.ai_provider_combo)
        row0.addWidget(QtWidgets.QLabel("费用类型"))
        row0.addWidget(self.ai_cost_type_label, 1)
        row0.addWidget(QtWidgets.QLabel("接口类型"))
        row0.addWidget(self.ai_api_type_combo)
        cfg.addLayout(row0, 0, 0, 1, 8)

        cfg.addWidget(QtWidgets.QLabel("Base URL"), 1, 0)
        cfg.addWidget(self.ai_base_url, 1, 1, 1, 7)
        cfg.addWidget(QtWidgets.QLabel("模型名"), 2, 0)
        cfg.addWidget(self.ai_model, 2, 1, 1, 3)
        cfg.addWidget(QtWidgets.QLabel("API Key"), 2, 4)
        cfg.addWidget(self.ai_api_key, 2, 5, 1, 3)

        self.ai_robot_name_edit = QtWidgets.QLineEdit("AI小助手")
        self.ai_robot_name_edit.setFixedWidth(100)
        self.ai_robot_name_edit.setToolTip("自定义助手昵称，聊天记录和标题中显示此名称。")
        self.ai_robot_name_edit.textChanged.connect(self._on_ai_robot_name_changed)
        self.ai_user_name_edit = QtWidgets.QLineEdit("用户")
        self.ai_user_name_edit.setFixedWidth(100)
        self.ai_user_name_edit.setToolTip("自定义用户昵称，聊天记录里显示此名称。")
        self.ai_user_name_edit.textChanged.connect(self._on_ai_user_name_changed)

        row3 = QtWidgets.QHBoxLayout()
        row3.setSpacing(10)
        row3.addWidget(self.ai_save_key_chk)
        row3.addWidget(QtWidgets.QLabel("温度"))
        row3.addWidget(self.ai_temperature)
        row3.addWidget(QtWidgets.QLabel("保留轮数"))
        row3.addWidget(self.ai_history_spin)
        row3.addWidget(QtWidgets.QLabel("助手昵称:"))
        row3.addWidget(self.ai_robot_name_edit)
        row3.addWidget(QtWidgets.QLabel("用户昵称:"))
        row3.addWidget(self.ai_user_name_edit)
        row3.addStretch()
        cfg.addLayout(row3, 3, 1, 1, 7)

        btn_row = QtWidgets.QHBoxLayout()
        btn_row.setSpacing(8)
        for b in [
            self.btn_ai_get_key, self.btn_ai_activate, self.btn_ai_diagnose, self.btn_ai_test,
            self.btn_ai_fetch_models, self.btn_ai_save_config, self.btn_ai_provider_docs, self.btn_ai_billing, self.btn_ai_help
        ]:
            btn_row.addWidget(b)
        btn_row.addStretch()
        cfg.addLayout(btn_row, 4, 0, 1, 8)

        cfg.addWidget(self.ai_provider_note, 5, 0, 1, 8)

        suggest_row = QtWidgets.QHBoxLayout()
        suggest_row.setSpacing(8)
        suggest_row.addWidget(QtWidgets.QLabel("可用模型"))
        suggest_row.addWidget(self.ai_model_suggest_combo, 1)
        suggest_row.addWidget(self.btn_ai_use_suggested_model)
        suggest_row.addStretch()
        cfg.addLayout(suggest_row, 6, 0, 1, 8)
        diag_row = QtWidgets.QHBoxLayout()
        diag_row.setSpacing(8)
        diag_row.addWidget(QtWidgets.QLabel("诊断日志"))
        self.btn_ai_open_diag = QtWidgets.QPushButton("打开诊断日志")
        self.btn_ai_open_diag.clicked.connect(self.ai_open_diagnosis_log)
        diag_row.addWidget(self.btn_ai_open_diag)
        self.btn_ai_clear_diag = QtWidgets.QPushButton("清除诊断日志")
        self.btn_ai_clear_diag.clicked.connect(self.ai_clear_diagnosis_log)
        diag_row.addWidget(self.btn_ai_clear_diag)
        diag_row.addStretch()
        cfg.addLayout(diag_row, 7, 0, 1, 8)
        self.ai_diag_view = QtWidgets.QPlainTextEdit()
        self.ai_diag_view.setReadOnly(True)
        self.ai_diag_view.setPlainText("")
        self.ai_diag_view.setMaximumHeight(1)
        self.ai_diag_view.hide()
        cfg.addWidget(self.ai_diag_view, 8, 0, 1, 8)

        main.addWidget(cfg_box)

        # ---------------- 聊天区 ----------------
        # 不使用 QSplitter，避免"用户信息框下方可拖动条"影响视觉和皮肤兼容。
        chat_wrap = QtWidgets.QWidget()
        chat_wrap_lay = QtWidgets.QVBoxLayout(chat_wrap)
        chat_wrap_lay.setContentsMargins(0, 0, 0, 0)
        chat_wrap_lay.setSpacing(8)
        main.addWidget(chat_wrap, 1)

        chat_panel = QtWidgets.QWidget()
        chat_lay = QtWidgets.QVBoxLayout(chat_panel)
        chat_lay.setContentsMargins(0, 0, 0, 0)
        chat_lay.setSpacing(8)

        chat_tools = QtWidgets.QFrame()
        chat_tools.setObjectName("aiChatFontBar")
        chat_tools.setMinimumHeight(68)
        chat_tools_main = QtWidgets.QVBoxLayout(chat_tools)
        chat_tools_main.setContentsMargins(8, 6, 8, 6)
        chat_tools_main.setSpacing(4)

        font_row = QtWidgets.QHBoxLayout()
        font_row.setContentsMargins(0, 0, 0, 0)
        font_row.setSpacing(8)

        font_row_h = 32
        lbl_font_size = QtWidgets.QLabel("聊天字号")
        lbl_font_size.setFixedHeight(font_row_h)
        lbl_font_size.setMinimumWidth(68)
        lbl_font_size.setAlignment(QT_ALIGN_CENTER)
        font_row.addWidget(lbl_font_size)

        self.btn_ai_font_minus = QtWidgets.QPushButton("A-")
        self.btn_ai_font_minus.setFixedSize(48, font_row_h)
        self.btn_ai_font_minus.clicked.connect(lambda: self.ai_change_font_size(-1))
        font_row.addWidget(self.btn_ai_font_minus)

        self.ai_font_size_spin = QtWidgets.QSpinBox()
        self.ai_font_size_spin.setRange(8, 24)
        self.ai_font_size_spin.setValue(8)
        self.ai_font_size_spin.setFixedSize(104, font_row_h)
        self.ai_font_size_spin.setMinimumWidth(104)
        self.ai_font_size_spin.setAlignment(QT_ALIGN_CENTER)
        self.ai_font_size_spin.setButtonSymbols(QtWidgets.QAbstractSpinBox.UpDownArrows)
        self.ai_font_size_spin.setToolTip("调整聊天记录、输入框和诊断框字号")
        self.ai_font_size_spin.valueChanged.connect(lambda _v: self.ai_update_chat_style())
        font_row.addWidget(self.ai_font_size_spin)

        self.btn_ai_font_plus = QtWidgets.QPushButton("A+")
        self.btn_ai_font_plus.setFixedSize(48, font_row_h)
        self.btn_ai_font_plus.clicked.connect(lambda: self.ai_change_font_size(1))
        font_row.addWidget(self.btn_ai_font_plus)

        self.btn_ai_toggle_config = QtWidgets.QPushButton("")
        self.btn_ai_toggle_config.setFixedHeight(font_row_h)
        self.btn_ai_toggle_config.setMinimumWidth(104)
        self.btn_ai_toggle_config.clicked.connect(self.ai_toggle_config_collapsed)
        font_row.addWidget(self.btn_ai_toggle_config)

        self.ai_config_summary_label = QtWidgets.QLabel("")
        self.ai_config_summary_label.setObjectName("hintLabel")
        self.ai_config_summary_label.setWordWrap(False)
        self.ai_config_summary_label.setMinimumWidth(360)
        self.ai_config_summary_label.setToolTip("当前 AI 配置摘要：方案、接口、模型、Key 和可测试状态。")
        font_row.addWidget(self.ai_config_summary_label, 1)

        self.btn_ai_popout = QtWidgets.QPushButton("独立窗口")
        self.btn_ai_popout.setFixedHeight(font_row_h)
        self.btn_ai_popout.setMinimumWidth(92)
        self.btn_ai_popout.clicked.connect(self.ai_open_popout_chat)
        font_row.addWidget(self.btn_ai_popout)

        self.btn_ai_web = QtWidgets.QPushButton("浏览器面板")
        self.btn_ai_web.setFixedHeight(font_row_h)
        self.btn_ai_web.setMinimumWidth(92)
        self.btn_ai_web.setObjectName("primaryButton")
        self.btn_ai_web.clicked.connect(self.ai_open_web_chat)
        font_row.addWidget(self.btn_ai_web)

        self.btn_ai_sync_state = QtWidgets.QPushButton("同步消息")
        self.btn_ai_sync_state.setFixedHeight(font_row_h)
        self.btn_ai_sync_state.setMinimumWidth(84)
        self.btn_ai_sync_state.clicked.connect(lambda: self.ai_sync_shared_state(announce=True))
        font_row.addWidget(self.btn_ai_sync_state)

        self.ai_browser_port_spin = QtWidgets.QSpinBox()
        self.ai_browser_port_spin.setRange(1025, 65535)
        self.ai_browser_port_spin.setFixedSize(104, font_row_h)
        self.ai_browser_port_spin.setToolTip("本地桥接服务端口。Chrome 扩展、PBR 推送和 Web AI 都使用同一个端口。")
        self.ai_browser_port_spin.valueChanged.connect(lambda _v: self.ai_set_browser_port_from_toolbar())
        font_row.addWidget(self.ai_browser_port_spin)

        self.btn_ai_browser_port_check = QtWidgets.QPushButton("端口检查")
        self.btn_ai_browser_port_check.setFixedHeight(font_row_h)
        self.btn_ai_browser_port_check.setMinimumWidth(84)
        self.btn_ai_browser_port_check.clicked.connect(lambda: (self.ai_set_browser_port_from_toolbar(), self.pbr_check_push_server_port()))
        font_row.addWidget(self.btn_ai_browser_port_check)

        font_row.addStretch()
        chat_tools_main.addLayout(font_row)

        try:
            chat_tools.setStyleSheet(
                "QFrame#aiChatFontBar { background: transparent; }"
                "QLabel { padding: 0px; margin: 0px; }"
                "QSpinBox { min-height: 30px; max-height: 32px; padding-top: 0px; padding-bottom: 0px; }"
                "QPushButton { min-height: 30px; max-height: 32px; padding-top: 0px; padding-bottom: 0px; }"
            )
        except Exception:
            pass
        chat_lay.addWidget(chat_tools)
        chat_lay.addSpacing(10)

        action_bar = QtWidgets.QFrame()
        action_bar.setObjectName("aiSmartActionBar")
        action_bar.setMinimumHeight(42)
        action_lay = QtWidgets.QHBoxLayout(action_bar)
        action_lay.setContentsMargins(8, 6, 8, 6)
        action_lay.setSpacing(8)
        self.btn_ai_detect_actions = QtWidgets.QPushButton("识别内容")
        self.btn_ai_detect_actions.setFixedHeight(30)
        self.btn_ai_detect_actions.clicked.connect(self.ai_rebuild_smart_actions)
        self.ai_action_combo = QtWidgets.QComboBox()
        self.ai_action_combo.setMinimumWidth(420)
        self.ai_action_combo.setFixedHeight(30)
        self.ai_action_combo.addItem("选中聊天内容后可识别网址/路径")
        self.ai_action_combo.setEnabled(False)
        self.btn_ai_run_action = QtWidgets.QPushButton("执行")
        self.btn_ai_run_action.setFixedHeight(30)
        self.btn_ai_run_action.setObjectName("primaryButton")
        self.btn_ai_run_action.setEnabled(False)
        self.btn_ai_run_action.clicked.connect(self.ai_run_smart_action)
        self.btn_ai_open_url = QtWidgets.QPushButton("打开网址")
        self.btn_ai_open_url.setFixedHeight(30)
        self.btn_ai_open_url.setEnabled(False)
        self.btn_ai_open_url.clicked.connect(self.ai_open_first_url_action)
        self.btn_ai_copy_action_value = QtWidgets.QPushButton("复制内容")
        self.btn_ai_copy_action_value.setFixedHeight(30)
        self.btn_ai_copy_action_value.setEnabled(False)
        self.btn_ai_copy_action_value.clicked.connect(self.ai_copy_action_value)
        self.btn_ai_run_script = QtWidgets.QPushButton("▶ 执行脚本")
        self.btn_ai_run_script.setFixedHeight(30)
        self.btn_ai_run_script.setObjectName("primaryButton")
        self.btn_ai_run_script.setEnabled(False)
        self.btn_ai_run_script.setToolTip("执行AI回答中的最后一段代码块（MAXScript或Python）")
        self.btn_ai_run_script.clicked.connect(self.ai_run_last_code_block)
        action_lay.addWidget(self.btn_ai_detect_actions)
        action_lay.addWidget(self.ai_action_combo, 1)
        action_lay.addWidget(self.btn_ai_run_action)
        action_lay.addWidget(self.btn_ai_open_url)
        action_lay.addWidget(self.btn_ai_copy_action_value)
        action_lay.addWidget(self.btn_ai_run_script)
        try:
            action_bar.setStyleSheet("QFrame#aiSmartActionBar { background: transparent; } QPushButton, QComboBox { min-height: 28px; }")
        except Exception:
            pass
        chat_lay.addWidget(action_bar)
        chat_lay.addSpacing(8)

        self.ai_smart_actions = []

        self.ai_chat_view = QtWidgets.QListWidget()
        self.ai_chat_view.setSelectionMode(QtWidgets.QAbstractItemView.SingleSelection)
        self.ai_chat_view.setVerticalScrollMode(QtWidgets.QAbstractItemView.ScrollPerPixel)
        self.ai_chat_view.setHorizontalScrollBarPolicy(QtCore.Qt.ScrollBarAlwaysOff)
        self.ai_chat_view.setResizeMode(QtWidgets.QListView.Adjust)
        self.ai_chat_view.setSpacing(2)
        self.ai_chat_view.setMinimumHeight(300)
        chat_lay.addWidget(self.ai_chat_view, 1)

        input_panel = QtWidgets.QWidget()
        input_lay = QtWidgets.QGridLayout(input_panel)
        input_lay.setContentsMargins(0, 0, 0, 0)
        input_lay.setHorizontalSpacing(8)
        input_lay.setVerticalSpacing(8)

        self.ai_template_combo = QtWidgets.QComboBox()
        self.ai_template_combo.setMinimumWidth(300)
        self.ai_template_combo.addItems([
            "常规问题",
            "分析3ds Max报错",
            "MAXScript报错分析",
            "Python/PySide报错分析",
            "插件使用指导",
            "安装/工具栏/图标问题",
            "PBR材质问题",
            "PBR通道识别问题",
            "V-Ray材质问题",
            "Corona材质问题",
            "Physical/PBR材质问题",
            "法线DX/GL判断",
            "UE贴图流送问题",
            "UE导入前检查清单",
            "下载库问题",
            "贴图丢失/路径问题",
            "模型整理/重命名建议",
            "场景卡顿优化",
            "写给客户/同事的说明",
            "给我排查清单"
        ])
        self.ai_input = QtWidgets.QPlainTextEdit()
        self.ai_input.setPlaceholderText("在这里输入问题。")
        self.ai_input.setMinimumHeight(120)
        self.ai_input.setSizePolicy(QtWidgets.QSizePolicy.Expanding, QtWidgets.QSizePolicy.Fixed)
        self.ai_input.installEventFilter(self)
        self.ai_image_preview_label = QtWidgets.QLabel("未附加图片")
        self.ai_image_preview_label.setObjectName("hintLabel")

        self.btn_ai_send = QtWidgets.QPushButton("发送问题")
        self.btn_ai_send.setObjectName("primaryButton")
        self.btn_ai_send.clicked.connect(self.ai_send_message)
        self.btn_ai_attach_image = QtWidgets.QPushButton("发送图片")
        self.btn_ai_attach_image.clicked.connect(self.ai_choose_images)
        self.btn_ai_image_edit_mode = QtWidgets.QPushButton("")
        self.btn_ai_image_edit_mode.clicked.connect(self.ai_toggle_image_edit_mode)
        self.btn_ai_clear_image = QtWidgets.QPushButton("清空图片")
        self.btn_ai_clear_image.clicked.connect(self.ai_clear_images)
        self.btn_ai_scene_summary = QtWidgets.QPushButton("附加场景摘要")
        self.btn_ai_scene_summary.clicked.connect(self.ai_insert_scene_summary)
        self.btn_ai_recent_log = QtWidgets.QPushButton("附加最近日志")
        self.btn_ai_recent_log.clicked.connect(self.ai_insert_recent_log)
        self.btn_ai_copy = QtWidgets.QPushButton("复制最后回答")
        self.btn_ai_copy.clicked.connect(self.ai_copy_last_answer)
        self.btn_ai_clear = QtWidgets.QPushButton("清空对话")
        self.btn_ai_clear.setObjectName("dangerButton")
        self.btn_ai_clear.clicked.connect(self.ai_clear_chat)

        template_row = QtWidgets.QHBoxLayout()
        template_row.setSpacing(8)
        template_row.addWidget(QtWidgets.QLabel("提问模板"))
        template_row.addWidget(self.ai_template_combo)
        template_row.addWidget(self.ai_image_preview_label, 1)
        template_row.addStretch()
        input_lay.addLayout(template_row, 0, 0, 1, 9)
        input_lay.addWidget(self.ai_input, 1, 0, 1, 9)
        input_lay.addWidget(self.btn_ai_send, 2, 0)
        input_lay.addWidget(self.btn_ai_attach_image, 2, 1)
        input_lay.addWidget(self.btn_ai_clear_image, 2, 2)
        input_lay.addWidget(self.btn_ai_image_edit_mode, 2, 3)
        input_lay.addWidget(self.btn_ai_scene_summary, 2, 4)
        input_lay.addWidget(self.btn_ai_recent_log, 2, 5)
        input_lay.addWidget(self.btn_ai_copy, 2, 6)
        input_lay.addWidget(self.btn_ai_clear, 2, 7)
        input_lay.addWidget(self.btn_ai_popout, 2, 8)
        chat_lay.addWidget(input_panel)

        chat_wrap_lay.addWidget(chat_panel, 1)

        hint = QtWidgets.QLabel("说明：AI小助手不内置账号或密钥。先选服务商方案，点\"获取API Key\"打开官网，填入Key后点\"激活使用\"。本插件不会上传场景文件，只会发送你输入/附加的文字内容到你配置的接口。API Key若选择保存，会以明文保存在插件配置中。")
        hint.setObjectName("hintLabel")
        hint.setWordWrap(True)
        chat_wrap_lay.addWidget(hint, 0)
        self.ai_image_edit_mode_label = QtWidgets.QLabel("")
        self.ai_image_edit_mode_label.setObjectName("hintLabel")
        self.ai_image_edit_mode_label.setWordWrap(True)
        chat_wrap_lay.addWidget(self.ai_image_edit_mode_label, 0)

        self.ai_diag_highlighter = AIPlainTextHighlighter(self.ai_diag_view.document(), name_getter=lambda: getattr(self, "_ai_robot_name", "AI小助手"))
        self.ai_messages = []
        self._ai_pending_images = []
        self._ai_image_edit_mode = bool(getattr(self, "_ai_image_edit_mode", False))
        self.ai_active_provider_name = ai_provider_key_from_name(self.ai_provider_combo.currentText())
        self.ai_bind_config_summary_refreshers()
        self.ai_bind_image_capability_refreshers()
        self.ai_apply_preset()
        self.ai_refresh_image_preview()
        self.ai_refresh_image_capability_hint()
        self.ai_refresh_image_edit_mode_ui()
        self.ai_refresh_config_collapse_ui()
        self.ai_refresh_config_summary()
        self.ai_sync_browser_port_controls()
        self.ai_update_chat_style()
        self.ai_render_chat()

    def build_texture_tab(self):
        main = QtWidgets.QVBoxLayout(self.texture_tab); main.setContentsMargins(12,12,12,12)

        warn_box = self.card("⚠ 谨慎使用 · UE 纹理流送整理")
        lay = QtWidgets.QGridLayout(warn_box)

        caution = QtWidgets.QLabel(
            "此功能会复制外部贴图到指定目录，并可限制最大尺寸，让贴图更适合 UE 纹理流送。"
            "默认不覆盖源文件、不强制2幂、不修改 Max 贴图路径。请先在测试项目中使用。"
        )
        caution.setObjectName("previewHint")
        try: caution.setWordWrap(True)
        except Exception: pass
        lay.addWidget(caution, 0, 0, 1, 8)

        self.image_tools_status = QtWidgets.QLabel("图像处理工具：未检测")
        self.image_tools_status.setObjectName("hintLabel")
        self.btn_detect_image_tools = QtWidgets.QPushButton("🔎 检测")
        self.btn_install_pillow = QtWidgets.QPushButton("⬇ Pillow")
        self.btn_install_pillow.setToolTip("安装到当前 3ds Max Python。会打开命令窗口，安装后请重启 Max。")
        self.btn_install_imagemagick = QtWidgets.QPushButton("⬇ ImageMagick")
        self.btn_install_imagemagick.setToolTip("优先通过 winget 安装系统 ImageMagick。安装后请重启 Max。")
        self.btn_open_imagemagick_page = QtWidgets.QPushButton("🌐 下载页")
        self.btn_detect_image_tools.clicked.connect(self.detect_image_tools_from_ui)
        self.btn_install_pillow.clicked.connect(self.install_pillow_from_ui)
        self.btn_install_imagemagick.clicked.connect(self.install_imagemagick_from_ui)
        self.btn_open_imagemagick_page.clicked.connect(self.open_imagemagick_page_from_ui)
        lay.addWidget(self.image_tools_status, 1, 0, 1, 3)
        lay.addWidget(self.btn_detect_image_tools, 1, 3)
        lay.addWidget(self.btn_install_pillow, 1, 4)
        lay.addWidget(self.btn_install_imagemagick, 1, 5)
        lay.addWidget(self.btn_open_imagemagick_page, 1, 6)

        # V54：列表管理按钮明确分开，避免"清空"和"清除选择"挤在同一个格子里。
        self.btn_scan_scene_textures = QtWidgets.QPushButton("🔍 深度扫描场景")
        self.btn_scan_scene_textures.clicked.connect(self.scan_scene_textures)
        lay.addWidget(self.btn_scan_scene_textures, 2, 0)

        self.btn_scan_selected_textures = QtWidgets.QPushButton("🎯 深度扫描选中")
        self.btn_scan_selected_textures.clicked.connect(self.scan_selected_object_textures)
        lay.addWidget(self.btn_scan_selected_textures, 2, 1)

        self.btn_scan_material_textures = QtWidgets.QPushButton("🧱 深度扫描材质")
        self.btn_scan_material_textures.clicked.connect(self.scan_material_list_textures)
        lay.addWidget(self.btn_scan_material_textures, 2, 2)

        self.btn_clear_texture_list = QtWidgets.QPushButton("🧹 清空列表")
        self.btn_clear_texture_list.setObjectName("dangerButton")
        self.btn_clear_texture_list.setToolTip("清空UE贴图流送列表；不删除磁盘贴图，也不修改场景材质。")
        self.btn_clear_texture_list.clicked.connect(self.clear_texture_list)
        lay.addWidget(self.btn_clear_texture_list, 2, 3)

        self.btn_remove_texture_rows = QtWidgets.QPushButton("− 清除选择")
        self.btn_remove_texture_rows.setObjectName("dangerButton")
        self.btn_remove_texture_rows.setToolTip("只从UE贴图流送列表清除高亮选择的行；不删除磁盘文件。")
        self.btn_remove_texture_rows.clicked.connect(self.clear_selected_texture_rows)
        lay.addWidget(self.btn_remove_texture_rows, 2, 4)

        self.btn_recheck_textures = QtWidgets.QPushButton("↻ 重新检查")
        self.btn_recheck_textures.setToolTip("用户整理贴图后，重新读取尺寸和合格状态。")
        self.btn_recheck_textures.setObjectName("primaryButton")
        self.btn_recheck_textures.clicked.connect(self.recheck_all_textures)
        lay.addWidget(self.btn_recheck_textures, 2, 5)

        self.btn_select_texture_objects = QtWidgets.QPushButton("🎯 选择相关物体")
        self.btn_select_texture_objects.setToolTip("选择当前高亮贴图在场景中对应的物体。会自动打开组，冻结物体不会被强选。")
        self.btn_select_texture_objects.clicked.connect(self.select_scene_objects_for_selected_textures)
        lay.addWidget(self.btn_select_texture_objects, 2, 6, 1, 2)

        self.btn_open_source_texture_folder = QtWidgets.QPushButton("📁 源位置")
        self.btn_open_source_texture_folder.setToolTip("打开源贴图所在文件夹。")
        self.btn_open_source_texture_folder.clicked.connect(lambda _=False: self.open_selected_texture_folders(output=False))
        lay.addWidget(self.btn_open_source_texture_folder, 3, 0)

        self.btn_open_source_texture_file = QtWidgets.QPushButton("🖼 源贴图")
        self.btn_open_source_texture_file.setToolTip("打开源贴图文件。")
        self.btn_open_source_texture_file.clicked.connect(lambda _=False: self.open_selected_texture_files(output=False))
        lay.addWidget(self.btn_open_source_texture_file, 3, 1)

        self.btn_open_output_texture_folder = QtWidgets.QPushButton("📁 输出位置")
        self.btn_open_output_texture_folder.setToolTip("打开新输出贴图所在文件夹。")
        self.btn_open_output_texture_folder.clicked.connect(lambda _=False: self.open_selected_texture_folders(output=True))
        lay.addWidget(self.btn_open_output_texture_folder, 3, 2)

        self.btn_open_output_texture_file = QtWidgets.QPushButton("🖼 输出贴图")
        self.btn_open_output_texture_file.setToolTip("打开新输出贴图文件。")
        self.btn_open_output_texture_file.clicked.connect(lambda _=False: self.open_selected_texture_files(output=True))
        lay.addWidget(self.btn_open_output_texture_file, 3, 3)

        self.btn_texture_scan_stop = QtWidgets.QPushButton("⛔ 停止扫描")
        self.btn_texture_scan_stop.setObjectName("dangerButton")
        self.btn_texture_scan_stop.setEnabled(False)
        self.btn_texture_scan_stop.setToolTip("停止当前深度扫描，已扫描到的贴图会保留在列表里。")
        self.btn_texture_scan_stop.clicked.connect(self.stop_texture_deep_scan)
        lay.addWidget(self.btn_texture_scan_stop, 3, 4)

        self.btn_clear_missing_textures = QtWidgets.QPushButton("🗑 清除不存在")
        self.btn_clear_missing_textures.setObjectName("dangerButton")
        self.btn_clear_missing_textures.setToolTip(
            "从列表中移除源文件在磁盘上不存在的贴图条目。\n"
            "仅清除列表项，不修改场景材质，不删除磁盘文件。"
        )
        self.btn_clear_missing_textures.clicked.connect(self.clear_missing_texture_entries)
        lay.addWidget(self.btn_clear_missing_textures, 3, 5, 1, 3)

        self.texture_output_dir = QtWidgets.QLineEdit(make_scene_texture_output_dir(default_texture_root_dir()))
        self.btn_choose_texture_dir = QtWidgets.QPushButton("📂 选择根目录")
        self.btn_choose_texture_dir.clicked.connect(self.choose_texture_output_dir)
        self.btn_open_texture_output_dir = QtWidgets.QPushButton("📁 打开此目录")
        self.btn_open_texture_output_dir.setToolTip("直接打开当前UE贴图输出目录。")
        self.btn_open_texture_output_dir.clicked.connect(self.open_current_texture_output_dir)
        self.btn_check_texture_output_dir = QtWidgets.QPushButton("🔎 检查输出")
        self.btn_check_texture_output_dir.setToolTip("扫描输出目录，识别已经处理好的贴图，避免重复输出。")
        self.btn_check_texture_output_dir.clicked.connect(lambda: self.mark_existing_texture_outputs_from_dir(silent=False, refresh=True))
        self.btn_sync_scene_output_dir = QtWidgets.QPushButton("↺ 同步模型路径")
        self.btn_sync_scene_output_dir.setToolTip(
            "重新读取当前 Max 文件路径，自动把输出目录更新为【模型所在文件夹/模型同名子文件夹】。\n"
            "插件启动时如果场景还未保存，或中途换了场景文件，可点此刷新。"
        )
        self.btn_sync_scene_output_dir.clicked.connect(self.sync_output_dir_to_scene)
        lay.addWidget(QtWidgets.QLabel("输出目录（自动建当前模型同名子文件夹）"), 4, 0)
        lay.addWidget(self.texture_output_dir, 4, 1, 1, 3)
        lay.addWidget(self.btn_sync_scene_output_dir, 4, 4)
        lay.addWidget(self.btn_choose_texture_dir, 4, 5)
        lay.addWidget(self.btn_open_texture_output_dir, 4, 6)
        lay.addWidget(self.btn_check_texture_output_dir, 4, 7)

        self.texture_engine_combo = QtWidgets.QComboBox()
        self.texture_engine_combo.addItems(["自动：Pillow优先，ImageMagick备用", "Pillow优先", "ImageMagick", "只复制，不处理"])
        self.texture_engine_combo.setToolTip("强制合格时使用的图像处理引擎。普通合格贴图输出通常只复制。")

        self.texture_max_size = QtWidgets.QSpinBox()
        self.texture_max_size.setRange(256, 16384)
        self.texture_max_size.setSingleStep(512)
        self.texture_max_size.setValue(4096)

        self.texture_large_warn_size = QtWidgets.QSpinBox()
        self.texture_large_warn_size.setRange(2048, 32768)
        self.texture_large_warn_size.setSingleStep(1024)
        self.texture_large_warn_size.setValue(8192)

        self.chk_texture_ue_name = QtWidgets.QCheckBox("UE命名")
        self.chk_texture_ue_name.setChecked(True)
        self.chk_texture_no_overwrite = QtWidgets.QCheckBox("不覆盖已有文件")
        self.chk_texture_no_overwrite.setChecked(True)
        self.chk_texture_skip_existing_good = QtWidgets.QCheckBox("已有合格输出则跳过")
        self.chk_texture_skip_existing_good.setChecked(True)
        self.chk_texture_require_power2 = QtWidgets.QCheckBox("合格要求2幂尺寸")
        self.chk_texture_require_power2.setChecked(True)
        self.chk_texture_only_problem = QtWidgets.QCheckBox("只显示/处理问题项")
        self.chk_texture_only_problem.setChecked(False)
        self.chk_texture_force_power2 = QtWidgets.QCheckBox("自动强制2幂尺寸（禁用：请外部软件处理）")
        self.chk_texture_force_power2.setChecked(False)
        self.chk_texture_force_power2.setEnabled(False)
        self.chk_texture_sync_objects = QtWidgets.QCheckBox("选择贴图同步场景物体")
        self.chk_texture_sync_objects.setChecked(False)
        self.chk_texture_sync_objects.setToolTip("选中UE贴图流送列表行时，自动选择场景中使用该贴图的物体。大量贴图时建议关闭。")

        self.btn_update_texture_paths = QtWidgets.QPushButton("🔗 关联UE输出贴图")
        self.btn_update_texture_paths.setObjectName("dangerButton")
        self.btn_update_texture_paths.setToolTip("只有列表全部合格并已输出后才允许更新 Max 里的贴图路径。关联时会自动给新贴图加时间戳重命名，防止与旧贴图文件冲突。")
        self.btn_update_texture_paths.clicked.connect(self.update_max_texture_paths_after_output)

        self.btn_clear_output_textures = QtWidgets.QPushButton("🧹 清除已输出项")
        self.btn_clear_output_textures.setToolTip("从列表中移除已经输出成功/已处理的贴图项；不删除磁盘文件。优先清除高亮，其次打勾，否则清除列表全部已输出项。")
        self.btn_clear_output_textures.clicked.connect(self.clear_output_success_texture_entries)

        lay.addWidget(QtWidgets.QLabel("处理引擎"), 5, 0)
        lay.addWidget(self.texture_engine_combo, 5, 1, 1, 3)
        lay.addWidget(QtWidgets.QLabel("最大尺寸"), 5, 4)
        lay.addWidget(self.texture_max_size, 5, 5)
        lay.addWidget(QtWidgets.QLabel("大图提示"), 5, 6)
        lay.addWidget(self.texture_large_warn_size, 5, 7)
        lay.addWidget(self.chk_texture_ue_name, 6, 0)
        lay.addWidget(self.chk_texture_no_overwrite, 6, 1)
        lay.addWidget(self.chk_texture_skip_existing_good, 6, 2)
        lay.addWidget(self.chk_texture_require_power2, 6, 3)
        lay.addWidget(self.chk_texture_only_problem, 7, 0, 1, 2)
        lay.addWidget(self.chk_texture_force_power2, 7, 2, 1, 3)
        lay.addWidget(self.chk_texture_sync_objects, 7, 4)
        lay.addWidget(self.btn_update_texture_paths, 7, 5, 1, 2)
        lay.addWidget(self.btn_clear_output_textures, 7, 7)

        # 关联重命名规则控件（第8行）
        self.ue_rename_prefix = QtWidgets.QLineEdit("T_")
        self.ue_rename_prefix.setFixedWidth(52)
        self.ue_rename_prefix.setToolTip("输出文件名前缀，默认 T_（符合 UE 命名规范）。")
        self.chk_ue_rename_include_mat = QtWidgets.QCheckBox("含材质名")
        self.chk_ue_rename_include_mat.setChecked(False)
        self.chk_ue_rename_include_mat.setToolTip("在文件名中插入材质名称。")
        self.chk_ue_rename_include_obj = QtWidgets.QCheckBox("含模型名")
        self.chk_ue_rename_include_obj.setChecked(False)
        self.chk_ue_rename_include_obj.setToolTip("在文件名中插入关联模型名称。")
        self.ue_rename_sep = QtWidgets.QLineEdit("_")
        self.ue_rename_sep.setFixedWidth(28)
        self.ue_rename_sep.setToolTip("文件名各部分之间的分隔符，默认 _。")
        lay.addWidget(QtWidgets.QLabel("关联命名规则"), 8, 0)
        lay.addWidget(QtWidgets.QLabel("前缀"), 8, 1)
        lay.addWidget(self.ue_rename_prefix, 8, 2)
        lay.addWidget(self.chk_ue_rename_include_mat, 8, 3)
        lay.addWidget(self.chk_ue_rename_include_obj, 8, 4)
        lay.addWidget(QtWidgets.QLabel("分隔符"), 8, 5)
        lay.addWidget(self.ue_rename_sep, 8, 6)

        self.btn_texture_process_all = QtWidgets.QPushButton("📤 输出全部合格")
        self.btn_texture_process_checked = QtWidgets.QPushButton("☑ 输出打勾")
        self.btn_texture_process_selected = QtWidgets.QPushButton("↗ 输出高亮")
        self.btn_texture_process_checked.setObjectName("primaryButton")
        self.btn_texture_stop = QtWidgets.QPushButton("⛔ 停止")
        self.btn_texture_stop.setObjectName("dangerButton")
        self.btn_texture_stop.setEnabled(False)

        self.btn_texture_force_checked = QtWidgets.QPushButton("⚠ 强制合格打勾")
        self.btn_texture_force_checked.setObjectName("dangerButton")
        self.btn_texture_force_checked.setToolTip("用居中裁剪+缩放生成合格贴图，不直接修改原图。需要 Pillow。")
        self.btn_texture_force_selected = QtWidgets.QPushButton("⚠ 强制合格高亮")
        self.btn_texture_force_selected.setObjectName("dangerButton")
        self.btn_texture_force_selected.setToolTip("用居中裁剪+缩放生成合格贴图，不直接修改原图。需要 Pillow。")

        self.btn_texture_process_all.clicked.connect(lambda: self.start_texture_streaming_process("all", force=False))
        self.btn_texture_process_checked.clicked.connect(lambda: self.start_texture_streaming_process("checked", force=False))
        self.btn_texture_process_selected.clicked.connect(lambda: self.start_texture_streaming_process("selected", force=False))
        self.btn_texture_force_checked.clicked.connect(lambda: self.start_texture_streaming_process("checked", force=True))
        self.btn_texture_force_selected.clicked.connect(lambda: self.start_texture_streaming_process("selected", force=True))
        self.btn_texture_stop.clicked.connect(lambda: self.stop_texture_streaming_process(forced=False))

        lay.addWidget(self.btn_texture_process_all, 9, 0, 1, 2)
        lay.addWidget(self.btn_texture_process_checked, 9, 2, 1, 2)
        lay.addWidget(self.btn_texture_process_selected, 9, 4, 1, 2)
        lay.addWidget(self.btn_texture_stop, 9, 6)
        lay.addWidget(self.btn_texture_force_checked, 10, 0, 1, 2)
        lay.addWidget(self.btn_texture_force_selected, 10, 2, 1, 2)

        hint = QtWidgets.QLabel(
            "推荐策略：先扫描，默认只复制并限制最大边，保持比例。"
            "非2幂贴图会标记出来；是否强制2幂请谨慎决定。"
            "如果当前 Max Python 没有 Pillow，过大贴图会只复制不缩放。"
        )
        hint.setObjectName("hintLabel")
        try: hint.setWordWrap(True)
        except Exception: pass
        lay.addWidget(hint, 11, 0, 1, 8)

        main.addWidget(warn_box)

        self.texture_tree = QtWidgets.QTreeWidget()
        self.texture_tree.setColumnCount(13)
        self.texture_tree.setHeaderLabels(["贴图", "通道", "源尺寸", "合格后尺寸", "源图状态", "输出状态", "2幂", "问题/建议", "相关物体", "源路径", "输出路径", "引用数", "处理状态"])
        self.prepare_tree(self.texture_tree)
        try:
            self.texture_tree.itemDoubleClicked.connect(self.on_texture_item_double_clicked)
            self.texture_tree.itemSelectionChanged.connect(self.on_texture_tree_selection_changed)
            self.texture_tree.setContextMenuPolicy(qt_enum("CustomContextMenu", ("ContextMenuPolicy",)))
            self.texture_tree.customContextMenuRequested.connect(self.on_texture_tree_context_menu)
        except Exception:
            pass
        main.addWidget(self.make_check_bar("UE贴图流送列表", "texture_tree"))
        main.addWidget(self.texture_tree, 1)

    def current_material_target_mode(self):
        try:
            return self.pbr_target_combo.currentText()
        except Exception:
            return "PBR Material Metal/Rough"

    def refresh_pbr_tree(self):
        self.ignore_pbr_selection=True; self.pbr_tree.clear()
        skip_existing = self.chk_pbr_skip_existing.isChecked() if hasattr(self, "chk_pbr_skip_existing") else True
        try_complex = self.chk_pbr_try_complex.isChecked() if hasattr(self, "chk_pbr_try_complex") else False
        convert_mso = self.chk_pbr_convert_mso.isChecked() if hasattr(self, "chk_pbr_convert_mso") else True
        target_mode = self.current_material_target_mode()
        for entry in self.pbr_cache:
            mat=entry.get("mat"); parent=entry.get("parent"); role=entry.get("role","MAT")
            info = pbr_status_for_entry(entry, skip_already=skip_existing, try_complex=try_complex, convert_multi_children=convert_mso, target_mode=target_mode)
            item=QtWidgets.QTreeWidgetItem(); item.setText(0,get_material_name(mat)); item.setText(1,get_class_name(mat)); item.setText(2,role); item.setText(3,get_material_name(parent) if is_valid_material(parent) else "-")
            item.setText(4, info.get("judge", "")); item.setText(5, info.get("action", "")); item.setText(6, "等待" if info.get("ok") else info.get("note", "跳过"))
            self.set_item_checkable(item, True); self.pbr_tree.addTopLevelItem(item)
        for i in range(7): self.pbr_tree.resizeColumnToContents(i)
        self.ignore_pbr_selection=False; self.log("材质标准化列表数量：{}".format(len(self.pbr_cache)))


    # ---------- UE 贴图流送 ----------
    def detect_image_tools_from_ui(self):
        pillow = is_pillow_available()
        im = is_imagemagick_available()
        msg = "Pillow：{}{}；ImageMagick：{}".format(
            "可用 " if pillow else "不可用",
            pillow_version_text() if pillow else "",
            imagemagick_version_text() if im else "不可用"
        )
        try:
            self.image_tools_status.setText(msg)
        except Exception:
            pass
        self.log(msg)
        return pillow, im

    def install_pillow_from_ui(self):
        if is_pillow_available():
            self.detect_image_tools_from_ui()
            self.log("Pillow 已可用，无需安装")
            return
        ret = QtWidgets.QMessageBox.warning(
            self,
            "安装 Pillow",
            "将尝试给当前 3ds Max Python 安装 Pillow。\\n\\n会打开一个命令窗口，安装完成后通常需要重启 3ds Max。是否继续？",
            QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.No
        )
        if ret != QtWidgets.QMessageBox.Yes:
            self.log("已取消安装 Pillow")
            return
        ok, msg = install_pillow_for_current_max_python()
        self.log("安装 Pillow：{}".format(msg if ok else "失败：" + msg))

    def install_imagemagick_from_ui(self):
        if is_imagemagick_available():
            self.detect_image_tools_from_ui()
            self.log("ImageMagick 已可用，无需安装")
            return
        ret = QtWidgets.QMessageBox.warning(
            self,
            "安装 ImageMagick",
            "将尝试通过 Windows winget 安装 ImageMagick。\\n\\n这是系统软件，可能需要网络、权限或用户确认。安装完成后通常需要重启 3ds Max。是否继续？",
            QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.No
        )
        if ret != QtWidgets.QMessageBox.Yes:
            self.log("已取消安装 ImageMagick")
            return
        ok, msg = install_imagemagick_with_winget()
        if not ok:
            self.log("自动安装 ImageMagick 不可用：{}。已尝试打开下载页。".format(msg))
            open_imagemagick_download_page()
        else:
            self.log("安装 ImageMagick：{}".format(msg))

    def open_imagemagick_page_from_ui(self):
        if open_imagemagick_download_page():
            self.log("已打开 ImageMagick 下载页")
        else:
            self.log("无法打开 ImageMagick 下载页")

    def current_texture_engine(self):
        try:
            return self.texture_engine_combo.currentText()
        except Exception:
            return "自动：Pillow优先，ImageMagick备用"

    def current_ue_rename_opts(self):
        try:
            prefix = self.ue_rename_prefix.text().strip() or "T_"
            sep = self.ue_rename_sep.text() or "_"
            include_mat = self.chk_ue_rename_include_mat.isChecked()
            include_obj = self.chk_ue_rename_include_obj.isChecked()
            return dict(prefix=prefix, sep=sep, include_mat=include_mat, include_obj=include_obj)
        except Exception:
            return dict(prefix="T_", sep="_", include_mat=False, include_obj=False)

    def process_texture_force_with_engine(self, entry):
        engine = self.current_texture_engine()
        out_dir = self.texture_output_dir.text().strip()
        max_size = self.texture_max_size.value()
        ue_naming = self.chk_texture_ue_name.isChecked()
        no_overwrite = self.chk_texture_no_overwrite.isChecked()
        rename_opts = self.current_ue_rename_opts() if ue_naming else None

        if "只复制" in engine:
            return copy_texture_only_for_ue(entry, out_dir, ue_naming=ue_naming, no_overwrite=no_overwrite, rename_opts=rename_opts)

        if "ImageMagick" == engine:
            return force_process_texture_with_imagemagick(entry, out_dir, max_size=max_size, ue_naming=ue_naming, no_overwrite=no_overwrite, rename_opts=rename_opts)

        # 自动 / Pillow优先：使用 Pillow；失败时函数内部会回退到 ImageMagick
        return force_process_texture_for_ue(entry, out_dir, max_size=max_size, ue_naming=ue_naming, no_overwrite=no_overwrite, rename_opts=rename_opts)

    def process_texture_copy_with_engine(self, entry):
        ue_naming = self.chk_texture_ue_name.isChecked()
        rename_opts = self.current_ue_rename_opts() if ue_naming else None
        return copy_texture_only_for_ue(
            entry,
            self.texture_output_dir.text().strip(),
            ue_naming=ue_naming,
            no_overwrite=self.chk_texture_no_overwrite.isChecked(),
            rename_opts=rename_opts
        )

    # ---------- PBR贴图套装 ----------
    def choose_pbrset_folder(self):
        try:
            d = QtWidgets.QFileDialog.getExistingDirectory(self, "选择 PBR 贴图文件夹", self.pbrset_folder.text() or current_scene_folder())
            if d:
                self.pbrset_folder.setText(d)
        except Exception:
            self.log(status_text_for_exception("选择PBR贴图文件夹失败"))

    def scan_pbrset_folder(self):
        folder = self.pbrset_folder.text().strip()
        if not folder or not os.path.isdir(folder):
            self.log("PBR贴图套装：文件夹无效")
            return
        self.begin_operation("扫描PBR贴图套装", 0, cancellable=False)
        try:
            self.log("开始扫描PBR贴图套装：{}".format(folder))
            self.pbrset_cache = scan_pbr_texture_sets(
                folder,
                recursive=self.chk_pbrset_recursive.isChecked(),
                group_by_folder=self.chk_pbrset_group_by_folder.isChecked()
            )
            self.refresh_pbrset_tree()
            self.log("PBR贴图套装扫描完成：{} 个套装。".format(len(self.pbrset_cache)))
        except Exception:
            self.log(status_text_for_exception("扫描PBR贴图套装失败"))
        finally:
            self.finish_operation("PBR贴图套装扫描完成")

    def pbrset_entries_by_scope(self, scope):
        if scope == "all":
            return list(self.pbrset_cache)
        if scope == "checked":
            entries = []
            for i, e in enumerate(self.pbrset_cache):
                item = self.pbrset_tree.topLevelItem(i)
                if item and item.checkState(0) == QT_CHECKED:
                    entries.append(e)
            return entries
        if scope == "selected":
            entries = []
            for item in self.pbrset_tree.selectedItems():
                row = self.pbrset_tree.indexOfTopLevelItem(item)
                if 0 <= row < len(self.pbrset_cache):
                    entries.append(self.pbrset_cache[row])
            return entries
        return []

    def current_pbrset_creation_signature(self, entry):
        """
        创建签名：目标材质 + 创建选项 + 当前贴图映射。
        只要这些变化，之前创建的材质就不能直接复用。
        """
        try:
            return "|".join([
                "target={}".format(self.pbrset_target_combo.currentText()),
                "prefix={}".format(self.pbrset_prefix.text()),
                "normal={}".format(self.pbrset_normal_combo.currentText()),
                "gloss={}".format(self.pbrset_gloss_combo.currentText()),
                "mapping={}".format(pbrset_mapping_signature(entry)),
            ])
        except Exception:
            return pbrset_mapping_signature(entry)

    def pbrset_existing_material_matches_current_options(self, entry):
        mat = entry.get("created_mat")
        if not is_valid_material(mat):
            return False

        old_sig = safe_str(entry.get("created_signature", ""), "")
        new_sig = self.current_pbrset_creation_signature(entry)

        if not old_sig or old_sig != new_sig:
            return False

        return True

    def invalidate_pbrset_created_material(self, entry, reason="创建选项变化"):
        """
        不删除场景里的旧材质，只是不再把它当成当前套装的有效材质。
        这样安全，不会破坏已经赋给模型的旧材质。
        """
        if not entry:
            return
        old_name = ""
        try:
            if is_valid_material(entry.get("created_mat")):
                old_name = get_material_name(entry.get("created_mat"))
        except Exception:
            pass
        entry["created_mat"] = None
        entry["created_target"] = ""
        entry["created_class"] = ""
        entry["created_material_name"] = ""
        entry["created_signature"] = ""
        entry["status"] = "需重新创建：{}".format(reason)
        if old_name:
            self.log("PBR套装 {}：{}，旧材质 {} 已保留在场景中但不再复用。".format(entry.get("name", ""), reason, old_name))

    def try_manual_configure_pbr_material_slots(self, entry, mat):
        """
        自动连接失败后，允许用户对未接入贴图选择当前材质真实槽位。
        应用成功的槽位会保存到 entry['slot_overrides']，下次创建自动使用。
        """
        report = entry.get("_last_connection_report", {})
        if not report or report.get("ok", False):
            return True

        dlg = PBRMaterialSlotDialog(mat, entry, report, self.pbrset_target_combo.currentText(), self)
        if not dialog_accepted(dlg):
            return False

        connected = list(report.get("connected", []))
        remaining = []
        overrides = dict(entry.get("slot_overrides", {}))

        for item in dlg.result:
            ch = item.get("channel", "")
            path = item.get("path", "")
            prop = item.get("prop", "")

            if not prop:
                # 用户明确跳过，仍然算未连接，后续需要确认。
                remaining.append(dict(channel=ch, path=path, reason="用户选择跳过"))
                continue

            tex = pbr_create_tex_for_report_item(item)
            if tex is None:
                remaining.append(dict(channel=ch, path=path, reason="贴图节点创建失败"))
                continue

            if pbr_try_set_specific_slot(mat, prop, tex, ch):
                connected.append(dict(channel=ch, path=path, prop=prop))
                overrides[pbr_slot_override_key(self.pbrset_target_combo.currentText(), ch)] = prop
                learn_pbr_slot(self.pbrset_target_combo.currentText(), mat, ch, prop)
            else:
                remaining.append(dict(channel=ch, path=path, reason="槽位写入失败：{}".format(prop)))

        entry["slot_overrides"] = overrides
        entry["_last_connection_report"] = dict(
            ok=(len(remaining) == 0),
            required=report.get("required", []),
            connected=connected,
            unconnected=remaining
        )

        if remaining:
            self.log("手动槽位配置后仍有未接入：{}".format(pbrset_report_unconnected_text(entry.get("_last_connection_report"))))
            return False

        self.log("手动槽位配置完成，所有应使用贴图已接入。")
        return True

    def confirm_keep_incomplete_pbr_material(self, entry, mat, notes):
        report = entry.get("_last_connection_report", {})
        if report.get("ok", False):
            return True

        unconnected = pbrset_report_unconnected_text(report)
        mat_name = get_material_name(mat) if is_valid_material(mat) else entry.get("name", "PBR_Material")
        msg = (
            "材质\"{}\"没有把所有应使用贴图都接上。\\n\\n"
            "未接入：{}\\n\\n"
            "建议先回到\"手动贴图映射\"或换目标材质。\\n"
            "是否仍然保留这个不完整材质？"
        ).format(mat_name, unconnected or "未知")

        try:
            ret = QtWidgets.QMessageBox.warning(
                self,
                "PBR材质贴图未全部接入",
                msg,
                QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.No
            )
            return ret == QtWidgets.QMessageBox.Yes
        except Exception:
            # 没有弹窗环境时保守处理：不确认
            return False

    def mark_pbr_material_creation_result(self, entry, mat, notes, require_confirm=True):
        if not is_valid_material(mat):
            entry["status"] = "创建失败"
            return False

        report = entry.get("_last_connection_report", {})
        if report and not report.get("ok", False):
            # 先给用户一个真正解决问题的机会：手动选择当前材质的真实贴图槽。
            if require_confirm:
                self.try_manual_configure_pbr_material_slots(entry, mat)
                report = entry.get("_last_connection_report", {})

            if report and not report.get("ok", False):
                try:
                    PBRConnectionReportDialog(mat, entry, report, self).exec_()
                except Exception:
                    pass
                if require_confirm and not self.confirm_keep_incomplete_pbr_material(entry, mat, notes):
                    entry["status"] = "取消：贴图未全部接入"
                    self.log("{}：贴图未全部接入，已取消登记为成功材质。{}".format(
                        entry.get("name", ""),
                        pbrset_report_unconnected_text(report)
                    ))
                    return False

                entry["created_mat"] = mat
                entry["created_target"] = self.pbrset_target_combo.currentText()
                entry["created_signature"] = self.current_pbrset_creation_signature(entry)
                entry["status"] = "⚠ 已创建但贴图未全接入"
                self.log("{}：用户确认保留不完整材质。未接入：{}".format(
                    get_material_name(mat),
                    pbrset_report_unconnected_text(report)
                ))
                return True

        entry["created_mat"] = mat
        entry["created_target"] = self.pbrset_target_combo.currentText()
        entry["created_class"] = get_class_name(mat)
        entry["created_material_name"] = get_material_name(mat)
        entry["created_signature"] = self.current_pbrset_creation_signature(entry)
        entry["status"] = "✔ 已创建：{}".format(get_material_name(mat))
        try:
            report = entry.get("_last_connection_report", {})
            self.log("{}：连接成功 {}/{} 个通道".format(
                get_material_name(mat),
                len(report.get("connected", [])),
                len(report.get("required", []))
            ))
        except Exception:
            pass
        return True

    def create_pbrset_materials_by_scope(self, scope):
        entries = self.pbrset_entries_by_scope(scope)
        if not entries:
            self.log("没有可创建的PBR贴图套装")
            return

        # V32：创建材质只处理基本完整项，不自动赋给对象。
        basic_entries = []
        skipped = 0
        for entry in entries:
            if pbrset_is_creation_ready(entry, self.pbrset_normal_combo.currentText()):
                basic_entries.append(entry)
            else:
                entry["status"] = "跳过：需要手动映射"
                skipped += 1

        if not basic_entries:
            self.refresh_pbrset_tree()
            self.log("没有基本完整的PBR贴图套装可创建；已跳过 {} 项。".format(skipped))
            return

        created = 0
        failed = 0
        cancelled = False

        self.begin_operation("创建PBR材质", len(basic_entries), cancellable=True)
        for op_i, entry in enumerate(basic_entries, 1):
            if self.safe_ui_step(op_i - 1, len(basic_entries), "准备：{}".format(entry.get("name", ""))):
                entry["status"] = "已停止"
                cancelled = True
                break
            try:
                # 只有创建签名完全一致时才复用旧材质。
                # 如果切换了目标材质 / 法线偏好 / Gloss处理 / 手动映射，则重新生成。
                if self.pbrset_existing_material_matches_current_options(entry):
                    entry["status"] = "已存在：{}".format(get_material_name(entry.get("created_mat")))
                    continue

                if is_valid_material(entry.get("created_mat")):
                    self.invalidate_pbrset_created_material(entry, "目标材质或创建选项已变化")

                mat, notes = create_material_from_pbr_texture_set(
                    entry,
                    target_mode=self.pbrset_target_combo.currentText(),
                    prefix=self.pbrset_prefix.text(),
                    normal_preference=self.pbrset_normal_combo.currentText(),
                    gloss_mode=self.pbrset_gloss_combo.currentText()
                )
                if is_valid_material(mat):
                    if self.mark_pbr_material_creation_result(entry, mat, notes, require_confirm=True):
                        created += 1
                    else:
                        failed += 1
                    if notes:
                        self.log("{}：{}".format(get_material_name(mat), "；".join(notes[:8])))
                else:
                    entry["status"] = "创建失败"
                    failed += 1
            except Exception:
                entry["status"] = status_text_for_exception("创建PBR材质失败")
                failed += 1
            self.update_operation(op_i, len(basic_entries), entry.get("name", ""))

        self.finish_operation("PBR材质创建完成", cancelled=cancelled)
        self.refresh_pbrset_tree()
        try:
            self.material_usage_map = build_material_usage_map()
        except Exception:
            pass
        self.log("PBR贴图套装创建完成：成功 {}，失败 {}，跳过非基本完整 {}。创建不会自动赋给对象。".format(created, failed, skipped))

    def assign_created_pbrset_to_selection(self):
        # 只处理当前高亮选择的一个套装，避免生成/赋予太多材质
        entry = self.get_current_pbrset_entry()
        if not entry:
            self.log("请先在PBR贴图套装列表中高亮选择一个材质套装")
            return

        mat = self.ensure_pbrset_material_created(entry)
        if not is_valid_material(mat):
            return

        try:
            nodes = [o for o in rt.selection if is_valid_geometry(o) and not is_frozen(o)]
        except Exception:
            nodes = []
        if not nodes:
            self.log("没有可赋材质的选中几何体")
            return

        count = 0
        for obj in nodes:
            try:
                obj.material = mat
                count += 1
            except Exception:
                pass

        entry["status"] = "✔ 已赋给选中物体"
        self.refresh_pbrset_tree()
        self.log("已将 {} 赋给选中物体：{} 个".format(get_material_name(mat), count))

    def preview_pbrset_material_in_medit(self):
        self.put_pbrset_material_to_medit()



    def apply_pbr_connection_table_changes(self, entry, mat, changes):
        if not entry or not is_valid_material(mat) or not changes:
            return False

        report = entry.get("_last_connection_report", {}) or {}
        connected = list(report.get("connected", []))
        unconnected = list(report.get("unconnected", []))
        overrides = dict(entry.get("slot_overrides", {}))
        target = self.pbrset_target_combo.currentText()

        def same_item(a, b):
            return safe_str(a.get("channel", ""), "") == safe_str(b.get("channel", ""), "") and safe_abs_texture_path(a.get("path", "")).lower() == safe_abs_texture_path(b.get("path", "")).lower()

        changed = 0
        for chg in changes:
            tex = pbr_create_tex_for_report_item(chg)
            if tex is None:
                # 保留为未接入
                unconnected.append(dict(channel=chg.get("channel", ""), path=chg.get("path", ""), reason="贴图节点创建失败"))
                continue

            ok = pbr_try_set_specific_slot(mat, chg.get("prop", ""), tex, chg.get("channel", ""))

            # 移除原连接/未连接记录
            connected = [x for x in connected if not same_item(x, chg)]
            unconnected = [x for x in unconnected if not same_item(x, chg)]

            if ok:
                connected.append(dict(channel=chg.get("channel", ""), path=chg.get("path", ""), prop=chg.get("prop", "")))
                overrides[pbr_slot_override_key(target, chg.get("channel", ""))] = chg.get("prop", "")
                learn_pbr_slot(target, mat, chg.get("channel", ""), chg.get("prop", ""))
                changed += 1
            else:
                unconnected.append(dict(channel=chg.get("channel", ""), path=chg.get("path", ""), reason="槽位写入/验证失败：{}".format(chg.get("prop", ""))))

        entry["slot_overrides"] = overrides
        entry["_last_connection_report"] = dict(
            ok=(len(unconnected) == 0),
            required=report.get("required", []),
            connected=connected,
            unconnected=unconnected
        )
        entry["created_signature"] = self.current_pbrset_creation_signature(entry)

        if entry["_last_connection_report"].get("ok"):
            entry["status"] = "✔ 对应表修改完成"
        else:
            entry["status"] = "⚠ 对应表修改后仍未全接入"

        self.refresh_pbrset_tree()
        self.log("已应用连接对应表修改：{} 项".format(changed))
        return changed > 0

    def show_current_pbr_connection_table(self):
        entry = self.get_current_pbrset_entry()
        if not entry:
            self.log("请先高亮选择一个PBR贴图套装")
            return
        mat = entry.get("created_mat")
        report = entry.get("_last_connection_report", {})
        if not is_valid_material(mat) or not report:
            self.log("当前套装还没有连接对应表，请先创建材质")
            return

        dlg = PBRConnectionTableDialog(mat, entry, report, self.pbrset_target_combo.currentText(), self)
        if dialog_accepted(dlg):
            if dlg.result:
                self.apply_pbr_connection_table_changes(entry, mat, dlg.result)
                # 应用后再打开报告，给用户确认最终结果
                try:
                    PBRConnectionReportDialog(mat, entry, entry.get("_last_connection_report", {}), self).exec_()
                except Exception:
                    pass
            else:
                self.log("连接对应表没有修改")

    def show_current_pbr_connection_report(self):
        entry = self.get_current_pbrset_entry()
        if not entry:
            self.log("请先高亮选择一个PBR贴图套装")
            return
        mat = entry.get("created_mat")
        report = entry.get("_last_connection_report", {})
        if not is_valid_material(mat) or not report:
            self.log("当前套装还没有连接报告，请先创建材质")
            return
        try:
            PBRConnectionReportDialog(mat, entry, report, self).exec_()
        except Exception:
            self.log(status_text_for_exception("打开连接报告失败"))

    def manual_config_current_pbrset_slots(self):
        entry = self.get_current_pbrset_entry()
        if not entry:
            self.log("请先高亮选择一个PBR贴图套装")
            return

        mat = entry.get("created_mat")
        if not is_valid_material(mat):
            self.log("当前套装还没有有效创建材质。请先创建一次材质，再配置材质槽。")
            return

        report = entry.get("_last_connection_report", {})
        if not report or report.get("ok", False):
            self.log("当前材质没有未接入贴图，暂不需要手动材质槽配置")
            return

        self.try_manual_configure_pbr_material_slots(entry, mat)
        report = entry.get("_last_connection_report", {})
        if report and report.get("ok", False):
            entry["created_signature"] = self.current_pbrset_creation_signature(entry)
            entry["status"] = "✔ 手动槽位配置完成"
        else:
            entry["status"] = "⚠ 手动槽位后仍未全接入"
        self.refresh_pbrset_tree()

    def manual_map_current_pbrset_textures(self):
        entry = self.get_current_pbrset_entry()
        if not entry:
            self.log("请先高亮选择一个PBR贴图套装")
            return

        dlg = PBRTextureMappingDialog(entry, self)
        if dialog_accepted(dlg):
            apply_manual_pbrset_mapping(entry, dlg.result_mapping)
            self.refresh_pbrset_tree()
            self.log("已应用手动贴图映射：{}。当前通道：{}".format(entry.get("name", ""), pbr_channel_summary(entry.get("channels", {}))))

    def clear_pbrset_unknown_records(self):
        """
        清除"识别通道"为未识别的套装行。
        也就是 channels 里一个有效 PBR 通道都没有的条目。
        不删除磁盘文件，只从当前列表/库视图里移除。
        """
        before = len(self.pbrset_cache)

        def has_any_recognized_channel(entry):
            channels = entry.get("channels", {})
            for ch in PBR_CHANNEL_ORDER:
                if ch in ("Preview", "Unknown"):
                    continue
                if ch in channels:
                    return True
            return False

        self.pbrset_cache = [e for e in self.pbrset_cache if has_any_recognized_channel(e)]
        removed = before - len(self.pbrset_cache)
        self.refresh_pbrset_tree()
        self.log("已从列表中清除未识别套装：{} 个。注意：没有删除磁盘文件。".format(removed))

    def get_current_pbrset_entry(self):
        entries = self.pbrset_entries_by_scope("selected")
        if entries:
            return entries[0]
        return None

    def ensure_pbrset_material_created(self, entry):
        if not entry:
            return None
        mat = entry.get("created_mat")
        if is_valid_material(mat):
            if self.pbrset_existing_material_matches_current_options(entry):
                return mat
            self.invalidate_pbrset_created_material(entry, "目标材质或创建选项已变化")

        if not pbrset_is_creation_ready(entry, self.pbrset_normal_combo.currentText()):
            self.log("当前套装还不能创建，请先手动映射：{}".format(pbr_set_display_issues(entry, self.pbrset_normal_combo.currentText())))
            return None

        mat, notes = create_material_from_pbr_texture_set(
            entry,
            target_mode=self.pbrset_target_combo.currentText(),
            prefix=self.pbrset_prefix.text(),
            normal_preference=self.pbrset_normal_combo.currentText(),
            gloss_mode=self.pbrset_gloss_combo.currentText()
        )
        if is_valid_material(mat):
            if self.mark_pbr_material_creation_result(entry, mat, notes, require_confirm=True):
                if notes:
                    self.log("{}：{}".format(get_material_name(mat), "；".join(notes[:8])))
                self.refresh_pbrset_tree()
                return mat
            self.refresh_pbrset_tree()
            return None

        self.log("当前PBR套装材质创建失败")
        self.refresh_pbrset_tree()
        return None

    def pbrset_materials_for_medit(self, entries):
        mats = []
        skipped = 0
        failed = 0

        for entry in entries:
            if not pbrset_is_creation_ready(entry, self.pbrset_normal_combo.currentText()):
                entry["status"] = "跳过：需要手动映射"
                skipped += 1
                continue

            mat = self.ensure_pbrset_material_created(entry)
            if is_valid_material(mat):
                mats.append(mat)
            else:
                failed += 1

        self.refresh_pbrset_tree()
        return mats, skipped, failed

    def import_pbrsets_to_medit(self, scope="selected"):
        entries = self.pbrset_entries_by_scope(scope)
        if not entries:
            self.log("没有可导入材质编辑器的PBR套装")
            return

        mats, skipped, failed = self.pbrset_materials_for_medit(entries)
        if not mats:
            self.log("没有成功创建/导入的材质。跳过 {}，失败 {}".format(skipped, failed))
            return

        self.pbrset_medit_queue = mats
        self.pbrset_medit_page = 0
        self.load_pbrset_medit_page()
        if skipped or failed:
            self.log("材质编辑器导入准备完成：材质 {}，跳过非完整 {}，失败 {}".format(len(mats), skipped, failed))

    def load_pbrset_medit_page(self):
        mats = list(self.pbrset_medit_queue)
        if not mats:
            self.log("没有材质编辑器导入队列")
            return

        slot_count = max(1, medit_slot_count())
        total = len(mats)
        pages = int(math.ceil(float(total) / float(slot_count)))
        page = max(0, min(self.pbrset_medit_page, pages - 1))
        start = page * slot_count
        end = min(start + slot_count, total)
        page_mats = mats[start:end]

        try:
            for i, mat in enumerate(page_mats):
                try:
                    rt.meditMaterials[i + 1] = mat
                except Exception:
                    pass
            try:
                rt.openMedit()
            except Exception:
                pass

            self.log("已导入材质编辑器第 {}/{} 页：{}-{} / {} 个。{}".format(
                page + 1,
                pages,
                start + 1,
                end,
                total,
                "槽位不够时请点\"下一页材质球\"。" if pages > 1 else ""
            ))
        except Exception:
            self.log(status_text_for_exception("导入材质编辑器失败"))

    def import_next_pbrset_medit_page(self):
        if not self.pbrset_medit_queue:
            self.log("没有材质编辑器分页队列，请先导入高亮/打勾/全部")
            return

        slot_count = max(1, medit_slot_count())
        total = len(self.pbrset_medit_queue)
        pages = int(math.ceil(float(total) / float(slot_count)))
        if pages <= 1:
            self.log("当前导入数量没有超过材质编辑器槽位，不需要分页")
            return

        self.pbrset_medit_page = (self.pbrset_medit_page + 1) % pages
        self.load_pbrset_medit_page()

    def put_pbrset_material_to_medit(self, entry=None):
        if entry:
            mats, skipped, failed = self.pbrset_materials_for_medit([entry])
            if not mats:
                self.log("当前PBR套装无法导入材质编辑器")
                return
            self.pbrset_medit_queue = mats
            self.pbrset_medit_page = 0
            self.load_pbrset_medit_page()
        else:
            self.import_pbrsets_to_medit("selected")

    def on_pbrset_tree_context_menu(self, pos):
        try:
            item = self.pbrset_tree.itemAt(pos)
            if item:
                self.pbrset_tree.setCurrentItem(item)

            menu = QtWidgets.QMenu(self.pbrset_tree)
            act_map = menu.addAction("手动贴图映射")
            act_slot = menu.addAction("手动材质槽配置")
            act_report = menu.addAction("查看连接报告")
            act_table = menu.addAction("查看/修改连接对应表")
            act_create = menu.addAction("创建当前PBR材质")
            act_assign = menu.addAction("赋给场景选中物体")
            act_medit = menu.addAction("导入当前到材质编辑器")
            act_medit_checked = menu.addAction("导入打勾到材质编辑器")
            act_medit_all = menu.addAction("导入全部到材质编辑器")
            menu.addSeparator()
            act_open_folder = menu.addAction("打开贴图文件夹")
            act_clear_unknown = menu.addAction("清除所有未识别套装")
            act = menu.exec_(self.pbrset_tree.viewport().mapToGlobal(pos))

            entry = self.get_current_pbrset_entry()

            if act == act_map:
                self.manual_map_current_pbrset_textures()
            elif act == act_slot:
                self.manual_config_current_pbrset_slots()
            elif act == act_report:
                if entry and is_valid_material(entry.get("created_mat")):
                    PBRConnectionReportDialog(entry.get("created_mat"), entry, entry.get("_last_connection_report", {}), self).exec_()
                else:
                    self.log("当前套装还没有可查看的连接报告，请先创建材质")
            elif act == act_table:
                self.show_current_pbr_connection_table()
            elif act == act_create:
                if entry:
                    self.create_pbrset_materials_by_scope("selected")
            elif act == act_assign:
                self.assign_created_pbrset_to_selection()
            elif act == act_medit:
                self.put_pbrset_material_to_medit(entry)
            elif act == act_medit_checked:
                self.import_pbrsets_to_medit("checked")
            elif act == act_medit_all:
                self.import_pbrsets_to_medit("all")
            elif act == act_open_folder:
                if entry:
                    open_folder_in_os(entry.get("folder", ""))
            elif act == act_clear_unknown:
                self.clear_pbrset_unknown_records()
        except Exception:
            self.log(status_text_for_exception("PBR套装右键菜单失败"))

    def save_pbr_material_library(self):
        if not self.pbrset_cache:
            self.log("没有PBR套装可保存")
            return
        try:
            path, _ = QtWidgets.QFileDialog.getSaveFileName(
                self,
                "保存PBR材质库",
                os.path.join(current_scene_folder(), "InteriorSceneStudio_PBR_Library.json"),
                "PBR Library (*.json);;All Files (*.*)"
            )
            if not path:
                return
            data = dict(
                version=1,
                type="InteriorSceneStudio_PBR_Library",
                saved_at=datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
                options=dict(
                    folder=self.pbrset_folder.text(),
                    recursive=self.chk_pbrset_recursive.isChecked(),
                    group_by_folder=self.chk_pbrset_group_by_folder.isChecked(),
                    target=self.pbrset_target_combo.currentText(),
                    prefix=self.pbrset_prefix.text(),
                    normal=self.pbrset_normal_combo.currentText(),
                    gloss=self.pbrset_gloss_combo.currentText()
                ),
                entries=[serialize_pbrset_entry(e) for e in self.pbrset_cache]
            )
            with open(path, "w", encoding="utf-8") as f:
                json.dump(data, f, ensure_ascii=False, indent=2)
            self.log("已保存PBR材质库：{}，套装 {} 个".format(path, len(self.pbrset_cache)))
        except Exception:
            self.log(status_text_for_exception("保存PBR材质库失败"))

    def load_pbr_material_library(self):
        try:
            path, _ = QtWidgets.QFileDialog.getOpenFileName(
                self,
                "加载PBR材质库",
                current_scene_folder(),
                "PBR Library (*.json);;All Files (*.*)"
            )
            if not path:
                return
            with open(path, "r", encoding="utf-8") as f:
                data = json.load(f)
            entries = data.get("entries", [])
            self.pbrset_cache = [deserialize_pbrset_entry(e) for e in entries]
            opts = data.get("options", {})
            try:
                if opts.get("folder"):
                    self.pbrset_folder.setText(opts.get("folder"))
                self.chk_pbrset_recursive.setChecked(bool(opts.get("recursive", self.chk_pbrset_recursive.isChecked())))
                self.chk_pbrset_group_by_folder.setChecked(bool(opts.get("group_by_folder", self.chk_pbrset_group_by_folder.isChecked())))
                target = opts.get("target")
                if target:
                    idx = self.pbrset_target_combo.findText(target)
                    if idx >= 0:
                        self.pbrset_target_combo.setCurrentIndex(idx)
                if opts.get("prefix"):
                    self.pbrset_prefix.setText(opts.get("prefix"))
                normal = opts.get("normal")
                if normal:
                    idx = self.pbrset_normal_combo.findText(normal)
                    if idx >= 0:
                        self.pbrset_normal_combo.setCurrentIndex(idx)
                gloss = opts.get("gloss")
                if gloss:
                    idx = self.pbrset_gloss_combo.findText(gloss)
                    if idx >= 0:
                        self.pbrset_gloss_combo.setCurrentIndex(idx)
            except Exception:
                pass
            self.refresh_pbrset_tree()
            self.log("已加载PBR材质库：{}，套装 {} 个".format(path, len(self.pbrset_cache)))
        except Exception:
            self.log(status_text_for_exception("加载PBR材质库失败"))

    def choose_texture_output_dir(self):
        try:
            d = QtWidgets.QFileDialog.getExistingDirectory(self, "选择 UE 贴图输出根目录", current_scene_folder())
            if d:
                out = make_scene_texture_output_dir(d)
                self.texture_output_dir.setText(out)
                self.log("已设置输出目录：{}（如果同名文件夹已存在会直接使用，不会重复创建）".format(out))
                self.mark_existing_texture_outputs_from_dir(silent=False, refresh=True)
        except Exception:
            self.log(status_text_for_exception("选择贴图目录失败"))

    def sync_output_dir_to_scene(self):
        """重新读取当前 Max 文件路径，把输出目录更新为模型所在目录的同名子文件夹。"""
        try:
            scene_folder = current_scene_folder()
            scene_name = current_scene_base_name()
            if scene_name == "UntitledScene":
                self.log("当前场景尚未保存，无法自动获取模型路径。请先保存 Max 文件，或手动选择输出根目录。")
                try:
                    QtWidgets.QMessageBox.information(
                        self, "场景未保存",
                        "当前 Max 文件尚未保存到磁盘，无法自动识别模型路径。\n\n"
                        "请先保存场景（Ctrl+S），然后再点击【同步模型路径】按钮。"
                    )
                except Exception:
                    pass
                return
            out = make_scene_texture_output_dir(scene_folder)
            self.texture_output_dir.setText(out)
            self.log("已同步输出目录到模型路径：{}  （模型：{}）".format(out, scene_name))
        except Exception:
            self.log(status_text_for_exception("同步模型路径失败"))

    def open_current_texture_output_dir(self):
        try:
            out_dir = self.normalize_texture_output_dir_from_ui()
            if not out_dir:
                self.log("请先设置输出目录")
                return
            if not os.path.isdir(out_dir):
                try:
                    os.makedirs(out_dir, exist_ok=True)
                except Exception:
                    pass
            if open_folder_in_os(out_dir):
                self.log("已打开输出目录：{}".format(out_dir))
            else:
                self.log("打开输出目录失败：{}".format(out_dir))
        except Exception:
            self.log(status_text_for_exception("打开输出目录失败"))

    def refresh_pbrset_tree(self):
        self.pbrset_tree.clear()
        for entry in self.pbrset_cache:
            issues = pbr_set_issues(entry)
            try:
                normal_pref = self.pbrset_normal_combo.currentText()
            except Exception:
                normal_pref = "DirectX / DX（UE常用）"
            basic_ok = pbrset_is_creation_ready(entry, normal_pref)
            item = QtWidgets.QTreeWidgetItem()
            item.setText(0, entry.get("name", "PBR_Material"))
            item.setText(1, pbr_channel_summary(entry.get("channels", {})))
            item.setText(2, pbr_set_display_issues(entry, normal_pref))
            item.setText(3, pbrset_created_material_name_text(entry))
            item.setText(4, pbrset_created_type_text(entry))
            item.setText(5, entry.get("folder", ""))
            item.setText(6, entry.get("status", "等待"))
            prev = pbrset_preview_path(entry)
            if prev and os.path.exists(prev):
                try:
                    item.setIcon(0, QtGui.QIcon(prev))
                    if not entry.get("preview") and "BaseColor" in entry.get("channels", {}):
                        item.setText(2, (item.text(2) + "，无预览图-用BaseColor缩略图").strip("，"))
                except Exception:
                    pass
            try:
                item.setSizeHint(0, QtCore.QSize(120, 78))
            except Exception:
                pass
            self.set_item_checkable(item, True)

            try:
                if not basic_ok:
                    brush = QtGui.QBrush(QtGui.QColor("#D79A52"))
                    item.setForeground(2, brush)
                else:
                    brush = QtGui.QBrush(QtGui.QColor("#3fbf7f"))
                    item.setForeground(2, brush)
                    item.setForeground(3, brush)
                    item.setForeground(4, brush)
                    item.setForeground(6, brush)
            except Exception:
                pass

            self.pbrset_tree.addTopLevelItem(item)

        for i in range(7):
            self.pbrset_tree.resizeColumnToContents(i)
        self.log("PBR贴图套装数量：{}".format(len(self.pbrset_cache)))

    def normalize_texture_output_dir_from_ui(self):
        out_dir = self.texture_output_dir.text().strip() if hasattr(self, "texture_output_dir") else ""
        if not out_dir:
            return ""
        try:
            scene_name = current_scene_base_name()
            if os.path.basename(os.path.normpath(out_dir)).lower() != scene_name.lower():
                out_dir = make_scene_texture_output_dir(out_dir)
                self.texture_output_dir.setText(out_dir)
        except Exception:
            pass
        return out_dir

    def mark_existing_texture_outputs_from_dir(self, entries=None, silent=True, refresh=False):
        """
        扫描输出目录，如果已经有处理好的合格贴图，自动标记到列表。
        原则：已有合格输出就不重复处理。
        """
        entries = entries if entries is not None else self.texture_cache
        if not entries:
            if not silent:
                self.log("没有贴图列表，无法检查输出目录")
            return 0

        out_dir = self.normalize_texture_output_dir_from_ui()
        if not out_dir:
            if not silent:
                self.log("请先设置输出目录")
            return 0

        max_size = self.texture_max_size.value() if hasattr(self, "texture_max_size") else 4096
        require_p2 = self.chk_texture_require_power2.isChecked() if hasattr(self, "chk_texture_require_power2") else True
        ue_naming = self.chk_texture_ue_name.isChecked() if hasattr(self, "chk_texture_ue_name") else True

        found = 0
        existing_dir = os.path.isdir(out_dir)
        if not existing_dir:
            if not silent:
                self.log("输出目录尚不存在：{}".format(out_dir))
            return 0

        for e in entries:
            try:
                existing = find_existing_qualified_texture_output(
                    e,
                    out_dir,
                    max_size=max_size,
                    require_power2=require_p2,
                    ue_naming=ue_naming
                )
                if existing:
                    old = safe_abs_texture_path(e.get("output", ""))
                    e["output"] = existing
                    info = texture_output_status_info(e, max_size=max_size, require_power2=require_p2)
                    e["output_width"] = int(info.get("width", 0) or 0)
                    e["output_height"] = int(info.get("height", 0) or 0)
                    e["status"] = "已处理/输出成功"
                    if safe_abs_texture_path(old).lower() != safe_abs_texture_path(existing).lower():
                        found += 1
            except Exception:
                pass

        if refresh:
            self.refresh_texture_tree()

        if not silent:
            self.log("输出目录检查完成：发现/确认已处理贴图 {} 张。{}".format(found, "不会重复输出这些项。" if found else "没有发现新的已处理贴图。"))

        return found

    def clear_output_success_texture_entries(self):
        """
        从列表中移除已输出成功的项，不删除磁盘文件。
        优先范围：高亮选择 -> 打勾项 -> 列表全部。
        """
        max_size = self.texture_max_size.value() if hasattr(self, "texture_max_size") else 4096
        require_p2 = self.chk_texture_require_power2.isChecked() if hasattr(self, "chk_texture_require_power2") else True

        selected = self.selected_texture_entries()
        checked = self.texture_entries_by_scope("checked") if hasattr(self, "texture_tree") else []

        if selected:
            scope_entries = selected
            scope_name = "高亮选择"
        elif checked:
            scope_entries = checked
            scope_name = "打勾项"
        else:
            scope_entries = list(self.texture_cache)
            scope_name = "列表全部"

        remove_keys = set()
        for e in scope_entries:
            if texture_output_is_success(e, max_size=max_size, require_power2=require_p2):
                remove_keys.add(safe_abs_texture_path(e.get("path", "")).lower())

        if not remove_keys:
            self.log("没有可清除的已输出成功项（范围：{}）".format(scope_name))
            return

        before = len(self.texture_cache)
        self.texture_cache = [e for e in self.texture_cache if safe_abs_texture_path(e.get("path", "")).lower() not in remove_keys]
        removed = before - len(self.texture_cache)
        self.refresh_texture_tree()
        self.log("已从列表清除已输出成功项：{} 个（范围：{}）。磁盘文件没有删除。".format(removed, scope_name))

    def refresh_texture_tree(self):
        self.ignore_texture_selection = True
        self.texture_tree.clear()
        max_size = self.texture_max_size.value() if hasattr(self, "texture_max_size") else 4096
        require_p2 = self.chk_texture_require_power2.isChecked() if hasattr(self, "chk_texture_require_power2") else True
        only_problem = self.chk_texture_only_problem.isChecked() if hasattr(self, "chk_texture_only_problem") else False

        # 自动扫描输出目录，已有合格输出就标记为已处理，避免重复操作。
        try:
            if not getattr(self, "texture_running", False):
                self.mark_existing_texture_outputs_from_dir(entries=self.texture_cache, silent=True, refresh=False)
        except Exception:
            pass

        for entry in self.texture_cache:
            # 源图检查
            w = int(entry.get("width", 0) or 0)
            h = int(entry.get("height", 0) or 0)
            source_ok, source_issues = texture_entry_passes_streaming(entry, max_size=max_size, require_power2=require_p2)

            # 输出图检查：强制合格/输出后，以输出图状态作为后续是否可更新路径的依据
            out_info = texture_output_status_info(entry, max_size=max_size, require_power2=require_p2)
            output_ok = bool(out_info.get("ok"))
            has_output = bool(out_info.get("has_output"))

            if only_problem and (source_ok or output_ok):
                continue

            if output_ok:
                source_state = "源图合格" if source_ok else "源图不合格"
                output_state = "✔ 输出合格"
                issue_text = "可更新Max路径" if not source_ok else "源图和输出均合格"
                row_good = True
            elif has_output:
                source_state = "源图合格" if source_ok else "源图不合格"
                output_state = "✖ 输出不合格"
                issue_text = out_info.get("text", "输出不合格")
                row_good = False
            else:
                source_state = "✔ 源图合格" if source_ok else "✖ 源图不合格"
                output_state = "未输出"
                issue_text = "合格，可输出" if source_ok else "，".join(source_issues) + "；请外部整理或强制合格"
                row_good = source_ok

            pot = "是" if is_power_of_two_int(w) and is_power_of_two_int(h) else "否"
            qualified_size = texture_qualified_size_text(entry, out_info=out_info, max_size=max_size, force_power2=self.texture_force_mode if hasattr(self, "texture_force_mode") else False)
            item = QtWidgets.QTreeWidgetItem()
            item.setText(0, entry.get("file", ""))
            item.setText(1, entry.get("channel", "Unknown"))
            item.setText(2, texture_size_text(w, h))
            item.setText(3, qualified_size)
            item.setText(4, source_state)
            item.setText(5, output_state)
            item.setText(6, pot)
            item.setText(7, issue_text)
            item.setText(8, texture_owner_nodes_text(entry))
            item.setText(9, entry.get("path", ""))
            item.setText(10, entry.get("output", ""))
            item.setText(11, str(len(entry.get("texmaps", []))))
            item.setText(12, entry.get("status", "等待"))
            try:
                item.setToolTip(8, "\n".join(entry.get("owner_node_names", [])) or texture_owner_nodes_text(entry, max_names=10))
            except Exception:
                pass
            self.set_item_checkable(item, True)

            # 只在状态列使用红/绿文字，不再刷整行背景，避免和某些皮肤冲突。
            try:
                good_brush = QtGui.QBrush(QtGui.QColor("#3fbf7f"))
                bad_brush = QtGui.QBrush(QtGui.QColor("#ff6b6b"))
                neutral_brush = QtGui.QBrush(QtGui.QColor("#d0d6de"))
                if output_ok:
                    item.setForeground(3, good_brush)
                    item.setForeground(5, good_brush)
                    item.setForeground(7, good_brush)
                    item.setForeground(12, good_brush)
                elif has_output:
                    item.setForeground(3, bad_brush)
                    item.setForeground(5, bad_brush)
                    item.setForeground(7, bad_brush)
                    item.setForeground(12, bad_brush)
                elif source_ok:
                    item.setForeground(3, good_brush)
                    item.setForeground(4, good_brush)
                    item.setForeground(7, good_brush)
                else:
                    item.setForeground(4, bad_brush)
                    item.setForeground(7, bad_brush)
                item.setForeground(0, neutral_brush)
            except Exception:
                pass

            self.texture_tree.addTopLevelItem(item)

        for i in range(13):
            self.texture_tree.resizeColumnToContents(i)
        self.ignore_texture_selection = False
        self.log("UE贴图流送列表数量：{}；显示 {} 行".format(len(self.texture_cache), self.texture_tree.topLevelItemCount()))

    def texture_scan_buttons_enabled(self, enabled):
        for name in [
            "btn_scan_scene_textures", "btn_scan_selected_textures", "btn_scan_material_textures",
            "btn_clear_texture_list", "btn_remove_texture_rows", "btn_recheck_textures",
            "btn_select_texture_objects", "btn_open_source_texture_folder", "btn_open_source_texture_file",
            "btn_open_output_texture_folder", "btn_open_output_texture_file",
            "btn_texture_process_all", "btn_texture_process_checked", "btn_texture_process_selected",
            "btn_texture_force_checked", "btn_texture_force_selected", "btn_update_texture_paths",
            "btn_clear_output_textures", "btn_choose_texture_dir", "btn_check_texture_output_dir", "btn_sync_scene_output_dir"
        ]:
            try:
                getattr(self, name).setEnabled(enabled)
            except Exception:
                pass
        try:
            self.btn_texture_scan_stop.setEnabled(not enabled)
        except Exception:
            pass

    def build_texture_deep_scan_tasks(self, mode):
        tasks = []

        if mode == "scene":
            for mat_entry in collect_scene_material_entries():
                mat = mat_entry.get("mat")
                if is_valid_material(mat):
                    tasks.append(dict(kind="material", mat=mat, owner=get_material_name(mat), label=get_material_name(mat)))
            return tasks

        if mode == "selected":
            try:
                nodes = [o for o in rt.selection if is_valid_geometry(o)]
            except Exception:
                nodes = []
            for obj in nodes:
                try:
                    mat = obj.material
                except Exception:
                    mat = None
                if is_valid_material(mat):
                    obj_name = safe_str(getattr(obj, "name", ""), "Object")
                    tasks.append(dict(
                        kind="selected_object",
                        obj=obj,
                        mat=mat,
                        owner="{} / {}".format(obj_name, get_material_name(mat)),
                        label=obj_name
                    ))
            return tasks

        if mode == "material_list":
            for mat_entry in self.material_cache:
                mat = mat_entry.get("mat") if isinstance(mat_entry, dict) else None
                if is_valid_material(mat):
                    tasks.append(dict(kind="material", mat=mat, owner=get_material_name(mat), label=get_material_name(mat)))
            return tasks

        return tasks

    def start_texture_deep_scan(self, mode):
        if self.texture_running:
            self.log("贴图输出正在进行中，请先停止或完成输出")
            return
        if self.texture_scan_running:
            self.log("深度扫描正在进行中")
            return

        tasks = self.build_texture_deep_scan_tasks(mode)
        if not tasks:
            self.log("没有可深度扫描的对象或材质")
            return

        self.texture_scan_mode = mode
        self.texture_scan_queue = tasks
        self.texture_scan_entries = {}
        self.texture_scan_index = 0
        self.texture_scan_running = True

        self.texture_cache = []
        self.texture_tree.clear()
        self.bar.setMinimum(0)
        self.bar.setMaximum(len(tasks))
        self.bar.setValue(0)

        self.texture_scan_buttons_enabled(False)
        mode_name = {"scene": "场景", "selected": "选中物体", "material_list": "材质列表"}.get(mode, mode)
        self.begin_operation("{}深度扫描".format(mode_name), len(tasks), cancellable=True)
        self.log("开始分步深度扫描{}外部贴图：共 {} 项。".format(mode_name, len(tasks)))
        self.set_status("深度扫描准备中：0/{}，已发现0张贴图".format(len(tasks)))
        self.texture_scan_timer.start(1)

    def process_texture_deep_scan_step(self):
        if not self.texture_scan_running:
            return
        if self.check_operation_cancelled():
            self.finish_texture_deep_scan(cancelled=True)
            return

        total = len(self.texture_scan_queue)
        if self.texture_scan_index >= total:
            self.finish_texture_deep_scan(cancelled=False)
            return

        task = self.texture_scan_queue[self.texture_scan_index]
        label = safe_str(task.get("label", ""), "材质")
        before = len(self.texture_scan_entries)

        try:
            mat = task.get("mat")
            owner = task.get("owner", label)

            if is_valid_material(mat):
                collect_texture_nodes_from_value(mat, owner, "", self.texture_scan_entries, owner_ref=mat)

                if task.get("kind") == "selected_object":
                    obj = task.get("obj")
                    if is_valid_node(obj):
                        # 把当前选中物体直接记录为贴图使用者。
                        for e in self.texture_scan_entries.values():
                            try:
                                if mat in e.get("owner_materials", []) and obj not in e.get("owner_nodes", []):
                                    e.setdefault("owner_nodes", []).append(obj)
                            except Exception:
                                pass
        except Exception:
            self.log(status_text_for_exception("扫描失败：{}".format(label)))

        self.texture_scan_index += 1
        found = len(self.texture_scan_entries)
        added = found - before
        self.bar.setValue(self.texture_scan_index)
        msg = "深度扫描 {}/{}：{}；已发现{}张贴图{}".format(
            self.texture_scan_index,
            total,
            label,
            found,
            "，本项新增{}".format(added) if added else ""
        )
        self.set_status(msg)
        self.update_operation(self.texture_scan_index, total, "{}；已发现{}张贴图".format(label, found))

        # 让用户能看到"一项一项"的进度；每几项刷新一次列表，避免刷新过于频繁拖慢扫描。
        if self.texture_scan_index % 5 == 0 or self.texture_scan_index >= total:
            try:
                self.texture_cache = list(self.texture_scan_entries.values())
                self.refresh_texture_tree()
            except Exception:
                pass

        try:
            QtWidgets.QApplication.processEvents()
        except Exception:
            pass

    def finish_texture_deep_scan(self, cancelled=False):
        try:
            self.texture_scan_timer.stop()
        except Exception:
            pass

        self.texture_scan_running = False
        self.texture_scan_buttons_enabled(True)

        # 扫描结束后用 getClassInstances 兜底，捕获 ColorCorrection 等包装节点内的外部贴图
        if not cancelled:
            try:
                collect_all_bitmaps_from_scene_instances(self.texture_scan_entries)
            except Exception:
                pass

        try:
            self.texture_cache = enrich_texture_entries_with_scene_objects(list(self.texture_scan_entries.values()))
        except Exception:
            self.texture_cache = list(self.texture_scan_entries.values())

        self.refresh_texture_tree()
        self.bar.setValue(self.bar.maximum())

        mode_name = {"scene": "场景", "selected": "选中物体", "material_list": "材质列表"}.get(self.texture_scan_mode, self.texture_scan_mode)
        msg = "{}深度扫描{}：扫描 {} 项，发现 {} 张外部贴图。".format(
            mode_name,
            "已停止" if cancelled else "完成",
            self.texture_scan_index,
            len(self.texture_cache)
        )
        self.log(msg)
        self.finish_operation(msg, cancelled=cancelled)

        self.texture_scan_queue = []
        self.texture_scan_entries = {}
        self.texture_scan_index = 0
        self.texture_scan_mode = ""

    def stop_texture_deep_scan(self):
        if not self.texture_scan_running:
            self.log("当前没有正在进行的深度扫描")
            return
        self.finish_texture_deep_scan(cancelled=True)

    def scan_scene_textures(self):
        self.start_texture_deep_scan("scene")

    def scan_selected_object_textures(self):
        self.start_texture_deep_scan("selected")

    def scan_material_list_textures(self):
        self.start_texture_deep_scan("material_list")

    def clear_texture_list(self):
        if self.texture_running or self.texture_scan_running:
            self.log("当前正在处理或扫描贴图，不能清空列表；请先停止当前任务。")
            return
        self.texture_cache = []
        self.texture_tree.clear()
        self.log("UE贴图流送列表已清空")

    def clear_selected_texture_rows(self):
        """
        只清除 UE 贴图流送列表里的高亮选择。
        不能用通用 row->cache 方式，因为列表可能启用了过滤/排序，行号不一定等于 cache 索引。
        """
        if self.texture_running or self.texture_scan_running:
            self.log("当前正在处理或扫描贴图，不能清除选择；请先停止当前任务。")
            return

        entries = self.selected_texture_entries()
        if not entries:
            self.log("UE贴图流送：没有高亮选择可清除")
            return

        keys = set()
        for e in entries:
            keys.add(safe_abs_texture_path(e.get("path", "")).lower())

        before = len(self.texture_cache)
        self.texture_cache = [e for e in self.texture_cache if safe_abs_texture_path(e.get("path", "")).lower() not in keys]
        removed = before - len(self.texture_cache)
        self.refresh_texture_tree()

        if removed:
            names = [e.get("file", "贴图") for e in entries[:5]]
            more = " 等" if len(entries) > 5 else ""
            self.log("UE贴图流送：已清除选择 {} 项：{}{}".format(removed, "，".join(names), more))
        else:
            self.log("UE贴图流送：没有匹配的列表项被清除")

    def clear_missing_texture_entries(self):
        """从 UE 贴图流送列表中移除源文件在磁盘上不存在的条目。不修改场景材质。"""
        if self.texture_running or self.texture_scan_running:
            self.log("当前正在处理贴图，请先停止")
            return

        missing = [
            e for e in self.texture_cache
            if not safe_texture_exists(safe_abs_texture_path(e.get("path", "")))
        ]
        if not missing:
            self.log("UE贴图流送：列表中所有贴图文件均存在于磁盘，无需清除")
            return

        names_preview = "，".join(e.get("file", "?") for e in missing[:5])
        if len(missing) > 5:
            names_preview += " 等"

        try:
            reply = QtWidgets.QMessageBox.question(
                self,
                "清除不存在贴图",
                "发现 {} 张贴图的源文件在磁盘上不存在，确定从列表中移除？\n{}\n\n（只移除列表项，不修改场景材质）".format(
                    len(missing), names_preview
                ),
                QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.No,
                QtWidgets.QMessageBox.No
            )
            if reply != QtWidgets.QMessageBox.Yes:
                self.log("已取消清除不存在贴图")
                return
        except Exception:
            pass

        missing_keys = {safe_abs_texture_path(e.get("path", "")).lower() for e in missing}
        before = len(self.texture_cache)
        self.texture_cache = [
            e for e in self.texture_cache
            if safe_abs_texture_path(e.get("path", "")).lower() not in missing_keys
        ]
        removed = before - len(self.texture_cache)
        self.refresh_texture_tree()
        self.log("UE贴图流送：已移除 {} 个磁盘不存在的贴图：{}".format(removed, names_preview))

    def selected_texture_entries(self):
        entries = []
        for item in self.texture_tree.selectedItems():
            row = self.texture_tree.indexOfTopLevelItem(item)
            # 注意：如果启用了"只显示问题项"过滤，树行和 cache 行不一定完全一致；
            # 因此通过源路径反查更稳。
            src = item.text(9)
            for e in self.texture_cache:
                if safe_abs_texture_path(e.get("path", "")).lower() == safe_abs_texture_path(src).lower():
                    entries.append(e)
                    break
        return entries

    def on_texture_item_double_clicked(self, item, column):
        try:
            # 双击输出路径/输出状态列时打开输出贴图；其它列默认打开源贴图。
            if column in (5, 10, 12):
                path = item.text(10)
                label = "输出贴图"
            else:
                path = item.text(9)
                label = "源贴图"
            if not open_file_in_os(path):
                self.log("无法打开{}：{}".format(label, path))
        except Exception:
            self.log(status_text_for_exception("双击打开贴图失败"))

    def on_texture_tree_context_menu(self, pos):
        try:
            item = self.texture_tree.itemAt(pos)
            if item:
                self.texture_tree.setCurrentItem(item)

            menu = QtWidgets.QMenu(self.texture_tree)

            src_menu = menu.addMenu("源贴图")
            act_open_src = src_menu.addAction("打开源贴图")
            act_reveal_src = src_menu.addAction("源贴图所在位置并选中")
            act_folder_src = src_menu.addAction("打开源贴图文件夹")

            out_menu = menu.addMenu("输出贴图")
            act_open_out = out_menu.addAction("打开输出贴图")
            act_reveal_out = out_menu.addAction("输出贴图所在位置并选中")
            act_folder_out = out_menu.addAction("打开输出贴图文件夹")

            menu.addSeparator()
            act_select_objs = menu.addAction("选择使用该贴图的场景物体")
            act_clear_selected = menu.addAction("清除选择")
            act_clear_all = menu.addAction("清空列表")
            act_recheck = menu.addAction("重新检查选中贴图")
            act_force = menu.addAction("强制合格高亮选择")

            act = menu.exec_(self.texture_tree.viewport().mapToGlobal(pos))

            if act == act_open_src:
                self.open_selected_texture_files(output=False)
            elif act == act_reveal_src:
                self.reveal_selected_texture_files(output=False)
            elif act == act_folder_src:
                self.open_selected_texture_folders(output=False)
            elif act == act_open_out:
                self.open_selected_texture_files(output=True)
            elif act == act_reveal_out:
                self.reveal_selected_texture_files(output=True)
            elif act == act_folder_out:
                self.open_selected_texture_folders(output=True)
            elif act == act_select_objs:
                self.select_scene_objects_for_selected_textures()
            elif act == act_clear_selected:
                self.clear_selected_texture_rows()
            elif act == act_clear_all:
                self.clear_texture_list()
            elif act == act_recheck:
                self.recheck_texture_entries(self.selected_texture_entries())
            elif act == act_force:
                self.start_texture_streaming_process("selected", force=True)
        except Exception:
            self.log(status_text_for_exception("贴图右键菜单失败"))

    def texture_owner_nodes_for_entries(self, entries):
        nodes = []
        for e in entries or []:
            nodes.extend(e.get("owner_nodes", []))
        return unique_by_handle([o for o in nodes if is_valid_node(o)])

    def select_scene_objects_for_texture_entries(self, entries, quiet=False):
        nodes = self.texture_owner_nodes_for_entries(entries)
        if not nodes:
            if not quiet:
                self.log("没有找到使用该贴图的场景物体；可能只来自材质库或场景物体已删除。")
            return 0

        try:
            open_all_groups()
        except Exception:
            pass

        count = select_nodes_in_scene_fast(nodes, allow_func=lambda o: is_valid_geometry(o))
        if not quiet:
            frozen = len([o for o in nodes if is_valid_node(o) and is_frozen(o)])
            msg = "已选择使用贴图的场景物体：{} 个".format(count)
            if frozen:
                msg += "；冻结物体 {} 个未强制选择".format(frozen)
            self.log(msg)
        return count

    def select_scene_objects_for_selected_textures(self):
        entries = self.selected_texture_entries()
        if not entries:
            self.log("没有高亮选择的贴图")
            return
        self.select_scene_objects_for_texture_entries(entries, quiet=False)

    def on_texture_tree_selection_changed(self):
        try:
            if getattr(self, "ignore_texture_selection", False):
                return
            if not hasattr(self, "chk_texture_sync_objects") or not self.chk_texture_sync_objects.isChecked():
                return
            entries = self.selected_texture_entries()
            if not entries:
                return
            self.select_scene_objects_for_texture_entries(entries, quiet=True)
        except Exception:
            pass

    def reveal_selected_texture_files(self, output=False):
        entries = self.selected_texture_entries()
        if not entries:
            self.log("没有高亮选择的贴图")
            return
        opened = 0
        for e in entries:
            path = e.get("output", "") if output else e.get("path", "")
            if reveal_file_in_os(path):
                opened += 1
        self.log("已在文件夹中定位{}贴图：{} 个".format("输出" if output else "源", opened))

    def open_selected_texture_folders(self, output=False):
        entries = self.selected_texture_entries()
        if not entries:
            self.log("没有高亮选择的贴图")
            return
        opened = 0
        missing = 0
        seen = set()
        for e in entries:
            path = e.get("output", "") if output else e.get("path", "")
            path = safe_abs_texture_path(path)
            if output and (not path or not os.path.exists(path)):
                missing += 1
                continue
            folder = os.path.dirname(path)
            if folder and folder.lower() not in seen:
                seen.add(folder.lower())
                if open_folder_in_os(folder):
                    opened += 1
        extra = "；{} 项还没有输出贴图".format(missing) if output and missing else ""
        self.log("已打开{}贴图所在位置：{} 个文件夹{}".format("输出" if output else "源", opened, extra))

    def open_selected_texture_files(self, output=False):
        entries = self.selected_texture_entries()
        if not entries:
            self.log("没有高亮选择的贴图")
            return
        opened = 0
        missing = 0
        for e in entries:
            path = e.get("output", "") if output else e.get("path", "")
            path = safe_abs_texture_path(path)
            if output and (not path or not os.path.exists(path)):
                missing += 1
                continue
            if open_file_in_os(path):
                opened += 1
        extra = "；{} 项还没有输出贴图".format(missing) if output and missing else ""
        self.log("已用外部程序打开{}贴图：{} 个{}".format("输出" if output else "源", opened, extra))

    def recheck_texture_entries(self, entries=None):
        if entries is None:
            entries = self.texture_cache
        for e in entries:
            refresh_texture_entry_info(e)
            e["status"] = "已重新检查"
        self.mark_existing_texture_outputs_from_dir(entries=entries, silent=True, refresh=False)
        self.refresh_texture_tree()

    def recheck_all_textures(self):
        self.recheck_texture_entries(self.texture_cache)
        max_size = self.texture_max_size.value()
        require_p2 = self.chk_texture_require_power2.isChecked()
        bad = []
        for e in self.texture_cache:
            ok, issues = texture_entry_passes_streaming(e, max_size=max_size, require_power2=require_p2)
            if not ok:
                bad.append((e, issues))
        if bad:
            self.log("重新检查完成：仍有 {} 张不合格贴图，请继续整理。".format(len(bad)))
        else:
            self.log("重新检查完成：全部贴图已合格，可以输出到贴图目录。")


    def texture_entries_by_scope(self, scope):
        if scope == "all":
            return list(self.texture_cache)
        if scope == "checked":
            entries = []
            for i in range(self.texture_tree.topLevelItemCount()):
                item = self.texture_tree.topLevelItem(i)
                if item and item.checkState(0) == QT_CHECKED:
                    src = item.text(9)
                    for e in self.texture_cache:
                        if safe_abs_texture_path(e.get("path", "")).lower() == safe_abs_texture_path(src).lower():
                            entries.append(e)
                            break
            return entries
        if scope == "selected":
            return self.selected_texture_entries()
        return []

    def set_texture_buttons_enabled(self, enabled):
        for name in [
            "btn_scan_scene_textures", "btn_scan_selected_textures", "btn_scan_material_textures", "btn_clear_texture_list",
            "btn_texture_process_all", "btn_texture_process_checked", "btn_texture_process_selected",
            "btn_remove_texture_rows", "btn_choose_texture_dir", "btn_open_texture_output_dir", "btn_check_texture_output_dir", "btn_update_texture_paths", "btn_clear_output_textures", "btn_open_source_texture_folder", "btn_open_source_texture_file", "btn_open_output_texture_folder", "btn_open_output_texture_file", "btn_recheck_textures", "btn_select_texture_objects", "btn_texture_force_checked", "btn_texture_force_selected", "btn_sync_scene_output_dir"
        ]:
            try:
                getattr(self, name).setEnabled(enabled)
            except Exception:
                pass
        try:
            self.btn_texture_stop.setEnabled(not enabled)
        except Exception:
            pass
        try:
            if not self.texture_scan_running:
                self.btn_texture_scan_stop.setEnabled(False)
        except Exception:
            pass

    def set_texture_row_status(self, row, status, output=None):
        try:
            if row is None or row < 0:
                return
            item = self.texture_tree.topLevelItem(row)
            if not item:
                return
            if output is not None:
                item.setText(10, output)
            item.setText(12, status)
            try:
                self.texture_tree.scrollToItem(item)
            except Exception:
                pass
        except Exception:
            pass

    def start_texture_streaming_process(self, scope, force=False):
        entries = self.texture_entries_by_scope(scope)
        if not entries:
            self.log("没有可输出的贴图条目")
            return
        if self.texture_running:
            self.log("贴图处理正在进行中")
            return

        out_dir = self.texture_output_dir.text().strip()
        if not out_dir:
            self.log("请先设置输出目录")
            return

        # 输出目录必须是当前模型同名子文件夹
        scene_name = current_scene_base_name()
        if os.path.basename(os.path.normpath(out_dir)).lower() != scene_name.lower():
            out_dir = make_scene_texture_output_dir(out_dir)
            self.texture_output_dir.setText(out_dir)

        max_size = self.texture_max_size.value()
        require_p2 = self.chk_texture_require_power2.isChecked()

        # 处理前先扫描输出目录：已有合格输出的贴图直接标记为已处理，避免重复操作。
        self.mark_existing_texture_outputs_from_dir(entries=entries, silent=True, refresh=False)

        invalid = []
        valid = []
        row_path_map = {}
        self.texture_force_mode = bool(force)
        for i in range(self.texture_tree.topLevelItemCount()):
            item = self.texture_tree.topLevelItem(i)
            row_path_map[safe_abs_texture_path(item.text(9)).lower()] = i

        already_output = 0
        for e in entries:
            refresh_texture_entry_info(e)
            e["_row"] = row_path_map.get(safe_abs_texture_path(e.get("path", "")).lower(), -1)

            out_info = texture_output_status_info(e, max_size=max_size, require_power2=require_p2)
            if out_info.get("ok"):
                e["status"] = "已处理/输出成功"
                self.set_texture_row_status(e.get("_row", -1), "↷ 已处理/输出成功，跳过", e.get("output", ""))
                already_output += 1
                continue

            ok, issues = texture_entry_passes_streaming(e, max_size=max_size, require_power2=require_p2)
            if ok:
                valid.append(e)
            else:
                invalid.append((e, issues))
                self.set_texture_row_status(e.get("_row", -1), "不合格：{}".format("，".join(issues)))

        if already_output:
            self.log("已识别并跳过输出成功/已处理贴图：{} 张".format(already_output))

        if invalid and not force:
            self.refresh_texture_tree()
            msg = "发现 {} 张不合格贴图，已阻止输出。请打开所在位置，用外部软件整理后点击\"重新检查\"，或使用\"强制合格\"生成裁剪/缩放后的新贴图。".format(len(invalid))
            self.log(msg)
            try:
                QtWidgets.QMessageBox.warning(self, "贴图未全部合格", msg)
            except Exception:
                pass
            return

        if force:
            self.texture_queue = list(entries)
            try:
                ret = QtWidgets.QMessageBox.warning(
                    self,
                    "强制合格",
                    "强制合格会为选中贴图生成新的合格贴图：居中裁剪 + 缩放到2幂尺寸，不直接修改原图，且只降不升。\n\n当前处理引擎：{}\n\n是否继续？".format(self.current_texture_engine()),
                    QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.No
                )
                if ret != QtWidgets.QMessageBox.Yes:
                    self.log("已取消强制合格")
                    return
            except Exception:
                pass
        else:
            if not valid:
                self.log("没有合格贴图可输出")
                return
            self.texture_queue = valid

        # 已存在合格输出则跳过，避免重复处理/重复启动 ImageMagick。
        if self.chk_texture_skip_existing_good.isChecked():
            kept = []
            skipped = 0
            for e in self.texture_queue:
                existing = find_existing_qualified_texture_output(
                    e,
                    out_dir,
                    max_size=max_size,
                    require_power2=require_p2,
                    ue_naming=self.chk_texture_ue_name.isChecked()
                )
                if existing:
                    e["output"] = existing
                    e["status"] = "已跳过：输出已存在且合格"
                    self.set_texture_row_status(e.get("_row", -1), "↷ 已跳过：输出已存在且合格", existing)
                    skipped += 1
                else:
                    kept.append(e)
            self.texture_queue = kept
            if skipped:
                self.log("已跳过已有合格输出贴图：{} 张".format(skipped))

        if not self.texture_queue:
            self.refresh_texture_tree()
            self.log("所有目标贴图已有合格输出，无需再次处理")
            return

        # 超大贴图提示，避免用户误以为卡死。
        large_limit = self.texture_large_warn_size.value() if hasattr(self, "texture_large_warn_size") else 8192
        large = [e for e in self.texture_queue if max(int(e.get("width", 0) or 0), int(e.get("height", 0) or 0)) >= large_limit]
        if large:
            try:
                ret = QtWidgets.QMessageBox.warning(
                    self,
                    "超大贴图提示",
                    "检测到 {} 张贴图尺寸达到或超过 {}，处理可能较慢。\n\n建议单独处理 HDR/EXR/TIF 或超大贴图。是否继续？".format(len(large), large_limit),
                    QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.No
                )
                if ret != QtWidgets.QMessageBox.Yes:
                    self.log("已取消超大贴图处理")
                    return
            except Exception:
                pass
        self.texture_index = 0
        self.texture_running = True
        self.texture_update_records = []
        self.bar.setMaximum(max(len(self.texture_queue), 1))
        self.bar.setValue(0)

        for e in self.texture_queue:
            self.set_texture_row_status(e.get("_row", -1), "排队输出")

        self.set_texture_buttons_enabled(False)
        self.begin_operation("UE贴图流送输出", len(self.texture_queue), cancellable=True)
        self.log("{}UE贴图流送目录：{} 张；输出目录：{}".format("开始强制合格输出到" if self.texture_force_mode else "开始输出", len(self.texture_queue), out_dir))
        self.texture_timer.start(1)

    def process_texture_streaming_step(self):
        if not self.texture_running:
            return
        if self.check_operation_cancelled():
            self.stop_texture_streaming_process(forced=False)
            return
        if self.texture_index >= len(self.texture_queue):
            self.finish_texture_streaming_process()
            return

        entry = self.texture_queue[self.texture_index]
        row = entry.get("_row", -1)
        self.set_texture_row_status(row, "处理中...")
        self.set_status("正在处理贴图：{} ({}/{})".format(entry.get("file", ""), self.texture_index + 1, len(self.texture_queue)))
        self.update_operation(self.texture_index, len(self.texture_queue), "正在处理：{}".format(entry.get("file", "")))

        try:
            if self.texture_force_mode:
                ok, out_path, msg = self.process_texture_force_with_engine(entry)
            else:
                ok, out_path, msg = self.process_texture_copy_with_engine(entry)
            if ok:
                entry["output"] = out_path
                out_info = texture_output_status_info(entry, max_size=self.texture_max_size.value(), require_power2=self.chk_texture_require_power2.isChecked())
                if out_info.get("ok"):
                    entry["output_width"] = int(out_info.get("width", 0) or 0)
                    entry["output_height"] = int(out_info.get("height", 0) or 0)
                    entry["status"] = "输出成功"
                    self.set_texture_row_status(row, "✔ 输出成功 {}x{}".format(entry.get("output_width", 0), entry.get("output_height", 0)), out_path)
                else:
                    entry["status"] = "已输出但需复查"
                    self.set_texture_row_status(row, "已输出但需复查：{}".format(out_info.get("text", "")), out_path)
                self.log("贴图已输出：{} -> {}".format(entry.get("file", ""), os.path.basename(out_path)))
            else:
                entry["status"] = "失败"
                self.set_texture_row_status(row, "失败：{}".format(msg), out_path)
                self.log("贴图处理失败：{} -> {}".format(entry.get("file", ""), msg))
        except Exception:
            msg = status_text_for_exception("贴图处理异常")
            entry["status"] = msg
            self.set_texture_row_status(row, msg)
            self.log("{}：{}".format(entry.get("file", ""), msg))

        self.texture_index += 1
        self.bar.setValue(self.texture_index)
        self.update_operation(self.texture_index, len(self.texture_queue), "已处理：{}".format(entry.get("file", "")))
        if self.texture_index % 5 == 0:
            try: QtWidgets.QApplication.processEvents()
            except Exception: pass

    def stop_texture_streaming_process(self, forced=False):
        try: self.texture_timer.stop()
        except Exception: pass
        self.texture_running = False
        self.set_texture_buttons_enabled(True)
        self.finish_operation("已{}贴图处理：{}/{}".format("强制停止" if forced else "停止", self.texture_index, len(self.texture_queue)), cancelled=True)
        self.log("已{}贴图处理：{}/{}".format("强制停止" if forced else "停止", self.texture_index, len(self.texture_queue)))

    def finish_texture_streaming_process(self):
        try: self.texture_timer.stop()
        except Exception: pass
        self.texture_running = False
        self.set_texture_buttons_enabled(True)
        self.bar.setValue(len(self.texture_queue))
        self.refresh_texture_tree()
        self.finish_operation("UE贴图流送输出完成：{} 张".format(len(self.texture_queue)))
        self.log("UE贴图流送输出完成：{} 张。列表已刷新为源图/输出状态；确认输出无误后，点击\"关联UE输出贴图\"。".format(len(self.texture_queue)))

    def update_max_texture_paths_after_output(self):
        if not self.texture_cache:
            self.log("没有贴图列表，无法更新路径")
            return

        max_size = self.texture_max_size.value()
        require_p2 = self.chk_texture_require_power2.isChecked()

        # ── 第一步：检测源贴图本身就不存在的条目 ──────────────────────────
        missing_source = [e for e in self.texture_cache if not e.get("exists") and not texture_entry_output_ready(e)]
        if missing_source:
            names = "\n".join("  • " + e.get("file", e.get("path", "?")) for e in missing_source[:20])
            if len(missing_source) > 20:
                names += "\n  …… 共 {} 张".format(len(missing_source))
            try:
                ret = QtWidgets.QMessageBox.warning(
                    self,
                    "发现源贴图缺失",
                    "以下 {} 张贴图的源文件不存在，无法输出，也无法关联：\n\n{}\n\n"
                    "• 点击【是】：从列表中移除这些条目，继续关联其余贴图\n"
                    "• 点击【否】：跳过这些条目（保留在列表中），继续关联其余贴图\n"
                    "• 点击【取消】：中止，不做任何操作".format(len(missing_source), names),
                    QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.No | QtWidgets.QMessageBox.Cancel
                )
            except Exception:
                ret = QtWidgets.QMessageBox.Cancel
            if ret == QtWidgets.QMessageBox.Cancel:
                self.log("已取消：发现 {} 张源贴图缺失，用户中止操作。".format(len(missing_source)))
                return
            if ret == QtWidgets.QMessageBox.Yes:
                missing_set = set(id(e) for e in missing_source)
                self.texture_cache = [e for e in self.texture_cache if id(e) not in missing_set]
                self.refresh_texture_tree()
                self.log("已移除 {} 张源文件缺失的贴图条目。".format(len(missing_source)))
            else:
                self.log("已跳过 {} 张源文件缺失的贴图条目，继续处理其余。".format(len(missing_source)))

        # ── 第二步：正常校验（有源文件但没有输出 / 输出不合格）──────────────
        invalid = []
        not_output = []
        for e in self.texture_cache:
            if not texture_entry_output_ready(e):
                not_output.append(e)
                continue
            ok, issues = texture_output_passes_streaming(e, max_size=max_size, require_power2=require_p2)
            if not ok:
                invalid.append((e, issues))

        if not_output:
            self.log("仍有 {} 张贴图没有输出文件，不能关联。".format(len(not_output)))
            try:
                QtWidgets.QMessageBox.warning(self, "不能关联贴图", "有贴图尚未输出到目标目录，请先输出全部合格贴图或使用强制合格输出。")
            except Exception:
                pass
            return

        if invalid:
            self.log("仍有 {} 张输出贴图不合格，不能关联。".format(len(invalid)))
            try:
                QtWidgets.QMessageBox.warning(self, "不能关联贴图", "输出目录中仍有不合格贴图，请重新输出或检查。")
            except Exception:
                pass
            return

        import datetime
        ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")

        # ── 让用户选择重命名方式 ────────────────────────────────────────────
        rename_map = {}   # entry id -> 最终目标文件名（含扩展名），空串表示用自动方案
        auto_rename_all = False

        try:
            mode_ret = QtWidgets.QMessageBox.question(
                self,
                "确认关联 UE 输出贴图",
                "所有贴图已合格并输出。关联前会把输出贴图复制为带时间戳的新文件，"
                "再将 Max 材质路径指向新文件，防止与旧贴图冲突。\n\n"
                "• 点击【是】：逐张贴图让你手动确认/修改文件名\n"
                "• 点击【否】：全部自动命名（文件名_时间戳），直接关联\n"
                "• 点击【取消】：中止操作\n\n"
                "建议确认已保存过 Max 备份。",
                QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.No | QtWidgets.QMessageBox.Cancel
            )
        except Exception:
            mode_ret = QtWidgets.QMessageBox.Cancel

        if mode_ret == QtWidgets.QMessageBox.Cancel:
            self.log("已取消关联 UE 输出贴图")
            return

        auto_rename_all = (mode_ret == QtWidgets.QMessageBox.No)

        if not auto_rename_all:
            # 逐张弹对话框让用户输入文件名（不含路径，含扩展名）
            skip_rest = False
            for e in self.texture_cache:
                out = e.get("output", "")
                if not out or not os.path.isfile(out):
                    continue
                root, ext = os.path.splitext(out)
                default_name = os.path.basename("{}_{}{}".format(root, ts, ext))
                if skip_rest:
                    rename_map[id(e)] = ""   # 后续全部自动
                    continue
                try:
                    dialog = QtWidgets.QDialog(self)
                    dialog.setWindowTitle("重命名贴图副本")
                    dlg_lay = QtWidgets.QVBoxLayout(dialog)
                    dlg_lay.addWidget(QtWidgets.QLabel(
                        "源文件：{}\n输出文件：{}\n\n请输入关联用的新文件名（含扩展名）：".format(
                            e.get("file", ""), os.path.basename(out))
                    ))
                    name_edit = QtWidgets.QLineEdit(default_name)
                    dlg_lay.addWidget(name_edit)
                    btn_row = QtWidgets.QHBoxLayout()
                    btn_ok = QtWidgets.QPushButton("确定")
                    btn_skip = QtWidgets.QPushButton("跳过（用自动名）")
                    btn_auto_rest = QtWidgets.QPushButton("此后全部自动")
                    btn_cancel = QtWidgets.QPushButton("取消整个操作")
                    btn_row.addWidget(btn_ok)
                    btn_row.addWidget(btn_skip)
                    btn_row.addWidget(btn_auto_rest)
                    btn_row.addWidget(btn_cancel)
                    dlg_lay.addLayout(btn_row)
                    result = [None]
                    btn_ok.clicked.connect(lambda: result.__setitem__(0, "ok") or dialog.accept())
                    btn_skip.clicked.connect(lambda: result.__setitem__(0, "skip") or dialog.accept())
                    btn_auto_rest.clicked.connect(lambda: result.__setitem__(0, "auto_rest") or dialog.accept())
                    btn_cancel.clicked.connect(lambda: result.__setitem__(0, "cancel") or dialog.reject())
                    dialog.exec_()
                    action = result[0]
                except Exception:
                    action = "skip"

                if action == "cancel":
                    self.log("已取消关联 UE 输出贴图（用户在重命名对话框中取消）")
                    return
                elif action == "auto_rest":
                    skip_rest = True
                    rename_map[id(e)] = ""
                elif action == "ok":
                    custom = name_edit.text().strip()
                    rename_map[id(e)] = custom if custom else ""
                else:  # skip
                    rename_map[id(e)] = ""

        changed_total = 0
        renamed_total = 0
        cancelled = False
        self.begin_operation("关联UE输出贴图", len(self.texture_cache), cancellable=True)
        for op_i, e in enumerate(self.texture_cache, 1):
            if self.safe_ui_step(op_i - 1, len(self.texture_cache), e.get("file", "")):
                cancelled = True
                break
            out = e.get("output", "")
            changed = 0

            # 关联前把输出贴图复制为重命名副本（自动时间戳 or 用户自定义名）。
            relink_target = out
            if out and os.path.isfile(out):
                try:
                    folder = os.path.dirname(out)
                    root, ext = os.path.splitext(out)
                    custom_name = rename_map.get(id(e), "")
                    if custom_name:
                        # 确保用户输入的名字带正确扩展名
                        if not custom_name.lower().endswith(ext.lower()):
                            custom_name = os.path.splitext(custom_name)[0] + ext
                        stamped = os.path.join(folder, custom_name)
                    else:
                        stamped = "{}_{}{}".format(root, ts, ext)
                    if not os.path.exists(stamped):
                        shutil.copy2(out, stamped)
                    relink_target = stamped
                    renamed_total += 1
                except Exception:
                    relink_target = out  # 复制失败则回退原路径

            # V53：深度扫描可能发现的是某个材质/程序贴图对象上的字符串路径属性。
            # 先按记录到的具体属性写回，成功率比只猜 filename 更高。
            for src_obj, prop_name in e.get("path_sources", []):
                if src_obj is None or not prop_name:
                    continue
                try:
                    old_val = safe_str(getattr(src_obj, prop_name), "")
                    if old_val:
                        setattr(src_obj, prop_name, relink_target)
                        changed += 1
                except Exception:
                    pass

            for tex in e.get("texmaps", []):
                if update_texmap_path(tex, relink_target):
                    changed += 1
            changed_total += changed
            if changed:
                e["status"] = "已关联：{}".format(os.path.basename(relink_target))
                e["output"] = relink_target
            else:
                e["status"] = "无路径引用更新"
            self.update_operation(op_i, len(self.texture_cache), e.get("file", ""))

        self.finish_operation("关联UE输出贴图完成", cancelled=cancelled)
        self.refresh_texture_tree()
        self.log("已关联 UE 输出贴图：{} 处，重命名副本：{} 张（时间戳：{}）。{}".format(
            changed_total, renamed_total, ts, "（已停止）" if cancelled else ""))


    # ---------- 加载 ----------
    def reset_group_open_cache(self): self.groups_opened_for_sync = False
    def ensure_groups_opened_once(self):
        if not self.groups_opened_for_sync:
            c=open_all_groups(); self.groups_opened_for_sync=True; self.log("已打开组：{} 个".format(c))

    def load_selected_to_object_list(self): self.object_cache=get_selected_object_list_nodes(); self.reset_group_open_cache(); self.refresh_object_tree(); self.rb_list.setChecked(True)
    def add_selected_to_object_list(self): self.object_cache=unique_by_handle(self.object_cache+get_selected_object_list_nodes()); self.reset_group_open_cache(); self.refresh_object_tree(); self.rb_list.setChecked(True)
    def load_scene_to_object_list(self): self.object_cache=get_scene_object_list_nodes(); self.reset_group_open_cache(); self.refresh_object_tree(); self.rb_list.setChecked(True)
    def clear_object_list(self): self.object_cache=[]; self.object_tree.clear(); self.log("对象列表已清空")

    def load_selected_to_group_list(self): self.group_cache=get_selected_groups(); self.reset_group_open_cache(); self.refresh_group_tree()
    def add_selected_to_group_list(self): self.group_cache=unique_by_handle(self.group_cache+get_selected_groups()); self.reset_group_open_cache(); self.refresh_group_tree()
    def load_scene_to_group_list(self): self.group_cache=get_scene_groups(); self.reset_group_open_cache(); self.refresh_group_tree()
    def clear_group_list(self): self.group_cache=[]; self.group_tree.clear(); self.log("组列表已清空")

    def load_selected_to_light_list(self): self.light_cache=get_selected_lights(); self.refresh_light_tree()
    def add_selected_to_light_list(self): self.light_cache=unique_by_handle(self.light_cache+get_selected_lights()); self.refresh_light_tree()
    def load_scene_to_light_list(self): self.light_cache=get_scene_lights(); self.refresh_light_tree()
    def clear_light_list(self): self.light_cache=[]; self.light_tree.clear(); self.log("灯光列表已清空")

    def load_selected_to_camera_list(self): self.camera_cache=get_selected_cameras(); self.refresh_camera_tree()
    def add_selected_to_camera_list(self): self.camera_cache=unique_by_handle(self.camera_cache+get_selected_cameras()); self.refresh_camera_tree()
    def load_scene_to_camera_list(self): self.camera_cache=get_scene_cameras(); self.refresh_camera_tree()
    def clear_camera_list(self): self.camera_cache=[]; self.camera_tree.clear(); self.log("相机列表已清空")

    def load_selected_to_material_list(self): self.material_cache=collect_selected_material_entries(); self.material_usage_map=build_material_usage_map(); self.refresh_material_tree()
    def add_selected_to_material_list(self):
        combined=self.material_cache+collect_selected_material_entries(); used=set(); result=[]
        for e in combined:
            k=material_context_key(e)
            if k not in used: used.add(k); result.append(e)
        self.material_cache=result; self.material_usage_map=build_material_usage_map(); self.refresh_material_tree()
    def load_scene_to_material_list(self): self.material_cache=collect_scene_material_entries(); self.material_usage_map=build_material_usage_map(); self.refresh_material_tree()
    def clear_material_list(self): self.material_cache=[]; self.material_tree.clear(); self.log("材质列表已清空")

    def load_material_list_to_pbr_list(self):
        self.pbr_cache = list(self.material_cache)
        self.refresh_pbr_tree()
    def load_selected_to_pbr_list(self):
        self.pbr_cache = collect_selected_material_entries()
        self.refresh_pbr_tree()
    def add_selected_to_pbr_list(self):
        combined = self.pbr_cache + collect_selected_material_entries(); used=set(); result=[]
        for e in combined:
            k=material_context_key(e)
            if k not in used: used.add(k); result.append(e)
        self.pbr_cache = result; self.refresh_pbr_tree()
    def load_scene_to_pbr_list(self):
        self.pbr_cache = collect_scene_material_entries()
        self.refresh_pbr_tree()
    def clear_pbr_list(self):
        self.pbr_cache=[]; self.pbr_tree.clear(); self.log("材质标准化列表已清空")

    # ---------- 检测 / 筛选 ----------
    def scan_object_issues(self, load_scene=False):
        if load_scene:
            self.object_cache = get_scene_object_list_nodes(); self.rb_list.setChecked(True)
        if not self.object_cache:
            self.log("对象列表为空，无法扫描")
            return
        self.object_issue_map = {}
        total = len(self.object_cache)
        counts = {}
        cancelled = False
        self.begin_operation("对象问题扫描", total, cancellable=True)
        try:
            for i, obj in enumerate(self.object_cache):
                if self.safe_ui_step(i, total, safe_str(getattr(obj, "name", ""), "对象")):
                    cancelled = True
                    break
                try:
                    issues = detect_geometry_issues(obj)
                    self.object_issue_map[get_anim_handle(obj)] = issues
                    for issue in issues:
                        counts[issue] = counts.get(issue,0)+1
                except Exception:
                    self.log(status_text_for_exception("对象扫描失败：{}".format(safe_str(getattr(obj, "name", ""), ""))))
                self.update_operation(i + 1, total, safe_str(getattr(obj, "name", ""), "对象"))
                if i % 20 == 0:
                    QtWidgets.QApplication.processEvents()
        finally:
            self.finish_operation("对象问题扫描完成", cancelled=cancelled)
        self.refresh_object_tree()
        self.log("扫描{}：{} 个对象；{}".format("已停止" if cancelled else "完成", total, "，".join(["{} {}".format(k,v) for k,v in sorted(counts.items())]) if counts else "未发现问题"))

    def apply_object_filter(self):
        if not hasattr(self, "object_tree"):
            return
        try:
            mode = self.object_filter_combo.currentText()
        except Exception:
            mode = "全部"
        for row in range(self.object_tree.topLevelItemCount()):
            item = self.object_tree.topLevelItem(row)
            if row >= len(self.object_cache):
                continue
            obj = self.object_cache[row]
            issues = self.object_issue_map.get(get_anim_handle(obj), [])
            show = True
            if mode == "有问题": show = bool(issues and issues != ["组对象"])
            elif mode == "无材质": show = "无材质" in issues
            elif mode == "缩放异常": show = "缩放异常" in issues
            elif mode == "轴心异常": show = "轴心异常" in issues
            elif mode == "非等比缩放": show = "非等比缩放" in issues
            elif mode == "负缩放": show = "负缩放" in issues
            elif mode == "非Poly": show = "非Poly" in issues
            elif mode == "有修改器": show = "有修改器" in issues
            elif mode == "冻结": show = "冻结" in issues
            elif mode == "隐藏": show = "隐藏" in issues
            elif mode == "代理/外链": show = "代理/外链" in issues
            elif mode == "组对象": show = "组对象" in issues
            elif mode == "无问题": show = not issues
            item.setHidden(not show)

    # ---------- 选择同步 ----------
    def request_object_selection_sync(self):
        if self.ignore_object_selection or self.running:
            return
        if hasattr(self, "chk_sync_object_selection") and not self.chk_sync_object_selection.isChecked():
            return
        self.pending_sync_type="object"; self.selection_sync_timer.start(self.auto_sync_delay)
    def request_group_selection_sync(self):
        if self.ignore_group_selection or self.running or not self.chk_sync_group_selection.isChecked(): return
        self.pending_sync_type="group"; self.selection_sync_timer.start(self.auto_sync_delay)
    def request_light_selection_sync(self):
        if self.ignore_light_selection or self.running or not self.chk_sync_light_selection.isChecked(): return
        self.pending_sync_type="light"; self.selection_sync_timer.start(self.auto_sync_delay)
    def request_camera_selection_sync(self):
        if self.ignore_camera_selection or self.running or not self.chk_sync_camera_selection.isChecked(): return
        self.pending_sync_type="camera"; self.selection_sync_timer.start(self.auto_sync_delay)
    def request_material_selection_sync(self):
        if self.ignore_material_selection or self.running or not self.chk_sync_material_selection.isChecked(): return
        self.pending_sync_type="material"; self.selection_sync_timer.start(self.auto_sync_delay)

    def request_pbr_selection_sync(self):
        if self.ignore_pbr_selection or self.running or self.pbr_conversion_running:
            return
        try:
            if not self.chk_sync_pbr_selection.isChecked():
                return
        except Exception:
            return
        self.pending_sync_type="pbr"; self.selection_sync_timer.start(self.auto_sync_delay)

    def do_debounced_selection_sync(self):
        if self.pending_sync_type=="object": self.sync_object_selection_to_scene(False)
        elif self.pending_sync_type=="group": self.sync_group_selection_to_scene(False)
        elif self.pending_sync_type=="light": self.sync_light_selection_to_scene(False)
        elif self.pending_sync_type=="camera": self.sync_camera_selection_to_scene(False)
        elif self.pending_sync_type=="material": self.sync_material_selection_to_scene(False)
        elif self.pending_sync_type=="pbr": self.sync_pbr_selection_to_scene(False)
        self.pending_sync_type=None

    def selected_tree_nodes(self, tree, cache, predicate):
        nodes=[]
        for item in tree.selectedItems():
            row=tree.indexOfTopLevelItem(item)
            if row<0 or row>=len(cache): continue
            obj=cache[row]
            if predicate(obj) and not is_frozen(obj): nodes.append(obj)
        return unique_by_handle(nodes)
    def sync_object_selection_to_scene(self, force=False):
        nodes=self.selected_tree_nodes(self.object_tree,self.object_cache,lambda o:is_valid_geometry(o) or is_group_head(o))
        if not nodes: self.log("没有可同步选择的对象"); return
        if not force and len(nodes)>self.max_auto_sync_count: self.log("对象数量较多：{} 个，已跳过自动同步".format(len(nodes))); return
        self.ensure_groups_opened_once(); c=select_nodes_in_scene_fast(nodes,lambda o:is_valid_geometry(o) or is_group_head(o)); self.log("已同步选择对象：{} 个".format(c))
    def sync_group_selection_to_scene(self, force=False):
        nodes=self.selected_tree_nodes(self.group_tree,self.group_cache,is_group_head)
        if not nodes: self.log("没有可同步选择的组"); return
        if not force and len(nodes)>self.max_auto_sync_count: self.log("组数量较多：{} 个，已跳过自动同步".format(len(nodes))); return
        c=select_nodes_in_scene_fast(nodes,is_group_head); self.log("已同步选择组：{} 个".format(c))

    def sync_light_selection_to_scene(self, force=False):
        nodes=self.selected_tree_nodes(self.light_tree,self.light_cache,is_valid_light)
        if not nodes: self.log("没有可同步选择的灯光"); return
        if not force and len(nodes)>self.max_auto_sync_count: self.log("灯光数量较多：{} 个，已跳过自动同步".format(len(nodes))); return
        self.ensure_groups_opened_once(); c=select_nodes_in_scene_fast(nodes,is_valid_light); self.log("已同步选择灯光：{} 个".format(c))
    def sync_camera_selection_to_scene(self, force=False):
        nodes=self.selected_tree_nodes(self.camera_tree,self.camera_cache,is_valid_camera)
        if not nodes: self.log("没有可同步选择的相机"); return
        if not force and len(nodes)>self.max_auto_sync_count: self.log("相机数量较多：{} 个，已跳过自动同步".format(len(nodes))); return
        self.ensure_groups_opened_once(); c=select_nodes_in_scene_fast(nodes,is_valid_camera); self.log("已同步选择相机：{} 个".format(c))
    def sync_material_selection_to_scene(self, force=False):
        nodes=[]
        for item in self.material_tree.selectedItems():
            row=self.material_tree.indexOfTopLevelItem(item)
            if row<0 or row>=len(self.material_cache): continue
            mat=self.material_cache[row].get("mat")
            if is_valid_material(mat): nodes += self.material_usage_map.get(get_anim_handle(mat), [])
        nodes=unique_by_handle(nodes)
        if not nodes: self.log("没有找到使用该材质的物体"); return
        if not force and len(nodes)>self.max_auto_sync_count: self.log("材质关联物体较多：{} 个，已跳过自动同步".format(len(nodes))); return
        self.ensure_groups_opened_once(); c=select_nodes_in_scene_fast(nodes,is_valid_geometry); self.log("已选择使用该材质的物体：{} 个".format(c))

    def sync_pbr_selection_to_scene(self, force=False):
        nodes=[]
        try:
            if not self.material_usage_map:
                self.material_usage_map = build_material_usage_map()
        except Exception:
            pass

        for item in self.pbr_tree.selectedItems():
            row=self.pbr_tree.indexOfTopLevelItem(item)
            if row<0 or row>=len(self.pbr_cache): continue
            mat=self.pbr_cache[row].get("mat")
            if is_valid_material(mat):
                nodes += self.material_usage_map.get(get_anim_handle(mat), [])

        nodes=unique_by_handle(nodes)
        if not nodes:
            # 使用标准化列表时，材质引用可能刚更新过，重新构建一次再找。
            try:
                self.material_usage_map = build_material_usage_map()
                for item in self.pbr_tree.selectedItems():
                    row=self.pbr_tree.indexOfTopLevelItem(item)
                    if row<0 or row>=len(self.pbr_cache): continue
                    mat=self.pbr_cache[row].get("mat")
                    if is_valid_material(mat):
                        nodes += self.material_usage_map.get(get_anim_handle(mat), [])
                nodes=unique_by_handle(nodes)
            except Exception:
                pass

        if not nodes:
            self.log("材质标准化列表：没有找到使用该材质的物体")
            return
        if not force and len(nodes)>self.max_auto_sync_count:
            self.log("材质标准化列表关联物体较多：{} 个，已跳过自动同步".format(len(nodes)))
            return
        self.ensure_groups_opened_once()
        c=select_nodes_in_scene_fast(nodes,is_valid_geometry)
        self.log("材质标准化列表已选择使用该材质的物体：{} 个".format(c))

    # ---------- 组操作 ----------
    def checked_groups(self):
        return self.checked_nodes(self.group_tree, self.group_cache, is_group_head)

    def set_checked_groups_open_state(self, state=True):
        groups = self.checked_groups()
        if not groups:
            self.log("没有勾选可操作的组")
            return
        count = 0
        for g in groups:
            if set_group_open_state(g, state):
                count += 1
        self.reset_group_open_cache()
        self.refresh_group_tree()
        self.log("已{}勾选组：{} 个".format("打开" if state else "关闭", count))

    def open_all_groups_from_ui(self):
        c = open_all_groups()
        self.groups_opened_for_sync = True
        self.refresh_group_tree()
        self.log("已打开全部组：{} 个".format(c))

    def close_all_groups_from_ui(self):
        c = close_all_groups()
        self.groups_opened_for_sync = False
        self.refresh_group_tree()
        self.log("已关闭全部组：{} 个".format(c))

    # ---------- 勾选收集 ----------
    def checked_nodes(self, tree, cache, predicate):
        nodes=[]
        for i,obj in enumerate(cache):
            item=tree.topLevelItem(i)
            if item and item.checkState(0)==QT_CHECKED and predicate(obj): nodes.append(obj)
        return nodes
    def checked_material_entries(self):
        entries=[]
        for i,e in enumerate(self.material_cache):
            item=self.material_tree.topLevelItem(i)
            if item and item.checkState(0)==QT_CHECKED: entries.append(e)
        return entries

    # ---------- 材质标准化 ----------
    def pbr_entries_by_scope(self, scope):
        if scope == "all": return list(self.pbr_cache)
        if scope == "checked":
            entries=[]
            for i,e in enumerate(self.pbr_cache):
                item=self.pbr_tree.topLevelItem(i)
                if item and item.checkState(0)==QT_CHECKED: entries.append(e)
            return entries
        if scope == "selected":
            entries=[]
            for item in self.pbr_tree.selectedItems():
                row=self.pbr_tree.indexOfTopLevelItem(item)
                if 0 <= row < len(self.pbr_cache): entries.append(self.pbr_cache[row])
            return entries
        return []

    def add_pbr_plan_row_indices(self, plan):
        """给计划条目记录其在材质标准化列表里的行号，方便转换时实时更新状态。"""
        row_map = {}
        for i, entry in enumerate(self.pbr_cache):
            try:
                row_map[material_context_key(entry)] = i
            except Exception:
                pass
        for item in plan:
            try:
                item["row"] = row_map.get(material_context_key(item.get("entry", {})), -1)
            except Exception:
                item["row"] = -1
        return plan

    def set_pbr_row_status(self, row, status, action=None):
        try:
            if row is None or row < 0:
                return
            item = self.pbr_tree.topLevelItem(row)
            if not item:
                return
            if action is not None:
                item.setText(5, safe_str(action, ""))
            item.setText(6, safe_str(status, ""))
            try:
                self.pbr_tree.scrollToItem(item)
            except Exception:
                pass
        except Exception:
            pass

    def set_pbr_buttons_enabled_for_conversion(self, enabled):
        for name in [
            "btn_preview_pbr_all", "btn_preview_pbr_checked", "btn_preview_pbr_selected",
            "btn_undo_pbr", "btn_remove_pbr_rows"
        ]:
            try:
                getattr(self, name).setEnabled(enabled)
            except Exception:
                pass
        try:
            self.btn_stop_pbr.setEnabled(not enabled)
        except Exception:
            pass

    def preview_pbr_conversion_by_scope(self, scope):
        entries = self.pbr_entries_by_scope(scope)
        if not entries:
            self.log("没有可预览转换的材质条目"); return
        plan = make_pbr_conversion_plan(entries, skip_already=self.chk_pbr_skip_existing.isChecked(), try_complex=self.chk_pbr_try_complex.isChecked(), convert_multi_children=self.chk_pbr_convert_mso.isChecked(), target_mode=self.current_material_target_mode())
        plan = self.add_pbr_plan_row_indices(plan)
        dlg = PBRConversionPreviewDialog(plan, self)
        if not dialog_accepted(dlg):
            self.log("已取消材质标准化"); return
        self.apply_pbr_conversion_plan(plan)

    def apply_pbr_conversion_plan(self, plan):
        actionable = [p for p in plan if p.get("ok")]
        if not actionable:
            self.log("没有可执行的材质标准化条目")
            return

        if self.pbr_conversion_running:
            self.log("材质标准化正在进行中，请先停止当前任务")
            return

        self.pbr_conversion_queue = actionable
        self.pbr_conversion_index = 0
        self.pbr_conversion_converted = {}
        self.pbr_conversion_undo_refs = []
        self.pbr_conversion_notes = []
        self.pbr_conversion_running = True
        self.force_stop_requested = False

        self.bar.setMaximum(max(len(self.pbr_conversion_queue), 1))
        self.bar.setValue(0)

        for item in self.pbr_conversion_queue:
            self.set_pbr_row_status(item.get("row", -1), "排队中")

        self.set_pbr_buttons_enabled_for_conversion(False)

        try:
            rt.disableSceneRedraw()
            self.redraw_disabled = True
        except Exception:
            self.redraw_disabled = False

        self.log("开始材质标准化：{} 条。目标：{}".format(len(self.pbr_conversion_queue), self.current_material_target_mode()))
        self.pbr_conversion_timer.start(1)

    def convert_pbr_plan_item(self, item):
        entry = item.get("entry", {})
        mat = entry.get("mat")
        role = entry.get("role", "MAT")
        prefix = self.pbr_prefix.text() if hasattr(self, "pbr_prefix") else "MAT_STD"

        if not is_valid_material(mat):
            return False, 0, "无效材质"

        sub_entries = []

        if role == "MSO" or is_multi_material(mat):
            for sub in get_multi_material_subs(mat):
                sub_mat = sub.get("mat")
                if is_valid_material(sub_mat):
                    sub_entry = {
                        "mat": sub_mat,
                        "role": "SUB",
                        "parent": mat,
                        "parent_name": get_material_name(mat),
                        "slot": sub.get("slot", 0),
                        "mat_id": sub.get("mat_id", 0)
                    }
                    info = pbr_status_for_entry(
                        sub_entry,
                        skip_already=self.chk_pbr_skip_existing.isChecked(),
                        try_complex=self.chk_pbr_try_complex.isChecked(),
                        convert_multi_children=True,
                        target_mode=item.get("target_mode", self.current_material_target_mode())
                    )
                    if info.get("ok"):
                        sub_entries.append(sub_entry)
        else:
            sub_entries = [entry]

        if not sub_entries:
            return False, 0, "没有可转换的子材质/材质"

        changed_total = 0
        local_notes = []

        for conv_entry in sub_entries:
            old_mat = conv_entry.get("mat")
            if not is_valid_material(old_mat):
                continue

            old_h = get_anim_handle(old_mat)

            if old_h in self.pbr_conversion_converted:
                new_mat = self.pbr_conversion_converted[old_h]
                conv_notes = ["复用本次已生成的标准化材质"]
            else:
                new_mat, conv_notes = create_standardized_material_from_source(
                    old_mat,
                    prefix=prefix,
                    target_mode=item.get("target_mode", self.current_material_target_mode()),
                    preserve_external_maps=self.chk_pbr_simplify_maps.isChecked()
                )
                if not is_valid_material(new_mat):
                    local_notes.append("{}：创建目标材质失败".format(get_material_name(old_mat)))
                    continue
                self.pbr_conversion_converted[old_h] = new_mat

            refs = collect_exact_material_references(old_mat)
            changed_refs = []

            for ref in refs:
                if apply_material_reference(ref, new_mat):
                    changed_refs.append(ref)

            if changed_refs:
                self.pbr_conversion_undo_refs.append({"old": old_mat, "new": new_mat, "refs": changed_refs})
                changed_total += 1
                if conv_notes:
                    local_notes.append("{}：{}".format(get_material_name(old_mat), "；".join(conv_notes[:2])))
            else:
                local_notes.append("{}：未找到需要替换的引用".format(get_material_name(old_mat)))

        if changed_total:
            return True, changed_total, "完成：{} 个材质引用".format(changed_total)

        return False, 0, "未替换引用；{}".format("；".join(local_notes[:2]) if local_notes else "无变化")

    def process_pbr_conversion_step(self):
        if not self.pbr_conversion_running:
            return

        if self.force_stop_requested:
            self.stop_pbr_conversion(forced=True)
            return

        if self.pbr_conversion_index >= len(self.pbr_conversion_queue):
            self.finish_pbr_conversion()
            return

        item = self.pbr_conversion_queue[self.pbr_conversion_index]
        row = item.get("row", -1)
        mat_name = item.get("old", get_material_name(item.get("mat")))

        self.set_pbr_row_status(row, "转换中...", item.get("action", None))
        self.set_status("正在标准化材质：{} ({}/{})".format(
            mat_name,
            self.pbr_conversion_index + 1,
            len(self.pbr_conversion_queue)
        ))

        try:
            ok, count, msg = self.convert_pbr_plan_item(item)
            if ok:
                self.set_pbr_row_status(row, "✔ {}".format(msg))
                self.log("材质标准化成功：{} -> {}".format(mat_name, msg))
            else:
                self.set_pbr_row_status(row, "跳过/失败：{}".format(msg))
                self.log("材质标准化跳过/失败：{} -> {}".format(mat_name, msg))
        except Exception:
            msg = status_text_for_exception("材质标准化失败")
            self.set_pbr_row_status(row, msg)
            self.log("{}：{}".format(mat_name, msg))

        self.pbr_conversion_index += 1
        self.bar.setValue(self.pbr_conversion_index)

        if self.pbr_conversion_index % 5 == 0:
            try:
                QtWidgets.QApplication.processEvents()
            except Exception:
                pass

    def stop_pbr_conversion(self, forced=False):
        try:
            self.pbr_conversion_timer.stop()
        except Exception:
            pass

        self.pbr_conversion_running = False

        try:
            rt.enableSceneRedraw()
        except Exception:
            pass
        try:
            rt.redrawViews()
        except Exception:
            pass

        self.set_pbr_buttons_enabled_for_conversion(True)

        if self.pbr_conversion_undo_refs:
            self.pbr_undo_stack.append(self.pbr_conversion_undo_refs)

        if forced:
            self.log("已强制停止材质标准化：已完成 {} / {}。已完成部分可用撤回按钮恢复引用。".format(
                self.pbr_conversion_index,
                len(self.pbr_conversion_queue)
            ))
        else:
            self.log("已停止材质标准化：已完成 {} / {}。已完成部分可用撤回按钮恢复引用。".format(
                self.pbr_conversion_index,
                len(self.pbr_conversion_queue)
            ))

        self.material_usage_map = build_material_usage_map()
        self.refresh_material_tree()
        # 不刷新 pbr_tree，避免覆盖每一行的完成/失败状态
        self.redraw_disabled = False

    def finish_pbr_conversion(self):
        try:
            self.pbr_conversion_timer.stop()
        except Exception:
            pass

        self.pbr_conversion_running = False

        try:
            rt.enableSceneRedraw()
        except Exception:
            pass
        try:
            rt.redrawViews()
        except Exception:
            pass

        self.set_pbr_buttons_enabled_for_conversion(True)

        if self.pbr_conversion_undo_refs:
            self.pbr_undo_stack.append(self.pbr_conversion_undo_refs)

        self.material_usage_map = build_material_usage_map()
        self.refresh_material_tree()
        # 不刷新 pbr_tree，保留每一行转换结果，方便检查。
        self.bar.setValue(len(self.pbr_conversion_queue))
        self.redraw_disabled = False

        self.log("材质标准化完成：{} / {} 条；已替换引用 {} 组。".format(
            len(self.pbr_conversion_queue),
            len(self.pbr_conversion_queue),
            len(self.pbr_conversion_undo_refs)
        ))

    def undo_last_pbr_conversion(self):
        if not self.pbr_undo_stack:
            self.log("没有可撤回的材质标准化记录"); return
        records = self.pbr_undo_stack.pop(); restored = 0
        try: rt.disableSceneRedraw()
        except Exception: pass
        try:
            for rec in records:
                old_mat = rec.get("old")
                for ref in rec.get("refs", []):
                    if is_valid_material(old_mat) and apply_material_reference(ref, old_mat): restored += 1
            self.material_usage_map = build_material_usage_map(); self.refresh_material_tree(); self.refresh_pbr_tree()
            self.log("已撤回上次材质标准化，恢复材质引用 {} 处。新生成的标准化材质不会自动删除，可在材质编辑器中手动清理。".format(restored))
        except Exception:
            self.log(status_text_for_exception("撤回 材质标准化失败"))
        finally:
            try: rt.enableSceneRedraw()
            except Exception: pass
            try: rt.redrawViews()
            except Exception: pass

    # ---------- 重命名预览 / 应用 / 撤回 ----------
    def confirm_and_apply_rename(self, plan, title, refresh_callback):
        actionable=[p for p in plan if p.get("ok")]
        if not actionable:
            self.log("没有可执行的重命名条目")
            return
        dlg=RenamePreviewDialog(plan, title, self)
        if not dialog_accepted(dlg):
            self.log("已取消重命名")
            return
        undo=[]
        for p in actionable:
            if p.get("kind")=="node":
                ref=p.get("ref")
                if is_valid_node(ref):
                    undo.append(dict(kind="node", ref=ref, old=p.get("old"), new=p.get("new")))
                    ref.name=p.get("new")
            elif p.get("kind")=="material":
                mat=p.get("ref"); entry=p.get("entry",{})
                if is_valid_material(mat):
                    undo.append(dict(kind="material", ref=mat, old=p.get("old"), new=p.get("new"), entry=entry))
                    mat.name=p.get("new")
                    if entry.get("role")=="SUB":
                        parent=entry.get("parent"); slot=entry.get("slot",0)
                        if is_valid_material(parent) and slot: set_multi_slot_name(parent, slot, p.get("new"))
        if undo:
            self.rename_undo_stack.append(undo)
        refresh_callback()
        self.log("重命名完成：{} 条；可用顶部按钮撤回上次重命名".format(len(undo)))

    def undo_last_rename(self):
        if not self.rename_undo_stack:
            self.log("没有可撤回的重命名记录")
            return
        undo=self.rename_undo_stack.pop()
        count=0
        for item in reversed(undo):
            if item.get("kind")=="node":
                ref=item.get("ref")
                if is_valid_node(ref): ref.name=item.get("old"); count+=1
            elif item.get("kind")=="material":
                mat=item.get("ref")
                if is_valid_material(mat):
                    mat.name=item.get("old"); count+=1
                    entry=item.get("entry",{})
                    if entry.get("role")=="SUB":
                        parent=entry.get("parent"); slot=entry.get("slot",0)
                        if is_valid_material(parent) and slot: set_multi_slot_name(parent, slot, item.get("old"))
        self.refresh_object_tree(); self.refresh_group_tree(); self.refresh_light_tree(); self.refresh_camera_tree(); self.refresh_material_tree()
        self.log("已撤回上次重命名：{} 条".format(count))

    def scope_label(self, scope):
        return {"all": "列表全部", "checked": "打勾项", "selected": "高亮选择"}.get(scope, scope)

    def nodes_by_scope(self, tree, cache, predicate, scope):
        nodes = []
        if scope == "all":
            for obj in cache:
                if predicate(obj):
                    nodes.append(obj)
        elif scope == "checked":
            nodes = self.checked_nodes(tree, cache, predicate)
        elif scope == "selected":
            for item in tree.selectedItems():
                row = tree.indexOfTopLevelItem(item)
                if 0 <= row < len(cache):
                    obj = cache[row]
                    if predicate(obj):
                        nodes.append(obj)
        return unique_by_handle(nodes)

    def material_entries_by_scope(self, scope):
        entries = []
        if scope == "all":
            entries = list(self.material_cache)
        elif scope == "checked":
            entries = self.checked_material_entries()
        elif scope == "selected":
            used = set()
            for item in self.material_tree.selectedItems():
                row = self.material_tree.indexOfTopLevelItem(item)
                if 0 <= row < len(self.material_cache):
                    entry = self.material_cache[row]
                    key = material_context_key(entry)
                    if key in used:
                        continue
                    used.add(key)
                    entries.append(entry)
        return entries

    def rename_objects_by_scope(self, scope):
        nodes = self.nodes_by_scope(self.object_tree, self.object_cache, lambda o: is_valid_geometry(o) or is_group_head(o), scope)
        if not nodes:
            self.log("没有{}可重命名的对象".format(self.scope_label(scope)))
            return
        plan = make_node_rename_plan(nodes, build_object_name, self.obj_prefix.text(), self.obj_start_index.value(), self.obj_padding.value(), use_layer=self.chk_obj_layer.isChecked(), use_material=self.chk_obj_material.isChecked(), use_group_tag=self.chk_obj_group_tag.isChecked())
        self.confirm_and_apply_rename(plan, "对象重命名前预览 - {}".format(self.scope_label(scope)), self.refresh_object_tree)

    def rename_groups_by_scope(self, scope):
        nodes = self.nodes_by_scope(self.group_tree, self.group_cache, is_group_head, scope)
        if not nodes:
            self.log("没有{}可重命名的组".format(self.scope_label(scope)))
            return
        plan = make_node_rename_plan(nodes, build_group_name, self.group_prefix.text(), self.group_start_index.value(), self.group_padding.value(), use_layer=self.chk_group_layer.isChecked(), use_group_tag=self.chk_group_tag.isChecked(), use_member_count=self.chk_group_count.isChecked())
        self.confirm_and_apply_rename(plan, "组重命名前预览 - {}".format(self.scope_label(scope)), self.refresh_group_tree)

    def rename_lights_by_scope(self, scope):
        nodes = self.nodes_by_scope(self.light_tree, self.light_cache, is_valid_light, scope)
        if not nodes:
            self.log("没有{}可重命名的灯光".format(self.scope_label(scope)))
            return
        plan = make_node_rename_plan(nodes, build_light_name, self.light_prefix.text(), self.light_start_index.value(), self.light_padding.value(), use_layer=self.chk_light_layer.isChecked(), use_type=self.chk_light_type.isChecked(), use_light_tag=self.chk_light_tag.isChecked())
        self.confirm_and_apply_rename(plan, "灯光重命名前预览 - {}".format(self.scope_label(scope)), self.refresh_light_tree)

    def rename_cameras_by_scope(self, scope):
        nodes = self.nodes_by_scope(self.camera_tree, self.camera_cache, is_valid_camera, scope)
        if not nodes:
            self.log("没有{}可重命名的相机".format(self.scope_label(scope)))
            return
        plan = make_node_rename_plan(nodes, build_camera_name, self.camera_prefix.text(), self.camera_start_index.value(), self.camera_padding.value(), use_layer=self.chk_camera_layer.isChecked(), use_type=self.chk_camera_type.isChecked(), use_camera_tag=self.chk_camera_tag.isChecked())
        self.confirm_and_apply_rename(plan, "相机重命名前预览 - {}".format(self.scope_label(scope)), self.refresh_camera_tree)

    def rename_materials_by_scope(self, scope):
        entries = self.material_entries_by_scope(scope)
        if not entries:
            self.log("没有{}可重命名的材质".format(self.scope_label(scope)))
            return
        plan = make_material_rename_plan(entries, self.mat_prefix.text(), self.mat_start_index.value(), self.mat_padding.value(), use_class=self.chk_mat_class.isChecked(), use_parent=self.chk_mat_parent.isChecked())
        self.confirm_and_apply_rename(plan, "材质重命名前预览 - {}".format(self.scope_label(scope)), self.refresh_material_tree)

    # 兼容旧按钮/旧调用名
    def rename_checked_objects(self):
        self.rename_objects_by_scope("checked")
    def rename_checked_groups(self):
        self.rename_groups_by_scope("checked")
    def rename_checked_lights(self):
        self.rename_lights_by_scope("checked")
    def rename_checked_cameras(self):
        self.rename_cameras_by_scope("checked")
    def rename_checked_materials(self):
        self.rename_materials_by_scope("checked")

    # ---------- 修复 ----------
    def collect_repair_items(self):
        self.work_items=[]; self.work_rows=[]
        if self.rb_selected.isChecked():
            self.object_cache=get_selected_object_list_nodes(); self.refresh_object_tree()
            for i,obj in enumerate(self.object_cache):
                if is_valid_geometry(obj): self.work_items.append(obj); self.work_rows.append(i)
        elif self.rb_list.isChecked():
            for i,obj in enumerate(self.object_cache):
                item=self.object_tree.topLevelItem(i)
                if not item or item.checkState(0)!=QT_CHECKED: continue
                if is_valid_geometry(obj): self.work_items.append(obj); self.work_rows.append(i)
        elif self.rb_scene.isChecked():
            self.object_cache=get_scene_geometry(); self.refresh_object_tree(); self.work_items=self.object_cache[:]; self.work_rows=list(range(len(self.object_cache)))
    def start_repair(self):
        if self.running:
            return
        self.collect_repair_items()
        if not self.work_items:
            self.log("没有可修复的几何体")
            return

        plan = []
        new_items = []
        new_rows = []
        for obj, row in zip(self.work_items, self.work_rows):
            p = planned_repair_actions(
                obj,
                fix_material=self.chk_fix_material.isChecked(),
                fix_scale=self.chk_fix_scale.isChecked(),
                fix_pivot=self.chk_fix_pivot.isChecked(),
                skip_frozen=self.chk_skip_frozen_repair.isChecked()
            )
            p["row"] = row
            plan.append(p)
            if p.get("ok"):
                new_items.append(obj)
                new_rows.append(row)

        if not new_items:
            self.log("没有需要执行的修复项")
            return

        dlg = RepairPreviewDialog(plan, self)
        if not dialog_accepted(dlg):
            self.log("已取消修复")
            return

        if self.chk_auto_backup.isChecked():
            ok, msg = backup_current_max_file_copy(max_keep=self.get_backup_keep_count())
            if ok:
                self.log("执行前已复制备份：{}；最多保留：{}".format(msg, self.get_backup_keep_count()))
            else:
                self.log("执行前备份失败：{}".format(msg))
                try:
                    ret = QtWidgets.QMessageBox.warning(
                        self,
                        "备份失败",
                        "{}\\n\\n是否仍然继续修复？".format(msg),
                        QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.No
                    )
                    if ret != QtWidgets.QMessageBox.Yes:
                        self.log("用户取消修复：备份未完成")
                        return
                except Exception:
                    return

        self.work_items = new_items
        self.work_rows = new_rows
        self.index = 0
        self.running = True
        self.force_stop_requested = False
        self.btn_start.setEnabled(False)
        self.btn_stop.setEnabled(True)
        self.btn_force_stop.setEnabled(True)
        self.bar.setMaximum(len(self.work_items))
        self.bar.setValue(0)

        try:
            rt.disableSceneRedraw()
            self.redraw_disabled = True
        except Exception:
            self.redraw_disabled = False

        self.log("开始修复：{} 个几何体".format(len(self.work_items)))
        self.timer.start(1)

    def stop(self):
        if not self.running: return
        self.timer.stop(); self.running=False; self.btn_start.setEnabled(True); self.btn_stop.setEnabled(False); self.btn_force_stop.setEnabled(False); self.enable_redraw(); self.log("已停止：{}/{}".format(self.index,len(self.work_items)))
    def force_stop(self):
        # 如果 3ds Max 正卡在单个 MaxScript 调用里，按钮要等该调用返回后才能响应。
        # 响应后会立即清空队列、恢复视口刷新和按钮状态。
        self.force_stop_requested = True
        try: self.timer.stop()
        except Exception: pass
        try:
            if self.pbr_conversion_running:
                self.stop_pbr_conversion(forced=True)
        except Exception:
            pass
        try:
            if self.texture_running:
                self.stop_texture_streaming_process(forced=True)
        except Exception:
            pass
        self.work_items = []
        self.work_rows = []
        self.running = False
        try:
            self.btn_start.setEnabled(True); self.btn_stop.setEnabled(False); self.btn_force_stop.setEnabled(False)
        except Exception:
            pass
        self.enable_redraw()
        try: QtWidgets.QApplication.processEvents()
        except Exception: pass
        self.log("已强制停止：队列已清空，视口刷新已恢复")

    def process_step(self):
        if self.force_stop_requested or self.check_operation_cancelled():
            self.force_stop(); return
        if self.index>=len(self.work_items): self.finish(); return
        obj=self.work_items[self.index]; row=self.work_rows[self.index]
        ok,msg=repair_geometry(obj,fix_material=self.chk_fix_material.isChecked(),fix_scale=self.chk_fix_scale.isChecked(),fix_pivot=self.chk_fix_pivot.isChecked(),skip_frozen=self.chk_skip_frozen_repair.isChecked())
        item=self.object_tree.topLevelItem(row)
        if item:
            item.setText(0,safe_str(getattr(obj,"name",""),"<无效对象>")); item.setText(3,get_object_material_name(obj)); item.setText(7,msg)
            self.object_issue_map[get_anim_handle(obj)] = detect_geometry_issues(obj)
            item.setText(6,"，".join(self.object_issue_map.get(get_anim_handle(obj), [])) or "无问题")
        self.index+=1; self.bar.setValue(self.index)
        self.update_operation(self.index, len(self.work_items), safe_str(getattr(obj, "name", ""), "对象"))
        if self.index%10==0: QtWidgets.QApplication.processEvents()
    def finish(self):
        self.timer.stop(); self.running=False; self.btn_start.setEnabled(True); self.btn_stop.setEnabled(False); self.btn_force_stop.setEnabled(False); self.enable_redraw(); self.apply_object_filter(); self.finish_operation("修复完成：已检查 {} 个几何体".format(len(self.work_items))); self.log("修复完成：已检查 {} 个几何体".format(len(self.work_items)))
    def enable_redraw(self):
        if self.redraw_disabled:
            try: rt.enableSceneRedraw()
            except Exception: pass
            try: rt.redrawViews()
            except Exception: pass
        self.redraw_disabled=False
    def closeEvent(self,event):
        global _pbr_push_ui_instance
        try: self.stop()
        except Exception: pass
        try: self.selection_sync_timer.stop()
        except Exception: pass
        try:
            if self.texture_running:
                self.stop_texture_streaming_process(forced=True)
        except Exception:
            pass
        try:
            if self.texture_scan_running:
                self.stop_texture_deep_scan()
        except Exception:
            pass
        try:
            self.operation_cancel_requested = True
            if hasattr(self, "pbr_clipboard_timer"):
                self.pbr_clipboard_timer.stop()
        except Exception:
            pass
        try:
            if _pbr_push_ui_instance is self and _pbr_push_server_instance is not None:
                stop_pbr_push_server()
        except Exception:
            pass
        try:
            if _pbr_push_ui_instance is self:
                _pbr_push_ui_instance = None
        except Exception:
            pass
        event.accept()


# ============================================================
# 启动
# ============================================================

_ui_instance = None

def run():
    global _ui_instance
    try:
        if _ui_instance:
            _ui_instance.close()
    except Exception:
        pass
    _ui_instance = InteriorSceneStudioPro()
    # V17 默认不置顶，避免打开 Max 文件时遮挡"丢失贴图 / 单位转换 / Gamma"等系统对话框。
    # 需要置顶时，在插件顶部勾选"窗口置顶"。
    try:
        _ui_instance.setWindowFlags(QT_WINDOW)
    except Exception:
        pass
    _ui_instance.show()
    return _ui_instance

ui = run()
