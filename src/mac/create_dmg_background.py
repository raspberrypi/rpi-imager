#!/usr/bin/env python3
"""
Script to create a stylized background image for the Raspberry Pi Imager DMG.
This creates a background that guides users to drag the app to Applications.
"""

import os
import sys
from PIL import Image, ImageDraw, ImageFont
import subprocess

def create_dmg_background(output_path="dmg_background.png", version_str="", width=600, height=400):
    """Create a stylized DMG background image."""
    
    # Use supersampling for better antialiasing - render at 2x resolution then scale down
    scale_factor = 2
    high_width = width * scale_factor
    high_height = height * scale_factor
    
    # Create high-resolution image with RGBA for proper alpha blending
    img = Image.new('RGBA', (high_width, high_height), color=(248, 249, 250, 255))
    draw = ImageDraw.Draw(img)
    
    # Create a subtle gradient background (scaled for high resolution)
    for y in range(high_height):
        # Gradient from light gray to white with full alpha
        gray_value = int(248 + (7 * y / high_height))  # 248 to 255
        color = (gray_value, gray_value, gray_value, 255)
        draw.line([(0, y), (high_width, y)], fill=color)
    
    # Try to load high-quality system fonts (Raspberry Pi website uses Inter/system fonts)
    try:
        # Try to find the best system fonts for macOS (similar to raspberrypi.com)
        font_paths = [
            '/System/Library/Fonts/SF-Pro-Display-Regular.otf',  # macOS system font
            '/System/Library/Fonts/SF-Pro-Text-Regular.otf',     # macOS system font
            '/System/Library/Fonts/Helvetica.ttc',               # Classic fallback
            '/System/Library/Fonts/Arial.ttf',                   # Windows compatibility
            '/Library/Fonts/Arial.ttf'                           # Additional fallback
        ]
        
        title_font = None
        instruction_font = None
        
        for font_path in font_paths:
            if os.path.exists(font_path):
                try:
                    # Use larger fonts scaled for high resolution
                    title_font = ImageFont.truetype(font_path, 32 * scale_factor)
                    instruction_font = ImageFont.truetype(font_path, 18 * scale_factor)
                    print(f"Using font: {font_path}")
                    break
                except Exception as e:
                    print(f"Failed to load {font_path}: {e}")
                    continue
        
        if not title_font:
            # Fallback to default font with larger sizes
            title_font = ImageFont.load_default()
            instruction_font = ImageFont.load_default()
            print("Using default fonts")
            
    except Exception as e:
        print(f"Warning: Could not load custom font: {e}")
        title_font = ImageFont.load_default()
        instruction_font = ImageFont.load_default()
    
    # Colors inspired by raspberrypi.com design
    raspberry_red = '#C51A4A'      # Official Raspberry Pi red
    text_color = '#1a1a1a'         # Darker, more readable text
    light_text = '#666666'         # Better contrast gray
    
    # Draw title with version if provided
    if version_str:
        title_text = f"Raspberry Pi Imager {version_str}"
    else:
        title_text = "Raspberry Pi Imager"
    
    title_bbox = draw.textbbox((0, 0), title_text, font=title_font)
    title_width = title_bbox[2] - title_bbox[0]
    title_x = (high_width - title_width) // 2
    title_y = 50 * scale_factor
    
    # Draw title with clean, modern appearance
    draw.text((title_x, title_y), title_text, fill=raspberry_red, font=title_font)
    
    # No instruction text needed - the visual elements speak for themselves
    
    # Draw arrow pointing from app position to Applications position (scaled)
    app_x = 150 * scale_factor  # Position where app will be placed
    app_y = 200 * scale_factor  # Vertically centered in 400px window
    apps_x = 450 * scale_factor  # Position where Applications shortcut will be
    apps_y = 200 * scale_factor  # Vertically centered in 400px window
    

    
    # Add some decorative elements (scaled)
    # Draw larger circles around the expected icon positions
    circle_color = '#ecf0f1'
    circle_width = 2 * scale_factor
    icon_size = 128 * scale_factor
    circle_padding = 30 * scale_factor  # Reduced by 80px radius from previous 110px
    
    # Circle around app position
    draw.ellipse([
        (app_x - icon_size//2 - circle_padding, app_y - icon_size//2 - circle_padding),
        (app_x + icon_size//2 + circle_padding, app_y + icon_size//2 + circle_padding)
    ], outline=circle_color, width=circle_width)
    
    # Circle around Applications position
    draw.ellipse([
        (apps_x - icon_size//2 - circle_padding, apps_y - icon_size//2 - circle_padding),
        (apps_x + icon_size//2 + circle_padding, apps_y + icon_size//2 + circle_padding)
    ], outline=circle_color, width=circle_width)
    
    # Draw high-quality chevron arrow using simple lines with RGBA antialiasing
    chevron_color = raspberry_red
    chevron_y = app_y  # Same vertical level as the icons
    chevron_left_x = app_x + icon_size//2 + circle_padding + (20 * scale_factor)  # Start after left circle
    chevron_right_x = apps_x - icon_size//2 - circle_padding - (20 * scale_factor)  # End before right circle
    chevron_center_x = (chevron_left_x + chevron_right_x) // 2
    chevron_size = 24 * scale_factor  # Scaled for high resolution
    
    # Draw chevron as a single polygon for perfect shape
    line_width = 6 * scale_factor
    half_width = line_width // 2
    
    # Calculate chevron geometry
    left_x = chevron_center_x - chevron_size//2
    right_x = chevron_center_x + chevron_size//2
    top_y = chevron_y - chevron_size//2
    bottom_y = chevron_y + chevron_size//2
    center_y = chevron_y
    
    # Create chevron as a single polygon with proper thickness
    # This creates a ">" shape with consistent thickness
    chevron_points = [
        # Top line outer edge
        (left_x, top_y - half_width),
        (right_x - half_width, center_y - half_width),
        # Point of the chevron
        (right_x, center_y),
        # Bottom line outer edge  
        (right_x - half_width, center_y + half_width),
        (left_x, bottom_y + half_width),
        # Bottom line inner edge
        (left_x, bottom_y - half_width),
        (right_x - half_width * 2, center_y),
        # Top line inner edge
        (left_x, top_y + half_width)
    ]
    
    # Draw the complete chevron as one smooth polygon
    draw.polygon(chevron_points, fill=chevron_color)
    
    # Applications folder shortcut will have its own label, so we don't add one here
    
    # Scale down to final resolution with high-quality resampling for smooth antialiasing
    final_img = img.resize((width, height), Image.LANCZOS)
    
    # Convert to RGB for final output (since DMG backgrounds should be RGB)
    if final_img.mode == 'RGBA':
        # Create white background and composite
        rgb_img = Image.new('RGB', (width, height), (248, 249, 250))
        rgb_img.paste(final_img, mask=final_img.split()[-1])  # Use alpha channel as mask
        final_img = rgb_img
    
    # Save the image
    final_img.save(output_path, 'PNG', optimize=True)
    print(f"DMG background created: {output_path}")
    return True

def check_dependencies():
    """Check if required dependencies are available."""
    try:
        import PIL
        return True
    except ImportError:
        print("Error: Pillow (PIL) is required to generate the DMG background.")
        print("Install it with: pip3 install Pillow")
        return False

def main():
    """Main function."""
    if not check_dependencies():
        sys.exit(1)
    
    # Get output path and version from command line or use defaults
    output_path = sys.argv[1] if len(sys.argv) > 1 else "dmg_background.png"
    version_str = sys.argv[2] if len(sys.argv) > 2 else ""
    
    try:
        success = create_dmg_background(output_path, version_str)
        if success:
            print("DMG background image created successfully!")
            print(f"Image saved to: {output_path}")
            print("Image dimensions: 600x400 pixels")
            print("This image will be used as the background for the DMG installer.")
        else:
            print("Failed to create DMG background image.")
            sys.exit(1)
    except Exception as e:
        print(f"Error creating DMG background: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main() 