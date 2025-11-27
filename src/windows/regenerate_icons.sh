#!/bin/bash
#
# Regenerate Windows .ico file from PNG sources
# Requires ImageMagick (magick or convert command)
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ICON_DIR="${SCRIPT_DIR}/icon"
OUTPUT_ICO="${SCRIPT_DIR}/../icons/rpi-imager.ico"

# Check for ImageMagick
if command -v magick &> /dev/null; then
    CONVERT_CMD="magick"
elif command -v convert &> /dev/null; then
    CONVERT_CMD="convert"
else
    echo "Error: ImageMagick not found. Please install ImageMagick."
    echo "  macOS: brew install imagemagick"
    echo "  Ubuntu/Debian: sudo apt install imagemagick"
    echo "  Windows: https://imagemagick.org/script/download.php"
    exit 1
fi

# Windows .ico supports these sizes (in order of preference for Windows)
# Using the available sizes from the icon directory
SIZES=(16 20 24 32 40 48 64 256)

echo "Generating Windows icon from PNGs..."
echo "Using ImageMagick: $CONVERT_CMD"
echo ""

# Build list of input files (only use sizes that Windows actually uses)
INPUT_FILES=""
for size in "${SIZES[@]}"; do
    PNG_FILE="${ICON_DIR}/Windows imager icon Full hight_WIN ${size}x${size}.png"
    if [[ -f "$PNG_FILE" ]]; then
        INPUT_FILES="$INPUT_FILES \"$PNG_FILE\""
        echo "  Adding ${size}x${size}"
    else
        echo "  Warning: ${size}x${size} not found, skipping"
    fi
done

if [[ -z "$INPUT_FILES" ]]; then
    echo "Error: No input PNG files found in ${ICON_DIR}"
    exit 1
fi

# Generate .ico file
# The -colors 256 ensures compatibility, but modern Windows handles true color fine
echo ""
echo "Creating ${OUTPUT_ICO}..."

eval "$CONVERT_CMD" $INPUT_FILES "${OUTPUT_ICO}"

if [[ -f "$OUTPUT_ICO" ]]; then
    echo ""
    echo "Successfully created: ${OUTPUT_ICO}"
    ls -la "$OUTPUT_ICO"
else
    echo "Error: Failed to create ${OUTPUT_ICO}"
    exit 1
fi


