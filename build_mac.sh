#!/bin/sh
# Compila la versione macOS e crea SmartyLens.app (servono gli strumenti da riga di comando di Xcode:
#   xcode-select --install). L'app e' universale: Apple Silicon e Intel, macOS 11 o successivo.
set -e
cd "$(dirname "$0")"
APP=SmartyLens.app
rm -rf "$APP" SmartyLens-macos.zip
mkdir -p "$APP/Contents/MacOS"
clang -fobjc-arc -Os -Wall -Wno-unused-function -arch arm64 -arch x86_64 -mmacosx-version-min=11.0 \
    -framework Cocoa -framework ApplicationServices -lcurl \
    -o "$APP/Contents/MacOS/SmartyLens" smartylens_mac.m
cat > "$APP/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key><string>Smarty Lens</string>
    <key>CFBundleDisplayName</key><string>Smarty Lens</string>
    <key>CFBundleIdentifier</key><string>io.github.smartylens</string>
    <key>CFBundleExecutable</key><string>SmartyLens</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>1.2</string>
    <key>CFBundleVersion</key><string>1.2</string>
    <key>LSMinimumSystemVersion</key><string>11.0</string>
    <key>LSUIElement</key><true/>
    <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
PLIST
# firma "ad hoc": non sostituisce la firma Apple, ma serve a macOS per ricordare il permesso Accessibilita'
codesign --force --sign - "$APP"
ditto -c -k --keepParent "$APP" SmartyLens-macos.zip
echo "OK: $(pwd)/$APP e SmartyLens-macos.zip"
