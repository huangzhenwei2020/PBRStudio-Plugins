# PBRStudio 三端插件使用说明

版本：1.1.4  
适用对象：Unreal Engine 编辑器、Chrome 浏览器、3ds Max  
项目地址：https://github.com/huangzhenwei2020/PBRStudio-Plugins

PBRStudio 是一套面向建筑可视化、室内设计和 PBR 材质整理流程的三端工具。它把网页素材下载、3ds Max 场景整理、UE 材质创建和场景材质转换串到一个工作流里。

## 一分钟上手

推荐使用 `Releases/PBRStudio_Tri_Plugin_Installer_v1.1.4.exe` 作为入口。如果公司安全策略拦截 exe，也可以分别安装三个端：

| 端 | 推荐文件 | 作用 |
| --- | --- | --- |
| UE | `Releases/PBRStudio_UE_Plugin_v1.1.4.zip` 或 `UE_Plugin/PBRStudio` | UE 里创建材质、下载素材、转换场景材质、管理魔法大纲 |
| Chrome | `Releases/PBRStudio_Chrome_Extension_v1.1.4.zip` 或 `Chrome_Extension/chrome_extension` | 从素材网站把下载链接推送到 UE 或 3ds Max |
| 3ds Max | `Releases/PBRStudio_3dsMax_v1.1.4.mzp` | 整理 Max 场景、批量修复、PBR 贴图套件、下载库 |

默认本地通信端口：

| 目标 | 端口 |
| --- | --- |
| Chrome 推送到 3ds Max | `19527` |
| Chrome 推送到 UE | `19528` |

## 项目结构

```text
PBRStudio-Plugins/
  Chrome_Extension/chrome_extension/       Chrome Manifest V3 扩展
  Docs/                                    离线使用说明
  Releases/                                发布包、安装包
  Tools/                                   三端安装器构建工具
  UE_Plugin/PBRStudio/                     Unreal Engine 编辑器插件源码
  InteriorSceneStudioPro_v95_topbar_width_collapse_clean.py
                                           3ds Max 主脚本
  install.ms                               3ds Max MZP 安装入口
  _pbr_clean_utils.py                      3ds Max 辅助工具
```

## 一键安装

1. 关闭 Unreal Editor、3ds Max 和 Chrome 扩展管理页。
2. 运行 `Releases/PBRStudio_Tri_Plugin_Installer_v1.1.4.exe`。
3. 按安装器提示选择需要安装的端。
4. UE 项目级安装时，目标目录应是你的项目目录或项目下的 `Plugins` 目录。
5. 安装完成后按下面对应端的说明检查一次。

如果一键安装器不能运行，使用下面的手动安装方式。

## UE 插件安装

### 手动复制

1. 关闭 Unreal Editor。
2. 打开你的 UE 项目目录。
3. 如果没有 `Plugins` 文件夹，新建一个。
4. 把 `UE_Plugin/PBRStudio` 整个文件夹复制到：

```text
你的项目/Plugins/PBRStudio
```

5. 重新打开项目。
6. 如果 UE 提示需要编译插件，点击确认。
7. 编译完成后，在 UE 工具栏或菜单中打开 `PBR Studio`。

### 编译要求

这是 UE 编辑器 C++ 插件。不同 UE 版本、不同引擎安装路径或非 C++ 项目可能需要重新编译。

推荐环境：

- Unreal Engine 5.x
- Visual Studio 2022
- Windows 10/11 SDK
- UE C++ 编译工具链

非 C++ 项目如果无法编译，可以给项目添加一个空 C++ 类，让 UE 生成工程文件后再编译。

## UE 插件主要功能

### 1. 贴图套件

用于把一组本地 PBR 贴图创建成 UE 材质实例。

使用步骤：

1. 打开 `PBR Studio`。
2. 进入 `贴图套件`。
3. 选择贴图文件夹。
4. 点击扫描。
5. 检查识别出的通道。
6. 选择材质类型，例如标准、木材、石材、瓷砖、布料、皮革、塑料、金属、半透明、玻璃、水、自发光。
7. 选择法线模式，DirectX 或 OpenGL。
8. 点击创建材质。

支持识别的常见通道：

- BaseColor / Albedo / Diffuse
- Normal / NormalDX / NormalGL
- Roughness / Glossiness
- Metallic
- Specular
- AO
- Height / Displacement
- Opacity
- Emissive
- ORM / ARM

### 2. 下载库

用于接收 Chrome 推送或手动粘贴下载链接，并整理素材包。

使用步骤：

1. 进入 `下载库`。
2. 设置材质库目录。
3. 启动 Chrome 推送服务。
4. 默认监听端口是 `19528`。
5. 从 Chrome 插件推送链接，或在 UE 内手动粘贴 URL。
6. 下载完成后会自动解压并整理到材质库。
7. 切到 `贴图套件` 扫描并创建材质。

压缩包支持：

