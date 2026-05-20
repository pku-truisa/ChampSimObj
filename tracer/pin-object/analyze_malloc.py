#!/usr/bin/env python3
"""
内存分配追踪文件分析程序
功能：
1. 解析malloc/calloc/realloc调用，将小于阈值的size参数调整为大于或等于该值的最小的2的幂次方
2. 追踪活跃内存对象，记录最大内存占用情况
3. 对比分析修改前后的峰值内存占用差异

使用方法：
    python3 analyze_malloc.py -i <input_file> [-o <output_file>] [-s <threshold>]
    
参数说明：
    -i, --input     必需的输入文件路径
    -o, --output    可选的输出文件路径，默认为 input_file.modified
    -s, --size      可选的size阈值，默认为1024字节
    
示例：
    python3 analyze_malloc.py -i trace.malloc
    python3 analyze_malloc.py -i trace.malloc -o output.malloc
    python3 analyze_malloc.py -i trace.malloc -o output.malloc -s 2048
"""

import re
import sys
import argparse


def next_power_of_2(n):
    """计算大于或等于n的最小的2的幂次方"""
    if n <= 0:
        return 1
    # 检查n是否已经是2的幂次方
    if (n & (n - 1)) == 0:
        return n
    # 如果不是，计算大于n的最小的2的幂次方
    power = 1
    while power < n:
        power <<= 1
    return power


class MemoryTracker:
    """活跃内存对象追踪器"""
    
    def __init__(self):
        self.active_objects = {}  # {address: size} 活跃对象字典
        self.current_memory = 0   # 当前内存使用量
        self.max_memory = 0       # 峰值内存使用量
    
    def add_object(self, address, size):
        """添加新的内存对象"""
        self.active_objects[address] = size
        self.current_memory += size
        # 更新峰值
        if self.current_memory > self.max_memory:
            self.max_memory = self.current_memory
    
    def remove_object(self, address):
        """删除内存对象"""
        if address in self.active_objects:
            size = self.active_objects.pop(address)
            self.current_memory -= size
    
    def update_object(self, old_address, new_address, new_size):
        """更新内存对象（realloc）"""
        # 先删除旧对象
        self.remove_object(old_address)
        # 再添加新对象
        self.add_object(new_address, new_size)
    
    def get_max_memory_mb(self):
        """获取峰值内存（MB）"""
        return self.max_memory / (1024 * 1024)
    
    def get_current_memory_mb(self):
        """获取当前内存（MB）"""
        return self.current_memory / (1024 * 1024)
    
    def get_active_count(self):
        """获取活跃对象数量"""
        return len(self.active_objects)


