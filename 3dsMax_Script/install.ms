/*
================================================================================
  PBRStudio / InteriorSceneStudioPro — 3ds Max 一键安装脚本
  版本: v1.1.4 | 日期: 2026-05-05

  用法: 将此 .mzp 文件拖入 3ds Max 视口，自动完成安装。
        安装后在菜单栏 "PBR Studio" 中或自定义工具栏可找到按钮。
================================================================================
*/

macroScript PBRStudio_InteriorSceneStudioPro
    category: "PBR Studio"
    internalCategory: "PBR Studio"
    tooltip: "Interior Scene Studio Pro — 室内场景整理 & PBR 工作流"
    buttonText: "Interior Scene Studio Pro"
    icon:#("PBRStudio", 1)
(
    -- 获取用户脚本目录
    local userScriptsDir = pathConfig.appendPath (getDir #userScripts) ""
    local pyFile1 = pathConfig.appendPath userScriptsDir "InteriorSceneStudioPro_v95_topbar_width_collapse_clean.py"
    local pyFile2 = pathConfig.appendPath userScriptsDir "_pbr_clean_utils.py"

    -- 检查脚本是否存在
    if not (doesFileExist pyFile1) then
    (
        messageBox "未找到 InteriorSceneStudioPro 脚本文件。\n请重新运行 .mzp 安装包。" title:"PBR Studio"
        return undefined
    )

    -- 执行 Python 脚本
    try
    (
        python.ExecuteFile pyFile1
    )
    catch
    (
        local errMsg = getCurrentException()
        messageBox ("脚本执行失败:\n" + errMsg) title:"PBR Studio 错误"
    )
)

/*
================================================================================
  安装函数 — 复制文件到用户脚本目录
================================================================================
*/

fn installPBRStudioFiles =
(
    local srcDir = getSourceFileName()
    if srcDir == undefined or srcDir == "" then
    (
        srcDir = getThisScriptFilename()
    )

    -- 获取 MZP 解压后的临时目录
    local extractDir = pathConfig.removePathLeaf srcDir

    -- 源文件路径 (MZP 解压后的临时目录)
    local srcMain = pathConfig.appendPath extractDir "InteriorSceneStudioPro_v95_topbar_width_collapse_clean.py"
    local srcUtils = pathConfig.appendPath extractDir "_pbr_clean_utils.py"

    -- 如果解压目录找不到源文件，尝试从当前脚本目录查找
    if not (doesFileExist srcMain) then
    (
        local scriptPath = getThisScriptFilename()
        if scriptPath != undefined then
        (
            local scriptDir = pathConfig.removePathLeaf scriptPath
            srcMain = pathConfig.appendPath scriptDir "InteriorSceneStudioPro_v95_topbar_width_collapse_clean.py"
            srcUtils = pathConfig.appendPath scriptDir "_pbr_clean_utils.py"
        )
    )

    -- 目标目录
    local userScriptsDir = pathConfig.appendPath (getDir #userScripts) ""
    local dstMain = pathConfig.appendPath userScriptsDir "InteriorSceneStudioPro_v95_topbar_width_collapse_clean.py"
    local dstUtils = pathConfig.appendPath userScriptsDir "_pbr_clean_utils.py"

    -- 复制文件（先删除目标再复制，确保覆盖旧版本）
    local copiedCount = 0
    if doesFileExist srcMain then
    (
        if doesFileExist dstMain then deleteFile dstMain
        if (copyFile srcMain dstMain) then copiedCount += 1
    )
    if doesFileExist srcUtils then
    (
        if doesFileExist dstUtils then deleteFile dstUtils
        if (copyFile srcUtils dstUtils) then copiedCount += 1
    )

    -- 注册宏脚本文件
    local macroDir = pathConfig.appendPath (getDir #userMacros) ""
    local macroFile = pathConfig.appendPath macroDir "PBRStudio-InteriorSceneStudioPro.mcr"
    local macroContent = ""
    macroContent += "macroScript PBRStudio_InteriorSceneStudioPro\n"
    macroContent += "    category: \"PBR Studio\"\n"
    macroContent += "    internalCategory: \"PBR Studio\"\n"
    macroContent += "    tooltip: \"Interior Scene Studio Pro — 室内场景整理 & PBR 工作流\"\n"
    macroContent += "    buttonText: \"Interior Scene Studio Pro\"\n"
    macroContent += "    icon:#(\"PBRStudio\", 1)\n"
    macroContent += "(\n"
    macroContent += "    local pyFile = pathConfig.appendPath (getDir #userScripts) \"InteriorSceneStudioPro_v95_topbar_width_collapse_clean.py\"\n"
    macroContent += "    if doesFileExist pyFile then python.ExecuteFile pyFile\n"
    macroContent += "    else messageBox \"未找到 InteriorSceneStudioPro 脚本。\\n请重新运行 .mzp 安装包。\" title:\"PBR Studio\"\n"
    macroContent += ")\n"

    local macroStream = createFile macroFile
    if macroStream != undefined then
    (
        format "%" macroContent to:macroStream
        close macroStream
    )

    -- 显示结果
    local msg = "PBRStudio 安装完成!\n\n"
    msg += "已安装文件:\n"
    msg += "  • " + dstMain + "\n"
    msg += "  • " + dstUtils + "\n"
    msg += "  • " + macroFile + "\n\n"
    msg += "请重启 3ds Max 或使用 \"自定义 → 自定义用户界面\"\n"
    msg += "在类别 \"PBR Studio\" 中找到按钮，拖入工具栏即可使用。"
    messageBox msg title:"PBRStudio 安装成功"
)

-- 执行安装
installPBRStudioFiles()
