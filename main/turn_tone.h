#pragma once

#include <cstdint>

#include <driver/i2c_master.h>
#include <driver/i2s_std.h>
#include <esp_codec_dev.h>
#include <esp_codec_dev_defaults.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// TurnTone plays a short two-stage "ding" every time it becomes the local
// player's turn. The audio path is the on-board ES8311 DAC fed from I2S_NUM_0
// driven through the ESP32-C3 codec abstraction.
//
// The class is single-instance by construction (no static singleton), so the
// caller in app_main.cpp owns it directly. Initialize() is best-effort: if any
// of the I2C / I2S / codec stages fails the firmware keeps running, only the
// turn chime falls silent.
class TurnTone {
public:
    bool Initialize();
    void Play();

private:
    static void TaskEntry(void* argument);
    void TaskLoop();
    bool InitializeI2c();
    bool InitializeI2s();
    bool InitializeCodec();
    void WriteDing();

    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    i2s_chan_handle_t tx_handle_ = nullptr;
    i2s_chan_handle_t rx_handle_ = nullptr;
    const audio_codec_data_if_t* data_if_ = nullptr;
    const audio_codec_ctrl_if_t* ctrl_if_ = nullptr;
    const audio_codec_if_t* codec_if_ = nullptr;
    const audio_codec_gpio_if_t* gpio_if_ = nullptr;
    esp_codec_dev_handle_t device_ = nullptr;
    TaskHandle_t task_ = nullptr;
};