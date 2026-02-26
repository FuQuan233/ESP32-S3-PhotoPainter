#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <driver/gpio.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include "power_bsp.h"
#include "XPowersLib.h"

const char *TAG = "axp2101";

static XPowersPMU axp2101;

static I2cMasterBus           *i2cbus_   = NULL;
static i2c_master_dev_handle_t i2cPMICdev = NULL;
static uint8_t                 i2cPMICAddress;

static int AXP2101_SLAVE_Read(uint8_t devAddr, uint8_t regAddr, uint8_t *data, uint8_t len) {
    int ret;
    uint8_t count = 3;
    do
    {
        ret = (i2cbus_->i2c_read_buff(i2cPMICdev, regAddr, data, len) == ESP_OK) ? 0 : -1;
        if (ret == 0)
            break;
        vTaskDelay(pdMS_TO_TICKS(100));
        count--;
    } while (count);
    return ret;
}

static int AXP2101_SLAVE_Write(uint8_t devAddr, uint8_t regAddr, uint8_t *data, uint8_t len) {
    int ret;
    uint8_t count = 3;
    do
    {
        ret = (i2cbus_->i2c_write_buff(i2cPMICdev, regAddr, data, len) == ESP_OK) ? 0 : -1;
        if (ret == 0)
            break;
        vTaskDelay(pdMS_TO_TICKS(100));
        count--;
    } while (count);
    return ret;
}

void Custom_PmicPortGpioInit() {
    gpio_config_t io_conf = {};
    io_conf.intr_type    = GPIO_INTR_DISABLE;
    io_conf.mode         = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << AXP2101_iqr_PIN) | (1ULL << AXP2101_CHGLED_PIN);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en   = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);
}

void Custom_PmicPortInit(I2cMasterBus *i2cbus,uint8_t dev_addr) {
    if(i2cbus_ == NULL) {
        i2cbus_ = i2cbus;
    }
    if(i2cPMICdev == NULL) {
        i2c_master_bus_handle_t BusHandle = i2cbus_->Get_I2cBusHandle();
        i2c_device_config_t     dev_cfg   = {};
        dev_cfg.dev_addr_length           = I2C_ADDR_BIT_LEN_7;
        dev_cfg.scl_speed_hz              = 100000;
        dev_cfg.device_address            = dev_addr;
        ESP_ERROR_CHECK(i2c_master_bus_add_device(BusHandle, &dev_cfg, &i2cPMICdev));
        i2cPMICAddress = dev_addr;
    }
    if (axp2101.begin(i2cPMICAddress, AXP2101_SLAVE_Read, AXP2101_SLAVE_Write)) {
        ESP_LOGI(TAG, "Init PMU SUCCESS!");
    } else {
        ESP_LOGE(TAG, "Init PMU FAILED!");
    }
    Custom_PmicPortGpioInit();
    Custom_PmicRegisterInit();
}

void Custom_PmicRegisterInit(void) {
    axp2101.setVbusCurrentLimit(XPOWERS_AXP2101_VBUS_CUR_LIM_2000MA);

    if(axp2101.getDC1Voltage() != 3300) {
        axp2101.setDC1Voltage(3300);
        ESP_LOGW("axp2101_init_log","Set DCDC1 to output 3V3");
    }
    if(axp2101.getALDO1Voltage() != 3300) {
        axp2101.setALDO1Voltage(3300);
        ESP_LOGW("axp2101_init_log","Set ALDO1 to output 3V3");
    }
    if(axp2101.getALDO2Voltage() != 3300) {
        axp2101.setALDO2Voltage(3300);
        ESP_LOGW("axp2101_init_log","Set ALDO2 to output 3V3");
    }
    if(axp2101.getALDO3Voltage() != 3300) {
        axp2101.setALDO3Voltage(3300);
        ESP_LOGW("axp2101_init_log","Set ALDO3 to output 3V3");
    }
    if(axp2101.getALDO4Voltage() != 3300) {
        axp2101.setALDO4Voltage(3300);
        ESP_LOGW("axp2101_init_log","Set ALDO4 to output 3V3");
    }

    axp2101.setPrechargeCurr(XPOWERS_AXP2101_PRECHARGE_200MA);       // 5000mAh battery: 200mA precharge
    axp2101.setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_1000MA);  // 5000mAh battery: 1000mA (0.2C)
    axp2101.setChargerTerminationCurr(XPOWERS_AXP2101_CHG_ITERM_200MA); // 5000mAh battery: 200mA termination
}

// 获取充电状态字符串
static const char* get_charge_status_string(uint8_t status) {
    switch (status) {
        case XPOWERS_AXP2101_CHG_TRI_STATE:  return "tri_charge";
        case XPOWERS_AXP2101_CHG_PRE_STATE:  return "pre_charge";
        case XPOWERS_AXP2101_CHG_CC_STATE:   return "constant_charge";
        case XPOWERS_AXP2101_CHG_CV_STATE:   return "constant_voltage";
        case XPOWERS_AXP2101_CHG_DONE_STATE: return "charge_done";
        case XPOWERS_AXP2101_CHG_STOP_STATE: return "not_charging";
        default: return "unknown";
    }
}

