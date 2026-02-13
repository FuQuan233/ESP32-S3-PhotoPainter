#include <stdio.h>
#include <string.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include "codec_bsp.h"
#include "i2c_bsp.h"
#include <opus.h>
#include <vector>
#include <memory>

// 嵌入固件中的OGG音频数据声明（已压缩格式）
extern "C" {
    extern const uint8_t mode_ogg_start[] asm("_binary_mode_ogg_start");
    extern const uint8_t mode_ogg_end[] asm("_binary_mode_ogg_end");
    extern const uint8_t mode_1_ogg_start[] asm("_binary_mode_1_ogg_start");
    extern const uint8_t mode_1_ogg_end[] asm("_binary_mode_1_ogg_end");
    extern const uint8_t mode_2_ogg_start[] asm("_binary_mode_2_ogg_start");
    extern const uint8_t mode_2_ogg_end[] asm("_binary_mode_2_ogg_end");
    extern const uint8_t mode_3_ogg_start[] asm("_binary_mode_3_ogg_start");
    extern const uint8_t mode_3_ogg_end[] asm("_binary_mode_3_ogg_end");
    extern const uint8_t mode_4_ogg_start[] asm("_binary_mode_4_ogg_start");
    extern const uint8_t mode_4_ogg_end[] asm("_binary_mode_4_ogg_end");
}

#define SAMPLE_RATE 24000 // Sampling rate: 24000Hz
#define BIT_DEPTH 32      // Word size: 32 bits

esp_codec_dev_handle_t playback = NULL;
esp_codec_dev_handle_t record   = NULL;

// 音频文件缓冲区（动态分配）
static uint8_t *audio_buffer = NULL;
static size_t audio_buffer_size = 0;

// Opus解码器相关
static OpusDecoder *opus_decoder = NULL;
static const int SAMPLE_RATE_OPUS = 16000;  // Opus输出采样率
static const int CHANNELS = 1;               // 单声道
static const int FRAME_SIZE = 960;           // 60ms @ 16kHz

CodecPort::CodecPort(I2cMasterBus& i2cbus) :
i2cbus_(i2cbus) 
{
    set_codec_board_type("USER_CODEC_BOARD");
    codec_init_cfg_t codec_cfg = {};
    codec_cfg.in_mode          = CODEC_I2S_MODE_TDM;
    codec_cfg.out_mode         = CODEC_I2S_MODE_TDM;
    codec_cfg.in_use_tdm       = false;
    codec_cfg.reuse_dev        = false;
    ESP_ERROR_CHECK(init_codec(&codec_cfg));
    playback = get_playback_handle();
    record   = get_record_handle();

    i2c_master_bus_handle_t I2cMasterBus = i2cbus_.Get_I2cBusHandle();
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address  = Es8311Address;
    dev_cfg.scl_speed_hz    = 100000;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(I2cMasterBus, &dev_cfg, &I2c_DevEs8311));

    dev_cfg.device_address  = Es7210Address;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(I2cMasterBus, &dev_cfg, &I2c_DevEs7210));

    // 初始化Opus解码器
    int opus_error;
    opus_decoder = opus_decoder_create(SAMPLE_RATE_OPUS, CHANNELS, &opus_error);
    if (opus_error != OPUS_OK) {
        ESP_LOGE("CodecPort", "Failed to create Opus decoder: %d", opus_error);
        opus_decoder = NULL;
    } else {
        ESP_LOGI("CodecPort", "Opus decoder initialized successfully");
    }

CodecPort::~CodecPort() {
    if (opus_decoder) {
        opus_decoder_destroy(opus_decoder);
        opus_decoder = NULL;
    }
    if (audio_buffer) {
        free(audio_buffer);
        audio_buffer = NULL;
    }
}

uint8_t CodecPort::Codec_PlayInfoAudio() {
    esp_codec_dev_set_out_vol(playback, 100.0); //Set the volume to 100.
    esp_codec_dev_sample_info_t fs = {};
    fs.sample_rate                 = 16000;
    fs.channel                     = 2;
    fs.bits_per_sample             = 16;
    int     err                    = esp_codec_dev_open(playback, &fs); //Start playback
    uint8_t errx                   = (err == ESP_CODEC_DEV_OK) ? 1 : 0;
    return errx;
}

void CodecPort::Codec_PlayBackWrite(void *data_ptr, uint32_t len) {
    esp_codec_dev_write(playback, data_ptr, len);
}

uint8_t CodecPort::Codec_ClosePlay() {
    int     err  = esp_codec_dev_close(playback);
    uint8_t errx = (err == ESP_CODEC_DEV_OK) ? 1 : 0;
    return errx;
}

int CodecPort::Codec_GetMusicSizt(uint8_t value) {
    // 返回解码后的PCM数据大小
    int decoded_size = 0;
    uint8_t* decoded_data = Codec_GetDecodedModeAudio(value, &decoded_size);
    return decoded_data ? decoded_size : 0;
}

