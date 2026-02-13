# Xiaozhi ESP32 AI Assistant - Copilot Instructions

## Project Overview

This is an ESP32-S3 based AI voice assistant (xiaozhi-esp32) with custom PhotoPainter extensions. The project combines:
- Voice interaction with wake word detection
- Multi-mode operation (AI xiaozhi mode, Basic mode, Network mode, Mode Selection)
- E-paper display support
- Audio codec (ES8311/ES7210) integration
- OTA firmware updates with dual partition scheme

Based on: https://github.com/78/xiaozhi-esp32

## Architecture

### Multi-Mode System
The application supports 4 operating modes (selected via NVS `PhotPainterMode`):
- `0x01`: Basic mode - Local operations without network
- `0x02`: Network mode - Network-enabled operations
- `0x03`: Xiaozhi mode - Full AI assistant with voice interaction
- `0x04`: Mode Selection - UI for choosing modes

Mode initialization happens in [main.cc](main/main.cc#L58-L76) based on NVS settings.

### Core Components

**Singleton Application** ([application.h](main/application.h), [application.cc](main/application.cc))
- Central state machine managing device lifecycle
- Event-driven architecture using FreeRTOS event groups
- Key states: `kDeviceStateIdle`, `kDeviceStateListening`, `kDeviceStateSpeaking`, `kDeviceStateUpgrading`
- Schedule tasks via `Application::Schedule()` to ensure thread safety

**Audio Pipeline** ([audio_service.h](main/audio/audio_service.h))
```
(MIC) -> [Processors] -> {Encode Queue} -> [Opus Encoder] -> {Send Queue} -> (Server)
(Server) -> {Decode Queue} -> [Opus Decoder] -> {Playback Queue} -> (Speaker)
```
- Device-side AEC support via `CONFIG_USE_DEVICE_AEC`
- Wake word detection integrated into audio processing
- Opus codec with 60ms frame duration

**Protocol Layer** ([protocol.h](main/protocols/protocol.h))
- Abstract base class with WebSocket and MQTT implementations
- Binary protocol for audio streaming (BinaryProtocol2/3 structs)
- Callbacks: `OnIncomingAudio`, `OnIncomingJson`, `OnWakeWordDetected`

**Board Abstraction** (70+ supported boards in [main/boards/](main/boards/))
- Each board has `config.h`, `config.json`, and `xxx_board.cc`
- Board-specific pin mappings and hardware initialization
- **CRITICAL**: Never overwrite existing board configs - create new board types or use different builds in config.json

**Custom User BSP** ([components/user_app_bsp/](components/user_app_bsp/))
- PhotoPainter-specific implementations
- E-paper display integration ([display_bsp.h](components/port_bsp/display_bsp.h))
- Button handling via event groups
- Mode switching logic

**MCP Server** ([mcp_server.h](main/mcp_server.h))
- Model Context Protocol for AI tool integration
- User-only tools vs system tools distinction
- Image content encoding (Base64) for visual responses

## Build System (ESP-IDF)

### Standard Build Flow
```bash
idf.py set-target esp32s3          # Set chip target
idf.py menuconfig                   # Configure via menu
  # Select: Xiaozhi Assistant -> Board Type -> [your board]
idf.py build                        # Build project
idf.py flash                        # Flash to device
idf.py flash monitor               # Flash and open serial monitor
```

### Release Build (Recommended for custom boards)
```bash
python scripts/release.py [board-folder-name]
```
This reads `config.json` in board folder and handles sdkconfig automatically.

### Key Configuration
- Partition table: `partitions/v2/16m.csv` (for 16MB flash)
  - ota_0: 4MB, ota_1: 4MB, assets: 8MB SPIFFS
- Compiler: Size optimization `-Os` enabled
- PSRAM: Octal/Quad mode depending on board
- **sdkconfig.defaults**: Base configuration, board-specific overrides in board folders

## Critical Conventions

### NVS Storage Patterns
- Namespace: `"PhotoPainter"` for mode settings
- Keys: `NetworkMode`, `PhotPainterMode`, `Mode_Flag`
- Always call `nvs_commit()` after writes
- Error handling: Check `ESP_ERR_NVS_NO_FREE_PAGES` and erase if needed

### Event Group Patterns
Custom event groups for synchronization:
```cpp
EventGroupHandle_t GP4ButtonGroups;  // Button events
EventGroupHandle_t epaper_groups;    // Display events
EventGroupHandle_t ai_IMG_Group;     // AI image events
```
Use `xEventGroupSetBits()` / `xEventGroupWaitBits()` for inter-task communication.

### Memory Management
- SPIRAM enabled - use for large buffers
- Audio buffers in SPIRAM via `SPIRAM_MALLOC_ALWAYSINTERNAL=512`
- Heap caps API for specific allocations: `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)`

### Audio Codec Access
```cpp
AudioPort->Codec_PlayBackWrite(data, size);  // Playback
AudioPort->Codec_GetCodecReg("es8311", reg); // Read codec register
AudioPort->Codec_SetCodecReg("es8311", reg, val); // Write codec register
```

## OTA Firmware Updates

### Dual Partition Scheme
- ota_0 and ota_1 partitions (4MB each)
- Rollback support via `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`
- Validate new firmware with `ota.MarkCurrentVersionValid()`

### Update Flow
1. `Ota::CheckVersion()` - GET/POST to OTA server with system info JSON
2. Server response includes firmware version, URL, force flag
3. `Application::UpgradeFirmware()` - Download and flash
4. Auto-reboot after successful flash
5. First boot validates firmware or rolls back

### Version Format
Semantic versioning parsed by `Ota::ParseVersion()`: "2.0.1" -> [2, 0, 1]

## Debugging

### ESP-IDF Extension Commands
**Use these instead of terminal commands:**
- Build: Use ESP-IDF extension "Build" command (never `idf.py build` in terminal)
- Flash: Use ESP-IDF extension "Flash" command
- Monitor: Use ESP-IDF extension "Monitor" command

### Logging
- Tag-based logging: `#define TAG "ModuleName"`
- Levels: `ESP_LOGI`, `ESP_LOGW`, `ESP_LOGE`
- View via `idf.py monitor` or ESP-IDF extension monitor

### Common Issues
- **Flash/Monitor fails**: Check USB port selection in ESP-IDF extension settings
- **OTA partition errors**: Verify partition table matches flash size
- **Audio distortion**: Check I2S pin configuration and codec initialization
- **Mode not switching**: Verify NVS values with `nvs_get_u8()` logs

## File Organization

- [main/](main/) - Core application code
  - [boards/](main/boards/) - Board-specific implementations
  - [audio/](main/audio/) - Audio service and processors
  - [protocols/](main/protocols/) - Network protocol implementations
  - [display/](main/display/) - Display drivers and UI
- [components/](components/) - Reusable components
  - [user_app_bsp/](components/user_app_bsp/) - Custom PhotoPainter BSP
  - [port_bsp/](components/port_bsp/) - Hardware abstraction (I2C, codec, display)
  - [app_bsp/](components/app_bsp/) - Application-level services
- [managed_components/](managed_components/) - ESP component registry dependencies
- [partitions/](partitions/) - Partition table CSV files
- [scripts/](scripts/) - Build and release automation

## Testing

Run mode selection UI to verify button/display integration:
```cpp
Mode_Selection_Init();  // From user_app.h
```

Audio loopback testing via `AudioService::EnableAudioTesting(true)`.

## AI Agent Tips

- **State Changes**: Always use `Application::Schedule()` for cross-thread operations
- **Board Addition**: Copy existing board folder, modify config.h pins, update config.json
- **Audio Issues**: Check `AudioService` event group bits to diagnose pipeline state
- **OTA Testing**: Use `Application::UpgradeFirmware()` with test URL parameter
- **E-paper**: Access via `ePaperDisplay` global, uses semaphore `epaper_gui_semapHandle`
