#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="$ROOT_DIR/out"
ZIP_PATH="$OUT_DIR/Lineage_Hide.zip"
STAGE="$OUT_DIR/Lineage_Hide"

NATIVE_DIR="${1:-$ROOT_DIR/out/native}"
NATIVE_LIBRARY="$NATIVE_DIR/liblineage_hide.so"
CONFIGCTL="$NATIVE_DIR/configctl"
[[ -f "$NATIVE_LIBRARY" && -f "$CONFIGCTL" ]] || {
  echo 'error: build both liblineage_hide.so and configctl first (scripts/build-native.sh)' >&2
  exit 1
}

rm -rf "$STAGE" "$ZIP_PATH"
mkdir -p "$STAGE/zygisk" "$OUT_DIR"
cp "$ROOT_DIR/module/module.prop" "$STAGE/module.prop"
cp "$ROOT_DIR/module/service.sh" "$STAGE/service.sh"
cp "$ROOT_DIR/module/customize.sh" "$STAGE/customize.sh"
cp -R "$ROOT_DIR/module/webroot" "$STAGE/webroot"
cp -R "$ROOT_DIR/module/scripts" "$STAGE/scripts"
cp "$ROOT_DIR/LICENSE" "$STAGE/LICENSE"
mkdir -p "$STAGE/bin"
cp "$CONFIGCTL" "$STAGE/bin/configctl"
chmod 0755 "$STAGE/bin/configctl" "$STAGE/scripts/legacy-service.sh"
chmod 0755 "$STAGE/service.sh" "$STAGE/customize.sh"

cp "$NATIVE_LIBRARY" "$STAGE/zygisk/arm64-v8a.so"

(
  cd "$STAGE"
  zip -9 -r "$ZIP_PATH" .
)

test -s "$ZIP_PATH"
unzip -l "$ZIP_PATH" | grep -q 'module.prop'
unzip -l "$ZIP_PATH" | grep -q 'zygisk/arm64-v8a.so'
unzip -l "$ZIP_PATH" | grep -q 'customize.sh'
unzip -l "$ZIP_PATH" | grep -q 'webroot/index.html'
unzip -l "$ZIP_PATH" | grep -q 'bin/configctl'
echo "Packaged $ZIP_PATH from $NATIVE_LIBRARY"
