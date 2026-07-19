import datetime as _dt
import os
import shutil
import subprocess
import sys
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk


APP_NAME = "PBRStudio 三端一键安装器"
VERSION = "1.1.5"


def app_data_dir():
    root = os.environ.get("LOCALAPPDATA") or str(Path.home() / "AppData" / "Local")
    path = Path(root) / "PBRStudio"
    path.mkdir(parents=True, exist_ok=True)
    return path


def payload_root():
    base = Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parent))
    return base / "payload"


def timestamp():
    return _dt.datetime.now().strftime("%Y%m%d_%H%M%S")


def copy_file(src, dst):
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)


def copy_tree_replace(src, dst, backup_existing=True):
    if dst.exists():
        if backup_existing:
            backup = dst.with_name(dst.name + "_backup_" + timestamp())
            shutil.move(str(dst), str(backup))
        else:
            shutil.rmtree(dst)
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(src, dst)


def open_path(path):
    try:
        os.startfile(str(path))
    except Exception:
        subprocess.Popen(["explorer.exe", str(path)], shell=False)


def open_chrome_extensions():
    try:
        os.startfile("chrome://extensions")
    except Exception:
        try:
            subprocess.Popen(["cmd", "/c", "start", "", "chrome://extensions"], shell=False)
        except Exception:
            pass


def detect_max_profiles():
    profiles = []
    local = os.environ.get("LOCALAPPDATA")
    if not local:
        return profiles
    base = Path(local) / "Autodesk" / "3dsMax"
    if not base.exists():
        return profiles
    for candidate in base.glob("*"):
        if not candidate.is_dir():
            continue
        for lang in ("ENU", "CHS", "JPN", "KOR"):
            profile = candidate / lang
            if profile.exists():
                profiles.append(profile)
    return sorted(set(profiles), key=lambda p: str(p).lower())


def make_max_macro():
    return '''macroScript PBRStudio_InteriorSceneStudioPro
    category: "PBR Studio"
    internalCategory: "PBR Studio"
    tooltip: "Interior Scene Studio Pro - PBR Studio"
    buttonText: "Interior Scene Studio Pro"
    icon:#("PBRStudio", 1)
(
    local pyFile = pathConfig.appendPath (getDir #userScripts) "InteriorSceneStudioPro_v95_topbar_width_collapse_clean.py"
    if doesFileExist pyFile then
    (
        try (python.ExecuteFile pyFile)
        catch (messageBox ("PBRStudio 脚本执行失败:\\n" + getCurrentException()) title:"PBR Studio")
    )
    else
    (
        messageBox "未找到 InteriorSceneStudioPro 脚本。请重新运行安装器。" title:"PBR Studio"
    )
)
'''


def install_max(profile_dir):
    max_payload = payload_root() / "Max"
    if not max_payload.exists():
        raise RuntimeError("缺少 Max 安装文件 payload/Max")
    profile = Path(profile_dir)
    scripts = profile / "scripts"
    macros = profile / "usermacros"
    icons = profile / "usericons"
    scripts.mkdir(parents=True, exist_ok=True)
    macros.mkdir(parents=True, exist_ok=True)
    icons.mkdir(parents=True, exist_ok=True)

    copy_file(max_payload / "InteriorSceneStudioPro_v95_topbar_width_collapse_clean.py", scripts / "InteriorSceneStudioPro_v95_topbar_width_collapse_clean.py")
    copy_file(max_payload / "_pbr_clean_utils.py", scripts / "_pbr_clean_utils.py")
    copy_file(max_payload / "PBRStudio.bmp", icons / "PBRStudio.bmp")
    macro_file = macros / "PBRStudio-InteriorSceneStudioPro.mcr"
    macro_file.write_text(make_max_macro(), encoding="utf-8-sig")
    return [
        str(scripts / "InteriorSceneStudioPro_v95_topbar_width_collapse_clean.py"),
        str(scripts / "_pbr_clean_utils.py"),
        str(icons / "PBRStudio.bmp"),
        str(macro_file),
    ]


def install_chrome_extension(target_dir):
    src = payload_root() / "Chrome" / "chrome_extension"
    if not src.exists():
        raise RuntimeError("缺少 Chrome 扩展 payload/Chrome/chrome_extension")
    dst = Path(target_dir)
    copy_tree_replace(src, dst, backup_existing=False)
    return dst


