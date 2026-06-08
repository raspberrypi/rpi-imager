#!/bin/bash

# Script to generate DMG background image for Raspberry Pi Imager
# This script will create a background image or provide instructions
#
# Pillow is sourced, in order:
#   1. The system python3, if Pillow is already importable.
#   2. A build-local virtualenv, into which Pillow is pip-installed. This works
#      even when the system python3 is PEP 668 "externally-managed" (e.g. Homebrew
#      or Debian Python), where a plain `pip3 install` is refused — and it never
#      pollutes the user's global Python.
# Anything past that falls back to the manual instructions below.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUTPUT_PATH="${1:-$SCRIPT_DIR/dmg_background.png}"
VERSION_STR="${2:-}"
PY_GENERATOR="$SCRIPT_DIR/create_dmg_background.py"

# Remove existing background image to force regeneration
rm -f "$OUTPUT_PATH"

echo "Generating DMG background image..."

# Run the Pillow generator with the given interpreter; succeeds only if the PNG appears.
run_generator() {
    "$1" "$PY_GENERATOR" "$OUTPUT_PATH" "$VERSION_STR" && [ -f "$OUTPUT_PATH" ]
}

if command -v python3 &> /dev/null; then
    echo "Found Python 3, checking for Pillow..."

    # 1. System python3 already has Pillow.
    if python3 -c "import PIL" 2>/dev/null; then
        echo "Pillow found in system python3, generating background image..."
        if run_generator python3; then
            echo "✓ Background image created successfully: $OUTPUT_PATH"
            exit 0
        fi
        echo "✗ Generator failed despite Pillow being importable"
    else
        echo "Pillow not available in system python3."
    fi

    # 2. Build-local virtualenv. Reused across rebuilds; sidesteps PEP 668.
    VENV_DIR="$(dirname "$OUTPUT_PATH")/.dmg-bg-venv"
    VENV_PY="$VENV_DIR/bin/python3"
    echo "Setting up build-local virtualenv for Pillow at $VENV_DIR ..."
    if [ ! -x "$VENV_PY" ]; then
        python3 -m venv "$VENV_DIR" 2>/dev/null || {
            echo "Warning: could not create virtualenv (is the venv module available?)"
            VENV_PY=""
        }
    fi
    if [ -n "$VENV_PY" ] && [ -x "$VENV_PY" ]; then
        if ! "$VENV_PY" -c "import PIL" 2>/dev/null; then
            echo "Installing Pillow into virtualenv..."
            "$VENV_PY" -m pip install --quiet --upgrade pip 2>/dev/null || true
            "$VENV_PY" -m pip install --quiet Pillow || {
                echo "Warning: 'pip install Pillow' into virtualenv failed (no network?)"
            }
        fi
        if "$VENV_PY" -c "import PIL" 2>/dev/null; then
            echo "Generating background image using virtualenv Pillow..."
            if run_generator "$VENV_PY"; then
                echo "✓ Background image created successfully: $OUTPUT_PATH"
                exit 0
            fi
            echo "✗ Generator failed in virtualenv"
        fi
    fi
else
    echo "Python 3 not found"
fi

# Fallback: Create a simple background using built-in tools
echo "Creating fallback DMG background using built-in tools..."

# Check if we're on macOS and have the necessary tools
if [[ "$OSTYPE" == "darwin"* ]]; then
    # Create a simple background using macOS built-in tools
    echo "Using macOS built-in tools to create a simple background..."
    
    # Use sips to create a simple colored background
    # Create a temporary solid color image first
    TEMP_DIR=$(mktemp -d)
    TEMP_IMAGE="$TEMP_DIR/temp_bg.png"
    
    # Create a simple gradient using the built-in tools
    cat > "$TEMP_DIR/create_bg.py" << EOF
import sys
try:
    # Try to create a simple image without PIL
    width, height = 600, 400
    
    # Create a simple HTML file that we can screenshot
    # Include version in title if provided
    title_text = "Raspberry Pi Imager"
    if len(sys.argv) > 2 and sys.argv[2]:
        title_text += f" {sys.argv[2]}"
    
    html_content = f'''
    <!DOCTYPE html>
    <html>
    <head>
        <style>
            body {{
                margin: 0;
                padding: 0;
                width: {width}px;
                height: {height}px;
                background: linear-gradient(135deg, #f8f9fa 0%, #ffffff 100%);
                font-family: -apple-system, BlinkMacSystemFont, "SF Pro Display", "SF Pro Text", "Helvetica Neue", Helvetica, Arial, sans-serif;
                display: flex;
                flex-direction: column;
                align-items: center;
                justify-content: center;
                position: relative;
                /* High-quality text rendering for macOS */
                -webkit-font-smoothing: antialiased;
                -moz-osx-font-smoothing: grayscale;
                text-rendering: optimizeLegibility;
                font-feature-settings: "kern" 1;
                font-kerning: normal;
            }}
            .title {{
                color: #C51A4A;
                font-size: 32px;
                font-weight: 600;
                margin-bottom: 20px;
                letter-spacing: -0.02em;
                line-height: 1.2;
            }}

            .circle {{
                position: absolute;
                border: 2px solid #ecf0f1;
                border-radius: 50%;
                width: 188px;
                height: 188px;
            }}
            .circle.app {{
                left: 56px;
                top: 106px;
            }}
            .circle.apps {{
                right: 56px;
                top: 106px;
            }}
            .chevron {{
                position: absolute;
                top: 50%;
                left: 50%;
                transform: translate(-50%, -50%);
                color: #C51A4A;
                font-size: 32px;
                font-weight: bold;
            }}
        </style>
    </head>
    <body>
        <div class="title">{title_text}</div>
        <div class="circle app"></div>
        <div class="circle apps"></div>
        <div class="chevron">›</div>
    </body>
    </html>
    '''
    
    with open(sys.argv[1], 'w') as f:
        f.write(html_content)
    print("Created HTML template")
    
except Exception as e:
    print(f"Error: {e}")
    sys.exit(1)
EOF
    
    HTML_FILE="$TEMP_DIR/background.html"
    python3 "$TEMP_DIR/create_bg.py" "$HTML_FILE" "$VERSION_STR"
    
    if [ -f "$HTML_FILE" ]; then
        # Try to use webkit2png if available
        if command -v webkit2png &> /dev/null; then
            webkit2png --width=600 --height=400 --fullsize --filename="$TEMP_DIR/bg" "file://$HTML_FILE"
            if [ -f "$TEMP_DIR/bg.png" ]; then
                cp "$TEMP_DIR/bg.png" "$OUTPUT_PATH"
                echo "✓ Background image created using webkit2png: $OUTPUT_PATH"
                rm -rf "$TEMP_DIR"
                exit 0
            fi
        fi
        
        echo "Could not create image automatically."
        echo "HTML template created at: $HTML_FILE"
        echo "You can open this in a browser and take a screenshot manually."
    fi
    
    rm -rf "$TEMP_DIR"
fi

# Final fallback - provide instructions
echo ""
echo "⚠ Could not create DMG background image automatically."
echo ""
echo "To create a custom DMG background:"
echo "1. Install Python 3 and Pillow: pip3 install Pillow"
echo "2. Run: python3 $SCRIPT_DIR/create_dmg_background.py $OUTPUT_PATH"
echo ""
echo "Or create a 600x400 PNG image manually with:"
echo "- Title: 'Raspberry Pi Imager'"
echo "- Instruction: 'Drag the app to Applications to install'"
echo "- Visual guides for app and Applications folder positions"
echo ""
echo "The DMG will work without a background image, but won't look as polished."

exit 1 