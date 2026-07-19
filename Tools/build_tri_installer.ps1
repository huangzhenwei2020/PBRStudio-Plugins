param(
    [string]$UEPackageDir = ""
)

$ErrorActionPreference = "Stop"

$Version = "1.1.5"
$Root = Split-Path -Parent $PSScriptRoot
$ReleaseDir = Join-Path $Root "Releases"
$BuildDir = Join-Path $Root "build"
$MzpStage = Join-Path $BuildDir "mzp_stage"
$PyWork = Join-Path $BuildDir "pyinstaller"
$PySpec = Join-Path $BuildDir "pyinstaller_spec"
$UEStageRoot = Join-Path $BuildDir "ue_release_stage"
$UEReleaseDir = Join-Path $UEStageRoot "PBRStudio"
$UESourceDir = Join-Path $Root "UE_Plugin\PBRStudio"

if ([string]::IsNullOrWhiteSpace($UEPackageDir)) {
    $UEPayloadDir = $UESourceDir
} else {
    $UEPayloadDir = (Resolve-Path -LiteralPath $UEPackageDir).Path
    if (!(Test-Path -LiteralPath (Join-Path $UEPayloadDir "PBRStudio.uplugin"))) {
        throw "UE package directory does not contain PBRStudio.uplugin: $UEPayloadDir"
    }
}

New-Item -ItemType Directory -Force -Path $ReleaseDir | Out-Null
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

function Remove-IfExists($Path) {
    if (Test-Path -LiteralPath $Path) {
        Remove-Item -LiteralPath $Path -Recurse -Force
    }
}

function New-ZipFromPath($SourcePath, $DestinationPath) {
    if (Test-Path -LiteralPath $DestinationPath) {
        Remove-Item -LiteralPath $DestinationPath -Force
    }
    Compress-Archive -Path $SourcePath -DestinationPath $DestinationPath -CompressionLevel Optimal -Force
}

Write-Host "==> Building release packages v$Version"

Remove-IfExists $UEStageRoot
New-Item -ItemType Directory -Force -Path $UEReleaseDir | Out-Null
Copy-Item -Path (Join-Path $UEPayloadDir "*") -Destination $UEReleaseDir -Recurse -Force
Remove-IfExists (Join-Path $UEReleaseDir "Intermediate")
Get-ChildItem -LiteralPath $UEReleaseDir -Recurse -File -Filter "*.pdb" | Remove-Item -Force

Remove-IfExists $MzpStage
New-Item -ItemType Directory -Force -Path $MzpStage | Out-Null
Copy-Item -LiteralPath (Join-Path $Root "install.ms") -Destination $MzpStage -Force
Copy-Item -LiteralPath (Join-Path $Root "InteriorSceneStudioPro_v95_topbar_width_collapse_clean.py") -Destination $MzpStage -Force
Copy-Item -LiteralPath (Join-Path $Root "_pbr_clean_utils.py") -Destination $MzpStage -Force
Copy-Item -LiteralPath (Join-Path $Root "PBRStudio.bmp") -Destination $MzpStage -Force

$MzpZip = Join-Path $ReleaseDir "PBRStudio_3dsMax_v$Version.zip"
$MzpOut = Join-Path $ReleaseDir "PBRStudio_3dsMax_v$Version.mzp"
New-ZipFromPath (Join-Path $MzpStage "*") $MzpZip
if (Test-Path -LiteralPath $MzpOut) { Remove-Item -LiteralPath $MzpOut -Force }
Move-Item -LiteralPath $MzpZip -Destination $MzpOut
Write-Host "    $MzpOut"

$ChromeZip = Join-Path $ReleaseDir "PBRStudio_Chrome_Extension_v$Version.zip"
New-ZipFromPath (Join-Path $Root "Chrome_Extension\chrome_extension\*") $ChromeZip
Write-Host "    $ChromeZip"

$UEZip = Join-Path $ReleaseDir "PBRStudio_UE_Plugin_v$Version.zip"
New-ZipFromPath $UEReleaseDir $UEZip
Write-Host "    $UEZip"

Write-Host "==> Building installer EXE"

$InstallerName = "PBRStudio_Tri_Plugin_Installer_v$Version"
$InstallerScript = Join-Path $Root "Tools\PBRStudioTriInstaller.py"
$MaxMain = Join-Path $Root "InteriorSceneStudioPro_v95_topbar_width_collapse_clean.py"
$MaxUtils = Join-Path $Root "_pbr_clean_utils.py"
$MaxIcon = Join-Path $Root "PBRStudio.bmp"
$ChromeDir = Join-Path $Root "Chrome_Extension\chrome_extension"
$UEDir = $UEReleaseDir
$InstallMs = Join-Path $Root "install.ms"

$PyInstallerArgs = @(
    "-m", "PyInstaller",
    "--noconfirm",
    "--clean",
    "--noconsole",
    "--onefile",
    "--name", $InstallerName,
    "--distpath", $ReleaseDir,
    "--workpath", $PyWork,
    "--specpath", $PySpec,
    "--add-data", "$MaxMain;payload\Max",
    "--add-data", "$MaxUtils;payload\Max",
    "--add-data", "$MaxIcon;payload\Max",
    "--add-data", "$InstallMs;payload\Max",
    "--add-data", "$ChromeDir;payload\Chrome\chrome_extension",
    "--add-data", "$UEDir;payload\UE\PBRStudio",
    $InstallerScript
)

python @PyInstallerArgs

$ExePath = Join-Path $ReleaseDir "$InstallerName.exe"
if (!(Test-Path -LiteralPath $ExePath)) {
    throw "Installer EXE was not created: $ExePath"
}

Write-Host "==> Done"
Write-Host "    $ExePath"
