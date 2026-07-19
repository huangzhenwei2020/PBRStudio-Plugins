import argparse
import json
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def load_json(relative_path):
    with (ROOT / relative_path).open("r", encoding="utf-8") as stream:
        return json.load(stream)


def main():
    parser = argparse.ArgumentParser(description="Validate PBRStudio release metadata and packages.")
    parser.add_argument("--require-release", action="store_true", help="Require all release artifacts to exist.")
    args = parser.parse_args()

    errors = []
    plugin = load_json("UE_Plugin/PBRStudio/PBRStudio.uplugin")
    manifest = load_json("Chrome_Extension/chrome_extension/manifest.json")
    version = plugin.get("VersionName", "")

    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        errors.append(f"Invalid UE version: {version!r}")
    if manifest.get("version") != version:
        errors.append(f"Chrome version {manifest.get('version')!r} does not match UE version {version!r}")

    optional_hosts = set(manifest.get("optional_host_permissions", []))
    for permission in ("https://*/*", "http://*/*"):
        if permission not in optional_hosts:
            errors.append(f"Missing optional Chrome host permission: {permission}")

    expected_releases = (
        f"PBRStudio_Tri_Plugin_Installer_v{version}.exe",
        f"PBRStudio_UE_Plugin_v{version}.zip",
        f"PBRStudio_Chrome_Extension_v{version}.zip",
        f"PBRStudio_3dsMax_v{version}.mzp",
    )
    if args.require_release:
        for filename in expected_releases:
            path = ROOT / "Releases" / filename
            if not path.is_file() or path.stat().st_size == 0:
                errors.append(f"Missing or empty release artifact: {path}")

    for relative_path in ("README.md", "Docs/PBRStudio_三端插件使用说明.md"):
        text = (ROOT / relative_path).read_text(encoding="utf-8")
        if f"版本：{version}" not in text:
            errors.append(f"{relative_path} does not declare version {version}")
        for filename in expected_releases:
            if filename not in text:
                errors.append(f"{relative_path} does not reference {filename}")

    if errors:
        print("Release validation failed:")
        for error in errors:
            print(f"- {error}")
        return 1

    suffix = " with release artifacts" if args.require_release else ""
    print(f"PBRStudio {version} metadata is consistent{suffix}.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
