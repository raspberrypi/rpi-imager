#!/bin/bash

# Simple script to create a basic DMG background using macOS built-in tools
# No external dependencies required

OUTPUT_PATH="${1:-dmg_background.png}"
VERSION_STR="${2:-}"
WIDTH=600
HEIGHT=400

echo "Creating simple DMG background using macOS built-in tools..."

# Create a temporary directory
TEMP_DIR=$(mktemp -d)
HTML_FILE="$TEMP_DIR/background.html"

# Create a simple HTML file with inline CSS for the background
cat > "$HTML_FILE" << 'EOF'
<!DOCTYPE html>
<html>
<head>
    <style>
        body {
            margin: 0;
            padding: 0;
            width: 600px;
            height: 400px;
            background: linear-gradient(135deg, #f8f9fa 0%, #ffffff 100%);
            font-family: -apple-system, BlinkMacSystemFont, "SF Pro Display", "SF Pro Text", "Helvetica Neue", Helvetica, Arial, sans-serif;
            /* High-quality text rendering for macOS */
            -webkit-font-smoothing: antialiased;
            -moz-osx-font-smoothing: grayscale;
            text-rendering: optimizeLegibility;
            font-feature-settings: "kern" 1;
            font-kerning: normal;
            display: flex;
            flex-direction: column;
            align-items: center;
            justify-content: center;
            position: relative;
            overflow: hidden;
        }
        .title {
            color: #C51A4A;
            font-size: 32px;
            font-weight: 600;
            margin-bottom: 20px;
            letter-spacing: -0.02em;
            line-height: 1.2;
            position: absolute;
            top: 50px;
        }

        .circle {
            position: absolute;
            border: 2px solid #ecf0f1;
            border-radius: 50%;
            width: 148px;
            height: 148px;
        }
        .circle.app {
            left: 76px;
            top: 76px;
        }
        .circle.apps {
            right: 76px;
            top: 76px;
        }
        .chevron {
            position: absolute;
            top: 50%;
            left: 50%;
            transform: translate(-50%, -50%);
            color: #C51A4A;
            font-size: 32px;
            font-weight: bold;
        }
    </style>
</head>
<body>
    <div class="title">Raspberry Pi Imager${VERSION_STR:+ $VERSION_STR}</div>
    <div class="circle app"></div>
    <div class="circle apps"></div>
    <div class="chevron">›</div>
</body>
</html>
EOF

echo "HTML template created at: $HTML_FILE"

# Try to convert HTML to PNG using built-in macOS tools
if command -v screencapture &> /dev/null && command -v open &> /dev/null; then
    echo "Attempting to create background using Safari and screencapture..."
    
    # Open the HTML file in Safari
    open -a Safari "file://$HTML_FILE"
    
    # Wait for Safari to load
    sleep 3
    
    echo "Please manually take a screenshot of the Safari window showing the background."
    echo "Then save it as: $OUTPUT_PATH"
    echo "The Safari window should be open now with the background design."
    echo ""
    echo "Alternative: You can copy the HTML file to view it in any browser:"
    echo "cp '$HTML_FILE' ~/Desktop/dmg_background.html"
    
    # Keep the HTML file available
    cp "$HTML_FILE" "$(dirname "$OUTPUT_PATH")/dmg_background.html"
    echo "HTML file copied to: $(dirname "$OUTPUT_PATH")/dmg_background.html"
    
    # Clean up temp directory but keep the HTML copy
    rm -rf "$TEMP_DIR"
    
    echo ""
    echo "Manual steps to create the background:"
    echo "1. Safari should now be open with the background design"
    echo "2. Resize the Safari window to approximately 600x400 pixels"
    echo "3. Use Cmd+Shift+4 to take a screenshot of just the content area"
    echo "4. Save the screenshot as: $OUTPUT_PATH"
    echo ""
    echo "Or skip the background - the DMG will work fine with a plain white background."
    
    exit 0
else
    echo "Required tools not available for automatic generation"
fi

# Fallback: Just provide instructions
echo ""
echo "Could not create background automatically."
echo "HTML template available at: $HTML_FILE"
echo ""
echo "To create the background manually:"
echo "1. Open $HTML_FILE in a web browser"
echo "2. Resize browser window to 600x400 pixels"
echo "3. Take a screenshot of the content area"
echo "4. Save as $OUTPUT_PATH"
echo ""
echo "The DMG will work without a background image."

# Keep the temp directory for manual use
echo "Temp directory preserved: $TEMP_DIR"
exit 1 