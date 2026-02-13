#!/usr/bin/env python3
"""
PCM音频压缩工具
将24000Hz 16bit PCM文件压缩为更小的格式

支持的压缩方法：
1. 降采样 (24kHz -> 16kHz, 12kHz, 8kHz)
2. 位深度降低 (16bit -> 12bit, 8bit)
3. 简单的差分编码
4. 音量动态范围压缩
"""

import os
import sys
import struct
import numpy as np
from pathlib import Path

def read_pcm_file(filepath, sample_rate=24000, channels=1, bit_depth=16):
    """读取PCM文件"""
    with open(filepath, 'rb') as f:
        data = f.read()
    
    if bit_depth == 16:
        audio = np.frombuffer(data, dtype=np.int16)
    elif bit_depth == 8:
        audio = np.frombuffer(data, dtype=np.uint8) - 128  # Convert to signed
    else:
        raise ValueError(f"Unsupported bit depth: {bit_depth}")
    
    return audio

def write_pcm_file(filepath, audio, bit_depth=16):
    """写入PCM文件"""
    if bit_depth == 16:
        data = audio.astype(np.int16).tobytes()
    elif bit_depth == 8:
        data = (audio.astype(np.int8) + 128).astype(np.uint8).tobytes()
    else:
        raise ValueError(f"Unsupported bit depth: {bit_depth}")
    
    with open(filepath, 'wb') as f:
        f.write(data)

def resample_audio(audio, orig_rate, target_rate):
    """简单的重采样（最近邻）"""
    if orig_rate == target_rate:
        return audio
    
    ratio = target_rate / orig_rate
    new_length = int(len(audio) * ratio)
    indices = np.arange(new_length) / ratio
    indices = np.clip(indices, 0, len(audio) - 1).astype(int)
    return audio[indices]

def reduce_bit_depth(audio, target_bits):
    """降低位深度"""
    if target_bits >= 16:
        return audio
    
    # 计算量化级别
    scale_factor = 2 ** (target_bits - 1) / 32768
    quantized = np.round(audio * scale_factor) / scale_factor
    return np.clip(quantized, -32768, 32767).astype(np.int16)

def differential_encode(audio):
    """差分编码 - 存储相邻样本的差值"""
    # 第一个样本保持原样，其余存储差值
    diff = np.diff(audio, prepend=audio[0])
    return diff

def differential_decode(diff_audio):
    """差分解码"""
    return np.cumsum(diff_audio)

def compress_dynamic_range(audio, threshold_db=-12, ratio=4):
    """动态范围压缩器"""
    # 转换到浮点数
    audio_float = audio.astype(np.float32) / 32768.0
    
    # 计算幅度
    amplitude = np.abs(audio_float)
    
    # 计算压缩阈值
    threshold = 10 ** (threshold_db / 20.0)
    
    # 应用压缩
    compressed = np.where(
        amplitude > threshold,
        np.sign(audio_float) * (threshold + (amplitude - threshold) / ratio),
        audio_float
    )
    
    return (compressed * 32768.0).astype(np.int16)

def compress_pcm_file(input_path, output_path, method='resample_8k'):
    """压缩PCM文件"""
    print(f"压缩 {input_path} -> {output_path}")
    
    # 读取原始音频
    orig_audio = read_pcm_file(input_path)
    orig_size = len(orig_audio) * 2  # 16-bit = 2 bytes per sample
    
    print(f"原始大小: {orig_size} bytes ({orig_size/1024:.1f} KB)")
    
    if method == 'resample_16k':
        # 降采样到16kHz
        compressed_audio = resample_audio(orig_audio, 24000, 16000)
        
    elif method == 'resample_12k':
        # 降采样到12kHz
        compressed_audio = resample_audio(orig_audio, 24000, 12000)
        
    elif method == 'resample_8k':
        # 降采样到8kHz
        compressed_audio = resample_audio(orig_audio, 24000, 8000)
        
    elif method == 'reduce_12bit':
        # 降低位深度到12bit
        compressed_audio = reduce_bit_depth(orig_audio, 12)
        
    elif method == 'reduce_8bit':
        # 降低位深度到8bit + 压缩动态范围
        compressed_audio = compress_dynamic_range(orig_audio, -6, 3)
        compressed_audio = reduce_bit_depth(compressed_audio, 8)
        
    elif method == 'combined':
        # 组合方法：降采样+位深度降低+动态范围压缩
        compressed_audio = resample_audio(orig_audio, 24000, 16000)
        compressed_audio = compress_dynamic_range(compressed_audio, -6, 2)
        compressed_audio = reduce_bit_depth(compressed_audio, 12)
        
    else:
        raise ValueError(f"Unknown compression method: {method}")
    
    # 写入压缩后的文件
    if '8bit' in method:
        write_pcm_file(output_path, compressed_audio, 8)
        new_size = len(compressed_audio)
    else:
        write_pcm_file(output_path, compressed_audio, 16)
        new_size = len(compressed_audio) * 2
    
    compression_ratio = orig_size / new_size
    print(f"压缩后大小: {new_size} bytes ({new_size/1024:.1f} KB)")
    print(f"压缩比: {compression_ratio:.2f}x")
    print()

def main():
    if len(sys.argv) < 2:
        print("使用方法: python compress_pcm.py <input_directory> [method]")
        print("方法: resample_16k, resample_12k, resample_8k, reduce_12bit, reduce_8bit, combined")
        print("默认方法: resample_8k")
        sys.exit(1)
    
    input_dir = Path(sys.argv[1])
    method = sys.argv[2] if len(sys.argv) > 2 else 'resample_8k'
    
    if not input_dir.exists():
        print(f"输入目录不存在: {input_dir}")
        sys.exit(1)
    
    # 创建输出目录
    output_dir = input_dir.parent / f"{input_dir.name}_compressed_{method}"
    output_dir.mkdir(exist_ok=True)
    
    # 处理所有PCM文件
    pcm_files = list(input_dir.glob("*.pcm"))
    if not pcm_files:
        print(f"在 {input_dir} 中未找到PCM文件")
        sys.exit(1)
    
    total_orig_size = 0
    total_compressed_size = 0
    
    for pcm_file in pcm_files:
        output_file = output_dir / pcm_file.name
        try:
            orig_size = pcm_file.stat().st_size
            compress_pcm_file(pcm_file, output_file, method)
            compressed_size = output_file.stat().st_size
            
            total_orig_size += orig_size
            total_compressed_size += compressed_size
        except Exception as e:
            print(f"处理 {pcm_file} 时出错: {e}")
    
    print("=" * 50)
    print(f"总体压缩统计:")
    print(f"原始总大小: {total_orig_size} bytes ({total_orig_size/1024:.1f} KB)")
    print(f"压缩总大小: {total_compressed_size} bytes ({total_compressed_size/1024:.1f} KB)")
    print(f"总压缩比: {total_orig_size/total_compressed_size:.2f}x")
    print(f"节省空间: {(total_orig_size-total_compressed_size)/1024:.1f} KB")

if __name__ == "__main__":
    main()