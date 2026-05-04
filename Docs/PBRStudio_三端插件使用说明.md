# PBRStudio 三端插件使用说明

版本日期：2026-05-03

本包包含三部分：

- UE 插件：`UE_Plugin/PBRStudio`
- Chrome 浏览器插件：`Chrome_Extension/chrome_extension`
- 3ds Max 脚本：`3dsMax_Script/InteriorSceneStudioPro_v95_topbar_width_collapse_clean.py`

## 1. UE 插件安装

### 项目级安装

1. 关闭 Unreal Editor。
2. 打开你的 UE 项目目录。
3. 如果项目里没有 `Plugins` 文件夹，手动创建一个。
4. 把 `UE_Plugin/PBRStudio` 整个文件夹复制到：

   `你的项目/Plugins/PBRStudio`

5. 打开项目。
6. 如果 UE 提示插件需要编译，点确认。
7. 在 UE 菜单里打开 PBRStudio 工具窗口。

### 非 C++ 项目是否能用

可以用。这个发布包已经带有 Win64 编译好的 UE 编辑器插件二进制文件，适合相同或兼容 UE 版本直接加载。

如果你的 UE 版本、引擎路径或编译环境不同，UE 可能仍会要求重新编译。遇到这种情况需要安装 Visual Studio C++ 编译工具，或把项目临时转成 C++ 项目后编译一次。

## 2. Chrome 插件安装

1. 打开 Chrome。
2. 进入：`chrome://extensions/`
3. 打开右上角“开发者模式”。
4. 点击“加载已解压的扩展程序”。
5. 选择：

   `Chrome_Extension/chrome_extension`

6. 修改插件文件后，需要在扩展管理页点“重新加载”。

### Chrome 推送端口

- 推送到 3ds Max：默认端口 `19527`
- 推送到 UE：默认端口 `19528`

两个端口必须分开，不能混用。

Chrome 插件现在不会自动一直检测端口。需要检测时，在弹窗里选择 Max 或 UE，然后点“手动检测”。

网页里识别到下载链接后，会出现两个短按钮：

- `Max`：推送到 3ds Max
- `UE`：推送到 Unreal Engine

## 3. 3ds Max 脚本使用

1. 打开 3ds Max。
2. 运行脚本：

   `3dsMax_Script/InteriorSceneStudioPro_v95_topbar_width_collapse_clean.py`

3. Max 端主要负责现有 InteriorSceneStudioPro 功能、PBR 下载库、PBR 贴图套装等流程。
4. Chrome 推送到 Max 时使用 Max 端口，默认 `19527`。

## 4. UE 端工作流程

UE 端主要负责：

- PBR 贴图套件扫描
- 根据贴图创建材质实例
- 创建分类母材质
- 特殊材质母材质
- 下载库整理材质包
- 从 Chrome 接收下载链接

### 下载库

下载库只负责：

- 下载文件
- 整理材质库文件夹
- 解压压缩包
- 把材质库路径交给贴图套件

下载库不负责导入 UE，不负责创建 UE 资产。导入和创建材质由“贴图套件”页完成。

下载库里的“材质库”路径会和贴图套件共用保存记录。重开插件后会自动恢复上次选择的路径。

“打开”按钮会用 Windows 资源管理器打开材质库文件夹。

### 压缩包处理

支持拖入和下载：

- `.zip`
- `.rar`
- `.7z`

ZIP 使用系统 PowerShell 解压。RAR 和 7Z 会优先调用本机 7-Zip 或 WinRAR。

解压成功后，原压缩包会自动删除，只保留解压后的贴图文件。

### 文件命名

下载文件名优先级：

1. HTTP `Content-Disposition` 文件名
2. URL 参数里的 `file`、`filename`、`name`、`download`、`dl`、`path`
3. URL 路径文件名
4. `download`

所以 `get?file=xxx.zip` 这类链接会按 `xxx.zip` 保存，不会再保存成 `get`。

## 5. 贴图套件

贴图套件负责：

- 扫描材质库文件夹
- 识别 BaseColor、Roughness、Metallic、Normal、AO、Height 等通道
- 选择材质类型
- 创建母材质和材质实例
- 应用材质到场景对象

贴图套件文件夹也会保存记录，并有“打开”按钮，可直接用资源管理器打开当前文件夹。

## 6. 推荐使用顺序

1. UE 打开 PBRStudio 下载库。
2. 启动 Chrome 推送到 UE 服务，端口默认 `19528`。
3. Chrome 插件选择 UE，点“手动检测”。
4. 在素材网站页面点击 `UE` 按钮推送链接。
5. UE 下载库下载并解压。
6. 切到贴图套件，扫描材质库。
7. 选择材质类型、法线模式，创建材质实例。
8. 把列表里的材质拖到场景对象，或应用到选中对象。

## 7. 常见问题

### Chrome 推送失败

检查 UE 下载库里的 Chrome 推送服务是否启动，端口是否为 `19528`。

### 非 C++ 项目打开失败

如果 UE 提示必须编译插件，说明当前环境和打包时的二进制不兼容。安装 Visual Studio C++ 编译工具后重新打开项目编译一次。

### RAR/7Z 无法自动解压

安装 7-Zip 或 WinRAR 后重试。

### 新增下载站点没有出现

插件会自动合并内置站点。如果仍没出现，可以删除项目 Saved/PBRStudio 下的 `PBRDownloadSites.json` 后重开 UE，让插件重新生成默认列表。

## 8. 文件夹结构

```text
PBRStudio_ThreeSide_Plugins_2026-05-03/
  UE_Plugin/
    PBRStudio/
  Chrome_Extension/
    chrome_extension/
  3dsMax_Script/
    InteriorSceneStudioPro_v95_topbar_width_collapse_clean.py
  Docs/
    PBRStudio_三端插件使用说明.md
    PBRStudio_三端插件使用说明.html
    PBRStudio_三端插件使用说明.pdf
```
