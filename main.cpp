#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/timer.h"
#include <cstdio>

// ==================== 引脚定义 ====================
// 输入：接收板 S1~S4
#define PIN_S1 2   // 前进后退 PWM 输入
#define PIN_S2 3   // 转向 PWM 输入
#define PIN_S3 4   // 灯总开关 高低电平输入
#define PIN_S4 5   // 喇叭开关 高低电平输入

// 输出：电机和舵机
#define PIN_MOTOR_PWM 6      // 动力马达 PWM (DRV8833 IN1/IN2 之一，另一接 GND 或互补)
#define PIN_MOTOR_DIR 7      // 动力马达方向 (高=正转，低=反转，可与 PWM 配合)
#define PIN_STEERING_PWM 8   // 转向舵机 PWM

// 输出：LED 灯 (共四个，独立控制)
#define PIN_LED_LF 9   // 左前灯
#define PIN_LED_LB 10  // 左后灯
#define PIN_LED_RF 11  // 右前灯
#define PIN_LED_RB 12  // 右后灯

// 输出：蜂鸣器 (低电平响)
#define PIN_BUZZER 13

// ==================== 共享数据结构体 ====================
// 所有成员都是原子类型（int8_t 和 bool），双核读写安全
struct SharedData {
    int8_t throttle;   // -100 ~ 100, 前进后退
    int8_t steering;   // -100 ~ 100, 转向角度
    bool light_switch; // S3 电平: true=开灯模式, false=关灯模式
    bool horn;         // S4 电平: true=响喇叭
};

static volatile SharedData shared;  // volatile 防止编译器过度优化

// ==================== 函数声明 ====================
void core1_entry();                     // core1 主函数
void measure_pwm_input();               // core1 中测量 PWM 并更新共享变量
void control_motor(int8_t throttle);    // core0 控制动力马达
void control_steering(int8_t steering); // core0 控制转向舵机
void control_lights(bool light_switch, int8_t steering); // 根据 S3 和转向值控制 LED
void control_horn(bool horn);           // 控制蜂鸣器

