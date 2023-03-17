/******************************************************************************
 *
 * Copyright 2017 karawin (http://www.karawin.fr)
 *
 * Receive and decode nec IR. Send result in a queue
 *
 *******************************************************************************/

#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#include "ClickEncoder.h"
#include "app_main.h"
#include "gpio.h"
#include "webclient.h"
#include "webserver.h"
#include "interface.h"

/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/rmt_rx.h"

#define IR_RESOLUTION_HZ 1000000 // 1MHz resolution, 1 tick = 1us
#define IR_NEC_DECODE_MARGIN 200 // Tolerance for parsing RMT symbols into bit stream

/**
 * @brief NEC timing spec
 */
#define NEC_LEADING_CODE_DURATION_0 9000
#define NEC_LEADING_CODE_DURATION_1 4500
#define NEC_PAYLOAD_ZERO_DURATION_0 560
#define NEC_PAYLOAD_ZERO_DURATION_1 560
#define NEC_PAYLOAD_ONE_DURATION_0 560
#define NEC_PAYLOAD_ONE_DURATION_1 1690
#define NEC_REPEAT_CODE_DURATION_0 9000
#define NEC_REPEAT_CODE_DURATION_1 2250

static const char *TAG = "IR_NEC";

/**
 * @brief Saving NEC decode results
 */
static uint16_t s_nec_code_address;
static uint16_t s_nec_code_command;

/**
 * @brief Check whether a duration is within expected range
 */
static inline bool nec_check_in_range(uint32_t signal_duration, uint32_t spec_duration)
{
    return (signal_duration < (spec_duration + IR_NEC_DECODE_MARGIN)) &&
           (signal_duration > (spec_duration - IR_NEC_DECODE_MARGIN));
}

/**
 * @brief Check whether a RMT symbol represents NEC logic zero
 */
static bool nec_parse_logic0(rmt_symbol_word_t *rmt_nec_symbols)
{
    return nec_check_in_range(rmt_nec_symbols->duration0, NEC_PAYLOAD_ZERO_DURATION_0) &&
           nec_check_in_range(rmt_nec_symbols->duration1, NEC_PAYLOAD_ZERO_DURATION_1);
}

/**
 * @brief Check whether a RMT symbol represents NEC logic one
 */
static bool nec_parse_logic1(rmt_symbol_word_t *rmt_nec_symbols)
{
    return nec_check_in_range(rmt_nec_symbols->duration0, NEC_PAYLOAD_ONE_DURATION_0) &&
           nec_check_in_range(rmt_nec_symbols->duration1, NEC_PAYLOAD_ONE_DURATION_1);
}

/**
 * @brief Decode RMT symbols into NEC address and command
 */
static bool nec_parse_frame(rmt_symbol_word_t *rmt_nec_symbols)
{
    rmt_symbol_word_t *cur = rmt_nec_symbols;
    uint16_t address = 0;
    uint16_t command = 0;
    bool valid_leading_code = nec_check_in_range(cur->duration0, NEC_LEADING_CODE_DURATION_0) &&
                              nec_check_in_range(cur->duration1, NEC_LEADING_CODE_DURATION_1);
    if (!valid_leading_code)
    {
        return false;
    }
    cur++;
    for (int i = 0; i < 16; i++)
    {
        if (nec_parse_logic1(cur))
        {
            address |= 1 << i;
        }
        else if (nec_parse_logic0(cur))
        {
            address &= ~(1 << i);
        }
        else
        {
            return false;
        }
        cur++;
    }
    for (int i = 0; i < 16; i++)
    {
        if (nec_parse_logic1(cur))
        {
            command |= 1 << i;
        }
        else if (nec_parse_logic0(cur))
        {
            command &= ~(1 << i);
        }
        else
        {
            return false;
        }
        cur++;
    }
    // save address and command
    s_nec_code_address = address;
    s_nec_code_command = command;
    return true;
}

/**
 * @brief Check whether the RMT symbols represent NEC repeat code
 */
static bool nec_parse_frame_repeat(rmt_symbol_word_t *rmt_nec_symbols)
{
    return nec_check_in_range(rmt_nec_symbols->duration0, NEC_REPEAT_CODE_DURATION_0) &&
           nec_check_in_range(rmt_nec_symbols->duration1, NEC_REPEAT_CODE_DURATION_1);
}

/**
 * @brief Decode RMT symbols into NEC scan code and print the result
 */
