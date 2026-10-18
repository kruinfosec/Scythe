#!/bin/bash
# Install or upgrade Scythe using the generated scythe.deb package

# Exit on error
set -e

# Path to the deb package
DEB_PATH="./scythe.deb"

if [ ! -f "$DEB_PATH" ]; then
    echo "Error: $DEB_PATH not found."
    exit 1
fi

echo "🔄 Updating package lists..."
sudo apt-get update || true

echo "📦 Installing Scythe runtime dependencies..."
sudo apt-get install -y \
    libgtk-3-0 \
    libvte-2.91-0 \
    libwebkit2gtk-4.1-0 \
    libsoup-3.0-0 \
    libjson-glib-1.0-0 \
    libhpdf-dev \
    libsqlite3-0 \
    uuid-runtime \
    nmap \
    nikto \
    python3 \
    python3-requests \
    python3-dnspython \
    python3-whois \
    python3-colorama

echo "🚀 Installing/updating Scythe package..."
sudo apt install -y "$DEB_PATH"

echo "Scythe v2.5 has been successfully installed/updated!"
echo "💡 Note: Please run Scythe from the directory containing 'Modelfile' to load the AI core prompt, or configure it via the interface."
