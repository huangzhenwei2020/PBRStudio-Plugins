# PBRStudio — 三端 PBR 工作流插件

[![Version](https://img.shields.io/badge/version-1.1.4-blue)](https://github.com/huangzhenwei2020/PBRStudio-Plugins)
[![License](https://img.shields.io/badge/license-MIT-green)](LICENSE)

一套覆盖 **Unreal Engine / Chrome 浏览器 / 3ds Max** 三端的 PBR 材质工作流工具集，主要面向室内设计、建筑可视化的 UE 资产整理流程。

---

## 目录

- [快速开始](#快速开始)
- [项目结构](#项目结构)
- [3ds Max 脚本](#3ds-max-脚本)
  - [一键安装 (MZP)](#一键安装-mzp)
  - [手动安装](#手动安装)
  - [功能详解](#max-功能详解)
- [Chrome 浏览器插件](#chrome-浏览器插件)
  - [安装方法](#chrome-插件安装)
  - [使用说明](#chrome-插件使用)
- [UE 插件 (PBRStudio)](#ue-插件-pbrstudio)
  - [安装方法](#ue-插件安装)
  - [功能介绍](#ue-插件功能)
- [三端协作流程](#三端协作流程)
- [常见问题](#常见问题)
- [技术栈](#技术栈)
- [版本历史](#版本历史)
- [许可证](#许可证)

---

## 快速开始

| 你要做什么 | 用哪个 | 怎么装 |
|-----------|--------|--------|
| 在 3ds Max 里整理场景 | **3ds Max 脚本** | 拖 `PBRStudio_3dsMax_v*.mzp` 进 Max 视口 |
| 在浏览器里推送素材链接 | **Chrome 扩展** | Chrome 扩展管理页加载 `Chrome_Extension/chrome_extension/` |
| 在 UE 里创建 PBR 材质 | **UE 插件** | 复制 `UE_Plugin/PBRStudio/` 到项目 `Plugins/` 目录 |

---

## 项目结构

```
PBRStudio-Plugins/
├── 3dsMax_Script/                # 3ds Max Python 脚本（源文件）
│   ├── install.ms                # MZP 安装入口脚本
│   ├── InteriorSceneStudioPro_v95_topbar_width_collapse_clean.py  # 主脚本（约 20,000 行）
│   └── _pbr_clean_utils.py       # 独立工具函数模块
│
├── Chrome_Extension/chrome_extension/  # Chrome 浏览器扩展
│   ├── popup.html / popup.js     # 弹出窗口（推送控制面板）
│   ├── ai_panel.html / ai_panel.js    # AI 助手面板
│   ├── background.js             # Service Worker（后台服务）
│   ├── content.js                # 内容脚本（页面链接提取 & 快捷按钮）
│   ├── manifest.json             # Chrome 扩展清单 (Manifest V3)
│   ├── _locales/zh_CN/           # 中文本地化
│   ├── icon16.png / icon48.png / icon128.png  # 扩展图标
│   └── vendor/katex/             # KaTeX 数学公式渲染
│
├── UE_Plugin/PBRStudio/          # Unreal Engine 编辑器插件
│   ├── Source/PBRStudio/         # C++ 源码
│   │   ├── Public/               # 头文件
│   │   │   ├── Models/           # 数据模型（材质条目、下载站点等）
│   │   │   ├── Services/         # 服务层（HTTP、下载、材质工厂等）
│   │   │   └── Widgets/          # Slate UI 组件
│   │   └── Private/              # 实现文件
│   ├── Resources/                # 插件资源（图标、示例纹理）
│   ├── Config/                   # 插件配置
│   └── PBRStudio.uplugin         # UE 插件描述文件
│
├── Docs/                         # 使用文档
│   ├── PBRStudio_三端插件使用说明.md
│   ├── PBRStudio_三端插件使用说明.html
│   └── PBRStudio_三端插件使用说明.pdf
│
├── Releases/                     # 打包发布文件
│   ├── PBRStudio_3dsMax_v*.mzp          # 3ds Max 一键安装包
│   ├── PBRStudio_Chrome_Extension_v*.zip # Chrome 扩展打包
│   └── PBRStudio_UE_Plugin_v*.zip       # UE 插件打包
│
├── README.md
└── LICENSE
```

---

## 3ds Max 脚本

**文件名**: `InteriorSceneStudioPro_v95_topbar_width_collapse_clean.py`

面向室内设计 UE 导入前的场景整理工具，基于 `pymxs`（3ds Max Python API）和 `PySide2/6`（Qt UI）。

### 一键安装 (MZP)

> 推荐方式，双击或拖拽即可完成安装。

1. 打开 3ds Max 2024（也支持 2020-2025）
2. 将 `Releases/PBRStudio_3dsMax_v*.mzp` **拖入 3ds Max 视口**
3. 弹出 "PBRStudio 安装成功" 对话框
4. 重启 3ds Max（或通过 **自定义 → 自定义用户界面** 加载）
5. 在 **自定义 → 自定义用户界面 → 工具栏** 中，类别选择 **"PBR Studio"**
6. 将 **"Interior Scene Studio Pro"** 按钮拖入任意工具栏
7. 点击按钮即可打开插件主界面

> **安装做了什么？**
> - 将 `.py` 脚本文件复制到 `%LOCALAPPDATA%\Autodesk\3dsMax\20xx - 64bit\ENU\scripts\`
> - 在 `usermacros\` 目录注册宏脚本 `.mcr`，使按钮出现在自定义界面中

### 手动安装

如果不使用 MZP，也可以手动运行：

1. 打开 3ds Max
2. 菜单栏: **脚本** → **运行脚本**
3. 选择 `3dsMax_Script/InteriorSceneStudioPro_v95_topbar_width_collapse_clean.py`
4. 确保 `_pbr_clean_utils.py` 在同一目录下

### Max 功能详解

#### 1. 模型管理
| 操作 | 说明 |
|------|------|
| 对象修复 | 批量修复：无材质补默认材质、缩放异常自动 Reset XForm + 转 Editable Poly、轴心归底居中 |
| 跳过冻结对象 | 修复时可选择跳过已冻结对象，保护参考模型 |
| 进度条 | 批量操作显示进度，可随时强制停止并恢复视口刷新 |
| 列表视图 | 完整对象列表，显示名称、类型、材质、层级等属性，支持排序 |
| 筛选与搜索 | 按名称/类型/问题/材质搜索；支持快捷键过滤 |
| 勾选工具 | 只勾选中 / 只勾未选中 / 全选 / 取消 / 反转 |
| 右键菜单 | 选中对象、隐藏、冻结、孤立编辑、删除、导出选中等 |

#### 2. 组管理
- 组列表（含嵌套组），展开/折叠组头
- 组的打开/关闭、选中组成员、解组
- 自动同步场景中的组变化

#### 3. 灯光管理
- 灯光列表，显示类型、强度、色温、阴影状态
- 按类型筛选（点光源、面光源、聚光灯、IES 等）
- 批量开关阴影、调整强度/颜色
- 选中/隔离/删除灯光

#### 4. 相机管理
- 相机列表，显示类型、焦距、FOV
- 快速切换相机视角
- 批量调整相机参数

#### 5. 材质管理
- 完整材质列表，显示名称、类型、使用次数
- 查找未使用材质、关联材质到对象
- 材质编辑器集成（双击打开 Slate Material Editor）
- 材质分类（Standard、VRay、Corona、Physical 等）

#### 6. 材质标准化（PBR 转换）
- 将传统材质批量转换为 Physical Material（PBR 标准）
- 支持 Standard → Physical、VRayMtl → Physical
- 预览转换计划，确认后执行
- 完整的撤销支持

#### 7. PBR 贴图套装
- 智能识别 PBR 贴图通道（BaseColor、Roughness、Metallic、Normal、AO、Height、Displacement、Opacity 等）
- 支持 DirectX 和 OpenGL 法线格式识别
- 自动匹配预览图
- 一键创建标准 PBR 材质（Physical Material + PBR 贴图连接）
- 支持 Metal/Roughness 和 Specular/Glossiness 两种工作流
- 批量导入、命名自动匹配

#### 8. PBR 下载库
- 内置多个免费 PBR 素材站点（AmbientCG、Poly Haven、ShareTextures 等）
- 网页链接下载 → 自动解压（支持 ZIP/RAR/7Z）
- 解压后自动整理到贴图套装对应目录
- 接收 Chrome 扩展推送（端口 19527）
- 路径持久化，重开插件自动恢复
- 关键词搜索、站点分类浏览

#### 9. 场景体检
- 一键扫描全场景问题：
  - 孤立顶点、零面积面、重叠面
  - 非均匀缩放对象、缺少 UV 的对象
  - 缺少材质的对象、材质贴图路径异常
  - 重名对象、空组、空层
- 问题列表，点击定位到问题对象
- 导出体检报告

#### 10. 重命名工具
- 批量重命名，支持：前缀/后缀、查找替换、序号、正则表达式
- 重命名前预览确认（显示旧名 → 新名对照表）
- 一键撤销上次重命名操作
- 命名规则可保存/加载配置

#### 11. UE 贴图流送
- 将场景贴图路径流式发送到 UE（本地 HTTP）
- 支持选择范围（选中对象 / 全部材质 / 指定材质）
- 实时进度显示，可随时停止

#### 12. AI 小助手
- 内置 AI 对话面板（基于 Web 引擎）
- 室内设计相关问答
- 支持深色/浅色主题跟随

#### 13. 其他功能
- **11 种 UI 皮肤**：暖木暗色、曜石蓝黑、石材灰、奶油浅色、米兰岩板、莫兰迪绿、铜黑展厅、日式原木、包豪斯白、经典灰、高对比深色
- **字号调节**：A- / A+ 按钮，9px ~ 18px 可调
- **窗口置顶**：可开关，默认关闭避免遮挡 Max 系统对话框
- **Excel / CSV 导出**：场景信息、材质列表、贴图清单
- **操作日志**：记录关键操作，方便回溯
- **配置文件导入/导出**：保存重命名规则、PBR 设置等

---

## Chrome 浏览器插件

**版本**: 1.1.3 | **清单版本**: Manifest V3

### Chrome 插件安装

1. 打开 Chrome，地址栏输入 `chrome://extensions/` 回车
2. 打开右上角 **开发者模式** 开关
3. 点击 **加载已解压的扩展程序**
4. 选择 `Chrome_Extension/chrome_extension` 文件夹
5. 扩展卡片出现在列表中，安装完成

> **更新方法**: 修改插件文件后，在扩展管理页点击刷新按钮即可重新加载。

### Chrome 插件使用

#### 弹出窗口（点击扩展图标）

| 功能 | 说明 |
|------|------|
| 目标切换 | 选择推送到 **Max**（3ds Max，端口 19527）或 **UE**（Unreal Engine，端口 19528） |
| 端口配置 | Max 和 UE 端口独立配置，互不干扰 |
| 手动检测 | 点击检测按钮验证目标端是否在线（不会自动检测，避免性能消耗） |
| 推送本页链接 | 将当前网页 URL 推送到目标端（Max 或 UE） |
| 重试队列 | 一键重试之前发送失败的所有链接 |
| 打开 AI 面板 | 启动室内场景 AI 助手（独立面板） |

#### 页面快捷按钮（Content Script）

浏览支持的素材网站时，页面右下角会出现两个快捷按钮：
- **Max** — 推送到 3ds Max 下载库（端口 19527）
- **UE** — 推送到 Unreal Engine 下载库（端口 19528）

支持的素材网站包括：AmbientCG、Poly Haven、ShareTextures、3DTextures.me、CG Bookcase 等。

#### AI 助手面板
- 室内设计 AI 对话助手
- 支持深色/浅色主题切换
- KaTeX 数学公式渲染
- 独立可拖拽面板窗口

---

## UE 插件 (PBRStudio)

**版本**: 1.1.3 | **模块类型**: Editor | **加载阶段**: PostEngineInit

### UE 插件安装

#### 第一步：复制插件

1. 关闭 Unreal Editor
2. 将 `UE_Plugin/PBRStudio` 文件夹复制到你的 UE 项目目录下：

```
你的项目/Plugins/PBRStudio/
├── Source/PBRStudio/
├── Resources/
├── Config/
└── PBRStudio.uplugin
```

> 如果项目没有 `Plugins` 文件夹，手动创建一个。

#### 第二步：编译（如需要）

3. 重新打开项目，如果 UE 提示插件需要编译，点击 **确认**
4. 编译完成后，在 UE 菜单栏找到 **PBR Studio** 工具窗口

> **非 C++ 项目**: 本仓库仅包含源码，首次使用需要在 Visual Studio 环境下编译一次。安装 Visual Studio 2022 + UE C++ 工具链，将项目临时转为 C++ 项目（添加一个空 `.cpp` 文件即可）。

### UE 插件功能

插件主窗口包含以下标签页：

#### 贴图套件 — PBR 纹理扫描与材质创建

| 步骤 | 操作 |
|------|------|
| 1. 选择文件夹 | 选择包含 PBR 贴图的文件夹 |
| 2. 扫描贴图 | 自动识别 PBR 通道：BaseColor、Roughness、Metallic、Normal、AO、Height 等 |
| 3. 选择材质类型 | Standard、Metal、Glass、Fabric、Leather、Wood、Stone、Tile、Plastic、Emissive、Water |
| 4. 选择法线模式 | DirectX（默认）或 OpenGL |
| 5. 批量创建 | 一键创建材质实例，自动连接贴图参数 |

#### 下载库 — 网络资源下载与整理

| 功能 | 说明 |
|------|------|
| 接收推送 | 通过本地 HTTP 服务（端口 19528）接收 Chrome 插件推送的下载链接 |
| 手动下载 | 粘贴 URL 直接下载 |
| 自动解压 | 支持 `.zip`（PowerShell）、`.rar` / `.7z`（需安装 7-Zip 或 WinRAR） |
| 智能命名 | 自动从 Content-Disposition、URL 参数提取文件名 |
| 材质库管理 | 解压后自动整理到材质库，与贴图套件共享路径 |
| 路径持久化 | 重开插件后自动恢复上次选择的路径 |
| 站点浏览 | 内置素材站点目录，分类浏览 |

#### Chrome 推送服务

- 本地 HTTP 服务器，默认端口 `19528`
- 接收 Chrome 插件推送的下载 URL
- 在 UE 内部直接触发下载和解压流程

---

## 三端协作流程

推荐的完整 PBR 资产工作流：

```
┌──────────────────┐                     ┌──────────────────┐
│   Chrome 插件     │                     │   素材网站        │
│   (弹出窗口)      │                     │   (AmbientCG,    │
│                   │                     │    Poly Haven…)  │
└────────┬─────────┘                     └────────┬─────────┘
         │                                        │
         │  点击推送 / 页面快捷按钮                  │
         │                                        │
         ▼                                        ▼
┌──────────────────┐                     ┌──────────────────┐
│  本地 HTTP 推送   │ ─────────────────→  │  Max 或 UE       │
│  (localhost)     │   端口 19527/19528   │  (接收下载 URL)   │
└──────────────────┘                     └────────┬─────────┘
                                                  │
                                         自动下载 & 解压
                                                  │
                                                  ▼
                                         ┌──────────────────┐
                                         │  贴图套件         │
                                         │  扫描 → 识别通道  │
                                         │  → 创建材质实例    │
                                         └──────────────────┘
```

**详细步骤**：

1. **启动接收端**: UE → PBRStudio → 下载库 → 启动 Chrome 推送服务（端口 19528），或 Max 端自动监听（端口 19527）
2. **验证连接**: Chrome 插件选择对应目标，点击"手动检测"确认连接成功
3. **浏览 & 推送**: 在素材网站找到需要的 PBR 材质，点击页面上的 `UE` 或 `Max` 快捷按钮
4. **自动下载**: 接收端自动下载 ZIP 并解压到材质库目录
5. **创建材质**: 切换到贴图套件页，扫描文件夹 → 选择材质类型 → 批量创建材质实例
6. **应用到场景**: 将材质拖拽应用到 UE 场景对象，或通过 3ds Max 的材质标准化功能

---

## 常见问题

### 3ds Max 相关

**Q: 拖入 MZP 没有反应？**
A: 确认使用 v1.1.4 或更高版本的 MZP。如果之前装过旧版，重启 Max 后重新拖入即可覆盖。

**Q: 点击按钮提示"未找到脚本文件"？**
A: 重新拖入 MZP 安装即可。安装程序会自动将脚本复制到正确的用户脚本目录。

**Q: Python 脚本执行报错？**
A: v1.1.4 已修复 `ModuleNotFoundError: No module named '_pbr_clean_utils'` 导入错误。确保使用最新版 MZP。

**Q: 界面显示异常（太大/太小/模糊）？**
A: 脚本已启用高 DPI 适配。如果仍有问题，可在面板顶部通过 A- / A+ 按钮调整字号，或切换不同的 UI 皮肤。

**Q: 窗口一闪而过？**
A: v1.1.4 已修复。如果仍有问题，请通过 MZP 安装后的工具栏按钮启动，不要直接 "运行脚本" 执行 .py 文件。

### Chrome 推送相关

**Q: Chrome 推送失败？**
- 确认目标端（Max/UE）的推送服务已启动
- 在 Chrome 插件弹窗中点击"手动检测"验证连通性
- 检查防火墙是否阻止了本地端口通信（19527/19528）
- 确认 Chrome 插件中选择的目标（Max/UE）与实际监听的端口一致

**Q: 页面没有出现快捷按钮？**
A: 检查是否在支持的素材网站上。如需添加新站点，可在 `content.js` 中扩展 `SUPPORTED_SITES` 配置。

### UE 插件相关

**Q: 非 C++ 项目无法打开插件？**
- 本仓库仅包含源码，需在 Visual Studio 环境下编译一次
- 安装 Visual Studio 2022 + UE 所需的 C++ 工具链
- 将项目临时转为 C++ 项目（添加一个空 .cpp 文件即可编译）

**Q: RAR / 7Z 无法自动解压？**
- 安装 [7-Zip](https://www.7-zip.org/) 或 [WinRAR](https://www.win-rar.com/) 后重试
- ZIP 格式使用系统 PowerShell 解压，无需额外软件

**Q: 新增下载站点没有出现？**
- 插件会自动合并内置站点配置
- 删除项目 `Saved/PBRStudio/PBRDownloadSites.json` 后重开 UE 即可恢复默认列表

---

## 功能矩阵

| 功能 | UE 插件 | Chrome 扩展 | 3ds Max 脚本 |
|------|:-------:|:-----------:|:------------:|
| PBR 贴图通道自动识别 | ✅ | — | ✅ |
| 材质实例自动创建 | ✅ | — | ✅ |
| 母材质模板管理 | ✅ | — | — |
| 网络资源下载 & 解压 | ✅ | — | ✅ |
| 网页链接推送 | 接收 | 发送 | 接收 |
| AI 场景助手 | — | ✅ | ✅ |
| 对象批量修复 | — | — | ✅ |
| 场景问题检测 | — | — | ✅ |
| 批量重命名（可撤销） | — | — | ✅ |
| 材质标准化 (PBR 转换) | — | — | ✅ |
| 多皮肤 UI | — | ✅ | ✅ |
| 场景信息导出 (CSV) | — | — | ✅ |
| 贴图流送到 UE | — | — | ✅ |

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
| 1.1.4 | 2026-05-05 | 修复 3ds Max MZP 安装后的 `ModuleNotFoundError` 导入错误；修复窗口闪退；MZP 安装覆盖可靠性改进 |
| 1.1.3 | 2026-05-03 | 三端功能完善，MZP 一键安装 |
| 1.0.0 | — | 初始版本 |

---

## 许可证

MIT License — 详见 [LICENSE](LICENSE) 文件。

---

*Made with PBRStudio Team*
