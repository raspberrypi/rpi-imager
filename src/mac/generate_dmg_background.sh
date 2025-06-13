#!/bin/bash

# Script to generate DMG background image for Raspberry Pi Imager
# This script will create a background image or provide instructions

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUTPUT_PATH="${1:-$SCRIPT_DIR/dmg_background.png}"
VERSION_STR="${2:-}"

# Remove existing background image to force regeneration
rm -f "$OUTPUT_PATH"

echo "Generating DMG background image..."

# Check if Python 3 and Pillow are available
if command -v python3 &> /dev/null; then
    echo "Found Python 3, checking for Pillow..."
    
    if python3 -c "import PIL" 2>/dev/null; then
        echo "Pillow found, generating background image..."
        python3 "$SCRIPT_DIR/create_dmg_background.py" "$OUTPUT_PATH" "$VERSION_STR"
        
        if [ -f "$OUTPUT_PATH" ]; then
            echo "✓ Background image created successfully: $OUTPUT_PATH"
            exit 0
        else
            echo "✗ Failed to create background image"
        fi
    else
        echo "Pillow not found. Installing..."
        if command -v pip3 &> /dev/null; then
            pip3 install Pillow
            echo "Pillow installed, generating background image..."
            python3 "$SCRIPT_DIR/create_dmg_background.py" "$OUTPUT_PATH" "$VERSION_STR"
            
            if [ -f "$OUTPUT_PATH" ]; then
                echo "✓ Background image created successfully: $OUTPUT_PATH"
                exit 0
            fi
        else
            echo "pip3 not found, cannot install Pillow automatically"
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