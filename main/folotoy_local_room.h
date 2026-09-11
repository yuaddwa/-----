#ifndef FOLOTOY_LOCAL_ROOM_H_
#define FOLOTOY_LOCAL_ROOM_H_

#include "folotoy_uno_duel.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

// FoloToyLocalRoom glues the room protocol, ESP-NOW transport, and the
// FoloToyUnoDuel game together. The duel build trims the original UNO
// behaviour:
//   * Hard cap of two players. The room rejects extra discovery clients.
//   * No UNO call-and-catch sub-state. The UI state no longer exposes
//     uno_target_id.
//   * No WildDrawFour challenge. The GameAction enum no longer carries
//     AcceptWildDrawFour / ChallengeWildDrawFour. Targets of +4 simply
//     draw four cards and lose their turn.
class FoloToyLocalRoom {
public:
    struct GameUiState {
        std::array<uint8_t, FoloToyUnoDuel::kMaxHandCards> hand = {};
        std::array<uint32_t, FoloToyUnoDuel::kMaxPlayers> player_ids = {};
        uint32_t self_id = 0;
        uint32_t host_id = 0;
        uint32_t turn_player_id = 0;
        uint32_t winner_id = 0;
        uint8_t hand_size = 0;
        uint8_t selected_index = 0;
        uint8_t top_card = 0;
        uint8_t current_color = 0;
        uint8_t draw_pile_size = 0;
        uint8_t player_count = 0;
        uint8_t selected_wild_color = 0;
        bool choosing_wild_color = false;
    };

    using UiCallback = std::function<void(const std::string& status, const std::string& role,
                                          const std::string& message)>;
    using GameUiCallback = std::function<void(const GameUiState& state)>;

    explicit FoloToyLocalRoom(UiCallback ui_callback, GameUiCallback game_ui_callback = {});
    ~FoloToyLocalRoom();

    FoloToyLocalRoom(const FoloToyLocalRoom&) = delete;
    FoloToyLocalRoom& operator=(const FoloToyLocalRoom&) = delete;

    bool Start();
    void Stop();
    bool active() const { return active_; }

    void OnUp();
    void OnDown();
    void OnConfirm();
    bool OnLongConfirm();

private:
    static constexpr size_t kMaxPacketSize = 128;
    static constexpr size_t kQueueDepth = 12;
    static constexpr size_t kMaxMembers = FoloToyUnoDuel::kMaxPlayers;

    struct ReceivedPacket {
        uint16_t size = 0;
        std::array<uint8_t, kMaxPacketSize> data = {};
    };

    struct Member {
        uint32_t id = 0;
        int64_t last_seen_us = 0;
    };

    UiCallback ui_callback_;
    GameUiCallback game_ui_callback_;
    mutable std::mutex mutex_;
    QueueHandle_t receive_queue_ = nullptr;
    TaskHandle_t worker_task_ = nullptr;
    std::array<Member, kMaxMembers> members_ = {};
    std::array<uint32_t, kMaxMembers> player_ids_ = {};
    size_t member_count_ = 0;
    FoloToyUnoDuel game_;
    std::array<uint8_t, FoloToyUnoDuel::kMaxHandCards> local_hand_ = {};
    uint8_t local_hand_size_ = 0;
    uint8_t selected_index_ = 0;
    uint8_t selected_phrase_ = 0;
    uint8_t top_card_ = 0;
    uint8_t current_color_ = 0;
    uint8_t draw_pile_size_ = 0;
    uint8_t player_count_ = 0;
    uint32_t self_id_ = 0;
    uint32_t room_id_ = 0;
    uint32_t host_id_ = 0;
    uint32_t turn_player_id_ = 0;
    uint32_t winner_id_ = 0;
    uint32_t sequence_ = 0;
    int64_t started_at_us_ = 0;
    int64_t last_broadcast_us_ = 0;
    int64_t last_host_seen_us_ = 0;
    uint8_t wifi_channel_ = 0;
    bool active_ = false;
    bool is_host_ = false;
    bool game_started_ = false;
    bool choosing_wild_color_ = false;
    uint8_t selected_wild_color_ = 0;

    static FoloToyLocalRoom* instance_;

    static void WorkerEntry(void* argument);
    void WorkerLoop();
    void Tick(int64_t now_us);
    void HandlePacket(const ReceivedPacket& received, int64_t now_us);
    bool InitializeTransport();
    void DeinitializeTransport();
    void ResetSession();
    void CreateRoom(int64_t now_us);
    void JoinRoom(uint32_t room_id, uint32_t host_id, int64_t now_us);
    bool AddOrRefreshMember(uint32_t member_id, int64_t now_us);
    void RemoveExpiredMembers(int64_t now_us);
    void SyncPlayerIdsFromMembers();
    bool SendPacket(uint8_t type, uint32_t target_id, const void* payload, size_t payload_size);
    void BroadcastDiscovery();
    void BroadcastBeacon();
    void SendJoin();
    void SendSnapshots();
    void SendSnapshot(uint32_t target_id);
    void StartGame();
    void ResolveAutomaticDraws();
    void HandleGameAction(uint32_t player_id, const uint8_t* payload, size_t size);
    void ApplyLocalSnapshot(const uint8_t* payload, size_t size);
    void RenderLobby();
    void RenderGame();
    void Notify(const std::string& status, const std::string& role,
                const std::string& message);
    const FoloToyUnoDuel::Player* LocalHostPlayer() const;
    std::string PlayerDisplayName(uint32_t player_id) const;
};

#endif  // FOLOTOY_LOCAL_ROOM_H_