def process_malloc_file(input_file, output_file, threshold):
    """
    处理内存分配追踪文件
    将所有malloc/calloc/realloc的size参数（如果小于threshold）调整为大于或等于该值的最小的2的幂次方
    同时追踪活跃内存对象和峰值内存使用，对比修改前后的差异
    """
    # 匹配malloc/calloc/realloc调用的正则表达式
    # malloc(size)=address
    # calloc(size)=address (和malloc一样，只有一个size参数)
    # realloc(address, size)=address
    malloc_pattern = re.compile(r'^(malloc)\((\d+)\)=(0x[0-9a-f]+)$')
    calloc_pattern = re.compile(r'^(calloc)\((\d+)\)=(0x[0-9a-f]+)$')
    realloc_pattern = re.compile(r'^(realloc)\((0x[0-9a-f]+),(\d+)\)=(0x[0-9a-f]+)$')
    free_pattern = re.compile(r'^free\((0x[0-9a-f]+)\)$')
    
    modified_count = 0
    total_count = 0
    
    # 创建两个追踪器：一个用于原始size，一个用于修改后的size
    tracker_original = MemoryTracker()
    tracker_modified = MemoryTracker()
    
    with open(input_file, 'r') as infile, open(output_file, 'w') as outfile:
        for line in infile:
            line = line.strip()
            
            # 处理malloc调用
            match = malloc_pattern.match(line)
            if match:
                total_count += 1
                func_name = match.group(1)
                original_size = int(match.group(2))
                address = match.group(3)
                
                # 调整size为2的幂次方（如果小于threshold）
                modified_size = original_size
                if original_size < threshold:
                    new_size = next_power_of_2(original_size)
                    if new_size != original_size:
                        modified_count += 1
                        modified_size = new_size
                
                # 分别追踪原始和修改后的内存对象
                tracker_original.add_object(address, original_size)
                tracker_modified.add_object(address, modified_size)
                
                # 写入修改后的行
                if original_size != modified_size:
                    outfile.write(f"{func_name}({modified_size})={address}\n")
                else:
                    outfile.write(line + '\n')
                continue
            
            # 处理calloc调用（和malloc一样，只有一个size参数）
            match = calloc_pattern.match(line)
            if match:
                total_count += 1
                func_name = match.group(1)
                original_size = int(match.group(2))
                address = match.group(3)
                
                # 调整size为2的幂次方（如果小于threshold）
                modified_size = original_size
                if original_size < threshold:
                    new_size = next_power_of_2(original_size)
                    if new_size != original_size:
                        modified_count += 1
                        modified_size = new_size
                
                # 分别追踪原始和修改后的内存对象
                tracker_original.add_object(address, original_size)
                tracker_modified.add_object(address, modified_size)
                
                # 写入修改后的行
                if original_size != modified_size:
                    outfile.write(f"{func_name}({modified_size})={address}\n")
                else:
                    outfile.write(line + '\n')
                continue
            
            # 处理realloc调用
            match = realloc_pattern.match(line)
            if match:
                total_count += 1
                func_name = match.group(1)
                address_old = match.group(2)
                original_size = int(match.group(3))
                address_new = match.group(4)
                
                # 调整size为2的幂次方（如果小于threshold）
                modified_size = original_size
                if original_size < threshold:
                    new_size = next_power_of_2(original_size)
                    if new_size != original_size:
                        modified_count += 1
                        modified_size = new_size
                
                # 分别追踪原始和修改后的内存对象
                tracker_original.update_object(address_old, address_new, original_size)
                tracker_modified.update_object(address_old, address_new, modified_size)
                
                # 写入修改后的行
                if original_size != modified_size:
                    outfile.write(f"{func_name}({address_old},{modified_size})={address_new}\n")
                else:
                    outfile.write(line + '\n')
                continue
            
            # 处理free调用
            match = free_pattern.match(line)
            if match:
                address = match.group(1)
                # 从两个追踪器中移除对象
                tracker_original.remove_object(address)
                tracker_modified.remove_object(address)
                outfile.write(line + '\n')
                continue
            
            # 其他行直接写入
            outfile.write(line + '\n')
    
    # 计算统计数据
    original_peak = tracker_original.max_memory
    modified_peak = tracker_modified.max_memory
    memory_increase = modified_peak - original_peak
    increase_percentage = (memory_increase / original_peak * 100) if original_peak > 0 else 0
    
    print(f"处理完成！")
    print(f"总共找到 {total_count} 个内存分配调用")
    print(f"修改了 {modified_count} 个size参数")
    print(f"\n=== 峰值内存占用对比分析 ===")
    print(f"修改前峰值内存: {tracker_original.get_max_memory_mb():.2f} MB ({original_peak:,} bytes)")
    print(f"修改后峰值内存: {tracker_modified.get_max_memory_mb():.2f} MB ({modified_peak:,} bytes)")
    print(f"内存增加量:     {memory_increase / (1024 * 1024):.2f} MB ({memory_increase:,} bytes)")
    print(f"增加百分比:     {increase_percentage:.2f}%")
    print(f"\n=== 最终状态统计 ===")
    print(f"最终活跃对象数: {tracker_modified.get_active_count():,}")
    print(f"最终内存占用:   {tracker_modified.get_current_memory_mb():.2f} MB ({tracker_modified.current_memory:,} bytes)")
    print(f"输出文件已保存到: {output_file}")


if __name__ == "__main__":
    # 创建命令行参数解析器
    parser = argparse.ArgumentParser(
        description='内存分配追踪文件分析工具',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  python3 analyze_malloc.py -i trace.malloc
  python3 analyze_malloc.py -i trace.malloc -o output.malloc
  python3 analyze_malloc.py -i trace.malloc -o output.malloc -s 2048
        """
    )
    
    # 添加参数选项
    parser.add_argument('-i', '--input', 
                        required=True,
                        help='必需的输入文件路径')
    
    parser.add_argument('-o', '--output',
                        default=None,
                        help='可选的输出文件路径，默认为 input_file.modified')
    
    parser.add_argument('-s', '--size',
                        type=int,
                        default=1024,
                        help='size阈值（字节），默认为1024。小于此值的size将被调整为2的幂次方')
    
    # 解析参数
    args = parser.parse_args()
    
    # 获取输入文件路径
    input_file = args.input
    
    # 确定输出文件路径
    if args.output:
        output_file = args.output
    else:
        # 如果没有指定输出文件，默认为 input_file.modified
        output_file = input_file + ".modified"
    
    # 获取阈值
    threshold = args.size
    
    print("开始处理内存分配追踪文件...")
    print(f"输入文件: {input_file}")
    print(f"输出文件: {output_file}")
    print(f"Size阈值: {threshold} 字节")
    print("-" * 50)
    
    process_malloc_file(input_file, output_file, threshold)