def normalize_ue_project(path_text):
    if not path_text:
        return None
    path = Path(path_text)
    if path.is_file() and path.suffix.lower() == ".uproject":
        return path
    if path.is_dir():
        projects = sorted(path.glob("*.uproject"))
        if projects:
            return projects[0]
    return None


def install_ue_plugin(project_path):
    project = normalize_ue_project(project_path)
    if not project:
        raise RuntimeError("请选择 .uproject 文件，或包含 .uproject 的 UE 项目目录")
    src = payload_root() / "UE" / "PBRStudio"
    if not src.exists():
        raise RuntimeError("缺少 UE 插件 payload/UE/PBRStudio")
    plugins_dir = project.parent / "Plugins"
    dst = plugins_dir / "PBRStudio"
    copy_tree_replace(src, dst, backup_existing=True)
    return dst


class InstallerApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title(f"{APP_NAME} v{VERSION}")
        self.geometry("760x610")
        self.minsize(720, 560)

        self.max_profiles = detect_max_profiles()
        self.var_install_max = tk.BooleanVar(value=bool(self.max_profiles))
        self.var_install_chrome = tk.BooleanVar(value=True)
        self.var_install_ue = tk.BooleanVar(value=False)
        self.var_max_profile = tk.StringVar(value=str(self.max_profiles[0]) if self.max_profiles else "")
        self.var_chrome_dir = tk.StringVar(value=str(app_data_dir() / "ChromeExtension" / "chrome_extension"))
        self.var_ue_project = tk.StringVar(value="")

        self._build_ui()
        self._log(f"{APP_NAME} v{VERSION}")
        self._log(f"Payload: {payload_root()}")
        if self.max_profiles:
            self._log(f"检测到 3ds Max 配置：{self.var_max_profile.get()}")
        else:
            self._log("未自动检测到 3ds Max 用户配置；可手动选择 3ds Max ENU/CHS 目录。")

    def _build_ui(self):
        pad = {"padx": 14, "pady": 8}
        title = ttk.Label(self, text="PBRStudio 三端一键安装器", font=("Microsoft YaHei UI", 16, "bold"))
        title.pack(anchor="w", **pad)
        subtitle = ttk.Label(self, text="安装 3ds Max 脚本、准备 Chrome 扩展、复制 UE 插件到项目 Plugins 目录。")
        subtitle.pack(anchor="w", padx=14)

        body = ttk.Frame(self)
        body.pack(fill="both", expand=True, padx=14, pady=10)

        self._section_max(body).pack(fill="x", pady=(0, 10))
        self._section_chrome(body).pack(fill="x", pady=(0, 10))
        self._section_ue(body).pack(fill="x", pady=(0, 10))

        actions = ttk.Frame(body)
        actions.pack(fill="x", pady=(4, 8))
        self.btn_install = ttk.Button(actions, text="开始安装", command=self.install_clicked)
        self.btn_install.pack(side="left")
        ttk.Button(actions, text="打开 Chrome 扩展页", command=open_chrome_extensions).pack(side="left", padx=8)
        ttk.Button(actions, text="打开安装目录", command=lambda: open_path(app_data_dir())).pack(side="left")

        self.log = tk.Text(body, height=12, wrap="word")
        self.log.pack(fill="both", expand=True)

    def _section_max(self, parent):
        frame = ttk.LabelFrame(parent, text="1. 3ds Max")
        row = ttk.Frame(frame)
        row.pack(fill="x", padx=10, pady=8)
        ttk.Checkbutton(row, text="安装 Max 脚本和工具栏宏", variable=self.var_install_max).pack(side="left")
        ttk.Entry(row, textvariable=self.var_max_profile).pack(side="left", fill="x", expand=True, padx=8)
        ttk.Button(row, text="选择目录", command=self.choose_max_profile).pack(side="left")
        tip = ttk.Label(frame, text="目录示例：%LOCALAPPDATA%\\Autodesk\\3dsMax\\2025 - 64bit\\ENU")
        tip.pack(anchor="w", padx=10, pady=(0, 8))
        return frame

    def _section_chrome(self, parent):
        frame = ttk.LabelFrame(parent, text="2. Chrome 扩展")
        row = ttk.Frame(frame)
        row.pack(fill="x", padx=10, pady=8)
        ttk.Checkbutton(row, text="复制扩展到固定目录", variable=self.var_install_chrome).pack(side="left")
        ttk.Entry(row, textvariable=self.var_chrome_dir).pack(side="left", fill="x", expand=True, padx=8)
        ttk.Button(row, text="选择目录", command=self.choose_chrome_dir).pack(side="left")
        tip = ttk.Label(frame, text="复制后需在 chrome://extensions 打开“开发者模式”，选择这个目录加载已解压扩展。")
        tip.pack(anchor="w", padx=10, pady=(0, 8))
        return frame

    def _section_ue(self, parent):
        frame = ttk.LabelFrame(parent, text="3. Unreal Engine")
        row = ttk.Frame(frame)
        row.pack(fill="x", padx=10, pady=8)
        ttk.Checkbutton(row, text="复制 UE 插件到项目", variable=self.var_install_ue).pack(side="left")
        ttk.Entry(row, textvariable=self.var_ue_project).pack(side="left", fill="x", expand=True, padx=8)
        ttk.Button(row, text="选择 .uproject", command=self.choose_ue_project).pack(side="left")
        ttk.Button(row, text="选择项目目录", command=self.choose_ue_dir).pack(side="left", padx=(6, 0))
        tip = ttk.Label(frame, text="会复制到 <项目目录>\\Plugins\\PBRStudio；如果已有旧插件，会先备份。")
        tip.pack(anchor="w", padx=10, pady=(0, 8))
        return frame

    def choose_max_profile(self):
        path = filedialog.askdirectory(title="选择 3ds Max 用户配置目录 ENU/CHS")
        if path:
            self.var_max_profile.set(path)
            self.var_install_max.set(True)

    def choose_chrome_dir(self):
        path = filedialog.askdirectory(title="选择 Chrome 扩展复制目录")
        if path:
            self.var_chrome_dir.set(path)
            self.var_install_chrome.set(True)

    def choose_ue_project(self):
        path = filedialog.askopenfilename(title="选择 UE .uproject 文件", filetypes=[("Unreal Project", "*.uproject"), ("All files", "*.*")])
        if path:
            self.var_ue_project.set(path)
            self.var_install_ue.set(True)

    def choose_ue_dir(self):
        path = filedialog.askdirectory(title="选择 UE 项目目录")
        if path:
            self.var_ue_project.set(path)
            self.var_install_ue.set(True)

    def _log(self, text):
        self.log.insert("end", str(text) + "\n")
        self.log.see("end")
        self.update_idletasks()

    def install_clicked(self):
        self.btn_install.configure(state="disabled")
        self.after(50, self._install)

    def _install(self):
        errors = []
        try:
            if self.var_install_max.get():
                try:
                    self._log("安装 3ds Max 端...")
                    files = install_max(self.var_max_profile.get())
                    for item in files:
                        self._log("  写入: " + item)
                except Exception as exc:
                    errors.append("3ds Max: " + str(exc))
                    self._log("  失败: " + str(exc))

            if self.var_install_chrome.get():
                try:
                    self._log("准备 Chrome 扩展...")
                    dst = install_chrome_extension(self.var_chrome_dir.get())
                    self._log("  已复制到: " + str(dst))
                    open_path(dst)
                    open_chrome_extensions()
                except Exception as exc:
                    errors.append("Chrome: " + str(exc))
                    self._log("  失败: " + str(exc))

            if self.var_install_ue.get():
                try:
                    self._log("安装 UE 插件...")
                    dst = install_ue_plugin(self.var_ue_project.get())
                    self._log("  已复制到: " + str(dst))
                except Exception as exc:
                    errors.append("UE: " + str(exc))
                    self._log("  失败: " + str(exc))

            log_path = app_data_dir() / "installer_last.log"
            log_path.write_text(self.log.get("1.0", "end"), encoding="utf-8")
            if errors:
                messagebox.showwarning("安装完成但有问题", "\n".join(errors))
            else:
                messagebox.showinfo("安装完成", "PBRStudio 三端安装步骤已完成。")
        finally:
            self.btn_install.configure(state="normal")


def main():
    app = InstallerApp()
    app.mainloop()


if __name__ == "__main__":
    main()
