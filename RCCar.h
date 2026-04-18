#pragma once

#include "pico/stdlib.h"
#include "vector"
#include "cstdio"
#include "string"
#include "hardware/adc.h"
#include "hardware/pwm.h"
#include "cstdint"
#include <tusb.h> // TinyUSB tud_cdc_connected()

// ==================== 引脚定义 ====================
// 使用 inline constexpr 直接定义，无需 extern
inline constexpr uint8_t Buzzer = 0;

inline constexpr uint8_t PowerMotor_1 = 1;      // 连接 DRV8833 AIN1
inline constexpr uint8_t PowerMotor_2 = 2;      // 连接 DRV8833 AIN2
inline constexpr uint8_t SteeringMotor_1 = 3;   // 连接 DRV8833 BIN1
inline constexpr uint8_t SteeringMotor_2 = 4;   // 连接 DRV8833 BIN2

// LED 输出
inline constexpr uint8_t led_lf = 9;
inline constexpr uint8_t led_rf = 10;
inline constexpr uint8_t led_lb = 11;
inline constexpr uint8_t led_rb = 12;

// ==================== PWM 参数 ====================
inline constexpr uint32_t PWM_FREQ = 20000;      // 20 kHz
inline constexpr uint16_t PWM_WRAP = 6249;       // 125e6 / 20000 - 1

// ==================== 函数声明 ====================

/**
 * @brief 初始化电机驱动 PWM 硬件
 */
void initMotorDriver();

/**
 * @brief 用户自定义的速度映射函数
 * @param input 输入速度值（0~255）
 * @return 映射后的 PWM 比较值（0 ~ PWM_WRAP）
 * 
 * @note 请在 MotorControl.cpp 中实现该函数，以实现所需的非线性曲线。
 *       例如：加入死区、低速敏感、高速平缓等。
 */
uint16_t mapSpeed(uint8_t input);

/**
 * @brief 控制动力电机的速度和方向
 * @param speed 速度值，范围 0~255。0 为停止，255 为全速
 * @param ahead 方向：true = 前进，false = 后退
 */
void setPowerMotor(uint8_t speed, bool ahead);

/**
 * @brief 控制转向电机的速度和方向
 * @param speed 速度值，范围 0~255。0 为停止，255 为全速
 * @param left 方向：true = 左转，false = 右转
 */
void setSteeringMotor(uint8_t speed, bool left);

/**
 * @brief 动力电机紧急刹车（将 AIN1、AIN2 均拉高）
 */
void brakePowerMotor();

/**
 * @brief 转向电机刹车/保持（将 BIN1、BIN2 均拉高）
 */
void centerSteeringMotor();