// ==================== 初始化 ====================
void init_gpio() {
    // 输入引脚
    gpio_init(PIN_S1); gpio_set_dir(PIN_S1, GPIO_IN);
    gpio_init(PIN_S2); gpio_set_dir(PIN_S2, GPIO_IN);
    gpio_init(PIN_S3); gpio_set_dir(PIN_S3, GPIO_IN);
    gpio_init(PIN_S4); gpio_set_dir(PIN_S4, GPIO_IN);

    // 输出引脚
    gpio_init(PIN_MOTOR_PWM); gpio_set_dir(PIN_MOTOR_PWM, GPIO_OUT);
    gpio_init(PIN_MOTOR_DIR); gpio_set_dir(PIN_MOTOR_DIR, GPIO_OUT);
    gpio_init(PIN_STEERING_PWM); gpio_set_dir(PIN_STEERING_PWM, GPIO_OUT);
    gpio_init(PIN_LED_LF); gpio_set_dir(PIN_LED_LF, GPIO_OUT);
    gpio_init(PIN_LED_LB); gpio_set_dir(PIN_LED_LB, GPIO_OUT);
    gpio_init(PIN_LED_RF); gpio_set_dir(PIN_LED_RF, GPIO_OUT);
    gpio_init(PIN_LED_RB); gpio_set_dir(PIN_LED_RB, GPIO_OUT);
    gpio_init(PIN_BUZZER); gpio_set_dir(PIN_BUZZER, GPIO_OUT);

    // 蜂鸣器默认高电平（不响）
    gpio_put(PIN_BUZZER, 1);

    // 初始化 PWM 硬件（转向舵机）
    gpio_set_function(PIN_STEERING_PWM, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(PIN_STEERING_PWM);
    pwm_set_wrap(slice, 20000);   // 20ms 周期
    pwm_set_clkdiv(slice, 1.0f);
    pwm_set_enabled(slice, true);
    pwm_set_gpio_level(PIN_STEERING_PWM, 1500); // 中位 1.5ms
}

// ==================== core1: 读取输入 ====================
void core1_entry() {
    // 为 S1 和 S2 配置 PWM 输入捕获（使用中断方式）
    // 定义每个引脚的中断回调，直接更新临时变量
    static volatile uint32_t s1_start = 0, s2_start = 0;
    static volatile uint32_t s1_width = 1500, s2_width = 1500;

    gpio_set_irq_enabled_with_callback(PIN_S1, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, [](uint gpio, uint32_t events) {
        if (gpio == PIN_S1) {
            if (events & GPIO_IRQ_EDGE_RISE) {
                s1_start = time_us_32();
            } else if (events & GPIO_IRQ_EDGE_FALL) {
                s1_width = time_us_32() - s1_start;
            }
        } else if (gpio == PIN_S2) {
            if (events & GPIO_IRQ_EDGE_RISE) {
                s2_start = time_us_32();
            } else if (events & GPIO_IRQ_EDGE_FALL) {
                s2_width = time_us_32() - s2_start;
            }
        }
    });

    gpio_set_irq_enabled(PIN_S1, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(PIN_S2, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);

    while (true) {
        // 读取 S3、S4 电平（直接读 GPIO）
        bool sw = gpio_get(PIN_S3);
        bool horn = gpio_get(PIN_S4);

        // 将测量的脉宽（微秒）转换为 int8_t 范围 -100~100
        // 假设脉宽范围 1000us ~ 2000us，中值 1500us
        uint32_t w1 = s1_width;
        uint32_t w2 = s2_width;
        int8_t throttle_val = (w1 - 1500) * 100 / 500;  // -100~100
        int8_t steering_val = (w2 - 1500) * 100 / 500;

        // 限幅
        if (throttle_val > 100) throttle_val = 100;
        if (throttle_val < -100) throttle_val = -100;
        if (steering_val > 100) steering_val = 100;
        if (steering_val < -100) steering_val = -100;

        // 更新共享变量（原子写入）
        shared.throttle = throttle_val;
        shared.steering = steering_val;
        shared.light_switch = sw;
        shared.horn = horn;

        // 休眠 5ms，避免过度占用 core1
        sleep_ms(5);
    }
}

// ==================== core0: 执行控制 ====================
void control_motor(int8_t throttle) {
    // 使用 DRV8833，一个 PWM 脚 + 一个方向脚
    // 假设正转：PWM 输出占空比，DIR=高；反转：PWM 输出占空比，DIR=低
    uint16_t duty = (throttle >= 0 ? throttle : -throttle) * 65535 / 100;
    gpio_put(PIN_MOTOR_DIR, throttle >= 0 ? 1 : 0);
    pwm_set_gpio_level(PIN_MOTOR_PWM, duty);
}

void control_steering(int8_t steering) {
    // 转向舵机：-100 对应 1ms (1000us)，+100 对应 2ms (2000us)
    uint16_t pulse_us = 1500 + (steering * 500 / 100);
    if (pulse_us < 1000) pulse_us = 1000;
    if (pulse_us > 2000) pulse_us = 2000;
    uint slice = pwm_gpio_to_slice_num(PIN_STEERING_PWM);
    pwm_set_gpio_level(PIN_STEERING_PWM, pulse_us * 65535 / 20000);
}

void control_lights(bool light_switch, int8_t steering) {
    // 转向灯逻辑：左转 (steering < -20) 亮左前后，右转 (steering > 20) 亮右前后
    bool left_turn = (steering < -20);
    bool right_turn = (steering > 20);
    bool straight = (!left_turn && !right_turn);

    // 前灯连体处理：若 light_switch 为 true 且直行，则左右前灯都亮；否则按转向灯逻辑
    bool lf_on, lb_on, rf_on, rb_on;
    if (light_switch && straight) {
        lf_on = true;  // 左前常亮
        rf_on = true;  // 右前常亮
    } else {
        lf_on = left_turn;
        rf_on = right_turn;
    }
    // 后灯始终跟随转向灯（即使 light_switch 关闭，转向灯也亮）
    lb_on = left_turn;
    rb_on = right_turn;

    gpio_put(PIN_LED_LF, lf_on);
    gpio_put(PIN_LED_LB, lb_on);
    gpio_put(PIN_LED_RF, rf_on);
    gpio_put(PIN_LED_RB, rb_on);
}

void control_horn(bool horn) {
    // 喇叭：低电平响，S4 高电平则响
    gpio_put(PIN_BUZZER, horn ? 0 : 1);
}

// ==================== main ====================
int main() {
    stdio_init_all();
    init_gpio();

    // 启动 core1，传入 core1_entry 函数
    multicore_launch_core1(core1_entry);

    // core0 主循环
    while (true) {
        // 读取共享变量（原子读取，安全）
        int8_t throttle = shared.throttle;
        int8_t steering = shared.steering;
        bool light_switch = shared.light_switch;
        bool horn = shared.horn;

        // 执行控制
        control_motor(throttle);
        control_steering(steering);
        control_lights(light_switch, steering);
        control_horn(horn);

        // 短暂休眠，释放 core0
        sleep_ms(10);
    }
}