#!/bin/bash
# Build a macOS .pkg installer for the Fénix I emulation (both plug-ins, all formats).
#
#   packaging/make_installer.sh [--build] [BUILD_DIR]     # default BUILD_DIR = build
#
# Output: dist/FenixI-<version>-macOS.pkg (+ a .zip of the raw bundles).
# The installer offers Fenix I (AU, VST3, CLAP, standalone app) and Fenix I FX
# (AU, VST3, CLAP) as separate choices and installs to
# /Library/Audio/Plug-Ins/{Components,VST3,CLAP} and /Applications.
#
# Signing: bundles are ad-hoc signed (no Apple Developer ID). Files installed by
# a .pkg are not quarantined, so the plug-ins load normally once the installer
# has been opened (right-click > Open the first time). If DEVELOPER_ID_APP /
# DEVELOPER_ID_INSTALLER / NOTARY_PROFILE are set, the script signs + notarizes.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
DIST="$ROOT/dist"
WORK="$(mktemp -d /tmp/fenix-pkgwork.XXXXXX)"
trap 'rm -rf "$WORK"' EXIT

BUILD=""
BUILD_DIR="build"
for a in "$@"; do
  case "$a" in
    --build) BUILD=1 ;;
    *) BUILD_DIR="$a" ;;
  esac
done
VERSION="$(sed -n 's/^project(FenixI VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
ORG="com.renkydenk"
if [[ -n "$BUILD" ]]; then
  cmake -S "$ROOT" -B "$ROOT/$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
  cmake --build "$ROOT/$BUILD_DIR" -j "$(sysctl -n hw.ncpu)"
fi
ART1="$ROOT/$BUILD_DIR/FenixI_artefacts/Release"
ART2="$ROOT/$BUILD_DIR/FenixIFX_artefacts/Release"

check () {
  [[ -d "$1" ]] || { echo "missing bundle: $1" >&2; exit 1; }
  local exe; exe="$(find "$1/Contents/MacOS" -type f | head -1)"
  local archs; archs="$(lipo -archs "$exe")"
  echo "  $(basename "$1"): $archs"
  [[ "$archs" == *arm64* && "$archs" == *x86_64* ]] || echo "  WARNING: $(basename "$1") is not universal ($archs)" >&2
}

SIGN_APP="${DEVELOPER_ID_APP:--}"
SIGN_PKG="${DEVELOPER_ID_INSTALLER:-}"
mkdir -p "$WORK/pkgs" "$WORK/res" "$DIST"

component () { # <bundle> <install-location> <pkg-id>
  local bundle="$1" loc="$2" pid="$3" stage="$WORK/stage-$3"
  check "$bundle"
  mkdir -p "$stage"
  ditto "$bundle" "$stage/$(basename "$bundle")"
  if [[ "$SIGN_APP" == "-" ]]; then
    codesign --force --deep -s - "$stage/$(basename "$bundle")"
  else
    codesign --force --deep --options runtime --timestamp -s "$SIGN_APP" "$stage/$(basename "$bundle")"
  fi
  pkgbuild --analyze --root "$stage" "$WORK/$pid.plist" >/dev/null
  plutil -replace 0.BundleIsRelocatable -bool NO "$WORK/$pid.plist"
  pkgbuild --root "$stage" --component-plist "$WORK/$pid.plist" \
           --install-location "$loc" --identifier "$pid" --version "$VERSION" \
           "$WORK/pkgs/$pid.pkg" >/dev/null
}
component "$ART1/AU/Fenix I.component"      "/Library/Audio/Plug-Ins/Components" "$ORG.fenixi.au"
component "$ART1/VST3/Fenix I.vst3"         "/Library/Audio/Plug-Ins/VST3"       "$ORG.fenixi.vst3"
component "$ART1/CLAP/Fenix I.clap"         "/Library/Audio/Plug-Ins/CLAP"       "$ORG.fenixi.clap"
component "$ART1/Standalone/Fenix I.app"    "/Applications"                      "$ORG.fenixi.app"
component "$ART2/AU/Fenix I FX.component"   "/Library/Audio/Plug-Ins/Components" "$ORG.fenixifx.au"
component "$ART2/VST3/Fenix I FX.vst3"      "/Library/Audio/Plug-Ins/VST3"       "$ORG.fenixifx.vst3"
component "$ART2/CLAP/Fenix I FX.clap"      "/Library/Audio/Plug-Ins/CLAP"       "$ORG.fenixifx.clap"

