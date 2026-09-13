#!/bin/sh
# Wrap the cross-built executable and the ROM into a Mac OS X .app bundle.
#
#   port/tools/make_bundle.sh [exe] [rom] ["out.app"]
#
# The bundle is self-contained: the PowerPC executable (SDL2 is linked in
# statically, so there is nothing else to install), the user's own ROM in
# Resources, the icon, and an Info.plist that names it for the Finder.
#
# The executable inside is named `isle` so the isle-ppc-tools g4 console
# runner (which hard-codes that name) can launch it unchanged: on the G4,
# `ln -sfn "/Applications/Snowboard Kids 2.app" ~/SnowboardKids2.app` and then
# `g4 use SnowboardKids2` / `g4 run` / `g4 shot` all still work.
set -e
here=$(cd "$(dirname "$0")" && pwd)
exe=${1:-$here/../build-ppc-darwin/snowboardkids2}
rom=${2:-$here/../../snowboardkids2.z64}
out=${3:-$here/../build-ppc-darwin/Snowboard Kids 2.app}

[ -f "$exe" ] || { echo "no executable at $exe (run port/build-ppc.sh)" >&2; exit 1; }
[ -f "$rom" ] || { echo "no ROM at $rom" >&2; exit 1; }

rm -rf "$out"
mkdir -p "$out/Contents/MacOS" "$out/Contents/Resources"
cp "$exe" "$out/Contents/MacOS/isle"
chmod 755 "$out/Contents/MacOS/isle"
cp "$rom" "$out/Contents/Resources/snowboardkids2.z64"
cp "$here/../resources/SnowboardKids.icns" "$out/Contents/Resources/SnowboardKids.icns"
cat > "$out/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleDevelopmentRegion</key><string>English</string>
	<key>CFBundleExecutable</key><string>isle</string>
	<key>CFBundleIconFile</key><string>SnowboardKids</string>
	<key>CFBundleIdentifier</key><string>com.southcitycapture.snowboardkids2</string>
	<key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
	<key>CFBundleName</key><string>Snowboard Kids 2</string>
	<key>CFBundleDisplayName</key><string>Snowboard Kids 2</string>
	<key>CFBundlePackageType</key><string>APPL</string>
	<key>CFBundleSignature</key><string>????</string>
	<key>CFBundleShortVersionString</key><string>1.0</string>
	<key>CFBundleVersion</key><string>1.0</string>
	<key>CFBundleGetInfoString</key><string>Snowboard Kids 2 1.0, Power Mac G4 port</string>
	<key>LSMinimumSystemVersion</key><string>10.5</string>
	<key>LSApplicationCategoryType</key><string>public.app-category.games</string>
	<key>NSHighResolutionCapable</key><false/>
</dict>
</plist>
PLIST
printf 'APPL????' > "$out/Contents/PkgInfo"
echo "bundle: $out"
ls -la "$out/Contents/MacOS" "$out/Contents/Resources"
