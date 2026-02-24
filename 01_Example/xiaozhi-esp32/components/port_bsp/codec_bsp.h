#pragma once

#include "codec_board.h"
#include "codec_init.h"
#include "i2c_bsp.h"

// 提示音枚举
enum PromptSound {
    PROMPT_WIFI_CONNECTING = 0,  // WiFi 正在连接
    PROMPT_WIFI_SUCCESS,         // WiFi 连接成功
    PROMPT_WIFI_FAIL,            // WiFi 连接失败
    PROMPT_WIFI_RESET,           // WiFi 配置已重置
    PROMPT_WAIT_CONFIG,          // 等待配网
    PROMPT_MANUAL_REFRESH,       // 手动刷新
};

class CodecPort
{
private:
    esp_codec_dev_handle_t playback = NULL;
    esp_codec_dev_handle_t record = NULL;
    I2cMasterBus& i2cbus_;
    i2c_master_dev_handle_t I2c_DevEs8311;
    i2c_master_dev_handle_t I2c_DevEs7210;
    const uint8_t Es8311Address = 0x18;
    const uint8_t Es7210Address = 0x40;

public:
    CodecPort(I2cMasterBus& i2cbus);
    ~CodecPort();

    // 基础播放控制 (PCM)
    uint8_t Codec_PlayInfoAudio();
    void Codec_PlayBackWrite(void *data_ptr, uint32_t len);
    uint8_t Codec_ClosePlay();

    // OGG (Opus) 音频播放 - 参考 AudioService::PlaySound 实现
    // 解析 OGG 容器，提取并解码 Opus 数据包，直接输出 PCM 到 codec
    void Codec_PlayOggAudio(const uint8_t* ogg_data, size_t ogg_size);

    // 播放指定模式的 OGG 音频 (0=mode, 1=mode_1, 2=mode_2, 3=mode_3, 4=mode_4)
    void Codec_PlayModeAudio(uint8_t mode);

    // 播放提示音
    void Codec_PlayPromptSound(PromptSound sound);

    // 获取 OGG 音频数据和大小 (替代原有的 PCM 版本)
    size_t Codec_GetOggSize(uint8_t value);
    const uint8_t* Codec_GetOggData(uint8_t value);

    void Codec_SetCodecReg(const char * str, uint8_t reg, uint8_t data);
    uint8_t Codec_GetCodecReg(const char *str, uint8_t reg);
};
