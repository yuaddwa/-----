#include "folotoy_uno_duel.h"

#include <algorithm>
#include <cstdio>

namespace {

constexpr uint8_t kWildColor = 4;

}  // namespace

uint32_t FoloToyUnoDuel::NextRandom() {
    // xorshift32 is deterministic across devices and sufficient for shuffling.
    // Determinism matters because every device in the room re-runs the same
    // shuffle from the host's broadcast seed to keep snapshots in sync.
    uint32_t value = random_state_;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    random_state_ = value;
    return value;
}

void FoloToyUnoDuel::Shuffle(uint8_t* cards, size_t count) {
    for (size_t index = count; index > 1; --index) {
        const size_t target = NextRandom() % index;
        std::swap(cards[index - 1], cards[target]);
    }
}

bool FoloToyUnoDuel::Start(const uint32_t* player_ids, uint32_t random_seed) {
    if (player_ids == nullptr || player_ids[0] == 0 || player_ids[1] == 0 ||
        player_ids[0] == player_ids[1]) {
        return false;
    }

    players_ = {};
    player_count_ = kMaxPlayers;
    players_[0].id = player_ids[0];
    players_[1].id = player_ids[1];

    deck_size_ = 0;
    // Slim duel deck: per color, one of each 1-9 plus one Skip and one +2.
    for (uint8_t color = 0; color < 4; ++color) {
        for (uint8_t rank = 1; rank <= 9; ++rank) {
            deck_[deck_size_++] = MakeCard(color, static_cast<uint8_t>(rank));
        }
        deck_[deck_size_++] = MakeCard(color, kSkip);
        deck_[deck_size_++] = MakeCard(color, kDrawTwo);
    }
    for (int copy = 0; copy < 4; ++copy) {
        deck_[deck_size_++] = MakeCard(kWildColor, kWild);
        deck_[deck_size_++] = MakeCard(kWildColor, kWildDrawFour);
    }
    // 4 * 11 + 8 == 52

    random_state_ = random_seed == 0 ? 1 : random_seed;
    Shuffle(deck_.data(), deck_size_);
    deck_position_ = 0;
    discard_size_ = 0;
    current_player_ = 0;
    direction_ = 1;
    winner_id_ = 0;
    active_ = false;

    // Deal five cards to each player. Faster opening keeps a duel feeling
    // snappy instead of dragging into a long mid-game.
    for (int card = 0; card < 5; ++card) {
        for (size_t player = 0; player < player_count_; ++player) {
            if (!DrawOne(players_[player])) {
                return false;
            }
        }
    }

    // The starter must be a numeric card so neither player gets blindsided
    // by a free Skip / WildDrawFour on the opening turn.
    size_t number_card = deck_position_;
    while (number_card < deck_size_ && CardRank(deck_[number_card]) > 9) {
        ++number_card;
    }
    if (number_card == deck_size_) {
        return false;
    }
    std::swap(deck_[deck_position_], deck_[number_card]);
    top_card_ = deck_[deck_position_++];
    current_color_ = CardColor(top_card_);
    discard_[discard_size_++] = top_card_;
    active_ = true;
    return true;
}

FoloToyUnoDuel::Player* FoloToyUnoDuel::FindMutablePlayer(uint32_t player_id) {
    for (size_t index = 0; index < player_count_; ++index) {
        if (players_[index].id == player_id) {
            return &players_[index];
        }
    }
    return nullptr;
}

const FoloToyUnoDuel::Player* FoloToyUnoDuel::FindPlayer(uint32_t player_id) const {
    for (size_t index = 0; index < player_count_; ++index) {
        if (players_[index].id == player_id) {
            return &players_[index];
        }
    }
    return nullptr;
}

uint32_t FoloToyUnoDuel::current_player_id() const {
    if (!active_ || player_count_ == 0) {
        return 0;
    }
    return players_[current_player_].id;
}

uint8_t FoloToyUnoDuel::draw_pile_size() const {
    const size_t remaining = deck_size_ > deck_position_ ? deck_size_ - deck_position_ : 0;
    return static_cast<uint8_t>(std::min<size_t>(remaining, 255));
}

bool FoloToyUnoDuel::IsPlayable(uint8_t card, uint8_t top_card, uint8_t current_color) {
    // Wilds always match. Otherwise either the color or the rank must match.
    return CardColor(card) == kWildColor || CardColor(card) == current_color ||
           CardRank(card) == CardRank(top_card);
}

