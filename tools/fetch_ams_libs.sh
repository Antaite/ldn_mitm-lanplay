#!/usr/bin/env bash
# Fetch Atmosphère's libraries for a given release tag into ./Atmosphere-libs
# usage: tools/fetch_ams_libs.sh 1.12.1
set -euo pipefail
TAG="${1:?usage: $0 <atmosphere tag, e.g. 1.12.1>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"
git clone --quiet --depth 1 --branch "$TAG" --filter=blob:none --sparse https://github.com/Atmosphere-NX/Atmosphere.git "$TMP/ams"
git -C "$TMP/ams" sparse-checkout set libraries
rm -rf "$ROOT/Atmosphere-libs"
mv "$TMP/ams/libraries" "$ROOT/Atmosphere-libs"
rm -rf "$TMP"
echo "Atmosphere-libs = Atmosphère $TAG"
