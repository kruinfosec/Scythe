#!/bin/bash

# Demo script to showcase available hacker fonts in Scythe terminal
# Run this in the terminal to see font availability

echo "🎯 Scythe Terminal - Available Hacker Fonts Demo"
echo "================================================"
echo ""

# Check font availability
fonts=(
    "Hack"
    "Fira Code" 
    "DejaVu Sans Mono"
    "Liberation Mono"
    "Courier"
    "FreeMono"
    "Noto Sans Mono"
)

echo "📋 Checking font availability on your system:"
echo ""

for font in "${fonts[@]}"; do
    if fc-list | grep -qi "$font"; then
        echo "✅ $font - Available"
    else
        echo "❌ $font - Not found"
    fi
done

echo ""
echo "🛠️  To install missing fonts on Kali Linux:"
echo "   sudo apt install fonts-hack fonts-firacode fonts-liberation"
echo "   sudo apt install fonts-dejavu fonts-freefont-ttf"
echo "   # Note: Most of these fonts should already be available on Kali"
echo ""
echo "🚀 Launch the enhanced terminal with: ./scythe-fixed"
echo "   Then access font settings via the customization dialog"