- `.zip`：使用系统 PowerShell 解压。
- `.rar` / `.7z`：优先调用本机 7-Zip 或 WinRAR。

下载文件名优先级：

1. HTTP `Content-Disposition`
2. URL 参数中的 `file`、`filename`、`name`、`download`、`dl`、`path`
3. URL 路径文件名
4. 默认 `download`

### 3. 材质转换

用于把场景里已有材质转换为 PBRStudio 的统一母材质体系。

使用步骤：

1. 打开 `材质转换`。
2. 扫描当前关卡材质。
3. 检查列表里的材质类型判断和贴图识别。
4. 设置输出目录，默认是 `/Game/PBRStudio/SceneReplaced`。
5. 勾选要转换的材质。
6. 执行转换。

转换逻辑会尽量继承原材质的：

- 基础色贴图或基础色参数
- 法线贴图
- 粗糙度贴图或粗糙度值
- 金属度贴图或金属度值
- AO、Specular、Height、Opacity、Emissive 等通道

玻璃、半透明、自发光会分别走对应的 PBRStudio 母材质，不再简单全部按普通不透明材质处理。玻璃会进入独立 `M_PBR_Glass` 母材质，逻辑参考 ArchRenderMaster 的 `M_ARM_Glass` / `MF_ARM_GlassOptics`，包含透明、折射、菲涅尔、吸收、污渍、扭曲、磨砂、阴影、焦散和光追相关参数。

### 4. 魔法大纲

魔法大纲是 UE 端的场景管理窗口，用于按模型、材质、灯光、相机、蓝图、关卡等维度查看和操作场景对象。

常用能力：

- 场景对象分类查看。
- 按 Actor 展示模型，并显示 Actor 下的网格体。
- 支持多选、勾选、反选、隔离和显示隐藏。
- 选中场景物体时，材质列表会自动聚焦对应材质。
- 模型批量重命名。
- 批量移动到文件夹。
- 可选择把所选模型挂到新 Actor 下。
- 灯光强度、色温、颜色批量调节。
- 相机与后期参数辅助调节。

### 5. 魔法大纲里的直接材质调整

这是 UE 端 1.1.4 的重点功能。

使用步骤：

1. 在 UE 场景里选中一个或多个物体。
2. 打开 `魔法大纲`。
3. 切到材质分类或材质视图。
4. 右侧 `被选择的材质` 会显示当前选中物体使用的材质。
5. 点击某一行或某个槽位的 `调参`。
6. 插件会把该材质槽接管成 PBRStudio 可编辑材质实例。
7. 下方会出现 `材质直接调整` 面板。
8. 根据材质类型显示不同参数组，并直接修改当前类型需要的参数。
9. 也可以切换材质类型；切换后会自动套用该类型的默认参数。

普通 Slate UI 会显示完整参数组；自绘 UI 也已接入调参功能，会在右侧显示紧凑版 `材质直接调整` 面板，可切换类型、调关键标量、切换贴图开关。

可切换类型：

- 标准
- 木材
- 石材
- 瓷砖
- 布料
- 皮革
- 塑料
- 金属
- 半透明
- 玻璃
- 水
- 自发光

常见参数组：

- 通用：基础色、基础色强度、粗糙度、高光、法线、AO、UV。
- 金属：金属度、金属度强度、各向异性、金属颗粒。
- 布料：织物绒毛颜色和强度。
- 皮革：清漆强度和清漆粗糙度。
- 半透明：透明度、透明贴图、折射率。
- 玻璃：透明度、折射率、透明菲涅尔、菲涅尔基础反射、菲涅尔指数、毛玻璃、吸收颜色、吸收强度、边缘染色、污渍、扭曲、阴影、焦散和光追参数。
- 水：水体颜色、水流速度、水波缩放和强度。
- 自发光：自发光颜色、贴图开关、色温、自发光强度。

接管后的材质实例默认生成到：

```text
/Game/PBRStudio/SelectedMaterials
```

注意：

- `调参` 是针对具体组件的具体材质槽，不是无提示地改全场景共享材质。
- 如果你想替换所有使用同一旧材质的槽，使用材质列表拖拽替换或 `材质转换`。
- 如果场景来自 Datasmith，移动 Actor 层级或替换材质可能影响后续 Datasmith 重新同步时的覆盖结果。建议重要项目先复制关卡或保存版本。

## Chrome 扩展安装

### 从 zip 安装

1. 解压 `Releases/PBRStudio_Chrome_Extension_v1.1.4.zip`。
2. 打开 Chrome。
3. 地址栏输入 `chrome://extensions/`。
4. 打开右上角 `开发者模式`。
5. 点击 `加载已解压的扩展程序`。
6. 选择解压后的 `chrome_extension` 文件夹。

### 从源码安装

直接加载：

```text
Chrome_Extension/chrome_extension
```

### Chrome 使用流程

