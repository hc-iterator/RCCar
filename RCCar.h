#pragma once
#include "pico/stdlib.h"
#include "vector"
#include "cstdio"
#include "string"
#include "hardware\adc.h"
#include "hardware\pwm.h"
#include "cstdint"
#include <tusb.h> // TinyUSB tud_cdc_connected()

// 引脚定义（应与实际硬件接线一致）
// 注意：此处仅为声明，具体引脚编号在主程序前根据接线调整
extern const uint8_t Buzzer;
extern const uint8_t PowerMotor_1; // 连接DRV8833 AIN1
extern const uint8_t PowerMotor_2; // 连接DRV8833 AIN2
extern const uint8_t SteeringMotor_1; // 连接DRV8833 BIN1
extern const uint8_t SteeringMotor_2; // 连接DRV8833 BIN2

// PWM参数声明
extern const uint32_t PWM_FREQ;
extern const uint16_t PWM_WRAP;

/**
 * @brief 初始化电机驱动所需的PWM硬件
 * @note 必须在main()函数开始时调用一次，以配置Pico的PWM功能
 */
void initMotorDriver();

/**
 * @brief 控制动力电机的速度和方向
 * @param speed 速度值，范围0-255。0为停止，255为全速
 * @param ahead 方向控制。true为前进，false为后退
 */
void setPowerMotor(uint16_t speed, bool ahead);

/**
 * @brief 控制转向电机的速度和方向
 * @param speed 速度值，范围0-255。0为停止，255为全速
 * @param left 方向控制。true为左转，false为右转
 */
void setSteeringMotor(uint16_t speed, bool left);

/**
 * @brief 动力电机紧急刹车
 * @note 将AIN1和AIN2均设为高电平，触发DRV8833的刹车模式，
 *       电机快速停止。比setPowerMotor(0, true)的滑行停止更迅速。
 */
void brakePowerMotor();

/**
 * @brief 转向电机回正/刹车
 * @note 将BIN1和BIN2均设为高电平，使转向电机快速停止。
 *       可用于转向后快速回正或保持当前转向角度。
 */
void centerSteeringMotor();
