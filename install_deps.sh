#!/bin/bash

# install_deps.sh
set -e  # Exit on any error

echo "Installing system dependencies for C++ Service..."

# Update package list
apt-get update

# Install compiler and build tools
apt-get install -y \
    clang-18 \
    cmake \
    ninja-build \
    build-essential

# Install libraries
apt-get install -y \
    rapidjson-dev \
    libfmt-dev \
    libyaml-cpp-dev \
    libspdlog-dev \
    libgtest-dev \
    libgmock-dev

# Install additional tools for development and monitoring
apt-get install -y \
    curl \
    wget \
    git

# Set clang-18 as default (optional)
# sudo update-alternatives --install /usr/bin/cc cc /usr/bin/clang-18 100
# sudo update-alternatives --install /usr/bin/c++ c++ /usr/bin/clang++-18 100

echo "Dependencies installed successfully!"
echo "Compiler versions:"
clang++-18 --version
cmake --version
ninja --version