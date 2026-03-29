#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "RCCar.h"          // 电机控制模块头文件
#include <cstdio>

// ==================== 重新定义输入引脚（避开电机引脚 1~4） ====================
#define PIN_S1 16   // 前进后退 PWM 输入
#define PIN_S2 17   // 转向 PWM 输入
#define PIN_S3 18   // 灯总开关 高低电平输入
#define PIN_S4 19   // 喇叭开关 高低电平输入

// 输出：LED 灯和蜂鸣器（与电机引脚无冲突）
#define PIN_LED_LF 9
#define PIN_LED_LB 10
#define PIN_LED_RF 11
#define PIN_LED_RB 12
#define PIN_BUZZER 13

// ==================== 共享数据结构体（双核通信） ====================
struct SharedData {
    int8_t throttle;   // -100 ~ 100
    int8_t steering;   // -100 ~ 100
    bool light_switch;
    bool horn;
};
static volatile SharedData shared;

// ==================== 函数声明 ====================
void core1_entry();
void control_lights(bool light_switch, int8_t steering);
void control_horn(bool horn);

// ==================== 初始化 GPIO ====================
void init_gpio() {
    // 输入引脚
    gpio_init(PIN_S1); gpio_set_dir(PIN_S1, GPIO_IN);
    gpio_init(PIN_S2); gpio_set_dir(PIN_S2, GPIO_IN);
    gpio_init(PIN_S3); gpio_set_dir(PIN_S3, GPIO_IN);
    gpio_init(PIN_S4); gpio_set_dir(PIN_S4, GPIO_IN);

    // LED 输出
    gpio_init(PIN_LED_LF); gpio_set_dir(PIN_LED_LF, GPIO_OUT);
    gpio_init(PIN_LED_LB); gpio_set_dir(PIN_LED_LB, GPIO_OUT);
    gpio_init(PIN_LED_RF); gpio_set_dir(PIN_LED_RF, GPIO_OUT);
    gpio_init(PIN_LED_RB); gpio_set_dir(PIN_LED_RB, GPIO_OUT);
    gpio_init(PIN_BUZZER); gpio_set_dir(PIN_BUZZER, GPIO_OUT);
    gpio_put(PIN_BUZZER, 1);   // 蜂鸣器默认关闭

    // 电机驱动 PWM 初始化（来自 RCCar 模块）
    initMotorDriver();
}

// ==================== core1: 读取输入（PWM + 电平） ====================
void core1_entry() {
    static volatile uint32_t s1_start = 0, s2_start = 0;
    static volatile uint32_t s1_width = 1500, s2_width = 1500;

    // 设置 GPIO 中断回调（测量 S1, S2 高电平脉宽）
    gpio_set_irq_enabled_with_callback(PIN_S1, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true,
        [](uint gpio, uint32_t events) {
            if (gpio == PIN_S1) {
                if (events & GPIO_IRQ_EDGE_RISE) s1_start = time_us_32();
                else if (events & GPIO_IRQ_EDGE_FALL) s1_width = time_us_32() - s1_start;
            } else if (gpio == PIN_S2) {
                if (events & GPIO_IRQ_EDGE_RISE) s2_start = time_us_32();
                else if (events & GPIO_IRQ_EDGE_FALL) s2_width = time_us_32() - s2_start;
            }
        }
    );
    gpio_set_irq_enabled(PIN_S1, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(PIN_S2, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);

    while (true) {
        bool sw = gpio_get(PIN_S3);
        bool horn = gpio_get(PIN_S4);

        uint32_t w1 = s1_width;
        uint32_t w2 = s2_width;
        // 脉宽 1000~2000us -> -100~100
        int8_t throttle_val = (w1 - 1500) * 100 / 500;
        int8_t steering_val = (w2 - 1500) * 100 / 500;

        // 限幅
        if (throttle_val > 100) throttle_val = 100;
        if (throttle_val < -100) throttle_val = -100;
        if (steering_val > 100) steering_val = 100;
        if (steering_val < -100) steering_val = -100;

        shared.throttle = throttle_val;
        shared.steering = steering_val;
        shared.light_switch = sw;
        shared.horn = horn;

        sleep_ms(5);   // 200Hz 更新率
    }
}

// ==================== core0: 灯光控制 ====================
void control_lights(bool light_switch, int8_t steering) {
    bool left_turn = (steering < -20);
    bool right_turn = (steering > 20);
    bool straight = (!left_turn && !right_turn);

    bool lf_on, lb_on, rf_on, rb_on;
    if (light_switch && straight) {
        lf_on = true;
        rf_on = true;
    } else {
        lf_on = left_turn;
        rf_on = right_turn;
    }
    lb_on = left_turn;
    rb_on = right_turn;

    gpio_put(PIN_LED_LF, lf_on);
    gpio_put(PIN_LED_LB, lb_on);
    gpio_put(PIN_LED_RF, rf_on);
    gpio_put(PIN_LED_RB, rb_on);
}

void control_horn(bool horn) {
    gpio_put(PIN_BUZZER, horn ? 0 : 1);
}

// ==================== main ====================
int main() {
    stdio_init_all();
    init_gpio();

    // 启动 core1 负责输入采集
    multicore_launch_core1(core1_entry);

    // 映射表：throttle/steering (-100~100) -> 速度 (0 ~ PWM_WRAP)
    const uint16_t SPEED_MAX = PWM_WRAP;   // PWM_WRAP 定义在 RCCar.h 中，值为 6249

    while (true) {
        int8_t throttle = shared.throttle;
        int8_t steering = shared.steering;
        bool light_switch = shared.light_switch;
        bool horn = shared.horn;

        // ----- 动力电机控制 -----
        if (throttle == 0) {
            brakePowerMotor();          // 快速刹车停止
        } else {
            uint16_t speed = (abs(throttle) * SPEED_MAX) / 100;
            bool ahead = (throttle > 0);
            setPowerMotor(speed, ahead);
        }

        // ----- 转向电机控制 -----
        if (steering == 0) {
            centerSteeringMotor();      // 回正/刹车保持
        } else {
            uint16_t speed = (abs(steering) * SPEED_MAX) / 100;
            bool left = (steering < 0);
            setSteeringMotor(speed, left);
        }

        // ----- 灯光与蜂鸣器 -----
        control_lights(light_switch, steering);
        control_horn(horn);

        sleep_ms(10);   // 100Hz 控制周期
    }
}