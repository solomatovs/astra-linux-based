#!/usr/bin/env python3
"""Генератор тестового .vsix для проверки образа dmp/code-marketplace.

.vsix — обычный zip, маркетплейсу из него нужны extension.vsixmanifest
(publisher/id/version, по ним раскладывается каталог) и файлы из <Assets>.

  python3 mkvsix.py --outdir ./vsix --publisher dmp --name hello --version 1.0.0
"""

import argparse
import json
import struct
import zlib
import zipfile
from pathlib import Path


def png_1x1() -> bytes:
    """Минимальный валидный PNG 1x1 — иконка расширения."""

    def chunk(kind: bytes, data: bytes) -> bytes:
        return (
            struct.pack(">I", len(data))
            + kind
            + data
            + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
        )

    # ширина 1, высота 1, глубина 8 бит, тип 6 (RGBA), без интерлейса
    ihdr = struct.pack(">IIBBBBB", 1, 1, 8, 6, 0, 0, 0)
    # одна строка: фильтр 0 + прозрачный пиксель RGBA
    idat = zlib.compress(b"\x00" + b"\x00\x00\x00\x00")
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", ihdr)
        + chunk(b"IDAT", idat)
        + chunk(b"IEND", b"")
    )

MANIFEST = """<?xml version="1.0" encoding="utf-8"?>
<PackageManifest Version="2.0.0" xmlns="http://schemas.microsoft.com/developer/vsx-schema/2011" xmlns:d="http://schemas.microsoft.com/developer/vsx-schema-design/2011">
  <Metadata>
    <Identity Language="en-US" Id="{name}" Version="{version}" Publisher="{publisher}" />
    <DisplayName>{name}</DisplayName>
    <Description xml:space="preserve">Тестовое расширение для проверки dmp/code-marketplace</Description>
    <Tags>{name},test</Tags>
    <Categories>Other</Categories>
    <GalleryFlags>Public</GalleryFlags>
    <Properties>
      <Property Id="Microsoft.VisualStudio.Code.Engine" Value="^1.75.0" />
      <Property Id="Microsoft.VisualStudio.Code.ExtensionDependencies" Value="" />
      <Property Id="Microsoft.VisualStudio.Code.ExtensionPack" Value="" />
      <Property Id="Microsoft.VisualStudio.Code.ExtensionKind" Value="workspace,web" />
    </Properties>
    <License>extension/LICENSE.txt</License>
    <Icon>extension/images/icon.png</Icon>
  </Metadata>
  <Installation>
    <InstallationTarget Id="Microsoft.VisualStudio.Code" />
  </Installation>
  <Dependencies />
  <Assets>
    <Asset Type="Microsoft.VisualStudio.Code.Manifest" Path="extension/package.json" Addressable="true" />
    <Asset Type="Microsoft.VisualStudio.Services.Content.Details" Path="extension/README.md" Addressable="true" />
    <Asset Type="Microsoft.VisualStudio.Services.Content.License" Path="extension/LICENSE.txt" Addressable="true" />
    <Asset Type="Microsoft.VisualStudio.Services.Icons.Default" Path="extension/images/icon.png" Addressable="true" />
  </Assets>
</PackageManifest>
"""


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--outdir", required=True, help="куда положить .vsix")
    parser.add_argument("--publisher", required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()

    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    out = outdir / f"{args.publisher}.{args.name}-{args.version}.vsix"

    package_json = {
        "name": args.name,
        "displayName": args.name,
        "publisher": args.publisher,
        "version": args.version,
        "description": "Тестовое расширение для проверки dmp/code-marketplace",
        "engines": {"vscode": "^1.75.0"},
        "categories": ["Other"],
        # browser — точка входа web-расширения, качается через /files
        "browser": "./out/web/extension.js",
        "main": "./out/extension.js",
        "contributes": {},
    }

    # фиксированные даты — чтобы .vsix был воспроизводимым
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        def write(name: str, data) -> None:
            info = zipfile.ZipInfo(name, date_time=(2020, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            z.writestr(info, data)

        write(
            "extension.vsixmanifest",
            MANIFEST.format(
                publisher=args.publisher, name=args.name, version=args.version
            ),
        )
        write("extension/package.json", json.dumps(package_json, indent=2) + "\n")
        write("extension/README.md", f"# {args.name}\n\nТестовое расширение.\n")
        write("extension/LICENSE.txt", "MIT\n")
        write("extension/images/icon.png", png_1x1())
        write("extension/out/extension.js", "// test extension\n")
        write("extension/out/web/extension.js", "// test web extension\n")

    print(f"{out} ({out.stat().st_size} байт)")


if __name__ == "__main__":
    main()