static void parse_nec_frame(rmt_symbol_word_t *rmt_nec_symbols, size_t symbol_num)
{
    event_ir_t evt;
    // decode RMT symbols
    switch (symbol_num)
    {
    case 34: // NEC normal frame
        if (nec_parse_frame(rmt_nec_symbols))
        {
            ESP_LOGD(TAG, "Address=%04X, Command=%04X\r\n\r\n", s_nec_code_address, s_nec_code_command);
            evt.addr = s_nec_code_address;
            evt.cmd = s_nec_code_command;
            xQueueSend(event_ir, &evt, 0);
        }
        break;
    case 2: // NEC rDepeat frame
        if (nec_parse_frame_repeat(rmt_nec_symbols))
        {
            ESP_LOGD(TAG, "Address=%04X, Command=%04X, repeat\r\n\r\n", s_nec_code_address, s_nec_code_command);
            evt.addr = s_nec_code_address;
            evt.cmd = s_nec_code_command;
            xQueueSend(event_ir, &evt, 0);
        }
        break;
    default:
        ESP_LOGE(TAG, "Unknown NEC frame\r\n\r\n");
        break;
    }
}

static bool rmt_rx_done_callback(rmt_channel_handle_t channel, rmt_rx_done_event_data_t *edata, void *user_data)
{
    BaseType_t high_task_wakeup = pdFALSE;
    TaskHandle_t task_to_notify = (TaskHandle_t)user_data;
    // send the received RMT symbols to the parser task
    xTaskNotifyFromISR(task_to_notify, (uint32_t)edata, eSetValueWithOverwrite, &high_task_wakeup);
    return high_task_wakeup == pdTRUE;
}

rmt_channel_handle_t rmt_rx_channel = NULL;
// the following timing requirement is based on NEC protocol
rmt_receive_config_t receive_config =
    {
        .signal_range_min_ns = 1250,     // the shortest duration for NEC signal is 560us, 1250ns < 560us, valid signal won't be treated as noise
        .signal_range_max_ns = 12000000, // the longest duration for NEC signal is 9000us, 12000000ns > 9000us, the receive won't stop early
};

// save the received RMT symbols
rmt_symbol_word_t raw_symbols[64]; // 64 symbols should be sufficient for a standard NEC frame
rmt_rx_done_event_data_t *rx_data = NULL;

IRAM_ATTR bool rmt_nec_rx_init(void)
{
    esp_err_t err = ESP_OK;
    gpio_num_t ir;
    gpio_get_ir_signal(&ir);
    if (ir == GPIO_NONE)
        return false; // no IR needed

    ESP_LOGI(TAG, "create RMT RX channel");
    rmt_rx_channel_config_t rmt_rx_channel_cfg =
        {
            .clk_src = RMT_CLK_SRC_DEFAULT,
            .resolution_hz = 8000,
            .mem_block_symbols = 64, // amount of RMT symbols that the channel can store at a time
            .gpio_num = 19,
            .flags.with_dma = false,
            .flags.io_loop_back = false,
        };

    err = rmt_new_rx_channel(&rmt_rx_channel_cfg, &rmt_rx_channel);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "create RMT RX channel failed! %x", err);
        return false;
    }

    ESP_LOGI(TAG, "register RX done callback");
    TaskHandle_t cur_task = xTaskGetCurrentTaskHandle();
    rmt_rx_event_callbacks_t cbs =
        {
            .on_recv_done = rmt_rx_done_callback,
        };

    err = rmt_rx_register_event_callbacks(rmt_rx_channel, &cbs, cur_task);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "register RX callback failed! %x", err);
        return false;
    }

    err = rmt_enable(rmt_rx_channel);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "enable RX channel failed! %x", err);
        return false;
    }

    // ready to receive
    err = rmt_receive(rmt_rx_channel, raw_symbols, sizeof(raw_symbols), &receive_config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "received data failed! %x", err);
        return false;
    }

    return true;
}

IRAM_ATTR void rmt_nec_rx_task(void)
{
    if (rmt_nec_rx_init())
    {
        while (1)
        {
            // wait for RX done signal
            if (xTaskNotifyWait(0x00, ULONG_MAX, (uint32_t *)&rx_data, pdMS_TO_TICKS(1000)) == pdTRUE)
            {
                // parse the receive symbols and print the result
                parse_nec_frame(rx_data->received_symbols, rx_data->num_symbols);
                // start receive again
                ESP_ERROR_CHECK(rmt_receive(rmt_rx_channel, raw_symbols, sizeof(raw_symbols), &receive_config));
            }
        }
    }
    ESP_LOGD(TAG, "RMT finished");
    vTaskDelete(NULL);
}