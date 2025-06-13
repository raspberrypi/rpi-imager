#!/bin/bash

# Check for required dependencies for macOS DMG creation

echo "Checking macOS build dependencies..."

# Check for create-dmg
if command -v create-dmg &> /dev/null; then
    echo "✓ create-dmg is installed"
    CREATE_DMG_VERSION=$(create-dmg --version 2>/dev/null || echo "unknown")
    echo "  Version: $CREATE_DMG_VERSION"
else
    echo "❌ create-dmg is NOT installed"
    echo ""
    echo "To install create-dmg:"
    echo "  Option 1 (Homebrew): brew install create-dmg"
    echo "  Option 2 (Manual):   git clone https://github.com/create-dmg/create-dmg.git && cd create-dmg && make install"
    echo ""
    echo "create-dmg is required for creating styled DMGs with proper appearance."
    echo "See: https://github.com/create-dmg/create-dmg"
    MISSING_DEPS=true
fi

# Check for other tools
if command -v hdiutil &> /dev/null; then
    echo "✓ hdiutil is available"
else
    echo "❌ hdiutil is NOT available (should be included with macOS)"
    MISSING_DEPS=true
fi

if command -v codesign &> /dev/null; then
    echo "✓ codesign is available"
else
    echo "❌ codesign is NOT available (should be included with Xcode)"
    MISSING_DEPS=true
fi

# Check for Python (for background generation fallback)
if command -v python3 &> /dev/null; then
    echo "✓ python3 is available (for background generation)"
    PYTHON_VERSION=$(python3 --version 2>&1)
    echo "  $PYTHON_VERSION"
    
    # Check for Pillow
    if python3 -c "import PIL" &> /dev/null; then
        echo "✓ Python Pillow is available"
    else
        echo "⚠ Python Pillow is not available (background generation may fail)"
        echo "  Install with: pip3 install Pillow"
    fi
else
    echo "⚠ python3 is not available (background generation will use fallback)"
fi

echo ""

if [ "$MISSING_DEPS" = "true" ]; then
    echo "❌ Some required dependencies are missing."
    echo "Please install the missing dependencies before building."
    exit 1
else
    echo "✅ All required dependencies are available!"
    echo "You can now build the macOS DMG."
    exit 0
fi 