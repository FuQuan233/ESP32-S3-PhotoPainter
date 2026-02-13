/* 使用压缩音频示例
 * 
 * 此文件展示如何在用户代码中使用压缩的模式音频
 */

#include "application.h"
#include "codec_bsp.h"
#include <string_view>

// 方法1: 通过AudioService直接播放OGG（推荐）
void play_mode_audio_via_audioservice(uint8_t mode) {
    // 从codec_bsp获取压缩的OGG数据
    extern CodecPort* AudioPort;  // 假设全局AudioPort指针
    
    size_t ogg_size = 0;
    const uint8_t* ogg_data = AudioPort->Codec_GetCompressedModeAudio(mode, &ogg_size);
    
    if (ogg_data && ogg_size > 0) {
        // 创建string_view并播放
        std::string_view ogg_audio(reinterpret_cast<const char*>(ogg_data), ogg_size);
        
        // 使用Application播放音频
        auto& app = Application::GetInstance();
        app.PlaySound(ogg_audio);
        
        ESP_LOGI("AudioExample", "Playing mode %d audio (compressed), size: %d bytes", mode, ogg_size);
    } else {
        ESP_LOGE("AudioExample", "Failed to get mode %d audio data", mode);
    }
}

// 方法2: 兼容性函数 - 替换原来的PCM播放逻辑
void legacy_mode_audio_playback(uint8_t mode) {
    // 原来的代码可能是这样的:
    // uint8_t* pcm_data = AudioPort->Codec_GetMusicData(mode);
    // int pcm_size = AudioPort->Codec_GetMusicSizt(mode);
    // ... 然后进行PCM播放
    
    // 现在可以直接用压缩音频替换:
    play_mode_audio_via_audioservice(mode);
}

// 方法3: 在用户应用中的使用示例
void user_mode_selection_audio_feedback() {
    // 当用户选择模式时播放对应的音频提示
    
    // 模式0: 基础模式
    play_mode_audio_via_audioservice(0);
    vTaskDelay(pdMS_TO_TICKS(3000));  // 等待播放完成
    
    // 模式1: 网络模式 
    play_mode_audio_via_audioservice(1);
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    // 模式2: AI模式
    play_mode_audio_via_audioservice(2);
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    // 模式3: 照片日记模式
    play_mode_audio_via_audioservice(3);
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    // 模式4: 模式选择
    play_mode_audio_via_audioservice(4);
}

/* 集成到Mode_Selection.cpp的示例
 * 
 * 在Mode_Selection.cpp中，可以这样替换原来的音频播放:
 */
/*
void Mode_Selection_Audio_Play(uint8_t mode) {
    // 原来的PCM播放逻辑:
    // AudioPort->Codec_PlayInfoAudio();
    // uint8_t* music_data = AudioPort->Codec_GetMusicData(mode);
    // int music_size = AudioPort->Codec_GetMusicSizt(mode);
    // if (music_data) {
    //     AudioPort->Codec_PlayBackWrite(music_data, music_size);
    // }
    // AudioPort->Codec_ClosePlay();
    
    // 新的OGG播放逻辑（更简单）:
    play_mode_audio_via_audioservice(mode);
}
*/