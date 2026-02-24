#include <stdio.h>
#include <string.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <esp_heap_caps.h>
#include "codec_bsp.h"
#include "i2c_bsp.h"
#include "opus.h"

#define TAG "CodecPort"

#define SAMPLE_RATE 24000 // Sampling rate: 24000Hz
#define BIT_DEPTH 32      // Word size: 32 bits

// Opus 解码参数
#define OPUS_DECODE_SAMPLE_RATE  24000  // Opus 解码输出采样率
#define OPUS_FRAME_DURATION_MS   60     // 帧时长 60ms (与 AudioService 一致)
#define OPUS_MAX_FRAME_SIZE      (OPUS_DECODE_SAMPLE_RATE / 1000 * OPUS_FRAME_DURATION_MS) // 最大帧样本数

esp_codec_dev_handle_t playback = NULL;
esp_codec_dev_handle_t record   = NULL;

// OGG 音频资源 (Opus 编码，替代原有 PCM)
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

// 提示音 OGG 资源
extern const uint8_t wifi_connecting_ogg_start[] asm("_binary_wifi_connecting_ogg_start");
extern const uint8_t wifi_connecting_ogg_end[] asm("_binary_wifi_connecting_ogg_end");

extern const uint8_t wifi_success_ogg_start[] asm("_binary_wifi_success_ogg_start");
extern const uint8_t wifi_success_ogg_end[] asm("_binary_wifi_success_ogg_end");

extern const uint8_t wifi_fail_ogg_start[] asm("_binary_wifi_fail_ogg_start");
extern const uint8_t wifi_fail_ogg_end[] asm("_binary_wifi_fail_ogg_end");

extern const uint8_t wifi_reset_ogg_start[] asm("_binary_wifi_reset_ogg_start");
extern const uint8_t wifi_reset_ogg_end[] asm("_binary_wifi_reset_ogg_end");

extern const uint8_t wait_config_ogg_start[] asm("_binary_wait_config_ogg_start");
extern const uint8_t wait_config_ogg_end[] asm("_binary_wait_config_ogg_end");

extern const uint8_t manual_refresh_ogg_start[] asm("_binary_manual_refresh_ogg_start");
extern const uint8_t manual_refresh_ogg_end[] asm("_binary_manual_refresh_ogg_end");

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

}

