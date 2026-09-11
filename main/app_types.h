#pragma once

#include <cstdint>

// AppEvent is the four-way button vocabulary consumed by LocalRoomApp's event
// loop in app_main.cpp. PassportBoard translates raw gpio / adc events into
// these enums before pushing them onto the FreeRTOS queue.
enum class AppEvent : uint8_t {
    kUp,
    kDown,
    kConfirm,
    kConfirmLong,
};

struct AppMessage {
    AppEvent event = AppEvent::kUp;
};