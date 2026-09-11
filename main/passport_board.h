#pragma once

#include "app_types.h"

#include <array>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <iot_button.h>

// PassportBoard: the thinnest possible shim over the FoloToy AI Passport
// hardware. It only owns the LCD bring-up and the three-button input mapping.
//
// Three physical buttons map to AppEvent values as follows:
//   * Up          -> AppEvent::kUp
//   * Down        -> AppEvent::kDown
//   * Confirm     -> AppEvent::kConfirm (short press)
//                     AppEvent::kConfirmLong (long press)
//
// The class is intentionally stateless beyond the iot_button handles and the
// callback context table. Callers drive the rest of the firmware from the
// event queue handed to InitializeButtons.
class PassportBoard {
public:
    void InitializeDisplay();
    void InitializeButtons(QueueHandle_t event_queue);

private:
    struct CallbackContext {
        QueueHandle_t queue = nullptr;
        AppEvent event = AppEvent::kUp;
    };

    static void ButtonCallback(void* button, void* user_data);
    static void ConfirmReleaseCallback(void* button, void* user_data);

    std::array<button_handle_t, 3> buttons_{};
    std::array<CallbackContext, 4> callback_contexts_{};
};