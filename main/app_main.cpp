#include "app_types.h"
#include "folotoy_local_room.h"
#include "local_room_ui.h"
#include "passport_board.h"
#include "turn_tone.h"

#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <atomic>

namespace {

constexpr char kTag[] = "FoloToyDuelApp";
constexpr uint8_t kEspNowChannel = 1;

// LocalRoomApp owns every long-lived subsystem for the duel firmware:
//   * PassportBoard : LCD bring-up + 3 ADC buttons into a FreeRTOS queue.
//   * LocalRoomUi   : LVGL renderer for the duel screen.
//   * TurnTone      : ES8311 chime on each new local turn.
//   * FoloToyLocalRoom : ESP-NOW room, host arbitration, game orchestration.
//
// The class also wires a small atomic snapshot to detect the transition from
// "other player's turn" to "my turn" so the chime can fire exactly once per
// turn boundary.
class LocalRoomApp {
public:
    LocalRoomApp()
        : room_([this](const std::string& status, const std::string& role,
                       const std::string& message) { ui_.Show(status, role, message); },
                [this](const FoloToyLocalRoom::GameUiState& state) { HandleGameUi(state); }) {}

    void Run() {
        InitializeNvs();
        InitializeRadio();
        turn_tone_.Initialize();

        event_queue_ = xQueueCreate(12, sizeof(AppMessage));
        ESP_ERROR_CHECK(event_queue_ == nullptr ? ESP_ERR_NO_MEM : ESP_OK);

        board_.InitializeDisplay();
        ui_.Initialize();
        ui_.ShowReady();
        board_.InitializeButtons(event_queue_);
        ESP_LOGI(kTag, "Standalone duel firmware ready on channel %u", kEspNowChannel);

        while (true) {
            AppMessage message;
            if (xQueueReceive(event_queue_, &message, portMAX_DELAY) == pdTRUE) {
                HandleEvent(message.event);
            }
        }
    }

private:
    static void InitializeNvs() {
        esp_err_t result = nvs_flash_init();
        if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_ERROR_CHECK(nvs_flash_erase());
            result = nvs_flash_init();
        }
        ESP_ERROR_CHECK(result);
    }

    static void InitializeRadio() {
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&config));
        ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_start());
        ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
        ESP_ERROR_CHECK(esp_wifi_set_channel(kEspNowChannel, WIFI_SECOND_CHAN_NONE));
    }

    void HandleEvent(AppEvent event) {
        switch (event) {
            case AppEvent::kUp:
                if (room_.active()) {
                    room_.OnUp();
                }
                break;
            case AppEvent::kDown:
                if (room_.active()) {
                    room_.OnDown();
                }
                break;
            case AppEvent::kConfirm:
                if (room_.active()) {
                    room_.OnConfirm();
                } else {
                    self_turn_actionable_.store(false);
                    if (!room_.Start()) {
                        ui_.Show("启动失败", "system", "无线模块初始化失败，请重新启动");
                    }
                }
                break;
            case AppEvent::kConfirmLong:
                if (room_.active()) {
                    if (!room_.OnLongConfirm()) {
                        room_.Stop();
                        self_turn_actionable_.store(false);
                        ui_.ShowReady();
                    }
                }
                break;
        }
    }

    void HandleGameUi(const FoloToyLocalRoom::GameUiState& state) {
        ui_.ShowGame(state);
        const bool actionable = state.winner_id == 0 && state.turn_player_id == state.self_id;
        const bool was_actionable = self_turn_actionable_.exchange(actionable);
        const uint8_t previous_top_card = last_top_card_.exchange(state.top_card);
        const uint8_t previous_hand_size = last_hand_size_.exchange(state.hand_size);
        const bool self_took_another_turn =
            actionable && was_actionable &&
            (state.top_card != previous_top_card || state.hand_size != previous_hand_size);
        if (actionable && (!was_actionable || self_took_another_turn)) {
            turn_tone_.Play();
        }
    }

    QueueHandle_t event_queue_ = nullptr;
    PassportBoard board_;
    LocalRoomUi ui_;
    TurnTone turn_tone_;
    FoloToyLocalRoom room_;
    std::atomic_bool self_turn_actionable_ = false;
    std::atomic_uint8_t last_top_card_ = 0xff;
    std::atomic_uint8_t last_hand_size_ = 0xff;
};

}  // namespace

extern "C" void app_main() {
    static LocalRoomApp app;
    app.Run();
}