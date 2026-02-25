#pragma once

#include "i2c_bsp.h"
#include "src/XPowersLib.h"
#include <cstdint>
#include <stdbool.h>

#define AXP2101_iqr_PIN             GPIO_NUM_21
#define AXP2101_CHGLED_PIN          GPIO_NUM_3

// 电池状态结构体
typedef struct {
    uint16_t voltage_mv;      // 电池电压 mV
    uint8_t  percent;         // 电量百分比 0-100
    bool     is_charging;     // 是否正在充电
    uint8_t  charge_status;   // 充电状态码
    const char* charge_status_str;  // 充电状态字符串
} battery_info_t;

void Custom_PmicPortInit(I2cMasterBus *i2cbus,uint8_t dev_addr);
void Custom_PmicRegisterInit(void);
void Axp2101_isChargingTask(void *arg);

// 获取电池信息
battery_info_t Get_BatteryInfo(void);

bool enablePowerOutput(uint8_t channel);
bool disablePowerOutput(uint8_t channel);