uint8_t *CodecPort::Codec_GetMusicData(uint8_t value) {
    const uint8_t *data_ptr = NULL;
    size_t data_size = 0;
    
    switch(value) {
        case 0:
            data_ptr = mode_ogg_start;
            data_size = mode_ogg_end - mode_ogg_start;
            break;
        case 1:
            data_ptr = mode_1_ogg_start;
            data_size = mode_1_ogg_end - mode_1_ogg_start;
            break;
        case 2:
            data_ptr = mode_2_ogg_start;
            data_size = mode_2_ogg_end - mode_2_ogg_start;
            break;
        case 3:
            data_ptr = mode_3_ogg_start;
            data_size = mode_3_ogg_end - mode_3_ogg_start;
            break;
        case 4:
            data_ptr = mode_4_ogg_start;
            data_size = mode_4_ogg_end - mode_4_ogg_start;
            break;
        default:
            ESP_LOGE("CodecPort", "Invalid audio value: %d", value);
            return NULL;
    }
    
    if (!data_ptr || data_size == 0) {
        ESP_LOGE("CodecPort", "Empty embedded OGG audio data for value: %d", value);
        return NULL;
    }
    
    // 注意：这里返回的是压缩的OGG数据，需要解码才能播放
    // 建议使用其他音频播放函数处理OGG格式
    
    // 释放之前的缓冲区
    if (audio_buffer) {
        free(audio_buffer);
        audio_buffer = NULL;
        audio_buffer_size = 0;
    }
    
    // 分配新的缓冲区并复制数据
    audio_buffer_size = data_size;
    audio_buffer = (uint8_t *)malloc(audio_buffer_size);
    if (!audio_buffer) {
        ESP_LOGE("CodecPort", "Failed to allocate %d bytes for audio", audio_buffer_size);
        return NULL;
    }
    
    memcpy(audio_buffer, data_ptr, audio_buffer_size);
    
    ESP_LOGI("CodecPort", "Loaded embedded OGG audio %d, size: %d bytes", value, audio_buffer_size);
    return audio_buffer;
}

const uint8_t* CodecPort::Codec_GetCompressedModeAudio(uint8_t value, size_t* size) {
    const uint8_t *data_ptr = NULL;
    size_t data_size = 0;
    
    switch(value) {
        case 0:
            data_ptr = mode_ogg_start;
            data_size = mode_ogg_end - mode_ogg_start;
            break;
        case 1:
            data_ptr = mode_1_ogg_start;
            data_size = mode_1_ogg_end - mode_1_ogg_start;
            break;
        case 2:
            data_ptr = mode_2_ogg_start;
            data_size = mode_2_ogg_end - mode_2_ogg_start;
            break;
        case 3:
            data_ptr = mode_3_ogg_start;
            data_size = mode_3_ogg_end - mode_3_ogg_start;
            break;
        case 4:
            data_ptr = mode_4_ogg_start;
            data_size = mode_4_ogg_end - mode_4_ogg_start;
            break;
        default:
            ESP_LOGE("CodecPort", "Invalid mode audio value: %d", value);
            if (size) *size = 0;
            return NULL;
    }
    
    if (!data_ptr || data_size == 0) {
        ESP_LOGE("CodecPort", "Empty embedded OGG audio data for mode: %d", value);
        if (size) *size = 0;
        return NULL;
    }
    
    if (size) *size = data_size;
    ESP_LOGI("CodecPort", "Returning compressed mode audio %d, size: %d bytes", value, data_size);
    return data_ptr;
}

