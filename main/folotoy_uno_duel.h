#ifndef FOLOTOY_UNO_DUEL_H_
#define FOLOTOY_UNO_DUEL_H_

// FoloToyUnoDuel: a trimmed, two-player only UNO-style card engine.
//
// Differences from the upstream folotoy_uno_game that this file replaces:
//   * Exactly two players (kMaxPlayers == 2).
//   * Slimmer 52-card deck: 1-9, Skip, DrawTwo per color (44) plus 4 Wild and
//     4 WildDrawFour (8). No zero, no Reverse, no duplicates per rank/color.
//   * Five cards dealt to each player (faster opening than the 7-card deal).
//   * No "call UNO" window (skip CallUno / Catch Uno / CloseUnoWindow).
//   * No challenge against WildDrawFour (target simply draws 4 and loses the
//     turn). The ResolveWildDrawFour API is intentionally removed.
//   * No Reverse: with two players a Reverse would just bounce turns back and
//     forth, which is confusing in a fast duel.
//
// The class still has zero hardware dependencies. It can be compiled and unit
// tested on a host (g++/clang) without ESP-IDF, LVGL or FreeRTOS.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

class FoloToyUnoDuel {
public:
    static constexpr size_t kMaxPlayers = 2;
    static constexpr size_t kMaxHandCards = 36;
    static constexpr size_t kDeckCards = 52;

    // Card layout (high nibble = color, low nibble = rank):
    //   0..9  : numeric rank (1-9 used; 0 reserved / not produced in this deck)
    //   10    : kSkip
    //   11    : kReverse (kept for byte layout compatibility with upstream;
    //                     the duel deck never produces it)
    //   12    : kDrawTwo
    //   13    : kWild
    //   14    : kWildDrawFour
    //   15    : reserved, never produced
    // Color values 0..3 (red/yellow/green/blue) and 4 (wild) match upstream.
    enum Rank : uint8_t {
        kSkip = 10,
        kReverse = 11,
        kDrawTwo = 12,
        kWild = 13,
        kWildDrawFour = 14,
    };

    struct Player {
        uint32_t id = 0;
        std::array<uint8_t, kMaxHandCards> hand = {};
        uint8_t hand_size = 0;
    };

    bool Start(const uint32_t* player_ids, uint32_t random_seed);
    bool Play(uint32_t player_id, uint8_t card, uint8_t wild_color);
    bool Draw(uint32_t player_id);
    bool AutoDrawCurrentPlayerIfBlocked();

    bool active() const { return active_; }
    uint32_t current_player_id() const;
    uint32_t winner_id() const { return winner_id_; }
    uint8_t top_card() const { return top_card_; }
    uint8_t current_color() const { return current_color_; }
    size_t player_count() const { return player_count_; }
    uint8_t draw_pile_size() const;

    const Player* FindPlayer(uint32_t player_id) const;

    // True when `card` can legally be played on top of `top_card` under the
    // current `current_color`. Wilds are always legal.
    static bool IsPlayable(uint8_t card, uint8_t top_card, uint8_t current_color);

    static uint8_t CardColor(uint8_t card) { return card >> 4; }
    static uint8_t CardRank(uint8_t card) { return card & 0x0f; }
    static uint8_t MakeCard(uint8_t color, uint8_t rank) { return (color << 4) | rank; }

    // Short ASCII label such as "R3", "YS", "G+2", "W", "W+4" used for debug
    // logs and the legacy text fallback renderer.
    static std::string CardName(uint8_t card);

private:
    std::array<Player, kMaxPlayers> players_ = {};
    size_t player_count_ = 0;
    std::array<uint8_t, kDeckCards> deck_ = {};
    size_t deck_size_ = 0;
    size_t deck_position_ = 0;
    std::array<uint8_t, kDeckCards> discard_ = {};
    size_t discard_size_ = 0;
    size_t current_player_ = 0;
    int direction_ = 1;
    uint8_t top_card_ = 0;
    uint8_t current_color_ = 0;
    uint32_t winner_id_ = 0;
    uint32_t random_state_ = 1;
    bool active_ = false;

    uint32_t NextRandom();
    void Shuffle(uint8_t* cards, size_t count);
    bool DrawOne(Player& player);
    void RefillDrawPile();
    void Advance(size_t steps = 1);
    Player* FindMutablePlayer(uint32_t player_id);
};

#endif  // FOLOTOY_UNO_DUEL_H_