sed -e "s/@VERSION@/$VERSION/g" "$HERE/resources/welcome.html" > "$WORK/res/welcome.html"
cp "$HERE/resources/conclusion.html" "$WORK/res/conclusion.html"
cat > "$WORK/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>Fenix I $VERSION</title>
  <organization>$ORG</organization>
  <welcome file="welcome.html" mime-type="text/html"/>
  <conclusion file="conclusion.html" mime-type="text/html"/>
  <options customize="always" require-scripts="false" hostArchitectures="arm64,x86_64"/>
  <domains enable_localSystem="true"/>
  <os-version min="11.0"/>
  <choices-outline>
    <line choice="inst"><line choice="inst_au"/><line choice="inst_vst3"/><line choice="inst_clap"/><line choice="inst_app"/></line>
    <line choice="fx"><line choice="fx_au"/><line choice="fx_vst3"/><line choice="fx_clap"/></line>
  </choices-outline>
  <choice id="inst" title="Fenix I (instrument)" description="The synthesizer: MIDI in, patch it, audio out."/>
  <choice id="inst_au"   title="Audio Unit (AU)"  description="Logic, GarageBand, Live, Bitwig. Installs to /Library/Audio/Plug-Ins/Components."><pkg-ref id="$ORG.fenixi.au"/></choice>
  <choice id="inst_vst3" title="VST3"             description="Live, Bitwig, Reaper, Cubase. Installs to /Library/Audio/Plug-Ins/VST3."><pkg-ref id="$ORG.fenixi.vst3"/></choice>
  <choice id="inst_clap" title="CLAP"             description="Bitwig, Reaper and other CLAP hosts. Installs to /Library/Audio/Plug-Ins/CLAP."><pkg-ref id="$ORG.fenixi.clap"/></choice>
  <choice id="inst_app"  title="Standalone app"   description="Play without a DAW. Installs to /Applications."><pkg-ref id="$ORG.fenixi.app"/></choice>
  <choice id="fx" title="Fenix I FX (effect)" description="The same instrument as an audio effect: the two 1/4-inch inputs are the track input, rear CV jacks on a sidechain bus."/>
  <choice id="fx_au"   title="Audio Unit (AU)"><pkg-ref id="$ORG.fenixifx.au"/></choice>
  <choice id="fx_vst3" title="VST3"><pkg-ref id="$ORG.fenixifx.vst3"/></choice>
  <choice id="fx_clap" title="CLAP"><pkg-ref id="$ORG.fenixifx.clap"/></choice>
  <pkg-ref id="$ORG.fenixi.au">$ORG.fenixi.au.pkg</pkg-ref>
  <pkg-ref id="$ORG.fenixi.vst3">$ORG.fenixi.vst3.pkg</pkg-ref>
  <pkg-ref id="$ORG.fenixi.clap">$ORG.fenixi.clap.pkg</pkg-ref>
  <pkg-ref id="$ORG.fenixi.app">$ORG.fenixi.app.pkg</pkg-ref>
  <pkg-ref id="$ORG.fenixifx.au">$ORG.fenixifx.au.pkg</pkg-ref>
  <pkg-ref id="$ORG.fenixifx.vst3">$ORG.fenixifx.vst3.pkg</pkg-ref>
  <pkg-ref id="$ORG.fenixifx.clap">$ORG.fenixifx.clap.pkg</pkg-ref>
</installer-gui-script>
XML

OUT="$DIST/FenixI-$VERSION-macOS.pkg"
rm -f "$OUT"
productbuild --distribution "$WORK/distribution.xml" --package-path "$WORK/pkgs" \
             --resources "$WORK/res" ${SIGN_PKG:+--sign "$SIGN_PKG"} "$OUT" >/dev/null
if [[ -n "$SIGN_PKG" && -n "${NOTARY_PROFILE:-}" ]]; then
  xcrun notarytool submit "$OUT" --keychain-profile "$NOTARY_PROFILE" --wait
  xcrun stapler staple "$OUT"
fi

ZIP="$DIST/FenixI-$VERSION-macOS-bundles.zip"
rm -f "$ZIP"
BUNDLES="$WORK/Fenix I $VERSION"
mkdir -p "$BUNDLES"
for d in "$WORK"/stage-*; do cp -R "$d"/* "$BUNDLES"/; done
cp "$ROOT/resources/topology_params.json" "$BUNDLES/"
ditto -c -k --keepParent "$BUNDLES" "$ZIP"
echo "wrote $OUT"
echo "wrote $ZIP"