void FoloToyUnoDuel::RefillDrawPile() {
    if (discard_size_ <= 1) {
        return;
    }

    const uint8_t current_top = discard_[discard_size_ - 1];
    deck_size_ = discard_size_ - 1;
    std::copy_n(discard_.begin(), deck_size_, deck_.begin());
    Shuffle(deck_.data(), deck_size_);
    deck_position_ = 0;
    discard_[0] = current_top;
    discard_size_ = 1;
}

bool FoloToyUnoDuel::DrawOne(Player& player) {
    if (player.hand_size >= kMaxHandCards) {
        return false;
    }
    if (deck_position_ >= deck_size_) {
        RefillDrawPile();
    }
    if (deck_position_ >= deck_size_) {
        return false;
    }
    player.hand[player.hand_size++] = deck_[deck_position_++];
    return true;
}

void FoloToyUnoDuel::Advance(size_t steps) {
    if (player_count_ == 0) {
        return;
    }
    while (steps-- > 0) {
        current_player_ = (current_player_ + 1) % player_count_;
    }
}

bool FoloToyUnoDuel::Play(uint32_t player_id, uint8_t card, uint8_t wild_color) {
    if (!active_ || player_id != current_player_id()) {
        return false;
    }
    Player* player = FindMutablePlayer(player_id);
    if (player == nullptr || !IsPlayable(card, top_card_, current_color_)) {
        return false;
    }

    // Locate the chosen card in the player's hand so we can remove exactly
    // that copy (in case the player holds duplicates of the same card).
    size_t hand_index = player->hand_size;
    for (size_t index = 0; index < player->hand_size; ++index) {
        if (player->hand[index] == card) {
            hand_index = index;
            break;
        }
    }
    if (hand_index == player->hand_size) {
        return false;
    }

    for (size_t index = hand_index + 1; index < player->hand_size; ++index) {
        player->hand[index - 1] = player->hand[index];
    }
    --player->hand_size;

    top_card_ = card;
    current_color_ = CardColor(card) == kWildColor ? wild_color % 4 : CardColor(card);
    if (discard_size_ < discard_.size()) {
        discard_[discard_size_++] = card;
    }

    const uint8_t rank = CardRank(card);
    if (rank == kSkip) {
        // Other player loses their turn; we move two seats forward.
        Advance(2);
    } else if (rank == kDrawTwo) {
        Advance();
        Player& punished = players_[current_player_];
        for (int index = 0; index < 2; ++index) {
            DrawOne(punished);
        }
        Advance();
    } else if (rank == kWildDrawFour) {
        // No challenge window in this duel: target simply draws 4 and skips.
        Advance();
        Player& punished = players_[current_player_];
        for (int index = 0; index < 4; ++index) {
            DrawOne(punished);
        }
        Advance();
    } else {
        Advance();
    }

    if (player->hand_size == 0) {
        winner_id_ = player_id;
        active_ = false;
    }
    return true;
}

bool FoloToyUnoDuel::Draw(uint32_t player_id) {
    if (!active_ || player_id != current_player_id()) {
        return false;
    }
    Player* player = FindMutablePlayer(player_id);
    if (player == nullptr || !DrawOne(*player)) {
        return false;
    }
    Advance();
    return true;
}

bool FoloToyUnoDuel::AutoDrawCurrentPlayerIfBlocked() {
    if (!active_ || player_count_ == 0) {
        return false;
    }
    Player& player = players_[current_player_];
    for (size_t index = 0; index < player.hand_size; ++index) {
        if (IsPlayable(player.hand[index], top_card_, current_color_)) {
            return false;
        }
    }
    if (!DrawOne(player)) {
        return false;
    }
    Advance();
    return true;
}

std::string FoloToyUnoDuel::CardName(uint8_t card) {
    static constexpr char kColors[] = {'R', 'Y', 'G', 'B'};
    const uint8_t color = CardColor(card);
    const uint8_t rank = CardRank(card);
    if (rank == kWild) {
        return "W";
    }
    if (rank == kWildDrawFour) {
        return "W+4";
    }

    char rank_text[4] = {};
    if (rank <= 9) {
        std::snprintf(rank_text, sizeof(rank_text), "%u", rank);
    } else if (rank == kSkip) {
        std::snprintf(rank_text, sizeof(rank_text), "S");
    } else if (rank == kReverse) {
        // Never produced by Start, but kept here so any leftover byte still
        // formats sensibly in logs.
        std::snprintf(rank_text, sizeof(rank_text), "V");
    } else {
        std::snprintf(rank_text, sizeof(rank_text), "+2");
    }
    const char color_text = color < 4 ? kColors[color] : '?';
    return std::string(1, color_text) + rank_text;
}