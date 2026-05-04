# PBRStudio — 三端 PBR 工作流插件

[![Version](https://img.shields.io/badge/version-1.1.3-blue)](https://github.com/huangzhenwei2020/PBRStudio-Plugins)
[![License](https://img.shields.io/badge/license-MIT-green)](LICENSE)

一套覆盖 **Unreal Engine / Chrome 浏览器 / 3ds Max** 三端的 PBR 材质工作流工具集，主要面向室内设计、建筑可视化的 UE 资产整理流程。

---

## 目录

- [项目结构](#项目结构)
- [功能概述](#功能概述)
- [UE 插件 (PBRStudio)](#ue-插件-pbrstudio)
  - [安装方法](#ue-插件安装)
  - [功能介绍](#ue-插件功能)
  - [贴图套件](#贴图套件)
  - [下载库](#下载库)
  - [Chrome 推送服务](#chrome-推送服务)
- [Chrome 浏览器插件](#chrome-浏览器插件)
  - [安装方法](#chrome-插件安装)
  - [使用说明](#chrome-插件使用)
- [3ds Max 脚本](#3ds-max-脚本)
  - [安装与运行](#安装与运行)
  - [功能列表](#max-功能列表)
- [三端协作流程](#三端协作流程)
- [常见问题](#常见问题)
- [技术栈](#技术栈)

---

## 项目结构

```
PBRStudio-Plugins/
├── UE_Plugin/PBRStudio/          # Unreal Engine 编辑器插件
│   ├── Source/PBRStudio/         # C++ 源码
│   │   ├── Public/               # 头文件
│   │   │   ├── Models/           # 数据模型 (材质条目、下载站点等)
│   │   │   ├── Services/         # 服务层 (HTTP、下载、材质工厂等)
│   │   │   └── Widgets/          # Slate UI 组件
│   │   └── Private/              # 实现文件
│   ├── Resources/                # 插件资源 (图标、示例纹理)
│   ├── Config/                   # 插件配置
│   └── PBRStudio.uplugin         # UE 插件描述文件
├── Chrome_Extension/chrome_extension/  # Chrome 浏览器扩展
│   ├── popup.html / popup.js     # 弹出窗口 (推送控制面板)
│   ├── ai_panel.html / ai_panel.js    # AI 助手面板
│   ├── background.js             # Service Worker (后台服务)
│   ├── content.js                # 内容脚本 (页面链接提取)
│   ├── manifest.json             # Chrome 扩展清单
│   ├── _locales/zh_CN/           # 中文本地化
│   └── vendor/katex/             # KaTeX 数学公式渲染
├── 3dsMax_Script/                # 3ds Max Python 脚本
│   ├── InteriorSceneStudioPro_v95_topbar_width_collapse_clean.py  # 主脚本
│   └── _pbr_clean_utils.py       # 工具函数
└── Docs/                         # 使用文档
    ├── PBRStudio_三端插件使用说明.md
    ├── PBRStudio_三端插件使用说明.html
    └── PBRStudio_三端插件使用说明.pdf
```

---

## 功能概述

| 功能 | UE 插件 | Chrome 扩展 | 3ds Max 脚本 |
|------|:-------:|:-----------:|:------------:|
| PBR 贴图通道自动识别 | ✅ | — | ✅ |
| 材质实例自动创建 | ✅ | — | ✅ |
| 母材质模板管理 | ✅ | — | — |
| 网络资源下载与解压 | ✅ | — | ✅ |
| 网页链接推送 | 接收 | 发送 | 接收 |
| AI 场景助手 | — | ✅ | — |
| 对象批量修复与管理 | — | — | ✅ |
| 场景问题检测 | — | — | ✅ |
| 重命名工具 (可撤销) | — | — | ✅ |

---

## UE 插件 (PBRStudio)

**版本**: 1.1.3 | **模块类型**: Editor | **加载阶段**: PostEngineInit

### UE 插件安装

#### 项目级安装

1. 关闭 Unreal Editor
2. 将 `UE_Plugin/PBRStudio` 文件夹复制到你的 UE 项目目录下：

   ```
   你的项目/Plugins/PBRStudio
   ```

   > 如果项目没有 `Plugins` 文件夹，手动创建一个

3. 打开项目，若 UE 提示插件需要编译，点击确认
4. 在 UE 菜单栏中找到 **PBR Studio** 工具窗口

#### 非 C++ 项目

可以正常使用。本仓库不包含编译后的二进制文件 (Binaries 已加入 .gitignore)，因此首次使用需要在 Visual Studio 环境下编译一次插件。

### UE 插件功能

插件主窗口包含以下标签页：

#### 贴图套件

PBR 纹理扫描、材质实例创建、母材质管理的核心工具：

- **纹理扫描**: 自动扫描指定文件夹，识别 PBR 贴图通道 (BaseColor, Roughness, Metallic, Normal, AO, Height 等)
- **材质类型匹配**: 支持 Standard, Metal, Glass, Fabric, Leather, Wood, Stone, Tile, Plastic, Emissive, Water 等材质类型
- **母材质创建**: 根据材质类型自动创建对应的 UE 母材质
- **材质实例创建**: 批量创建材质实例，自动连接贴图参数
- **法线模式切换**: 支持 DirectX 和 OpenGL 法线格式

#### 下载库

网络资源下载和材质库整理工具：

- **接收 Chrome 推送**: 通过本地 HTTP 服务接收 Chrome 插件推送的下载链接
- **多格式解压**: 支持 `.zip`、`.rar`、`.7z` 压缩包自动解压
  - ZIP: 使用系统 PowerShell 解压
  - RAR/7Z: 自动调用本机 7-Zip 或 WinRAR
- **智能文件命名**: 自动从 `Content-Disposition`、URL 参数等提取文件名
- **材质库管理**: 解压后自动整理到材质库，与贴图套件共享路径
- **路径持久化**: 重开插件后自动恢复上次选择的路径

#### Chrome 推送服务

- 本地 HTTP 服务器，默认端口 `19528`
- 接收 Chrome 插件推送的下载 URL
- 在 UE 内部直接触发下载和解压流程

---

## Chrome 浏览器插件

**版本**: 1.1.3 | **清单版本**: Manifest V3

### Chrome 插件安装

1. 打开 Chrome，进入 `chrome://extensions/`
2. 开启右上角 **开发者模式**
3. 点击 **加载已解压的扩展程序**
4. 选择 `Chrome_Extension/chrome_extension` 文件夹
5. 修改插件文件后，在扩展管理页点击刷新按钮重新加载

### Chrome 插件使用

#### 弹出窗口 (Popup)

- **目标切换**: 选择推送到 **Max** (3ds Max) 或 **UE** (Unreal Engine)
- **端口配置**:
  - Max 默认端口: `19527`
  - UE 默认端口: `19528`
  - 两个端口独立配置，互不干扰
- **手动检测**: 点击检测按钮验证目标端是否在线
  - 不会自动检测 (避免界面闪烁和性能消耗)
- **推送本页链接**: 将当前网页 URL 推送到目标端
- **重试队列**: 重试之前发送失败的所有链接
- **打开 AI 面板**: 启动室内场景 AI 助手

#### 页面按钮 (Content Script)

浏览素材网站时，识别到可下载链接后，页面会出现两个快捷按钮：

- `Max` — 推送到 3ds Max
- `UE` — 推送到 Unreal Engine

#### AI 助手面板

- 室内设计 AI 对话助手
- 支持深色/浅色主题切换
- KaTeX 数学公式渲染
- 独立面板窗口

---

## 3ds Max 脚本

**文件名**: `InteriorSceneStudioPro_v95_topbar_width_collapse_clean.py`

面向室内设计 UE 导入前的场景整理工具，基于 pymxs (3ds Max Python API)。

### 安装与运行

1. 打开 3ds Max
2. 菜单栏: **脚本** → **运行脚本** (或拖拽 .py 文件到视口)
3. 选择 `3dsMax_Script/InteriorSceneStudioPro_v95_topbar_width_collapse_clean.py`

### Max 功能列表

#### 对象管理
- **对象修复**: 无材质对象自动补材质 / 缩放异常的 Reset XForm + 转 Poly / 轴心归底居中
- **批量对象操作**: 选中、隐藏、冻结、删除等
- **列表视图**: 对象 / 灯光 / 相机 / 材质四大列表，支持排序和筛选

#### 选择工具
- 只勾选中 / 只勾未选中 / 全选 / 取消 / 反转勾选
- 延迟同步选择 (避免列表卡顿)

#### 场景诊断
- 问题检测与筛选 (孤立顶点、零面积面、重叠面等)
- 一键定位问题对象

#### 重命名工具
- 批量重命名预览确认
- 支持前缀/后缀/替换/序号等多种模式
- **一键撤销**上次重命名操作

#### PBR 流程
- PBR 贴图通道智能识别 (BaseColor, Normal, Roughness, Metallic, AO 等)
- 预览图自动匹配
- PBR 下载库 (同 UE 端功能)
- 接收 Chrome 推送 (端口 19527)

#### 导入导出
- 材质自动关联到材质库
- 场景信息导出 CSV

---

## 三端协作流程

推荐工作流：

```
┌──────────────┐     推送链接      ┌──────────────┐
│  Chrome 插件  │ ──────────────→  │  UE / Max    │
│  (素材网站)   │   HTTP localhost  │  (接收端)     │
└──────────────┘                   └──────┬───────┘
                                          │
                                   下载 & 解压
                                          │
                                          ▼
                                   ┌──────────────┐
                                   │  贴图套件     │
                                   │  扫描 → 创建  │
                                   │  材质实例     │
                                   └──────────────┘
```

**具体步骤**:

1. UE 打开 PBRStudio → 下载库 → 启动 Chrome 推送服务 (端口 19528)
2. Chrome 插件选择 UE 目标，点击"手动检测"确认连接
3. 在素材网站 (AmbientCG, Poly Haven 等) 浏览，点击页面上的 `UE` 按钮推送链接
4. UE 下载库自动下载并解压
5. 切换到贴图套件页，扫描材质库文件夹
6. 选择材质类型、法线模式，批量创建材质实例
7. 将材质拖拽应用到场景对象

---

## 常见问题

### Chrome 推送失败

- 确认 UE 下载库中 Chrome 推送服务已启动 (端口 19528)
- 在 Chrome 插件弹窗中点击"手动检测"验证连通性
- 检查防火墙是否阻止了本地端口通信

### 非 C++ 项目无法打开插件

- 本仓库仅包含源码，需在 Visual Studio 环境下编译一次
- 安装 Visual Studio 2022 + UE 所需的 C++ 工具链
- 将项目临时转为 C++ 项目 (添加一个空 .cpp 文件即可)

### RAR / 7Z 无法自动解压

- 安装 [7-Zip](https://www.7-zip.org/) 或 [WinRAR](https://www.win-rar.com/) 后重试

### 新增下载站点没有出现

- 插件会自动合并内置站点配置
- 删除项目 `Saved/PBRStudio/PBRDownloadSites.json` 后重开 UE 即可恢复默认列表

### 3ds Max 脚本界面显示异常

- 脚本默认启用高 DPI 适配，如果界面元素过大或过小，可在脚本开头修改 DPI 缩放设置
- 需要 PySide2 或 PySide6 (3ds Max 2023+ 内置)

---

## 技术栈

| 组件 | 技术 |
|------|------|
| UE 插件 | C++ / Unreal Engine Slate UI |
| Chrome 扩展 | JavaScript (Manifest V3) / HTML / CSS |
| 3ds Max 脚本 | Python (pymxs) / PySide2/6 Qt |
| 通信协议 | HTTP (localhost) |
| 压缩处理 | PowerShell (ZIP) / 7-Zip / WinRAR |
| 公式渲染 | KaTeX |

---

## 版本历史

| 版本 | 日期 | 说明 |
|------|------|------|
| 1.1.3 | 2026-05-03 | 当前版本，三端功能完善 |

---

## 许可证

MIT License

---

*Made with PBRStudio Team*
