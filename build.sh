#!/bin/bash

# 构建脚本

BUILD_TYPE=${1:-Debug}

echo "清理旧的构建文件..."
rm -rf build/

echo "创建构建目录..."
mkdir -p build

echo "进入构建目录..."
cd build

echo "运行CMake配置..."
cmake .. -DCMAKE_BUILD_TYPE=$BUILD_TYPE

if [ $? -ne 0 ]; then
    echo "CMake配置失败!"
    exit 1
fi

echo "开始编译..."
make -j$(nproc)

if [ $? -ne 0 ]; then
    echo "编译失败!"
    exit 1
fi

echo "编译成功完成!"
echo "可执行文件位于 build/ 目录中"