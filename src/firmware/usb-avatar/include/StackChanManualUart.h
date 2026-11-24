#pragma once

#include <driver/uart.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "LabManualUart.h"

namespace lab {

struct StackChanManualUartApi {
    using Task = TaskHandle_t;
    Task currentTask() const { return xTaskGetCurrentTaskHandle(); }
    uint64_t nowUs() const { return static_cast<uint64_t>(esp_timer_get_time()); }
    bool installed() const { return uart_is_driver_installed(UART_NUM_1); }
    bool installNoTxRing() const {
        return uart_driver_install(UART_NUM_1, 1024, 0, 0, nullptr, 0) == ESP_OK;
    }
    bool configure() const {
        uart_config_t config{};
        config.baud_rate = 1000000;
        config.data_bits = UART_DATA_8_BITS;
        config.parity = UART_PARITY_DISABLE;
        config.stop_bits = UART_STOP_BITS_1;
        config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
        config.rx_flow_ctrl_thresh = 0;
        config.source_clk = UART_SCLK_DEFAULT;
        return uart_param_config(UART_NUM_1, &config) == ESP_OK;
    }
    bool setPins() const {
        return uart_set_pin(UART_NUM_1, 6, 7, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) == ESP_OK;
    }
    ManualIoState waitTx() const {
        const auto result = uart_wait_tx_done(UART_NUM_1, 0);
        return result == ESP_OK ? ManualIoState::Ready :
            result == ESP_ERR_TIMEOUT ? ManualIoState::Pending : ManualIoState::Failed;
    }
    int rxCount(size_t& count) const {
        return uart_get_buffered_data_len(UART_NUM_1, &count);
    }
    int send(const uint8_t* bytes, size_t count) const {
        return uart_tx_chars(UART_NUM_1, reinterpret_cast<const char*>(bytes), count);
    }
    int read(uint8_t* byte) const { return uart_read_bytes(UART_NUM_1, byte, 1, 0); }
};

} // namespace lab
