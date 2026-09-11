#include "local_room_ui.h"

#include <esp_lvgl_port.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>

LV_FONT_DECLARE(lv_font_folotoy_cn_18);

namespace {

constexpr uint32_t kBackground = 0x0B1020;
constexpr uint32_t kPanel = 0x172033;
constexpr uint32_t kText = 0xF5F7FA;
constexpr uint32_t kMuted = 0x9BA8BA;
constexpr uint32_t kBlue = 0x48A1FF;
constexpr uint32_t kGreen = 0x42D392;
constexpr uint32_t kOrange = 0xFFB347;
constexpr uint32_t kCardBack = 0x20283A;
constexpr uint32_t kCardWild = 0x17191F;
constexpr std::array<uint32_t, 4> kCardColors = {
    0xE53935,
    0xF9C846,
    0x31B765,
    0x2788E6,
};

uint32_t CardFaceColor(uint8_t card) {
    const uint8_t color = FoloToyUnoDuel::CardColor(card);
    return color < kCardColors.size() ? kCardColors[color] : kCardWild;
}

uint32_t CardTextColor(uint8_t card) {
    return FoloToyUnoDuel::CardColor(card) == 1 ? 0x302600 : 0xFFFFFF;
}

std::string CardRankText(uint8_t card) {
    const uint8_t rank = FoloToyUnoDuel::CardRank(card);
    if (rank <= 9) {
        return std::to_string(rank);
    }
    if (rank == FoloToyUnoDuel::kSkip) {
        return "X";
    }
    if (rank == FoloToyUnoDuel::kDrawTwo) {
        return "+2";
    }
    if (rank == FoloToyUnoDuel::kWildDrawFour) {
        return "+4";
    }
    return "W";
}

std::string CardDescription(uint8_t card, uint8_t current_color) {
    static constexpr std::array<const char*, 4> kColorNames = {"红色", "黄色", "绿色", "蓝色"};
    const uint8_t color = FoloToyUnoDuel::CardColor(card);
    const uint8_t rank = FoloToyUnoDuel::CardRank(card);
    if (rank == FoloToyUnoDuel::kWild) {
        return current_color < kColorNames.size() ? std::string("变色牌｜当前") + kColorNames[current_color]
                                                  : "变色牌";
    }
    if (rank == FoloToyUnoDuel::kWildDrawFour) {
        return current_color < kColorNames.size() ? std::string("加四牌｜当前") + kColorNames[current_color]
                                                  : "加四牌";
    }

    std::string description = color < kColorNames.size() ? kColorNames[color] : "未知";
    if (rank <= 9) {
        description += " " + std::to_string(rank);
    } else if (rank == FoloToyUnoDuel::kSkip) {
        description += " 禁止";
    } else {
        description += " 加二";
    }
    return description;
}

std::string PlayerLabel(const FoloToyLocalRoom::GameUiState& state, uint32_t player_id) {
    size_t seat = state.player_ids.size();
    for (size_t index = 0; index < state.player_ids.size(); ++index) {
        if (state.player_ids[index] == player_id) {
            seat = index;
            break;
        }
    }
    if (player_id == state.self_id) {
        return seat < state.player_ids.size() ? "你（玩家" + std::to_string(seat + 1) + "）" : "你";
    }
    if (player_id == state.host_id) {
        return seat < state.player_ids.size() ? "房主（玩家" + std::to_string(seat + 1) + "）" : "房主";
    }
    return seat < state.player_ids.size() ? "玩家" + std::to_string(seat + 1) : "对手";
}

void AddWildPips(lv_obj_t* card, int width, int height) {
    constexpr std::array<int, 4> kXSign = {-1, 1, -1, 1};
    constexpr std::array<int, 4> kYSign = {-1, -1, 1, 1};
    const int size = width > 50 ? 10 : 6;
    const int distance = width > 50 ? 8 : 5;
    for (size_t index = 0; index < kCardColors.size(); ++index) {
        lv_obj_t* pip = lv_obj_create(card);
        lv_obj_remove_style_all(pip);
        lv_obj_set_size(pip, size, size);
        lv_obj_set_style_radius(pip, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(pip, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(pip, lv_color_hex(kCardColors[index]), 0);
        lv_obj_set_pos(pip, width / 2 - size / 2 + kXSign[index] * distance,
                       height - size * 2 + kYSign[index] * (size / 2));
    }
}

lv_obj_t* CreateCard(lv_obj_t* parent, int x, int y, int width, int height, uint8_t card_value,
                     bool selected, bool playable, bool draw_card, int rotation = 0) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, width, height);
    lv_obj_set_style_radius(card, width > 50 ? 12 : 7, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(card,
                              lv_color_hex(draw_card ? kCardBack : CardFaceColor(card_value)), 0);
    lv_obj_set_style_border_width(card, selected ? 3 : 2, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(selected ? 0x55E6FF : 0xF7F8FA), 0);
    lv_obj_set_style_shadow_width(card, selected ? 12 : 0, 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0x26C6FF), 0);
    lv_obj_set_style_shadow_opa(card, selected ? LV_OPA_60 : LV_OPA_TRANSP, 0);
    lv_obj_set_style_opa(card, selected || playable ? LV_OPA_COVER : LV_OPA_40, 0);
    lv_obj_set_style_transform_pivot_x(card, width / 2, 0);
    lv_obj_set_style_transform_pivot_y(card, height, 0);
    lv_obj_set_style_transform_rotation(card, rotation, 0);

    lv_obj_t* rank = lv_label_create(card);
    const std::string rank_text = draw_card ? "+1" : CardRankText(card_value);
    lv_label_set_text(rank, rank_text.c_str());
    lv_obj_set_style_text_font(rank, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(
        rank, lv_color_hex(draw_card ? 0x55E6FF : CardTextColor(card_value)), 0);
    lv_obj_center(rank);
    lv_obj_set_style_transform_scale(rank, width > 50 ? 448 : 320, 0);

    lv_obj_t* corner = lv_label_create(card);
    static constexpr std::array<const char*, 5> kColorCodes = {"R", "Y", "G", "B", "W"};
    const uint8_t color = draw_card ? 4 : FoloToyUnoDuel::CardColor(card_value);
    lv_label_set_text(corner, draw_card ? "D" : (color < kColorCodes.size() ? kColorCodes[color] : "?"));
    lv_obj_set_style_text_font(corner, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(corner, lv_color_hex(draw_card ? 0x55E6FF : CardTextColor(card_value)),
                                0);
    lv_obj_align(corner, LV_ALIGN_TOP_LEFT, 5, 4);

    if (!draw_card && FoloToyUnoDuel::CardColor(card_value) == 4) {
        AddWildPips(card, width, height);
    }
    return card;
}

lv_obj_t* CreateChoiceButton(lv_obj_t* parent, int x, int y, int width, int height,
                             const char* text, uint32_t color, bool selected) {
    lv_obj_t* button = lv_obj_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_radius(button, 9, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
    lv_obj_set_style_border_width(button, selected ? 4 : 2, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(selected ? 0x55E6FF : 0xF7F8FA), 0);
    lv_obj_set_style_shadow_width(button, selected ? 12 : 0, 0);
    lv_obj_set_style_shadow_color(button, lv_color_hex(0x26C6FF), 0);
    lv_obj_set_style_shadow_opa(button, selected ? LV_OPA_60 : LV_OPA_TRANSP, 0);

    lv_obj_t* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_width(label, width - 8);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(label, &lv_font_folotoy_cn_18, 0);
    lv_obj_set_style_text_color(label,
                                lv_color_hex(color == kCardColors[1] ? 0x302600 : kText), 0);
    lv_obj_center(label);
    return button;
}

}  // namespace

void LocalRoomUi::Initialize() {
    lvgl_port_lock(0);
    screen_ = lv_screen_active();
    lv_obj_set_style_bg_color(screen_, lv_color_hex(kBackground), 0);
    lv_obj_set_style_bg_opa(screen_, LV_OPA_COVER, 0);
    lvgl_port_unlock();
}

void LocalRoomUi::Render(const char* status, const char* message, const char* footer,
                         uint32_t accent) {
    lvgl_port_lock(0);
    lv_obj_clean(screen_);
    lv_obj_set_style_bg_color(screen_, lv_color_hex(kBackground), 0);
    lv_obj_set_style_bg_opa(screen_, LV_OPA_COVER, 0);

    lv_obj_t* brand = lv_label_create(screen_);
    lv_label_set_text(brand, "FoloToy");
    lv_obj_set_style_text_font(brand, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(brand, lv_color_hex(kMuted), 0);
    lv_obj_align(brand, LV_ALIGN_TOP_LEFT, 12, 18);

    lv_obj_t* offline = lv_label_create(screen_);
    lv_label_set_text(offline, "OFFLINE  CH 1");
    lv_obj_set_style_text_font(offline, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(offline, lv_color_hex(kGreen), 0);
    lv_obj_align(offline, LV_ALIGN_TOP_RIGHT, -12, 18);

    lv_obj_t* panel = lv_obj_create(screen_);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, 216, 206);
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 4);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(kPanel), 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(accent), 0);
    lv_obj_set_style_radius(panel, 18, 0);

    lv_obj_t* status_label = lv_label_create(panel);
    lv_label_set_text(status_label, status);
    lv_obj_set_width(status_label, 190);
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(status_label, &lv_font_folotoy_cn_18, 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(accent), 0);
    lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, 18);

    lv_obj_t* message_label = lv_obj_create(panel);
    lv_obj_remove_style_all(message_label);
    lv_obj_set_size(message_label, 190, 118);
    lv_obj_align(message_label, LV_ALIGN_BOTTOM_MID, 0, -12);

    // The label sits inside the transparent panel so its background can be
    // cut by the panel's rounded corners.
    lv_obj_t* text_label = lv_label_create(message_label);
    lv_label_set_text(text_label, message);
    lv_obj_set_width(text_label, 190);
    lv_obj_set_style_text_align(text_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(text_label, &lv_font_folotoy_cn_18, 0);
    lv_obj_set_style_text_color(text_label, lv_color_hex(kText), 0);
    lv_obj_center(text_label);

    lv_obj_t* footer_label = lv_label_create(screen_);
    lv_label_set_text(footer_label, footer);
    lv_obj_set_width(footer_label, 224);
    lv_obj_set_style_text_align(footer_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(footer_label, &lv_font_folotoy_cn_18, 0);
    lv_obj_set_style_text_color(footer_label, lv_color_hex(kMuted), 0);
    lv_obj_align(footer_label, LV_ALIGN_BOTTOM_MID, 0, -12);
    lvgl_port_unlock();
}

void LocalRoomUi::ShowReady() {
    Render("决斗房", "完全离线，不连接互联网\n\n请准备两台 FoloToy 设备\n按住确认键开始匹配",
           "按确认键开始匹配附近设备", kBlue);
}

void LocalRoomUi::Show(const std::string& status, const std::string& role,
                       const std::string& message) {
    const uint32_t accent = role == "user" ? kGreen : (status.rfind("DUEL", 0) == 0 ? kOrange : kBlue);
    const char* footer = message.find("长按开始 DUEL") != std::string::npos
                             ? "上下选择｜确认发送｜长按开局"
                             : "上下选择｜确认操作｜长按退出";
    Render(status.c_str(), message.c_str(), footer, accent);
}

void LocalRoomUi::ShowGame(const FoloToyLocalRoom::GameUiState& state) {
    lvgl_port_lock(0);
    lv_obj_clean(screen_);
    lv_obj_set_style_bg_color(screen_, lv_color_hex(kBackground), 0);
    lv_obj_set_style_bg_opa(screen_, LV_OPA_COVER, 0);

    lv_obj_t* brand = lv_label_create(screen_);
    lv_label_set_text(brand, "DUEL");
    lv_obj_set_style_text_font(brand, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(brand, lv_color_hex(kOrange), 0);
    lv_obj_align(brand, LV_ALIGN_TOP_LEFT, 10, 8);

    char stats[24];
    std::snprintf(stats, sizeof(stats), "%uP  D%u", static_cast<unsigned>(state.player_count),
                  static_cast<unsigned>(state.draw_pile_size));
    lv_obj_t* stats_label = lv_label_create(screen_);
    lv_label_set_text(stats_label, stats);
    lv_obj_set_style_text_font(stats_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(stats_label, lv_color_hex(kMuted), 0);
    lv_obj_align(stats_label, LV_ALIGN_TOP_LEFT, 50, 10);

    char hand_count[24];
    std::snprintf(hand_count, sizeof(hand_count), "手牌 %u",
                  static_cast<unsigned>(state.hand_size));
    lv_obj_t* hand_count_label = lv_label_create(screen_);
    lv_label_set_text(hand_count_label, hand_count);
    lv_obj_set_style_text_font(hand_count_label, &lv_font_folotoy_cn_18, 0);
    lv_obj_set_style_text_color(hand_count_label, lv_color_hex(kText), 0);
    lv_obj_align(hand_count_label, LV_ALIGN_TOP_RIGHT, -10, 7);

    std::string turn_text;
    uint32_t turn_color = kMuted;
    if (state.winner_id != 0) {
        turn_text = state.winner_id == state.self_id ? "你赢了！"
                                                     : PlayerLabel(state, state.winner_id) + " 获胜";
        turn_color = kOrange;
    } else if (state.choosing_wild_color) {
        turn_text = "请选择万能牌颜色";
        turn_color = kGreen;
    } else if (state.turn_player_id == state.self_id) {
        turn_text = "轮到你，请选择一张牌";
        turn_color = kGreen;
    } else {
        turn_text = "等待 " + PlayerLabel(state, state.turn_player_id) + " 出牌";
    }
    lv_obj_t* turn_label = lv_label_create(screen_);
    lv_label_set_text(turn_label, turn_text.c_str());
    lv_obj_set_width(turn_label, 230);
    lv_obj_set_style_text_align(turn_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(turn_label, &lv_font_folotoy_cn_18, 0);
    lv_obj_set_style_text_color(turn_label, lv_color_hex(turn_color), 0);
    lv_obj_align(turn_label, LV_ALIGN_TOP_MID, 0, 27);

    CreateCard(screen_, 82, 58, 76, 104, state.top_card, false, true, false);

    lv_obj_t* top_description = lv_label_create(screen_);
    const std::string top_text = "桌面：" + CardDescription(state.top_card, state.current_color);
    lv_label_set_text(top_description, top_text.c_str());
    lv_obj_set_width(top_description, 230);
    lv_obj_set_style_text_align(top_description, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(top_description, &lv_font_folotoy_cn_18, 0);
    lv_obj_set_style_text_color(top_description, lv_color_hex(kText), 0);
    lv_obj_align(top_description, LV_ALIGN_TOP_MID, 0, 165);

    const size_t selected = state.hand_size == 0
                                ? 0
                                : std::min<size_t>(state.selected_index, state.hand_size - 1);
    if (state.choosing_wild_color) {
        static constexpr std::array<const char*, 4> kColorLabels = {"红", "黄", "绿", "蓝"};
        constexpr int kChoiceWidth = 48;
        constexpr int kChoiceGap = 6;
        constexpr int kRowWidth = 4 * kChoiceWidth + 3 * kChoiceGap;
        for (size_t index = 0; index < kColorLabels.size(); ++index) {
            CreateChoiceButton(screen_, (240 - kRowWidth) / 2 + static_cast<int>(index) *
                                                               (kChoiceWidth + kChoiceGap),
                               202, kChoiceWidth, 60, kColorLabels[index], kCardColors[index],
                               state.selected_wild_color == index);
        }
    } else {
        const size_t card_count = state.hand_size;
        constexpr int kCardWidth = 44;
        constexpr int kCardHeight = 64;
        constexpr int kMaximumStep = 26;
        constexpr int kAvailableSpan = 168;
        const int step = card_count > 1
                             ? std::min(kMaximumStep,
                                        kAvailableSpan / static_cast<int>(card_count - 1))
                             : 0;
        const int row_width = card_count == 0
                                  ? 0
                                  : kCardWidth + step * static_cast<int>(card_count - 1);
        const int row_x = (240 - row_width) / 2;

        // Draw the selected card last so it rises above the overlapping row and is fully visible.
        for (size_t index = 0; index < card_count; ++index) {
            if (index == selected) {
                continue;
            }
            const int fan_position = card_count > 1
                                         ? (static_cast<int>(index) * 200 /
                                                static_cast<int>(card_count - 1) -
                                            100)
                                         : 0;
            const int rotation = fan_position * 11 / 10;
            const int arc_drop = std::abs(fan_position) * 9 / 100;
            const uint8_t card = state.hand[index];
            CreateCard(screen_, row_x + static_cast<int>(index) * step, 203 + arc_drop,
                       kCardWidth, kCardHeight, card, false,
                       FoloToyUnoDuel::IsPlayable(card, state.top_card, state.current_color), false,
                       rotation);
        }
        if (card_count > 0) {
            const uint8_t card = state.hand[selected];
            const int selected_x = std::clamp(row_x + static_cast<int>(selected) * step - 4, 4,
                                              240 - 52 - 4);
            CreateCard(screen_, selected_x, 187, 52, 76, card, true,
                       FoloToyUnoDuel::IsPlayable(card, state.top_card, state.current_color), false);
        }
    }

    std::string help_text;
    if (state.winner_id != 0) {
        help_text = "长按确认退出房间";
    } else if (state.choosing_wild_color) {
        help_text = "上下选择颜色｜确认出牌";
    } else if (state.turn_player_id != state.self_id) {
        help_text = "等待对方出牌";
    } else if (state.hand_size == 0) {
        help_text = "正在自动摸牌";
    } else {
        const uint8_t selected_card = state.hand[selected];
        help_text = CardDescription(selected_card, state.current_color);
        help_text += FoloToyUnoDuel::IsPlayable(selected_card, state.top_card, state.current_color)
                         ? "｜按确认"
                         : "｜当前不可出";
    }
    lv_obj_t* help_label = lv_label_create(screen_);
    lv_label_set_text(help_label, help_text.c_str());
    lv_obj_set_width(help_label, 232);
    lv_obj_set_style_text_align(help_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(help_label, &lv_font_folotoy_cn_18, 0);
    lv_obj_set_style_text_color(help_label, lv_color_hex(kText), 0);
    lv_obj_align(help_label, LV_ALIGN_BOTTOM_MID, 0, -5);
    lvgl_port_unlock();
}