CodecPort::~CodecPort() {
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

// ============ OGG (Opus) 音频播放实现 ============

size_t CodecPort::Codec_GetOggSize(uint8_t value) {
    switch (value) {
        case 0: return (mode_ogg_end - mode_ogg_start);
        case 1: return (mode_1_ogg_end - mode_1_ogg_start);
        case 2: return (mode_2_ogg_end - mode_2_ogg_start);
        case 3: return (mode_3_ogg_end - mode_3_ogg_start);
        case 4: return (mode_4_ogg_end - mode_4_ogg_start);
        default: return 0;
    }
}

const uint8_t* CodecPort::Codec_GetOggData(uint8_t value) {
    switch (value) {
        case 0: return mode_ogg_start;
        case 1: return mode_1_ogg_start;
        case 2: return mode_2_ogg_start;
        case 3: return mode_3_ogg_start;
        case 4: return mode_4_ogg_start;
        default: return NULL;
    }
}

void CodecPort::Codec_PlayOggAudio(const uint8_t* ogg_data, size_t ogg_size) {
    if (ogg_data == NULL || ogg_size == 0) {
        ESP_LOGE(TAG, "Invalid OGG data");
        return;
    }

    // 打开 codec 用于播放
    esp_codec_dev_set_out_vol(playback, 100.0);
    esp_codec_dev_sample_info_t fs = {};
    fs.sample_rate     = 16000;
    fs.channel         = 2;
    fs.bits_per_sample = 16;
    int err = esp_codec_dev_open(playback, &fs);
    if (err != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to open codec for OGG playback");
        return;
    }

    // 解析 OGG 容器并解码 Opus 音频 (参考 AudioService::PlaySound)
    const uint8_t* buf = ogg_data;
    size_t size = ogg_size;
    size_t offset = 0;

    // 查找 OGG 页面同步标记 "OggS"
    auto find_page = [&](size_t start) -> size_t {
        for (size_t i = start; i + 4 <= size; ++i) {
            if (buf[i] == 'O' && buf[i+1] == 'g' && buf[i+2] == 'g' && buf[i+3] == 'S') return i;
        }
        return static_cast<size_t>(-1);
    };

    bool seen_head = false;
    bool seen_tags = false;
    int sample_rate = 16000;  // 默认采样率
    int channels = 1;

    // 创建 Opus 解码器
    int opus_err;
    OpusDecoder* decoder = opus_decoder_create(sample_rate, 1, &opus_err);
    if (decoder == NULL) {
        ESP_LOGE(TAG, "Failed to create Opus decoder, error: %d", opus_err);
        esp_codec_dev_close(playback);
        return;
    }

    // PCM 输出缓冲区 - 使用堆分配避免栈溢出
    int16_t* pcm_buf = (int16_t*)heap_caps_malloc(OPUS_MAX_FRAME_SIZE * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    int16_t* stereo_buf = (int16_t*)heap_caps_malloc(OPUS_MAX_FRAME_SIZE * 2 * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (pcm_buf == NULL || stereo_buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate PCM buffers");
        opus_decoder_destroy(decoder);
        esp_codec_dev_close(playback);
        if (pcm_buf) free(pcm_buf);
        if (stereo_buf) free(stereo_buf);
        return;
    }

    while (true) {
        size_t pos = find_page(offset);
        if (pos == static_cast<size_t>(-1)) break;
        offset = pos;
        if (offset + 27 > size) break;

        const uint8_t* page = buf + offset;
        uint8_t page_segments = page[26];
        size_t seg_table_off = offset + 27;
        if (seg_table_off + page_segments > size) break;

        size_t body_size = 0;
        for (size_t i = 0; i < page_segments; ++i) body_size += page[27 + i];

        size_t body_off = seg_table_off + page_segments;
        if (body_off + body_size > size) break;

        // 使用 lacing 解析数据包
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
            const uint8_t* pkt_ptr = buf + pkt_start;

            if (!seen_head) {
                // 解析 OpusHead 包
                if (pkt_len >= 19 && memcmp(pkt_ptr, "OpusHead", 8) == 0) {
                    seen_head = true;
                    channels = pkt_ptr[9];
                    if (pkt_len >= 16) {
                        sample_rate = pkt_ptr[12] | (pkt_ptr[13] << 8) |
                                     (pkt_ptr[14] << 16) | (pkt_ptr[15] << 24);
                        ESP_LOGI(TAG, "OpusHead: channels=%d, sample_rate=%d", channels, sample_rate);
                    }
                    // 按实际采样率重新创建解码器
                    if (sample_rate != 16000) {
                        opus_decoder_destroy(decoder);
                        decoder = opus_decoder_create(sample_rate, 1, &opus_err);
                        if (decoder == NULL) {
                            ESP_LOGE(TAG, "Failed to recreate Opus decoder for sample_rate=%d", sample_rate);
                            free(pcm_buf);
                            free(stereo_buf);
                            esp_codec_dev_close(playback);
                            return;
                        }
                        // 更新 codec 采样率
                        esp_codec_dev_close(playback);
                        fs.sample_rate = sample_rate;
                        esp_codec_dev_open(playback, &fs);
                    }
                }
                continue;
            }
            if (!seen_tags) {
                // 解析 OpusTags 包
                if (pkt_len >= 8 && memcmp(pkt_ptr, "OpusTags", 8) == 0) {
                    seen_tags = true;
                }
                continue;
            }

            // 音频数据包 - 解码 Opus 为 PCM
            int decoded_samples = opus_decode(decoder, pkt_ptr, pkt_len,
                                              pcm_buf, OPUS_MAX_FRAME_SIZE, 0);
            if (decoded_samples < 0) {
                ESP_LOGW(TAG, "Opus decode error: %d", decoded_samples);
                continue;
            }

            // 单声道转立体声 (codec 设置为 2 声道)
            for (int i = 0; i < decoded_samples; i++) {
                stereo_buf[i * 2]     = pcm_buf[i];
                stereo_buf[i * 2 + 1] = pcm_buf[i];
            }

            // 写入 codec 播放
            esp_codec_dev_write(playback, stereo_buf, decoded_samples * 2 * sizeof(int16_t));
        }

        offset = body_off + body_size;
    }

    // 清理
    free(pcm_buf);
    free(stereo_buf);
    opus_decoder_destroy(decoder);
    esp_codec_dev_close(playback);
    ESP_LOGI(TAG, "OGG playback finished");
}

void CodecPort::Codec_PlayModeAudio(uint8_t mode) {
    const uint8_t* ogg_data = Codec_GetOggData(mode);
    size_t ogg_size = Codec_GetOggSize(mode);
    if (ogg_data == NULL || ogg_size == 0) {
        ESP_LOGE(TAG, "No OGG audio data for mode %d", mode);
        return;
    }
    ESP_LOGI(TAG, "Playing mode %d OGG audio, size: %d bytes", mode, ogg_size);
    Codec_PlayOggAudio(ogg_data, ogg_size);
}

void CodecPort::Codec_PlayPromptSound(PromptSound sound) {
    const uint8_t* ogg_data = NULL;
    size_t ogg_size = 0;
    const char* name = "unknown";

    switch (sound) {
        case PROMPT_WIFI_CONNECTING:
            ogg_data = wifi_connecting_ogg_start;
            ogg_size = wifi_connecting_ogg_end - wifi_connecting_ogg_start;
            name = "wifi_connecting";
            break;
        case PROMPT_WIFI_SUCCESS:
            ogg_data = wifi_success_ogg_start;
            ogg_size = wifi_success_ogg_end - wifi_success_ogg_start;
            name = "wifi_success";
            break;
        case PROMPT_WIFI_FAIL:
            ogg_data = wifi_fail_ogg_start;
            ogg_size = wifi_fail_ogg_end - wifi_fail_ogg_start;
            name = "wifi_fail";
            break;
        case PROMPT_WIFI_RESET:
            ogg_data = wifi_reset_ogg_start;
            ogg_size = wifi_reset_ogg_end - wifi_reset_ogg_start;
            name = "wifi_reset";
            break;
        case PROMPT_WAIT_CONFIG:
            ogg_data = wait_config_ogg_start;
            ogg_size = wait_config_ogg_end - wait_config_ogg_start;
            name = "wait_config";
            break;
        case PROMPT_MANUAL_REFRESH:
            ogg_data = manual_refresh_ogg_start;
            ogg_size = manual_refresh_ogg_end - manual_refresh_ogg_start;
            name = "manual_refresh";
            break;
        default:
            ESP_LOGE(TAG, "Unknown prompt sound: %d", sound);
            return;
    }

    ESP_LOGI(TAG, "Playing prompt sound: %s, size: %d bytes", name, ogg_size);
    Codec_PlayOggAudio(ogg_data, ogg_size);
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