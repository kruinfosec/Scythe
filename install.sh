#!/bin/bash
# Install or upgrade Scythe using the generated scythe.deb package

# Exit on error
set -e

# Path to the deb package
DEB_PATH="./scythe.deb"

if [ ! -f "$DEB_PATH" ]; then
    echo "Error: $DEB_PATH not found. Please build the package first by running:"
    echo "dpkg-deb --build Scythe-pkg scythe.deb"
    exit 1
fi

echo "Installing/updating Scythe from $DEB_PATH..."
sudo apt-get update || true
sudo apt install -y "$DEB_PATH"

echo "Scythe v2.0 has been successfully installed/updated!"
