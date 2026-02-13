#!/usr/bin/env python3
"""
将PCM文件转换为压缩的OGG格式
参考项目中其他音频文件的压缩方案
"""

import os
import sys
import subprocess
from pathlib import Path

def convert_pcm_to_ogg(input_pcm, output_ogg, sample_rate=24000, channels=1, quality=1):
    """
    使用ffmpeg将PCM转换为OGG(Opus)格式
    
    Args:
        input_pcm: 输入PCM文件路径
        output_ogg: 输出OGG文件路径
        sample_rate: 采样率 (默认24000Hz)
        channels: 声道数 (默认1单声道)
        quality: Opus质量 0-10 (0最差最小，10最好最大，默认1适合语音)
    """
    
    cmd = [
        'ffmpeg',
        '-y',  # 覆盖输出文件
        '-f', 's16le',  # 输入格式：16位小端PCM
        '-ar', str(sample_rate),  # 采样率
        '-ac', str(channels),  # 声道数
        '-i', str(input_pcm),  # 输入文件
        '-c:a', 'libopus',  # 使用Opus编码器
        '-application', 'voip',  # 针对语音优化
        '-compression_level', '10',  # 最高压缩级别
        '-frame_duration', '60',  # 帧长度60ms (适合语音)
        '-vbr', 'on',  # 可变比特率
        '-b:a', '16k',  # 目标比特率16kbps (语音质量)
        str(output_ogg)  # 输出文件
    ]
    
    print(f"转换: {input_pcm.name} -> {output_ogg.name}")
    print(f"命令: {' '.join(cmd)}")
    
    try:
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode == 0:
            # 获取文件大小信息
            orig_size = input_pcm.stat().st_size
            new_size = output_ogg.stat().st_size
            compression_ratio = orig_size / new_size
            
            print(f"✓ 转换成功")
            print(f"  原始大小: {orig_size:,} bytes ({orig_size/1024:.1f} KB)")
            print(f"  压缩后: {new_size:,} bytes ({new_size/1024:.1f} KB)")
            print(f"  压缩比: {compression_ratio:.1f}x")
            print()
            return True
        else:
            print(f"✗ 转换失败: {result.stderr}")
            return False
    except FileNotFoundError:
        print("错误: 未找到ffmpeg。请确保ffmpeg已安装并在PATH中")
        return False
    except Exception as e:
        print(f"错误: {e}")
        return False

def batch_convert_pcm_files(input_dir, output_dir=None, quality=1):
    """批量转换PCM文件"""
    input_path = Path(input_dir)
    if not input_path.exists():
        print(f"输入目录不存在: {input_dir}")
        return False
    
    # 设置输出目录
    if output_dir is None:
        output_path = input_path / "compressed_ogg"
    else:
        output_path = Path(output_dir)
    
    output_path.mkdir(exist_ok=True)
    
    # 查找所有PCM文件
    pcm_files = list(input_path.glob("*.pcm"))
    if not pcm_files:
        print(f"在 {input_dir} 中未找到PCM文件")
        return False
    
    print(f"找到 {len(pcm_files)} 个PCM文件")
    print(f"输出目录: {output_path}")
    print("=" * 50)
    
    total_orig_size = 0
    total_compressed_size = 0
    success_count = 0
    
    for pcm_file in pcm_files:
        output_file = output_path / (pcm_file.stem + ".ogg")
        
        if convert_pcm_to_ogg(pcm_file, output_file, quality=quality):
            orig_size = pcm_file.stat().st_size
            compressed_size = output_file.stat().st_size
            
            total_orig_size += orig_size
            total_compressed_size += compressed_size
            success_count += 1
    
    if success_count > 0:
        print("=" * 50)
        print(f"批量转换完成:")
        print(f"  成功转换: {success_count}/{len(pcm_files)} 个文件")
        print(f"  总原始大小: {total_orig_size:,} bytes ({total_orig_size/1024:.1f} KB)")
        print(f"  总压缩大小: {total_compressed_size:,} bytes ({total_compressed_size/1024:.1f} KB)")
        print(f"  总体压缩比: {total_orig_size/total_compressed_size:.1f}x")
        print(f"  节省空间: {(total_orig_size-total_compressed_size)/1024:.1f} KB")
        return True
    else:
        print("没有文件转换成功")
        return False

def main():
    if len(sys.argv) < 2:
        print("使用方法:")
        print("  python pcm_to_ogg.py <input_directory> [output_directory] [quality]")
        print("  ")
        print("参数:")
        print("  input_directory   : 包含PCM文件的目录")
        print("  output_directory  : 输出目录 (可选，默认为 input_directory/compressed_ogg)")
        print("  quality          : Opus质量 0-10 (可选，默认1)")
        print("")
        print("示例:")
        print("  python pcm_to_ogg.py ./assets/common")
        print("  python pcm_to_ogg.py ./assets/common ./output 2")
        sys.exit(1)
    
    input_dir = sys.argv[1]
    output_dir = sys.argv[2] if len(sys.argv) > 2 else None
    quality = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    
    if not 0 <= quality <= 10:
        print("质量参数必须在0-10之间")
        sys.exit(1)
    
    success = batch_convert_pcm_files(input_dir, output_dir, quality)
    sys.exit(0 if success else 1)

if __name__ == "__main__":
    main()