#!/usr/bin/env python3

import os
import re

# 定义需要处理的文件扩展名
extensions = ['.h', '.cc']

# 遍历目录中的所有文件
def traverse_directory(directory):
    for root, dirs, files in os.walk(directory):
        for file in files:
            if any(file.endswith(ext) for ext in extensions):
                filepath = os.path.join(root, file)
                process_file(filepath)

# 处理单个文件
def process_file(filepath):
    print(f"Processing {filepath}...")
    
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()
    
    # 保存原始内容用于比较
    original_content = content
    
    # 匹配 m_xxxx 形式的成员变量定义和使用
    # 处理成员变量定义 (在类中的定义)
    # 匹配模式: m_xxxx; 或 m_xxxx{...}; 或 m_xxxx = ...;
    content = re.sub(r'(\s)m_([a-zA-Z0-9_]+)(\s*[;={])', r'\1\2_\3', content)
    
    # 处理成员变量使用 (this->m_xxxx 的形式)
    content = re.sub(r'(\bthis->)m_([a-zA-Z0-9_]+)', r'\1\2_', content)
    
    # 处理成员变量使用 (->m_xxxx 的形式)
    content = re.sub(r'(->)m_([a-zA-Z0-9_]+)', r'\1\2_', content)
    
    # 处理成员变量使用 (.m_xxxx 的形式)
    content = re.sub(r'(\.)m_([a-zA-Z0-9_]+)', r'.\2_', content)
    
    # 处理其他形式的成员变量使用 (直接使用 m_xxxx 的形式)
    content = re.sub(r'(?<!\w)m_([a-zA-Z0-9_]+)(?!\w)', r'\1_', content)
    
    # 如果内容有变化，则写入文件
    if content != original_content:
        with open(filepath, 'w', encoding='utf-8') as f:
            f.write(content)
        print(f"Updated {filepath}")
    else:
        print(f"No changes in {filepath}")

# 主函数
if __name__ == '__main__':
    # 处理 rocket 目录
    traverse_directory('rocket')
    print("Member variable renaming completed!")