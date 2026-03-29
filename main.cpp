#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include <tusb.h>

// ===== 引脚定义（按您已验证的接线）=====
#define PIN_SCK   2
#define PIN_MOSI  3
#define PIN_MISO  4
#define PIN_CSN   6
#define PIN_CE    5

// ===== 底层 SPI 函数（必须完整）=====
static inline void cs_select() {
    gpio_put(PIN_CSN, 0);
}
static inline void cs_deselect() {
    gpio_put(PIN_CSN, 1);
}
static uint8_t spi_xfer(uint8_t tx) {
    uint8_t rx;
    spi_write_read_blocking(spi0, &tx, &rx, 1);
    return rx;
}
// 写寄存器
static void nrf_write_reg(uint8_t reg, uint8_t val) {
    uint8_t cmd = 0x20 | (reg & 0x1F);
    cs_select();
    spi_xfer(cmd);
    spi_xfer(val);
    cs_deselect();
}
// 读寄存器
static uint8_t nrf_read_reg(uint8_t reg) {
    uint8_t cmd = reg & 0x1F;
    cs_select();
    spi_xfer(cmd);
    uint8_t val = spi_xfer(0xFF);
    cs_deselect();
    return val;
}
// 清空 TX FIFO
static void nrf_flush_tx() {
    cs_select();
    spi_xfer(0xE1);
    cs_deselect();
}
// 清空 RX FIFO
static void nrf_flush_rx() {
    cs_select();
    spi_xfer(0xE2);
    cs_deselect();
}
// 写入 TX 负载（1字节）
static void nrf_write_tx_payload(uint8_t data) {
    cs_select();
    spi_xfer(0xA0);
    spi_xfer(data);
    cs_deselect();
}

// ===== 延时微秒（精确）=====
static void delay_us(uint32_t us) {
    sleep_us(us);
}
// =====================================