1. 打开 UE 下载库并启动推送服务，或打开 Max 端下载库。
2. 点击 Chrome 扩展图标。
3. 选择目标：`UE` 或 `Max`。
4. 检查端口：UE 默认 `19528`，Max 默认 `19527`。
5. 点击手动检测。
6. 在素材网站页面点击 `UE` 或 `Max` 快捷按钮。
7. 目标端收到链接后开始下载。

支持常见素材站，例如 AmbientCG、Poly Haven、ShareTextures、CGBookcase、3DTextures 等。

## 3ds Max 插件安装

### MZP 一键安装

1. 打开 3ds Max。
2. 将 `Releases/PBRStudio_3dsMax_v1.1.4.mzp` 拖入 Max 视口。
3. 按提示完成安装。
4. 重启 3ds Max。
5. 进入 `自定义` - `自定义用户界面`。
6. 在类别中找到 `PBR Studio`。
7. 把 `Interior Scene Studio Pro` 按钮拖到工具栏。
8. 点击按钮打开界面。

### 手动运行

如果不使用 MZP：

1. 打开 3ds Max。
2. 菜单进入 `脚本` - `运行脚本`。
3. 选择：

```text
InteriorSceneStudioPro_v95_topbar_width_collapse_clean.py
```

4. 确保 `_pbr_clean_utils.py` 和主脚本在同一目录。

### 3ds Max 主要功能

- 模型列表、搜索、筛选、选中、隐藏、冻结、孤立。
- 批量修复无材质、异常缩放、轴心位置等问题。
- 场景体检，检查重名、空层、缺 UV、贴图路径异常等问题。
- 材质列表和材质标准化。
- PBR 贴图套件扫描与材质创建。
- 下载库接收 Chrome 推送，默认端口 `19527`。
- 批量重命名和导出。
- 多主题 UI、字号调节、操作日志。

## 推荐三端工作流

1. 在 UE 或 Max 中设置材质库目录。
2. 在目标端启动本地推送服务。
3. Chrome 扩展选择目标端并手动检测。
4. 浏览素材网站。
5. 点击页面里的 `UE` 或 `Max` 按钮推送下载链接。
6. 目标端下载并解压素材包。
7. 使用贴图套件扫描贴图。
8. 创建材质实例。
9. 在 UE 里使用魔法大纲或材质转换整理场景材质。

## 发布包说明

| 文件 | 用途 |
| --- | --- |
| `PBRStudio_Tri_Plugin_Installer_v1.1.4.exe` | 三端一键安装器 |
| `PBRStudio_UE_Plugin_v1.1.4.zip` | UE 插件发布包 |
| `PBRStudio_Chrome_Extension_v1.1.4.zip` | Chrome 扩展发布包 |
| `PBRStudio_3dsMax_v1.1.4.mzp` | 3ds Max 一键安装包 |

## 常见问题

### UE 提示插件无法编译

确认安装了 Visual Studio 2022、Windows SDK 和 UE C++ 工具链。非 C++ 项目可以先添加一个空 C++ 类，再重新打开项目编译。

### Chrome 推送失败

检查目标端是否启动服务、端口是否正确、防火墙是否拦截 localhost。UE 默认 `19528`，Max 默认 `19527`。

### 页面没有出现 UE/Max 快捷按钮

确认当前站点是否在支持列表中。也可以打开扩展弹窗，使用 `推送本页链接`。

### RAR 或 7Z 不能自动解压

安装 7-Zip 或 WinRAR 后重试。

### Datasmith 重新同步会不会覆盖修改

可能会。材质替换、Actor 层级调整、移动到新 Actor 等操作如果作用在 Datasmith 导入对象上，后续重新同步可能被 Datasmith 规则覆盖。正式项目建议先备份关卡或复制一份测试关卡。

### 魔法大纲调参会不会改到所有同材质对象

不会直接无提示全局修改。`调参` 面板接管的是当前组件的当前材质槽，会创建或复用 PBRStudio 可编辑实例。如果需要全局替换，使用材质行拖拽替换或 `材质转换`。

## 开发与打包

UE 编译示例：

```powershell
& "D:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat" test_cheshiEditor Win64 Development -Project="H:\UE_OFFICE\test_cheshi\test_cheshi.uproject" -WaitMutex -NoHotReloadFromIDE -NoUBA
```

三端安装器构建脚本：

```powershell
Tools/build_tri_installer.ps1
```

## 版本记录

### 1.1.4

- 增加三端一键安装器发布包。
- UE 贴图套件通道识别修复。
- UE 材质转换切换到 PBRStudio 统一母材质体系。
- UE 材质转换贴图开关、玻璃材质和自发光过亮问题修复。
- UE 魔法大纲支持选中物体后直接接管并调整材质。
- UE 魔法大纲支持切换材质类型。
- Chrome 扩展推送能力增强。
- 3ds Max MZP 安装与脚本加载稳定性改进。

## 许可证

MIT License。详见 `LICENSE`。
