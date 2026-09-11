#pragma once

#include "folotoy_local_room.h"

#include <string>

#include <lvgl.h>

// LocalRoomUi is the LVGL-backed renderer that turns room and game state
// into the 240x320 framebuffer of the FoloToy AI Passport.
//
// The duel build trims the original UI:
//   * No UNO call-and-catch confirmation button (the duel has no call-uno).
//   * No "Accept / Challenge WildDrawFour" choice row (target just draws).
//   * Everything else is visually identical to the upstream UNO screen so
//     players familiar with the upstream project feel at home.
class LocalRoomUi {
public:
    void Initialize();
    void ShowReady();
    void Show(const std::string& status, const std::string& role,
              const std::string& message);
    void ShowGame(const FoloToyLocalRoom::GameUiState& state);

private:
    void Render(const char* status, const char* message, const char* footer, uint32_t accent);

    lv_obj_t* screen_ = nullptr;
};