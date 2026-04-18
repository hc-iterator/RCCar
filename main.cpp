#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "RCCar.h"
#include <cstdio>

// ==================== 遥控输入引脚 ====================
#define PIN_S1 16   // 油门 PWM 输入
#define PIN_S2 17   // 转向 PWM 输入
#define PIN_S3 18   // 灯总开关（高电平有效）
#define PIN_S4 19   // 喇叭开关（高电平有效）

// ==================== 共享数据（双核通信） ====================
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

// ==================== GPIO 初始化 ====================
void init_gpio() {
    // 输入引脚
    gpio_init(PIN_S1); gpio_set_dir(PIN_S1, GPIO_IN);
    gpio_init(PIN_S2); gpio_set_dir(PIN_S2, GPIO_IN);
    gpio_init(PIN_S3); gpio_set_dir(PIN_S3, GPIO_IN);
    gpio_init(PIN_S4); gpio_set_dir(PIN_S4, GPIO_IN);

    // LED 输出
    gpio_init(led_lf); gpio_set_dir(led_lf, GPIO_OUT);
    gpio_init(led_rf); gpio_set_dir(led_rf, GPIO_OUT);
    gpio_init(led_lb); gpio_set_dir(led_lb, GPIO_OUT);
    gpio_init(led_rb); gpio_set_dir(led_rb, GPIO_OUT);
    gpio_init(Buzzer); gpio_set_dir(Buzzer, GPIO_OUT);
    gpio_put(Buzzer, 1);   // 默认高电平关闭蜂鸣器

    // 电机 PWM 初始化
    initMotorDriver();
}

// ==================== Core1: 读取遥控信号（含超时保护） ====================
void core1_entry() {
    static volatile uint32_t s1_start = 0, s2_start = 0;
    static volatile uint32_t s1_width = 1500, s2_width = 1500;
    static volatile uint32_t s1_last_edge_us = 0, s2_last_edge_us = 0;

    // 设置中断回调，测量高电平持续时间
    gpio_set_irq_enabled_with_callback(PIN_S1,
        GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true,
        [](uint gpio, uint32_t events) {
            uint32_t now = time_us_32();
            if (gpio == PIN_S1) {
                if (events & GPIO_IRQ_EDGE_RISE) {
                    s1_start = now;
                    s1_last_edge_us = now;
                } else if (events & GPIO_IRQ_EDGE_FALL) {
                    s1_width = now - s1_start;
                    s1_last_edge_us = now;
                }
            } else if (gpio == PIN_S2) {
                if (events & GPIO_IRQ_EDGE_RISE) {
                    s2_start = now;
                    s2_last_edge_us = now;
                } else if (events & GPIO_IRQ_EDGE_FALL) {
                    s2_width = now - s2_start;
                    s2_last_edge_us = now;
                }
            }
        }
    );
    // 为 S2 单独启用中断（回调已在上一步设置）
    gpio_set_irq_enabled(PIN_S2, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);

    const uint32_t TIMEOUT_US = 30000; // 30 ms 超时

    while (true) {
        // 读取开关量
        bool sw = gpio_get(PIN_S3);
        bool horn = gpio_get(PIN_S4);

        uint32_t w1 = s1_width;
        uint32_t w2 = s2_width;
        uint32_t now = time_us_32();

        // 超时检测：如果距离上一次边沿超过 TIMEOUT_US，认为信号丢失
        if ((now - s1_last_edge_us) > TIMEOUT_US) {
            w1 = 1500;   // 归中值，对应 throttle = 0
        }
        if ((now - s2_last_edge_us) > TIMEOUT_US) {
            w2 = 1500;
        }

        // 将 1000~2000 µs 映射到 -100 ~ 100
        int8_t throttle_val = (int32_t)(w1 - 1500) * 100 / 500;
        int8_t steering_val = (int32_t)(w2 - 1500) * 100 / 500;

        // 限幅
        if (throttle_val > 100) throttle_val = 100;
        if (throttle_val < -100) throttle_val = -100;
        if (steering_val > 100) steering_val = 100;
        if (steering_val < -100) steering_val = -100;

        // 更新共享数据
        shared.throttle = throttle_val;
        shared.steering = steering_val;
        shared.light_switch = sw;
        shared.horn = horn;

        sleep_ms(5);   // 200Hz 更新率
    }
}

// ==================== 灯光控制 ====================
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

    gpio_put(led_lf, lf_on);
    gpio_put(led_lb, lb_on);
    gpio_put(led_rf, rf_on);
    gpio_put(led_rb, rb_on);
}

// ==================== 喇叭控制 ====================
void control_horn(bool horn) {
    gpio_put(Buzzer, horn ? 0 : 1);
}

// ==================== 主函数（Core0） ====================
int main() {
    stdio_init_all();
    init_gpio();

    // 启动 Core1 处理遥控输入
    multicore_launch_core1(core1_entry);

    while (true) {
        int8_t throttle = shared.throttle;
        int8_t steering = shared.steering;
        bool light_switch = shared.light_switch;
        bool horn = shared.horn;

        // ----- 动力电机 -----
        if (throttle == 0) {
            brakePowerMotor();   // 零油门时刹车（可改为滑行，按你喜好）
        } else {
            uint8_t speed = (abs(throttle) * 255) / 100;
            bool ahead = (throttle > 0);
            setPowerMotor(speed, ahead);
        }

        // ----- 转向电机 -----
        if (steering == 0) {
            // 转向回中：刹车保持（你确认没问题，所以保持原逻辑）
            centerSteeringMotor();
        } else {
            uint8_t speed = (abs(steering) * 255) / 100;
            bool left = (steering < 0);
            setSteeringMotor(speed, left);
        }

        // ----- 灯光与喇叭 -----
        control_lights(light_switch, steering);
        control_horn(horn);

        sleep_ms(10);   // 100Hz 控制频率
    }

    return 0;
}