// 获取电池信息
battery_info_t Get_BatteryInfo(void) {
    battery_info_t info = {};
    info.voltage_mv = axp2101.getBattVoltage();
    info.percent = axp2101.getBatteryPercent();
    info.is_charging = axp2101.isCharging();
    info.charge_status = axp2101.getChargerStatus();
    info.charge_status_str = get_charge_status_string(info.charge_status);
    return info;
}

void Axp2101_isChargingTask(void *arg) {
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(20000));
        ESP_LOGI(TAG, "isCharging: %s", axp2101.isCharging() ? "YES" : "NO");
        uint8_t charge_status = axp2101.getChargerStatus();
        if (charge_status == XPOWERS_AXP2101_CHG_TRI_STATE) {
            ESP_LOGI(TAG, "Charger Status: tri_charge");
        } else if (charge_status == XPOWERS_AXP2101_CHG_PRE_STATE) {
            ESP_LOGI(TAG, "Charger Status: pre_charge");
        } else if (charge_status == XPOWERS_AXP2101_CHG_CC_STATE) {
            ESP_LOGI(TAG, "Charger Status: constant charge");
        } else if (charge_status == XPOWERS_AXP2101_CHG_CV_STATE) {
            ESP_LOGI(TAG, "Charger Status: constant voltage");
        } else if (charge_status == XPOWERS_AXP2101_CHG_DONE_STATE) {
            ESP_LOGI(TAG, "Charger Status: charge done");
        } else if (charge_status == XPOWERS_AXP2101_CHG_STOP_STATE) {
            ESP_LOGI(TAG, "Charger Status: not charge");
        }
        ESP_LOGI(TAG, "getBattVoltage: %dmV", axp2101.getBattVoltage());
        ESP_LOGI(TAG, "getBatteryPercent: %d%%", axp2101.getBatteryPercent());
    }
}

bool enablePowerOutput(uint8_t channel)
{
    switch (channel) {
    case XPOWERS_DCDC1:
        return axp2101.enableDC1();
    case XPOWERS_DCDC2:
        return axp2101.enableDC2();
    case XPOWERS_DCDC3:
        return axp2101.enableDC3();
    case XPOWERS_DCDC4:
        return axp2101.enableDC4();
    case XPOWERS_DCDC5:
        return axp2101.enableDC5();
    case XPOWERS_ALDO1:
        return axp2101.enableALDO1();
    case XPOWERS_ALDO2:
        return axp2101.enableALDO2();
    case XPOWERS_ALDO3:
        return axp2101.enableALDO3();
    case XPOWERS_ALDO4:
        return axp2101.enableALDO4();
    case XPOWERS_BLDO1:
        return axp2101.enableBLDO1();
    case XPOWERS_BLDO2:
        return axp2101.enableBLDO2();
    case XPOWERS_DLDO1:
        return axp2101.enableDLDO1();
    case XPOWERS_DLDO2:
        return axp2101.enableDLDO2();
    case XPOWERS_VBACKUP:
        return axp2101.enableButtonBatteryCharge();
    default:
        break;
    }
    return false;
}

bool disablePowerOutput(uint8_t channel)
{
    if (axp2101.getProtectedChannel(channel)) {
        log_e("Failed to disable the power channel, the power channel has been protected");
        return false;
    }
    switch (channel) {
    case XPOWERS_DCDC1:
        return axp2101.disableDC1();
    case XPOWERS_DCDC2:
        return axp2101.disableDC2();
    case XPOWERS_DCDC3:
        return axp2101.disableDC3();
    case XPOWERS_DCDC4:
        return axp2101.disableDC4();
    case XPOWERS_DCDC5:
        return axp2101.disableDC5();
    case XPOWERS_ALDO1:
        return axp2101.disableALDO1();
    case XPOWERS_ALDO2:
        return axp2101.disableALDO2();
    case XPOWERS_ALDO3:
        return axp2101.disableALDO3();
    case XPOWERS_ALDO4:
        return axp2101.disableALDO4();
    case XPOWERS_BLDO1:
        return axp2101.disableBLDO1();
    case XPOWERS_BLDO2:
        return axp2101.disableBLDO2();
    case XPOWERS_DLDO1:
        return axp2101.disableDLDO1();
    case XPOWERS_DLDO2:
        return axp2101.disableDLDO2();
    case XPOWERS_VBACKUP:
        return axp2101.disableButtonBatteryCharge();
    case XPOWERS_CPULDO:
        return axp2101.disableCPUSLDO();
    default:
        break;
    }
    return false;
}
