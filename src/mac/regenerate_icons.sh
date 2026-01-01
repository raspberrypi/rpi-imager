#!/bin/bash

# Complete icon regeneration script for macOS
# 
# This script handles all icon-related tasks AFTER exporting from Icon Composer:
# 1. Verifies exports exist and are valid
# 2. Generates .icns file for fallback (older macOS)
# 3. Copies .icon file to build for dark mode support (macOS Tahoe+)
#
# PREREQUISITES:
# 1. Export icons from Icon Composer to: src/icons/app_icon_macos Exports/
#    - Platform: macOS
#    - All sizes: 16, 32, 128, 256, 512, 1024 (@1x and @2x)
#    - All variants: Default, Dark, TintedLight, TintedDark, ClearLight, ClearDark
#
# 2. Update the .icon file at: src/icons/app_icon_macos.icon/
#
# Run from project root: ./src/mac/regenerate_icons.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

EXPORTS_DIR="$PROJECT_ROOT/src/icons/app_icon_macos Exports"
ICON_FILE="$PROJECT_ROOT/src/icons/app_icon_macos.icon"
ICONSET_DIR="$PROJECT_ROOT/src/icons/rpi-imager.iconset"
ICNS_FILE="$PROJECT_ROOT/src/icons/rpi-imager.icns"

echo "=== macOS Icon Regeneration Script ==="
echo ""

# Check prerequisites
echo "Checking prerequisites..."

if [ ! -d "$EXPORTS_DIR" ]; then
    echo "❌ Exports directory not found: $EXPORTS_DIR"
    echo "   Please export icons from Icon Composer first."
    exit 1
fi

if [ ! -d "$ICON_FILE" ]; then
    echo "❌ Icon Composer file not found: $ICON_FILE"
    exit 1
fi

echo "✓ Found exports directory"
echo "✓ Found Icon Composer file"
echo ""

# Verify exports are valid (Dark should be different from Default)
echo "Verifying export variants..."

python3 << 'PYEOF'
import sys
from PIL import Image

base_path = 'src/icons/app_icon_macos Exports'
size = '256x256@1x'

default_path = f'{base_path}/app_icon_macos-macOS-Default-{size}.png'
dark_path = f'{base_path}/app_icon_macos-macOS-Dark-{size}.png'

try:
    default = Image.open(default_path)
    dark = Image.open(dark_path)
    
    # Compare pixels directly (ImageChops can have issues with some formats)
    different_pixels = 0
    sample_size = min(default.width, 256)
    
    for y in range(0, default.height, default.height // sample_size):
        for x in range(0, default.width, default.width // sample_size):
            if default.getpixel((x, y)) != dark.getpixel((x, y)):
                different_pixels += 1
    
    if different_pixels == 0:
        print("⚠️  WARNING: Dark and Default exports are IDENTICAL!")
        print("   Dark mode icons won't work correctly.")
        print("   Please re-export from Icon Composer with dark mode configured.")
        sys.exit(1)
    else:
        print(f"✓ Dark variant is different from Default ({different_pixels} sampled pixels differ)")
except FileNotFoundError as e:
    print(f"❌ Required export not found: {e}")
    sys.exit(1)
except Exception as e:
    print(f"❌ Error checking exports: {e}")
    sys.exit(1)
PYEOF

if [ $? -ne 0 ]; then
    echo ""
    echo "Export verification failed. Please fix the issues above and re-run."
    exit 1
fi

echo ""

# Generate .icns file
echo "Generating .icns file for fallback..."

# Clean and create iconset directory
rm -rf "$ICONSET_DIR"
mkdir -p "$ICONSET_DIR"

# Size mappings: export_size -> iconset_name
SIZES=(
    "16x16@1x:icon_16x16.png"
    "16x16@2x:icon_16x16@2x.png"
    "32x32@1x:icon_32x32.png"
    "32x32@2x:icon_32x32@2x.png"
    "128x128@1x:icon_128x128.png"
    "128x128@2x:icon_128x128@2x.png"
    "256x256@1x:icon_256x256.png"
    "256x256@2x:icon_256x256@2x.png"
    "512x512@1x:icon_512x512.png"
    "1024x1024@1x:icon_512x512@2x.png"
)

echo "  Copying Default icons to iconset..."
copied=0
for size_mapping in "${SIZES[@]}"; do
    IFS=':' read -r size_key iconset_name <<< "$size_mapping"
    export_file="app_icon_macos-macOS-Default-${size_key}.png"
    
    if [ -f "$EXPORTS_DIR/$export_file" ]; then
        cp "$EXPORTS_DIR/$export_file" "$ICONSET_DIR/$iconset_name"
        ((copied++))
    else
        echo "  ⚠️  Missing: $export_file"
    fi
done
echo "  ✓ Copied $copied icon files"

# Generate .icns
echo "  Running iconutil..."
if command -v iconutil &> /dev/null; then
    iconutil -c icns "$ICONSET_DIR" -o "$ICNS_FILE"
    echo "  ✓ Generated $ICNS_FILE"
    echo "    Size: $(ls -lh "$ICNS_FILE" | awk '{print $5}')"
else
    echo "  ❌ iconutil not found"
    exit 1
fi

echo ""

# Summary
echo "=== Summary ==="
echo ""
echo "Generated files:"
echo "  • $ICNS_FILE (fallback for older macOS)"
echo "  • $ICON_FILE (dark mode for macOS Tahoe+)"
echo ""
echo "The build system will automatically:"
echo "  • Include rpi-imager.icns in the app bundle"
echo "  • Copy AppIcon.icon for dark mode support"
echo ""
echo "To rebuild the app, run: cd build && ninja"
echo ""
echo "✅ Icon regeneration complete!"

