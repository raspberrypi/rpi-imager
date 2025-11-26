#!/bin/bash

# Script to regenerate rpi-imager.icns with all appearance variants
# macOS supports multiple icon appearance variants:
# - Default (light): no suffix
# - Dark: ~dark suffix
# - Tinted: ~tinted suffix
# - Additional variants may be included for future compatibility
#
# This script should be run from the project root when icon branding changes are made.

set -e

ICONSET_DIR="src/icons/rpi-imager.iconset"
EXPORTS_DIR="src/icons/app_icon_macos Exports"
ICNS_FILE="src/icons/rpi-imager.icns"

echo "Regenerating macOS icon with all appearance variants..."

# Ensure iconset directory exists
mkdir -p "$ICONSET_DIR"

# Size mappings: export_size -> iconset_name
# Using parallel arrays to avoid associative array issues with @ symbols
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
    "512x512@2x:icon_512x512@2x.png"
)

# Function to copy icons for a specific variant
copy_variant() {
    local variant_name=$1
    local suffix=$2
    local variant_display_name=$3
    
    echo "Copying $variant_display_name icons..."
    local copied=0
    local missing=0
    
    for size_mapping in "${SIZES[@]}"; do
        # Split the mapping at the colon
        IFS=':' read -r size_key iconset_name <<< "$size_mapping"
        
        # Determine the target filename
        if [ -z "$suffix" ]; then
            target_name="$iconset_name"
        else
            target_name="${iconset_name%.png}${suffix}.png"
        fi
        
        export_file="app_icon_macos-iOS-${variant_name}-${size_key}.png"
        
        if [ -f "$EXPORTS_DIR/$export_file" ]; then
            cp "$EXPORTS_DIR/$export_file" "$ICONSET_DIR/$target_name"
            echo "  ✓ $export_file -> $target_name"
            ((copied++))
        else
            echo "  ✗ Missing: $export_file"
            ((missing++))
        fi
    done
    
    echo "  $variant_display_name: $copied copied, $missing missing"
    echo ""
}

# Copy all variants
copy_variant "Default" "" "Default (light mode)"
copy_variant "Dark" "~dark" "Dark mode"
copy_variant "TintedLight" "~tinted" "Tinted (light)"
copy_variant "TintedDark" "~tinteddark" "Tinted (dark)"
copy_variant "ClearLight" "~clearlight" "Clear (light)"
copy_variant "ClearDark" "~cleardark" "Clear (dark)"

# Regenerate .icns file using iconutil
echo ""
echo "Regenerating .icns file..."
if command -v iconutil &> /dev/null; then
    # Backup existing icns file if it exists
    if [ -f "$ICNS_FILE" ]; then
        cp "$ICNS_FILE" "${ICNS_FILE}.backup"
        echo "  Backed up existing .icns file"
    fi
    
    # Generate new .icns file
    iconutil -c icns "$ICONSET_DIR" -o "$ICNS_FILE"
    
    if [ -f "$ICNS_FILE" ]; then
        echo "  ✓ Successfully generated $ICNS_FILE"
        echo "  File size: $(ls -lh "$ICNS_FILE" | awk '{print $5}')"
    else
        echo "  ✗ Failed to generate .icns file"
        exit 1
    fi
else
    echo "  ✗ iconutil not found. Please run this on macOS."
    exit 1
fi

echo ""
echo "Done! The icon now includes all appearance variants:"
echo "  - Default (light mode)"
echo "  - Dark mode"
echo "  - Tinted variants"
echo "  - Clear variants"
echo ""
echo "Note: macOS officially supports Default, Dark (~dark), and Tinted (~tinted) variants."
echo "Other variants are included for potential future support or compatibility."