int main() {
    stdio_init_all();
    // 等待 USB 串口连接（调试用）
    while (!tud_cdc_connected()) sleep_ms(10);
    printf("\n========== NRF24L01 终极芯片诊断 ==========\n");

    // ---- 1. 初始化 SPI（降速至1MHz，保证稳定）----
    spi_init(spi0, 1000 * 1000);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_init(PIN_CSN); gpio_set_dir(PIN_CSN, GPIO_OUT); cs_deselect();
    gpio_init(PIN_CE);  gpio_set_dir(PIN_CE,  GPIO_OUT); gpio_put(PIN_CE, 0);
    delay_us(100);

    // ---- 2. 检查基础寄存器（SPI 通信是否正常）----
    uint8_t config = nrf_read_reg(0x00);
    uint8_t status = nrf_read_reg(0x07);
    printf("[1] 基础寄存器读取:\n");
    printf("    CONFIG = 0x%02X (应非0xFF/0x00)\n", config);
    printf("    STATUS = 0x%02X (通常0x0E)\n", status);
    if (config == 0xFF || config == 0x00) {
        printf("    ❌ SPI通信失败！请检查接线/电源。\n");
        while(1);
    } else {
        printf("    ✅ SPI通信正常，模块数字部分存活。\n");
    }

    // ---- 3. 载波检测测试（CD/RPD）----
    //    真模块：进入接收模式后，CD位会随机跳变（环境噪声）
    printf("\n[2] 载波检测测试 (CD) ...\n");
    nrf_write_reg(0x00, 0x00);      // 掉电
    delay_us(100);
    nrf_write_reg(0x00, 0x0D);      // 上电+接收模式 (PWR_UP=1, PRIM_RX=1)
    delay_us(150);                 // 等待晶振稳定
    gpio_put(PIN_CE, 1);           // CE拉高，进入RX模式
    delay_us(200);
    uint8_t cd = 0;
    int cd_variations = 0;
    for (int i = 0; i < 20; i++) {
        uint8_t new_cd = nrf_read_reg(0x09) & 0x01;
        if (i > 0 && new_cd != cd) cd_variations++;
        cd = new_cd;
        printf("    CD = %d\n", cd);
        sleep_ms(50);
    }
    if (cd_variations > 0) {
        printf("    ✅ CD有跳变，射频接收前端基本正常。\n");
    } else {
        printf("    ❌ CD始终为0，射频接收前端可能已死。\n");
    }
    gpio_put(PIN_CE, 0);           // 退出RX模式

    // ---- 4. 发射测试（TX_DS中断）----
    //    真模块：写入负载+CE脉冲后，STATUS寄存器的TX_DS位应置1
    printf("\n[3] 发射中断测试 (TX_DS) ...\n");
    nrf_write_reg(0x00, 0x00);      // 掉电
    delay_us(100);
    // 最小化配置：关闭自动应答、关闭重发、5字节地址、1Mbps
    nrf_write_reg(0x01, 0x00);      // EN_AA = 0
    nrf_write_reg(0x02, 0x00);      // EN_RXADDR = 0
    nrf_write_reg(0x03, 0x03);      // 5字节地址
    nrf_write_reg(0x04, 0x00);      // 无重发
    nrf_write_reg(0x05, 120);       // 信道120
    nrf_write_reg(0x06, 0x06);      // 1Mbps, 0dBm
    // 设置发送地址（任意5字节）
    uint8_t tx_addr[5] = {0x37,0x37,0x37,0x37,0x37};
    cs_select();
    spi_xfer(0x30);                 // 写 TX_ADDR
    for (int i = 0; i < 5; i++) spi_xfer(tx_addr[i]);
    cs_deselect();

    nrf_flush_tx();
    nrf_flush_rx();

    nrf_write_reg(0x00, 0x0C);      // 上电+发射模式 (PWR_UP=1, PRIM_RX=0)
    delay_us(5000);                // 等待晶振稳定（≥5ms）

    // 写入1字节负载
    nrf_write_tx_payload(0x55);
    printf("    负载写入，FIFO_STATUS = 0x%02X\n", nrf_read_reg(0x17));

    // CE脉冲 ≥10us 启动发射
    gpio_put(PIN_CE, 1);
    delay_us(15);
    gpio_put(PIN_CE, 0);
    delay_us(10);

    // 轮询 STATUS，等待 TX_DS 或 MAX_RT（最多5ms）
    uint8_t tx_status = 0;
    for (int i = 0; i < 5000; i++) {
        tx_status = nrf_read_reg(0x07);
        if (tx_status & 0x20) {     // TX_DS
            printf("    ✅ 产生 TX_DS 中断！射频发射链路正常。\n");
            break;
        }
        if (tx_status & 0x10) {     // MAX_RT（本应无，因为自动重发关闭）
            printf("    ⚠️ 产生 MAX_RT 中断（异常）\n");
            break;
        }
        delay_us(1);
    }
    if (!(tx_status & 0x20) && !(tx_status & 0x10)) {
        printf("    ❌ 无任何中断，射频发射链路死亡。\n");
    }

    // ---- 5. 最终裁决 ----
    printf("\n========== 诊断结论 ==========\n");
    if ((cd_variations > 0) && (tx_status & 0x20)) {
        printf("🎉 模块通过全部测试！射频接收/发射均正常，是真品。\n");
    } else if (cd_variations == 0 && (tx_status & 0x20)) {
        printf("⚠️ 能发射但收不到载波，可能接收部分损坏，可作单向发射用。\n");
    } else if (cd_variations > 0 && !(tx_status & 0x20)) {
        printf("⚠️ 能接收载波但无法发射，可作接收用。\n");
    } else {
        printf("💀 射频完全死亡，仅SPI可用。此模块为“数字傀儡”，请报废。\n");
    }

    printf("\n测试完成，建议测量发射时电流：正常应跳至>10mA。\n");
    while (1) sleep_ms(1000);
    return 0;
}