// OGG解析和解码函数
static size_t decode_ogg_to_pcm(const uint8_t* ogg_data, size_t ogg_size, std::vector<int16_t>& pcm_output) {
    if (!opus_decoder) {
        ESP_LOGE("CodecPort", "Opus decoder not initialized");
        return 0;
    }

    pcm_output.clear();
    
    // 搜索OGG页面的函数
    auto find_page = [&](size_t start) -> size_t {
        for (size_t i = start; i + 4 <= ogg_size; ++i) {
            if (ogg_data[i] == 'O' && ogg_data[i+1] == 'g' && ogg_data[i+2] == 'g' && ogg_data[i+3] == 'S') {
                return i;
            }
        }
        return static_cast<size_t>(-1);
    };

    size_t offset = 0;
    bool seen_head = false;
    bool seen_tags = false;
    int decoded_samples = 0;
    
    while (true) {
        size_t pos = find_page(offset);
        if (pos == static_cast<size_t>(-1)) break;
        offset = pos;
        if (offset + 27 > ogg_size) break;

        const uint8_t* page = ogg_data + offset;
        uint8_t page_segments = page[26];
        size_t seg_table_off = offset + 27;
        if (seg_table_off + page_segments > ogg_size) break;

        size_t body_size = 0;
        for (size_t i = 0; i < page_segments; ++i) {
            body_size += page[27 + i];
        }

        size_t body_off = seg_table_off + page_segments;
        if (body_off + body_size > ogg_size) break;

        // 解析包
        size_t cur = body_off;
        size_t seg_idx = 0;
        
        while (seg_idx < page_segments) {
            size_t pkt_len = 0;
            size_t pkt_start = cur;
            bool continued = false;
            
            do {
                uint8_t l = page[27 + seg_idx++];
                pkt_len += l;
                cur += l;
                continued = (l == 255);
            } while (continued && seg_idx < page_segments);

            if (pkt_len == 0) continue;
            const uint8_t* pkt_ptr = ogg_data + pkt_start;

            if (!seen_head) {
                // 解析OpusHead包
                if (pkt_len >= 19 && memcmp(pkt_ptr, "OpusHead", 8) == 0) {
                    seen_head = true;
                    ESP_LOGD("CodecPort", "Found OpusHead packet");
                }
                continue;
            }
            
            if (!seen_tags) {
                // OpusTags包
                if (pkt_len >= 8 && memcmp(pkt_ptr, "OpusTags", 8) == 0) {
                    seen_tags = true;
                    ESP_LOGD("CodecPort", "Found OpusTags packet");
                }
                continue;
            }

            // 音频数据包（Opus）
            int16_t frame_buffer[FRAME_SIZE * CHANNELS];
            int samples = opus_decode(opus_decoder, pkt_ptr, pkt_len, frame_buffer, FRAME_SIZE, 0);
            
            if (samples > 0) {
                // 将解码的样本添加到输出缓冲区
                pcm_output.insert(pcm_output.end(), frame_buffer, frame_buffer + samples * CHANNELS);
                decoded_samples += samples;
            } else if (samples < 0) {
                ESP_LOGE("CodecPort", "Opus decode error: %d", samples);
            }
        }

        offset = body_off + body_size;
    }
    
    ESP_LOGI("CodecPort", "Decoded %d samples from OGG", decoded_samples);
    return decoded_samples;
}

uint8_t *CodecPort::Codec_GetDecodedModeAudio(uint8_t value, int *pcm_size) {
    const uint8_t *ogg_data = NULL;
    size_t ogg_size = 0;
    
    // 获取压缩的OGG数据
    ogg_data = Codec_GetCompressedModeAudio(value, &ogg_size);
    if (!ogg_data || ogg_size == 0) {
        if (pcm_size) *pcm_size = 0;
        return NULL;
    }
    
    // 解码OGG为PCM
    std::vector<int16_t> pcm_data;
    size_t samples = decode_ogg_to_pcm(ogg_data, ogg_size, pcm_data);
    
    if (samples == 0 || pcm_data.empty()) {
        ESP_LOGE("CodecPort", "Failed to decode OGG audio for mode %d", value);
        if (pcm_size) *pcm_size = 0;
        return NULL;
    }
    
    // 释放之前的缓冲区
    if (audio_buffer) {
        free(audio_buffer);
        audio_buffer = NULL;
        audio_buffer_size = 0;
    }
    
    // 分配新的PCM缓冲区
    audio_buffer_size = pcm_data.size() * sizeof(int16_t);
    audio_buffer = (uint8_t *)heap_caps_malloc(audio_buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    
    if (!audio_buffer) {
        // 如果SPIRAM失败，尝试使用内部RAM
        audio_buffer = (uint8_t *)malloc(audio_buffer_size);
    }
    
    if (!audio_buffer) {
        ESP_LOGE("CodecPort", "Failed to allocate %d bytes for decoded PCM", audio_buffer_size);
        if (pcm_size) *pcm_size = 0;
        return NULL;
    }
    
    // 复制PCM数据
    memcpy(audio_buffer, pcm_data.data(), audio_buffer_size);
    
    if (pcm_size) *pcm_size = audio_buffer_size;
    ESP_LOGI("CodecPort", "Decoded mode %d: %d samples, %d bytes", value, samples, audio_buffer_size);
    return audio_buffer;
}

void CodecPort::Codec_SetCodecReg(const char *str, uint8_t reg, uint8_t data) {
    if (!strcmp(str, "es8311"))
        i2cbus_.i2c_write_buff(I2c_DevEs8311, reg, &data, 1);
    if (!strcmp(str, "es7210"))
        i2cbus_.i2c_write_buff(I2c_DevEs7210, reg, &data, 1);
}

uint8_t CodecPort::Codec_GetCodecReg(const char *str, uint8_t reg) {
    uint8_t data = 0x00;
    if (!strcmp(str, "es8311"))
        i2cbus_.i2c_read_buff(I2c_DevEs8311, reg, &data, 1);
    if (!strcmp(str, "es7210"))
        i2cbus_.i2c_read_buff(I2c_DevEs7210, reg, &data, 1);
    return data;
}
