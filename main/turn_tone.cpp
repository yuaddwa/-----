#include "turn_tone.h"

#include <array>
#include <cstddef>

#include <driver/gpio.h>
#include <esp_log.h>

namespace {

constexpr char kTag[] = "TurnTone";
constexpr uint32_t kSampleRate = 16000;
constexpr gpio_num_t kMclk = GPIO_NUM_6;
constexpr gpio_num_t kBclk = GPIO_NUM_5;
constexpr gpio_num_t kWordSelect = GPIO_NUM_3;
constexpr gpio_num_t kDataOut = GPIO_NUM_2;
constexpr gpio_num_t kDataIn = GPIO_NUM_4;
constexpr gpio_num_t kI2cSda = GPIO_NUM_10;
constexpr gpio_num_t kI2cScl = GPIO_NUM_7;
constexpr uint8_t kEs8311Address = ES8311_CODEC_DEFAULT_ADDR;

constexpr std::array<int16_t, 32> kSine = {
    0,      6393,   12539,  18204,  23170,  27245,  30273,  32138,
    32767,  32138,  30273,  27245,  23170,  18204,  12539,  6393,
    0,      -6393,  -12539, -18204, -23170, -27245, -30273, -32138,
    -32767, -32138, -30273, -27245, -23170, -18204, -12539, -6393,
};

}  // namespace

bool TurnTone::Initialize() {
    if (!InitializeI2c() || !InitializeI2s() || !InitializeCodec()) {
        ESP_LOGE(kTag, "Turn chime initialization failed; the game will continue silently");
        return false;
    }
    if (xTaskCreate(TaskEntry, "turn_tone", 3072, this, 3, &task_) != pdPASS) {
        ESP_LOGE(kTag, "Unable to create turn chime task");
        return false;
    }
    ESP_LOGI(kTag, "Local turn chime ready");
    return true;
}

void TurnTone::Play() {
    if (task_ != nullptr) {
        xTaskNotifyGive(task_);
    }
}

void TurnTone::TaskEntry(void* argument) {
    static_cast<TurnTone*>(argument)->TaskLoop();
}

void TurnTone::TaskLoop() {
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        WriteDing();
    }
}

bool TurnTone::InitializeI2c() {
    const i2c_master_bus_config_t config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = kI2cSda,
        .scl_io_num = kI2cScl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {.enable_internal_pullup = 1, .allow_pd = 0},
    };
    const esp_err_t result = i2c_new_master_bus(&config, &i2c_bus_);
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "I2C init failed: %s", esp_err_to_name(result));
        return false;
    }
    return true;
}

bool TurnTone::InitializeI2s() {
    const i2c_chan_config_t channel_config = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 8,
        .dma_frame_num = 160,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = false,
        .allow_pd = false,
        .intr_priority = 0,
    };
    if (i2s_new_channel(&channel_config, &tx_handle_, &rx_handle_) != ESP_OK) {
        ESP_LOGE(kTag, "I2S channel init failed");
        return false;
    }
    const i2s_std_config_t standard_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kSampleRate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = kMclk,
            .bclk = kBclk,
            .ws = kWordSelect,
            .dout = kDataOut,
            .din = kDataIn,
            .invert_flags = {},
        },
    };
    if (i2s_channel_init_std_mode(tx_handle_, &standard_config) != ESP_OK ||
        i2s_channel_init_std_mode(rx_handle_, &standard_config) != ESP_OK ||
        i2s_channel_enable(tx_handle_) != ESP_OK || i2s_channel_enable(rx_handle_) != ESP_OK) {
        ESP_LOGE(kTag, "I2S standard mode init failed");
        return false;
    }
    return true;
}

bool TurnTone::InitializeCodec() {
    audio_codec_i2s_cfg_t i2s_config = {
        .port = I2S_NUM_0,
        .rx_handle = rx_handle_,
        .tx_handle = tx_handle_,
        .clk_src = 0,
    };
    data_if_ = audio_codec_new_i2s_data(&i2s_config);

    audio_codec_i2c_cfg_t i2c_config = {
        .port = I2C_NUM_0,
        .addr = kEs8311Address,
        .bus_handle = i2c_bus_,
    };
    ctrl_if_ = audio_codec_new_i2c_ctrl(&i2c_config);
    gpio_if_ = audio_codec_new_gpio();
    if (data_if_ == nullptr || ctrl_if_ == nullptr || gpio_if_ == nullptr) {
        return false;
    }

    uint8_t reset = 0x1f;
    if (ctrl_if_->write_reg(ctrl_if_, 0x00, 1, &reset, 1) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(kTag, "ES8311 reset failed");
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(5));

    es8311_codec_cfg_t codec_config = {};
    codec_config.ctrl_if = ctrl_if_;
    codec_config.gpio_if = gpio_if_;
    codec_config.codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH;
    codec_config.pa_pin = GPIO_NUM_NC;
    codec_config.use_mclk = true;
    codec_config.hw_gain.pa_voltage = 5.0f;
    codec_config.hw_gain.codec_dac_voltage = 3.3f;
    codec_if_ = es8311_codec_new(&codec_config);
    if (codec_if_ == nullptr) {
        return false;
    }

    esp_codec_dev_cfg_t device_config = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
        .codec_if = codec_if_,
        .data_if = data_if_,
    };
    device_ = esp_codec_dev_new(&device_config);
    if (device_ == nullptr) {
        return false;
    }
    esp_codec_dev_sample_info_t sample_info = {
        .bits_per_sample = 16,
        .channel = 1,
        .channel_mask = 0,
        .sample_rate = kSampleRate,
        .mclk_multiple = 0,
    };
    if (esp_codec_dev_open(device_, &sample_info) != ESP_CODEC_DEV_OK ||
        esp_codec_dev_set_out_vol(device_, 60) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(kTag, "ES8311 output open failed");
        return false;
    }
    return true;
}

void TurnTone::WriteDing() {
    constexpr size_t kChunkSamples = 160;
    constexpr size_t kToneSamples = 2240;
    std::array<int16_t, kChunkSamples> samples = {};
    uint32_t phase = 0;

    for (size_t offset = 0; offset < kToneSamples; offset += samples.size()) {
        for (size_t index = 0; index < samples.size(); ++index) {
            const size_t position = offset + index;
            const uint32_t frequency = position < kToneSamples / 2 ? 1050 : 1400;
            phase += static_cast<uint32_t>((static_cast<uint64_t>(frequency) << 32) /
                                           kSampleRate);
            const int32_t envelope = static_cast<int32_t>(kToneSamples - position);
            samples[index] = static_cast<int16_t>(
                (static_cast<int32_t>(kSine[phase >> 27]) * envelope * 7) /
                (static_cast<int32_t>(kToneSamples) * 20));
        }
        if (esp_codec_dev_write(device_, samples.data(), samples.size() * sizeof(int16_t)) !=
            ESP_CODEC_DEV_OK) {
            ESP_LOGW(kTag, "Turn chime write failed");
            return;
        }
    }
    samples.fill(0);
    esp_codec_dev_write(device_, samples.data(), samples.size() * sizeof(int16_t));
    ESP_LOGI(kTag, "Turn chime played");
}