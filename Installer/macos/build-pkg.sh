#!/bin/bash
# DLAY - macOS installer package.
#
#   Installer/macos/build-pkg.sh <artefacts-dir> <version> <output.pkg>
#
# <artefacts-dir> is build/DLAY_artefacts/Release after a Release build. The package installs
#   /Library/Audio/Plug-Ins/VST3/DLAY.vst3
#   /Library/Audio/Plug-Ins/Components/DLAY.component
#   /Applications/DLAY.app
# for every user of the Mac (the installer asks for an administrator password).
#
# The bundles are sealed with an ad-hoc signature. The package itself is not signed by a
# registered Apple developer, so macOS asks the user to confirm it once (System Settings >
# Privacy & Security > Open Anyway). What it installs then loads without further steps:
# files put in place by the Installer carry no quarantine mark.
set -euo pipefail

artefacts="${1:?usage: build-pkg.sh <artefacts-dir> <version> <output.pkg>}"
version="${2:?version missing}"
output="${3:?output path missing}"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
root="$work/root"

mkdir -p "$root/Library/Audio/Plug-Ins/VST3" "$root/Library/Audio/Plug-Ins/Components" "$root/Applications"
ditto "$artefacts/VST3/DLAY.vst3"      "$root/Library/Audio/Plug-Ins/VST3/DLAY.vst3"
ditto "$artefacts/AU/DLAY.component"   "$root/Library/Audio/Plug-Ins/Components/DLAY.component"
ditto "$artefacts/Standalone/DLAY.app" "$root/Applications/DLAY.app"

# Seal each bundle as the very last change made to it. (JUCE signs the VST3 before it writes
# moduleinfo.json into it, which leaves that seal broken, and Apple Silicon only loads code
# whose signature is intact.)
for bundle in "$root/Library/Audio/Plug-Ins/VST3/DLAY.vst3" \
              "$root/Library/Audio/Plug-Ins/Components/DLAY.component" \
              "$root/Applications/DLAY.app"; do
    codesign --force --sign - "$bundle"
    codesign --verify --strict "$bundle"
    echo "sealed: ${bundle#"$root"}  ($(lipo -archs "$bundle/Contents/MacOS/DLAY"))"
done

# By default the Installer "relocates" a bundle: if it finds another copy with the same
# identifier anywhere on the disk (a build folder, a copy in the user's own Library) it
# updates that one instead of installing to the path given here. Switch that off, and let
# any version be installed over any other.
pkgbuild --analyze --root "$root" "$work/components.plist" > /dev/null
count=0
while /usr/libexec/PlistBuddy -c "Set :$count:BundleIsRelocatable false" "$work/components.plist" 2> /dev/null; do
    /usr/libexec/PlistBuddy -c "Set :$count:BundleIsVersionChecked false" "$work/components.plist"
    echo "fixed in place: $(/usr/libexec/PlistBuddy -c "Print :$count:RootRelativeBundlePath" "$work/components.plist")"
    count=$((count + 1))
done
[ "$count" -ge 1 ] || { echo "no bundles were found in the package root" >&2; exit 1; }

pkgbuild --root "$root" \
         --component-plist "$work/components.plist" \
         --identifier com.dlayaudio.dlay.pkg \
         --version "$version" \
         --install-location / \
         "$work/DLAY.pkg" > /dev/null

# Wrap it in a product archive that says which processors it is for. Without that the
# Installer assumes Intel only and offers to install Rosetta on an Apple Silicon Mac.
productbuild --synthesize --package "$work/DLAY.pkg" "$work/distribution.xml" > /dev/null

python3 - "$work/distribution.xml" "$version" <<'PY'
import re, sys

path, version = sys.argv[1], sys.argv[2]
xml = open(path, encoding="utf-8").read()
xml = re.sub(r"[ \t]*<options\b[^>]*/>\n?", "", xml)
head = ('\n    <title>DLAY ' + version + '</title>'
        '\n    <options customize="never" require-scripts="false" hostArchitectures="arm64,x86_64"/>')
xml, found = re.subn(r"(<installer-gui-script\b[^>]*>)", lambda m: m.group(1) + head, xml, count=1)
if found != 1:
    sys.exit("unexpected distribution file:\n" + xml)
open(path, "w", encoding="utf-8").write(xml)
PY

mkdir -p "$(dirname "$output")"
productbuild --distribution "$work/distribution.xml" --package-path "$work" "$output" > /dev/null

echo "built $output ($(du -h "$output" | cut -f1))"
pkgutil --payload-files "$output" | grep -E "(DLAY\.vst3|DLAY\.component|DLAY\.app)$" | sed 's/^/  installs /'
