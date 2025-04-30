#!/bin/bash

echo "🔧 Updating system..."
sudo apt update && sudo apt upgrade -y

echo "📦 Installing compiler and build tools..."
sudo apt install -y build-essential pkg-config curl git

echo "📚 Installing required development libraries..."
sudo apt install -y libgtk-3-dev libvte-2.91-dev libsoup2.4-dev libjson-glib-dev

echo "🧰 Installing runtime tools..."
sudo apt install -y bash python3

echo "🛠 Installing automation tools (Nikto, Nmap)..."
sudo apt install -y nikto nmap

if grep -qi microsoft /proc/version; then
  echo "⚠️ Detected WSL — skipping Ollama install (run it on Windows instead)"
  echo "💡 You can still access it from WSL via http://localhost:11434"
else
  echo "🧠 Installing Ollama (AI backend)..."
  curl -fsSL https://ollama.com/install.sh | sh
  echo "📥 Pulling LLaMA 3 model..."
  ollama pull llama3
fi

echo "✅ All dependencies installed successfully."
