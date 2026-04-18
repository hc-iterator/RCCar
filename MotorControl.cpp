#include "RCCar.h"

// ==================== PWM 初始化 ====================
void initMotorDriver() {
    // 设置引脚为 PWM 功能
    gpio_set_function(PowerMotor_1, GPIO_FUNC_PWM);
    gpio_set_function(PowerMotor_2, GPIO_FUNC_PWM);
    gpio_set_function(SteeringMotor_1, GPIO_FUNC_PWM);
    gpio_set_function(SteeringMotor_2, GPIO_FUNC_PWM);

    // 获取每个引脚对应的 PWM 切片号
    uint slice_p1 = pwm_gpio_to_slice_num(PowerMotor_1);
    uint slice_p2 = pwm_gpio_to_slice_num(PowerMotor_2);
    uint slice_s1 = pwm_gpio_to_slice_num(SteeringMotor_1);
    uint slice_s2 = pwm_gpio_to_slice_num(SteeringMotor_2);

    // 获取默认配置，设置 wrap 值（决定频率）
    pwm_config config = pwm_get_default_config();
    pwm_config_set_wrap(&config, PWM_WRAP);
    // 分频器保持默认 1，这样 125MHz / (1 * 6250) = 20kHz

    // 初始化各个切片（注意：相同切片会被多次初始化，但无妨）
    pwm_init(slice_p1, &config, true);
    pwm_init(slice_p2, &config, true);
    pwm_init(slice_s1, &config, true);
    pwm_init(slice_s2, &config, true);
}

// ==================== 速度映射函数（用户自行实现） ====================
uint16_t mapSpeed(uint8_t input) {
    // TODO: 在这里写你的映射逻辑
    // 示例：简单线性映射
    // return (uint16_t)input * PWM_WRAP / 255;

    // 你自己想实现的：
    // 1. 死区：小于某个阈值返回0
    // 2. 低速段分配更多分辨率，高速段平缓
    // 当前临时给一个线性映射，让你先跑起来
    return (uint16_t)input * PWM_WRAP / 255;
}

// ==================== 动力电机控制 ====================
void setPowerMotor(uint8_t speed, bool ahead) {
    if (speed == 0) {
        // 停止（滑行）
        pwm_set_gpio_level(PowerMotor_1, 0);
        pwm_set_gpio_level(PowerMotor_2, 0);
        return;
    }

    uint16_t pwm_val = mapSpeed(speed);
    if (ahead) {
        pwm_set_gpio_level(PowerMotor_1, pwm_val);
        pwm_set_gpio_level(PowerMotor_2, 0);
    } else {
        pwm_set_gpio_level(PowerMotor_1, 0);
        pwm_set_gpio_level(PowerMotor_2, pwm_val);
    }
}

// ==================== 转向电机控制 ====================
void setSteeringMotor(uint8_t speed, bool left) {
    if (speed == 0) {
        // 停止转向（断电滑行，让回中弹簧起作用）
        pwm_set_gpio_level(SteeringMotor_1, 0);
        pwm_set_gpio_level(SteeringMotor_2, 0);
        return;
    }

    uint16_t pwm_val = mapSpeed(speed);
    if (left) {
        pwm_set_gpio_level(SteeringMotor_1, pwm_val);
        pwm_set_gpio_level(SteeringMotor_2, 0);
    } else {
        pwm_set_gpio_level(SteeringMotor_1, 0);
        pwm_set_gpio_level(SteeringMotor_2, pwm_val);
    }
}

// ==================== 刹车 ====================
void brakePowerMotor() {
    pwm_set_gpio_level(PowerMotor_1, PWM_WRAP);
    pwm_set_gpio_level(PowerMotor_2, PWM_WRAP);
}

void centerSteeringMotor() {
    pwm_set_gpio_level(SteeringMotor_1, PWM_WRAP);
    pwm_set_gpio_level(SteeringMotor_2, PWM_WRAP);
}