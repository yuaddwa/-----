#include "folotoy_local_room.h"

#include <esp_idf_version.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <esp_wifi.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <utility>

namespace {

constexpr char kTag[] = "FoloToyLocalRoom";
constexpr uint32_t kPacketMagic = 0x314f4c46;  // "FLO1" in little endian.
constexpr uint8_t kProtocolVersion = 4;
constexpr int64_t kDiscoveryWindowUs = 2 * 1000 * 1000;
constexpr int64_t kBroadcastIntervalUs = 800 * 1000;
constexpr int64_t kMemberTimeoutUs = 6 * 1000 * 1000;
constexpr int64_t kHostTimeoutUs = 6 * 1000 * 1000;
constexpr std::array<uint8_t, 6> kBroadcastAddress = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
constexpr std::array<const char*, 6> kQuickPhrases = {
    "你好！", "一起 DUEL", "准备好了", "好牌！", "轮到你了", "再见",
};

enum PacketType : uint8_t {
    kPacketDiscovery = 1,
    kPacketBeacon = 2,
    kPacketJoin = 3,
    kPacketQuickChat = 4,
    kPacketGameAction = 5,
    kPacketGameSnapshot = 6,
};

// The duel build only carries Play / Draw. Call-uno / catch-uno and the
// WildDrawFour challenge were deliberately removed for the simplified duel.
enum GameAction : uint8_t {
    kActionPlay = 1,
    kActionDraw = 2,
};

#pragma pack(push, 1)
struct PacketHeader {
    uint32_t magic;
    uint8_t version;
    uint8_t type;
    uint8_t payload_size;
    uint8_t reserved;
    uint32_t room_id;
    uint32_t sender_id;
    uint32_t target_id;
    uint32_t sequence;
    uint16_t checksum;
};

struct PresencePayload {
    uint32_t host_id;
    uint8_t player_count;
    uint8_t game_started;
    uint8_t wifi_channel;
    uint32_t player_ids[FoloToyUnoDuel::kMaxPlayers];
};

struct QuickChatPayload {
    uint8_t phrase;
};

struct GameActionPayload {
    uint8_t action;
    uint8_t card;
    uint8_t wild_color;
};

struct GameSnapshotPayload {
    uint32_t host_id;
    uint32_t turn_player_id;
    uint32_t winner_id;
    uint8_t top_card;
    uint8_t current_color;
    uint8_t draw_pile_size;
    uint8_t player_count;
    uint8_t hand_size;
    uint8_t hand[FoloToyUnoDuel::kMaxHandCards];
    uint32_t player_ids[FoloToyUnoDuel::kMaxPlayers];
};
#pragma pack(pop)

static_assert(sizeof(PacketHeader) + sizeof(GameSnapshotPayload) <= 128,
              "Local room packets must fit the ESP-NOW v1 payload budget");

uint16_t CalculateChecksum(const uint8_t* bytes, size_t size) {
    uint16_t checksum = 0xffff;
    for (size_t index = 0; index < size; ++index) {
        checksum ^= bytes[index];
        for (int bit = 0; bit < 8; ++bit) {
            checksum = (checksum & 1) ? (checksum >> 1) ^ 0xa001 : checksum >> 1;
        }
    }
    return checksum;
}

std::string RoomName(uint32_t room_id) {
    char text[24];
    std::snprintf(text, sizeof(text), "决斗房 %04lX", static_cast<unsigned long>(room_id & 0xffff));
    return text;
}

char ColorName(uint8_t color) {
    static constexpr char kColors[] = {'R', 'Y', 'G', 'B'};
    return color < sizeof(kColors) ? kColors[color] : '?';
}

}  // namespace

FoloToyLocalRoom* FoloToyLocalRoom::instance_ = nullptr;

FoloToyLocalRoom::FoloToyLocalRoom(UiCallback ui_callback, GameUiCallback game_ui_callback)
    : ui_callback_(std::move(ui_callback)), game_ui_callback_(std::move(game_ui_callback)) {
    receive_queue_ = xQueueCreate(kQueueDepth, sizeof(ReceivedPacket));
    if (receive_queue_ != nullptr) {
        xTaskCreate(WorkerEntry, "folotoy_room", 5120, this, 4, &worker_task_);
    }
}

FoloToyLocalRoom::~FoloToyLocalRoom() {
    Stop();
    if (worker_task_ != nullptr) {
        vTaskDelete(worker_task_);
    }
    if (receive_queue_ != nullptr) {
        vQueueDelete(receive_queue_);
    }
}

void FoloToyLocalRoom::WorkerEntry(void* argument) {
    static_cast<FoloToyLocalRoom*>(argument)->WorkerLoop();
}

void FoloToyLocalRoom::WorkerLoop() {
    while (true) {
        ReceivedPacket received;
        if (xQueueReceive(receive_queue_, &received, pdMS_TO_TICKS(200)) == pdTRUE) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (active_) {
                HandlePacket(received, esp_timer_get_time());
            }
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_) {
            Tick(esp_timer_get_time());
        }
    }
}

bool FoloToyLocalRoom::InitializeTransport() {
    if (receive_queue_ == nullptr) {
        return false;
    }

    wifi_mode_t mode = WIFI_MODE_NULL;
    if (esp_wifi_get_mode(&mode) != ESP_OK || mode == WIFI_MODE_NULL) {
        ESP_LOGE(kTag, "Wi-Fi is not started");
        return false;
    }

    uint8_t secondary_channel = 0;
    wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE;
    if (esp_wifi_get_channel(&wifi_channel_, &secondary) != ESP_OK) {
        wifi_channel_ = 0;
    }
    secondary_channel = static_cast<uint8_t>(secondary);
    ESP_LOGI(kTag, "Starting ESP-NOW in Wi-Fi mode %d on channel %u/%u", mode, wifi_channel_,
             secondary_channel);

    const esp_err_t init_error = esp_now_init();
    if (init_error != ESP_OK) {
        ESP_LOGE(kTag, "esp_now_init failed: %s", esp_err_to_name(init_error));
        return false;
    }

    instance_ = this;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    const esp_err_t callback_error = esp_now_register_recv_cb(
        [](const esp_now_recv_info_t*, const uint8_t* data, int size) {
            FoloToyLocalRoom* room = instance_;
            if (room == nullptr || room->receive_queue_ == nullptr || data == nullptr || size <= 0 ||
                size > static_cast<int>(kMaxPacketSize)) {
                return;
            }
            ReceivedPacket packet;
            packet.size = static_cast<uint16_t>(size);
            std::memcpy(packet.data.data(), data, size);
            xQueueSend(room->receive_queue_, &packet, 0);
        });
#else
    const esp_err_t callback_error = esp_now_register_recv_cb(
        [](const uint8_t*, const uint8_t* data, int size) {
            FoloToyLocalRoom* room = instance_;
            if (room == nullptr || room->receive_queue_ == nullptr || data == nullptr || size <= 0 ||
                size > static_cast<int>(kMaxPacketSize)) {
                return;
            }
            ReceivedPacket packet;
            packet.size = static_cast<uint16_t>(size);
            std::memcpy(packet.data.data(), data, size);
            xQueueSend(room->receive_queue_, &packet, 0);
        });
#endif
    if (callback_error != ESP_OK) {
        ESP_LOGE(kTag, "esp_now_register_recv_cb failed: %s", esp_err_to_name(callback_error));
        instance_ = nullptr;
        esp_now_deinit();
        return false;
    }

    esp_now_peer_info_t peer = {};
    std::memcpy(peer.peer_addr, kBroadcastAddress.data(), kBroadcastAddress.size());
    peer.channel = 0;
    peer.encrypt = false;
    peer.ifidx = mode == WIFI_MODE_AP ? WIFI_IF_AP : WIFI_IF_STA;
    const esp_err_t peer_error = esp_now_add_peer(&peer);
    if (peer_error != ESP_OK && peer_error != ESP_ERR_ESPNOW_EXIST) {
        ESP_LOGE(kTag, "esp_now_add_peer failed: %s", esp_err_to_name(peer_error));
        esp_now_unregister_recv_cb();
        instance_ = nullptr;
        esp_now_deinit();
        return false;
    }
    return true;
}

void FoloToyLocalRoom::DeinitializeTransport() {
    instance_ = nullptr;
    esp_now_unregister_recv_cb();
    esp_now_del_peer(kBroadcastAddress.data());
    esp_now_deinit();
    if (receive_queue_ != nullptr) {
        xQueueReset(receive_queue_);
    }
}

bool FoloToyLocalRoom::Start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_) {
        return true;
    }

    uint8_t mac[6] = {};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
        Notify("决斗房", "system", "无法读取设备身份");
        return false;
    }
    self_id_ = (static_cast<uint32_t>(mac[2]) << 24) |
               (static_cast<uint32_t>(mac[3]) << 16) |
               (static_cast<uint32_t>(mac[4]) << 8) | mac[5];
    if (self_id_ == 0) {
        self_id_ = 1;
    }

    if (!InitializeTransport()) {
        Notify("决斗房", "system", "无线网络尚未就绪，请稍后重试");
        return false;
    }

    ResetSession();
    active_ = true;
    started_at_us_ = esp_timer_get_time();
    last_broadcast_us_ = 0;
    AddOrRefreshMember(self_id_, started_at_us_);
    Notify("正在匹配对手", "system", "请让另一台设备按确认键；长按退出");
    BroadcastDiscovery();
    return true;
}

void FoloToyLocalRoom::Stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_) {
        return;
    }
    active_ = false;
    DeinitializeTransport();
    ResetSession();
    Notify("待机", "system", "已退出决斗房");
}

void FoloToyLocalRoom::ResetSession() {
    members_ = {};
    player_ids_ = {};
    member_count_ = 0;
    local_hand_ = {};
    local_hand_size_ = 0;
    selected_index_ = 0;
    selected_phrase_ = 0;
    top_card_ = 0;
    current_color_ = 0;
    draw_pile_size_ = 0;
    player_count_ = 0;
    room_id_ = 0;
    host_id_ = 0;
    turn_player_id_ = 0;
    winner_id_ = 0;
    sequence_ = 0;
    last_host_seen_us_ = 0;
    is_host_ = false;
    game_started_ = false;
    choosing_wild_color_ = false;
    selected_wild_color_ = 0;
    game_ = FoloToyUnoDuel();
}

bool FoloToyLocalRoom::SendPacket(uint8_t type, uint32_t target_id, const void* payload,
                                  size_t payload_size) {
    if (!active_ || payload_size > kMaxPacketSize - sizeof(PacketHeader)) {
        return false;
    }

    std::array<uint8_t, kMaxPacketSize> bytes = {};
    auto* header = reinterpret_cast<PacketHeader*>(bytes.data());
    header->magic = kPacketMagic;
    header->version = kProtocolVersion;
    header->type = type;
    header->payload_size = static_cast<uint8_t>(payload_size);
    header->room_id = room_id_;
    header->sender_id = self_id_;
    header->target_id = target_id;
    header->sequence = ++sequence_;
    if (payload_size > 0 && payload != nullptr) {
        std::memcpy(bytes.data() + sizeof(PacketHeader), payload, payload_size);
    }
    const size_t packet_size = sizeof(PacketHeader) + payload_size;
    header->checksum = 0;
    header->checksum = CalculateChecksum(bytes.data(), packet_size);
    const esp_err_t error = esp_now_send(kBroadcastAddress.data(), bytes.data(), packet_size);
    if (error != ESP_OK) {
        ESP_LOGW(kTag, "esp_now_send failed: %s", esp_err_to_name(error));
        return false;
    }
    return true;
}

void FoloToyLocalRoom::BroadcastDiscovery() {
    PresencePayload payload = {};
    payload.host_id = host_id_;
    payload.player_count = static_cast<uint8_t>(member_count_);
    payload.game_started = static_cast<uint8_t>(game_started_);
    payload.wifi_channel = wifi_channel_;
    std::copy(player_ids_.begin(), player_ids_.end(), payload.player_ids);
    SendPacket(kPacketDiscovery, 0, &payload, sizeof(payload));
}

void FoloToyLocalRoom::BroadcastBeacon() {
    PresencePayload payload = {};
    payload.host_id = host_id_;
    payload.player_count = static_cast<uint8_t>(member_count_);
    payload.game_started = static_cast<uint8_t>(game_started_);
    payload.wifi_channel = wifi_channel_;
    std::copy(player_ids_.begin(), player_ids_.end(), payload.player_ids);
    SendPacket(kPacketBeacon, 0, &payload, sizeof(payload));
}

void FoloToyLocalRoom::SendJoin() {
    PresencePayload payload = {};
    payload.host_id = host_id_;
    payload.player_count = 1;
    payload.game_started = static_cast<uint8_t>(game_started_);
    payload.wifi_channel = wifi_channel_;
    payload.player_ids[0] = self_id_;
    SendPacket(kPacketJoin, host_id_, &payload, sizeof(payload));
}

void FoloToyLocalRoom::Tick(int64_t now_us) {
    if (now_us - last_broadcast_us_ < kBroadcastIntervalUs) {
        return;
    }
    last_broadcast_us_ = now_us;

    if (room_id_ == 0) {
        RemoveExpiredMembers(now_us);
        BroadcastDiscovery();
        if (now_us - started_at_us_ >= kDiscoveryWindowUs) {
            uint32_t lowest_id = self_id_;
            for (size_t index = 0; index < member_count_; ++index) {
                if (members_[index].id != 0) {
                    lowest_id = std::min(lowest_id, members_[index].id);
                }
            }
            if (lowest_id == self_id_) {
                CreateRoom(now_us);
            }
        }
        return;
    }

    if (is_host_) {
        RemoveExpiredMembers(now_us);
        BroadcastBeacon();
        if (game_started_) {
            SendSnapshots();
        }
    } else {
        if (last_host_seen_us_ != 0 && now_us - last_host_seen_us_ > kHostTimeoutUs) {
            Notify("决斗房", "system", "房主已离开，正在重新匹配");
            ResetSession();
            started_at_us_ = now_us;
            AddOrRefreshMember(self_id_, now_us);
            BroadcastDiscovery();
            return;
        }
        SendJoin();
    }
}

void FoloToyLocalRoom::CreateRoom(int64_t now_us) {
    room_id_ = self_id_;
    host_id_ = self_id_;
    is_host_ = true;
    last_host_seen_us_ = now_us;
    AddOrRefreshMember(self_id_, now_us);
    std::array<Member, kMaxMembers> ordered_members = {};
    size_t ordered_count = 0;
    ordered_members[ordered_count++] = {self_id_, now_us};
    for (size_t index = 0; index < member_count_; ++index) {
        if (members_[index].id != self_id_) {
            ordered_members[ordered_count++] = members_[index];
        }
    }
    members_ = ordered_members;
    member_count_ = ordered_count;
    SyncPlayerIdsFromMembers();
    ESP_LOGI(kTag, "Created duel room %08lx", static_cast<unsigned long>(room_id_));
    RenderLobby();
    BroadcastBeacon();
}

void FoloToyLocalRoom::JoinRoom(uint32_t room_id, uint32_t host_id, int64_t now_us) {
    if (room_id == 0 || host_id == 0) {
        return;
    }
    room_id_ = room_id;
    host_id_ = host_id;
    is_host_ = host_id == self_id_;
    last_host_seen_us_ = now_us;
    members_ = {};
    member_count_ = 0;
    player_ids_ = {};
    player_ids_[0] = host_id_;
    if (host_id_ != self_id_) {
        player_ids_[1] = self_id_;
    }
    AddOrRefreshMember(self_id_, now_us);
    AddOrRefreshMember(host_id_, now_us);
    ESP_LOGI(kTag, "Joined duel room %08lx hosted by %08lx", static_cast<unsigned long>(room_id_),
             static_cast<unsigned long>(host_id_));
    RenderLobby();
    SendJoin();
}

bool FoloToyLocalRoom::AddOrRefreshMember(uint32_t member_id, int64_t now_us) {
    if (member_id == 0) {
        return false;
    }
    for (size_t index = 0; index < member_count_; ++index) {
        if (members_[index].id == member_id) {
            members_[index].last_seen_us = now_us;
            return true;
        }
    }
    if (game_started_) {
        return false;
    }
    // The duel build caps at two: refuse a third device so the room never
    // silently degrades into an unsupported three-handed match.
    if (member_count_ >= kMaxMembers) {
        Notify("决斗房", "system", "房间已满（仅支持两人）");
        return false;
    }
    members_[member_count_++] = {member_id, now_us};
    if (is_host_) {
        SyncPlayerIdsFromMembers();
    }
    if (room_id_ != 0) {
        RenderLobby();
    }
    return true;
}

void FoloToyLocalRoom::RemoveExpiredMembers(int64_t now_us) {
    if (game_started_) {
        return;
    }
    const size_t old_count = member_count_;
    size_t output = 0;
    for (size_t index = 0; index < member_count_; ++index) {
        if (members_[index].id == self_id_ || now_us - members_[index].last_seen_us <= kMemberTimeoutUs) {
            members_[output++] = members_[index];
        }
    }
    member_count_ = output;
    if (is_host_) {
        SyncPlayerIdsFromMembers();
    }
    if (member_count_ != old_count) {
        RenderLobby();
    }
}

void FoloToyLocalRoom::SyncPlayerIdsFromMembers() {
    player_ids_ = {};
    for (size_t index = 0; index < member_count_; ++index) {
        player_ids_[index] = members_[index].id;
    }
}

void FoloToyLocalRoom::HandlePacket(const ReceivedPacket& received, int64_t now_us) {
    if (received.size < sizeof(PacketHeader)) {
        return;
    }
    PacketHeader header;
    std::memcpy(&header, received.data.data(), sizeof(header));
    if (header.magic != kPacketMagic || header.version != kProtocolVersion ||
        header.sender_id == 0 || header.sender_id == self_id_ ||
        header.payload_size > kMaxPacketSize - sizeof(PacketHeader) ||
        received.size != sizeof(PacketHeader) + header.payload_size) {
        return;
    }

    std::array<uint8_t, kMaxPacketSize> checksum_bytes = received.data;
    reinterpret_cast<PacketHeader*>(checksum_bytes.data())->checksum = 0;
    if (CalculateChecksum(checksum_bytes.data(), received.size) != header.checksum) {
        return;
    }
    if (header.target_id != 0 && header.target_id != self_id_) {
        return;
    }

    const uint8_t* payload = received.data.data() + sizeof(PacketHeader);
    if (header.type == kPacketDiscovery) {
        if (header.payload_size != sizeof(PresencePayload)) {
            return;
        }
        PresencePayload presence;
        std::memcpy(&presence, payload, sizeof(presence));
        if (room_id_ == 0) {
            AddOrRefreshMember(header.sender_id, now_us);
            if (header.room_id != 0 && presence.host_id != 0) {
                JoinRoom(header.room_id, presence.host_id, now_us);
            }
        } else if (is_host_) {
            BroadcastBeacon();
        }
        return;
    }

    if (header.type == kPacketBeacon) {
        if (header.payload_size != sizeof(PresencePayload)) {
            return;
        }
        PresencePayload presence;
        std::memcpy(&presence, payload, sizeof(presence));
        if (room_id_ == 0) {
            JoinRoom(header.room_id, presence.host_id, now_us);
            if (presence.player_count <= player_ids_.size()) {
                std::copy(std::begin(presence.player_ids), std::end(presence.player_ids),
                          player_ids_.begin());
                RenderLobby();
            }
        } else if (header.room_id == room_id_ && header.sender_id == host_id_) {
            last_host_seen_us_ = now_us;
            const bool player_count_changed = player_count_ != presence.player_count;
            player_count_ = presence.player_count;
            if (presence.player_count <= player_ids_.size()) {
                std::copy(std::begin(presence.player_ids), std::end(presence.player_ids),
                          player_ids_.begin());
            }
            if (!game_started_ && presence.game_started) {
                game_started_ = true;
            }
            if (!game_started_ && player_count_changed) {
                RenderLobby();
            }
        }
        return;
    }

    if (header.room_id == 0 || header.room_id != room_id_) {
        return;
    }
    AddOrRefreshMember(header.sender_id, now_us);

    if (header.type == kPacketJoin && is_host_) {
        SendSnapshot(header.sender_id);
    } else if (header.type == kPacketQuickChat &&
               header.payload_size == sizeof(QuickChatPayload)) {
        QuickChatPayload chat;
        std::memcpy(&chat, payload, sizeof(chat));
        if (chat.phrase < kQuickPhrases.size()) {
            Notify(RoomName(room_id_), "assistant",
                   PlayerDisplayName(header.sender_id) + "：" + kQuickPhrases[chat.phrase]);
        }
    } else if (header.type == kPacketGameAction && is_host_) {
        HandleGameAction(header.sender_id, payload, header.payload_size);
    } else if (header.type == kPacketGameSnapshot && header.sender_id == host_id_) {
        last_host_seen_us_ = now_us;
        ApplyLocalSnapshot(payload, header.payload_size);
    }
}

void FoloToyLocalRoom::StartGame() {
    if (!is_host_ || game_started_) {
        return;
    }
    if (member_count_ < 2) {
        Notify(RoomName(room_id_), "system", "需要 2 名玩家才能开始");
        return;
    }
    std::array<uint32_t, FoloToyUnoDuel::kMaxPlayers> ids = {};
    for (size_t index = 0; index < member_count_; ++index) {
        ids[index] = members_[index].id;
    }
    if (!game_.Start(ids.data(), esp_random())) {
        Notify(RoomName(room_id_), "system", "DUEL 初始化失败");
        return;
    }
    game_started_ = true;
    player_count_ = static_cast<uint8_t>(member_count_);
    selected_index_ = 0;
    ResolveAutomaticDraws();
    SendSnapshots();
    RenderGame();
}

void FoloToyLocalRoom::HandleGameAction(uint32_t player_id, const uint8_t* payload, size_t size) {
    if (!game_started_ || size != sizeof(GameActionPayload)) {
        return;
    }
    GameActionPayload action;
    std::memcpy(&action, payload, sizeof(action));
    bool changed = false;
    if (action.action == kActionPlay) {
        changed = game_.Play(player_id, action.card, action.wild_color);
    } else if (action.action == kActionDraw) {
        changed = game_.Draw(player_id);
    }
    if (changed) {
        ResolveAutomaticDraws();
        SendSnapshots();
        RenderGame();
    }
}

void FoloToyLocalRoom::ResolveAutomaticDraws() {
    for (size_t index = 0; index < FoloToyUnoDuel::kDeckCards; ++index) {
        if (!game_.AutoDrawCurrentPlayerIfBlocked()) {
            break;
        }
    }
}

void FoloToyLocalRoom::SendSnapshots() {
    if (!is_host_ || !game_started_) {
        return;
    }
    for (size_t index = 0; index < member_count_; ++index) {
        SendSnapshot(members_[index].id);
    }
}

void FoloToyLocalRoom::SendSnapshot(uint32_t target_id) {
    if (!is_host_ || !game_started_) {
        return;
    }
    const FoloToyUnoDuel::Player* player = game_.FindPlayer(target_id);
    if (player == nullptr) {
        return;
    }
    GameSnapshotPayload snapshot = {};
    snapshot.host_id = host_id_;
    snapshot.turn_player_id = game_.current_player_id();
    snapshot.winner_id = game_.winner_id();
    snapshot.top_card = game_.top_card();
    snapshot.current_color = game_.current_color();
    snapshot.draw_pile_size = game_.draw_pile_size();
    snapshot.player_count = static_cast<uint8_t>(game_.player_count());
    snapshot.hand_size = player->hand_size;
    std::copy_n(player->hand.begin(), player->hand_size, snapshot.hand);
    std::copy(player_ids_.begin(), player_ids_.end(), snapshot.player_ids);
    SendPacket(kPacketGameSnapshot, target_id, &snapshot, sizeof(snapshot));
    if (target_id == self_id_) {
        ApplyLocalSnapshot(reinterpret_cast<const uint8_t*>(&snapshot), sizeof(snapshot));
    }
}

void FoloToyLocalRoom::ApplyLocalSnapshot(const uint8_t* payload, size_t size) {
    if (size != sizeof(GameSnapshotPayload)) {
        return;
    }
    GameSnapshotPayload snapshot;
    std::memcpy(&snapshot, payload, sizeof(snapshot));
    if (snapshot.host_id != host_id_ || snapshot.hand_size > local_hand_.size() ||
        snapshot.player_count < 2 || snapshot.player_count > FoloToyUnoDuel::kMaxPlayers) {
        return;
    }
    game_started_ = true;
    turn_player_id_ = snapshot.turn_player_id;
    winner_id_ = snapshot.winner_id;
    top_card_ = snapshot.top_card;
    current_color_ = snapshot.current_color;
    draw_pile_size_ = snapshot.draw_pile_size;
    player_count_ = snapshot.player_count;
    local_hand_size_ = snapshot.hand_size;
    std::copy_n(snapshot.hand, local_hand_size_, local_hand_.begin());
    std::copy(std::begin(snapshot.player_ids), std::end(snapshot.player_ids), player_ids_.begin());
    if (selected_index_ >= local_hand_size_) {
        selected_index_ = 0;
    }
    const bool selected_card_is_wild =
        selected_index_ < local_hand_size_ &&
        FoloToyUnoDuel::CardColor(local_hand_[selected_index_]) == 4;
    if (choosing_wild_color_ &&
        (winner_id_ != 0 || turn_player_id_ != self_id_ || !selected_card_is_wild)) {
        choosing_wild_color_ = false;
    }
    RenderGame();
}

const FoloToyUnoDuel::Player* FoloToyLocalRoom::LocalHostPlayer() const {
    return is_host_ ? game_.FindPlayer(self_id_) : nullptr;
}

std::string FoloToyLocalRoom::PlayerDisplayName(uint32_t player_id) const {
    size_t seat = player_ids_.size();
    for (size_t index = 0; index < player_ids_.size(); ++index) {
        if (player_ids_[index] == player_id) {
            seat = index;
            break;
        }
    }
    if (seat == player_ids_.size()) {
        return player_id == self_id_ ? "你" : (player_id == host_id_ ? "房主" : "对手");
    }

    char text[40];
    if (player_id == self_id_ && player_id == host_id_) {
        std::snprintf(text, sizeof(text), "玩家%u（你，房主）", static_cast<unsigned>(seat + 1));
    } else if (player_id == self_id_) {
        std::snprintf(text, sizeof(text), "玩家%u（你）", static_cast<unsigned>(seat + 1));
    } else if (player_id == host_id_) {
        std::snprintf(text, sizeof(text), "玩家%u（房主）", static_cast<unsigned>(seat + 1));
    } else {
        std::snprintf(text, sizeof(text), "玩家%u", static_cast<unsigned>(seat + 1));
    }
    return text;
}

void FoloToyLocalRoom::OnUp() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_) {
        return;
    }
    if (!game_started_) {
        selected_phrase_ = static_cast<uint8_t>((selected_phrase_ + kQuickPhrases.size() - 1) %
                                                kQuickPhrases.size());
        RenderLobby();
        return;
    }
    if (choosing_wild_color_) {
        selected_wild_color_ = static_cast<uint8_t>((selected_wild_color_ + 3) % 4);
        RenderGame();
        return;
    }
    if (local_hand_size_ == 0) {
        RenderGame();
        return;
    }
    const uint8_t choices = local_hand_size_;
    selected_index_ = static_cast<uint8_t>((selected_index_ + choices - 1) % choices);
    RenderGame();
}

void FoloToyLocalRoom::OnDown() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_) {
        return;
    }
    if (!game_started_) {
        selected_phrase_ = static_cast<uint8_t>((selected_phrase_ + 1) % kQuickPhrases.size());
        RenderLobby();
        return;
    }
    if (choosing_wild_color_) {
        selected_wild_color_ = static_cast<uint8_t>((selected_wild_color_ + 1) % 4);
        RenderGame();
        return;
    }
    if (local_hand_size_ == 0) {
        RenderGame();
        return;
    }
    const uint8_t choices = local_hand_size_;
    selected_index_ = static_cast<uint8_t>((selected_index_ + 1) % choices);
    RenderGame();
}

void FoloToyLocalRoom::OnConfirm() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_ || room_id_ == 0) {
        return;
    }
    if (!game_started_) {
        const QuickChatPayload chat = {selected_phrase_};
        SendPacket(kPacketQuickChat, 0, &chat, sizeof(chat));
        Notify(RoomName(room_id_), "user",
               PlayerDisplayName(self_id_) + "：" + kQuickPhrases[selected_phrase_]);
        return;
    }
    if (winner_id_ != 0) {
        return;
    }
    auto submit_action = [this](const GameActionPayload& action) {
        if (is_host_) {
            HandleGameAction(self_id_, reinterpret_cast<const uint8_t*>(&action), sizeof(action));
        } else {
            SendPacket(kPacketGameAction, host_id_, &action, sizeof(action));
        }
    };

    if (turn_player_id_ != self_id_) {
        if (game_ui_callback_) {
            RenderGame();
        } else {
            Notify(RoomName(room_id_), "system", "还没轮到你");
        }
        return;
    }

    GameActionPayload action = {};
    if (selected_index_ >= local_hand_size_) {
        RenderGame();
        return;
    }
    action.action = kActionPlay;
    action.card = local_hand_[selected_index_];
    if (!FoloToyUnoDuel::IsPlayable(action.card, top_card_, current_color_)) {
        if (game_ui_callback_) {
            RenderGame();
        } else {
            Notify(RoomName(room_id_), "system", "这张牌现在不能出");
        }
        return;
    }
    if (FoloToyUnoDuel::CardColor(action.card) == 4 && !choosing_wild_color_) {
        choosing_wild_color_ = true;
        selected_wild_color_ = current_color_ < 4 ? current_color_ : 0;
        RenderGame();
        return;
    }
    action.wild_color = selected_wild_color_;
    choosing_wild_color_ = false;
    submit_action(action);
}

bool FoloToyLocalRoom::OnLongConfirm() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_ || game_started_ || room_id_ == 0 || !is_host_ || member_count_ < 2) {
        return false;
    }
    StartGame();
    return true;
}

void FoloToyLocalRoom::RenderLobby() {
    if (room_id_ == 0) {
        return;
    }
    char status[48];
    std::snprintf(status, sizeof(status), "%s  %u/2", RoomName(room_id_).c_str(),
                  static_cast<unsigned>(is_host_ ? member_count_ : std::max<uint8_t>(player_count_, 1)));
    std::string message = "快捷消息：";
    message += kQuickPhrases[selected_phrase_];
    message += is_host_ ? "\n确认发送，长按开始 DUEL" : "\n确认发送，等待房主开局";
    message += "\n身份：" + PlayerDisplayName(self_id_);
    Notify(status, "system", message);
}

void FoloToyLocalRoom::RenderGame() {
    if (!game_started_) {
        return;
    }
    const FoloToyUnoDuel::Player* host_player = LocalHostPlayer();
    if (host_player != nullptr) {
        local_hand_size_ = host_player->hand_size;
        std::copy_n(host_player->hand.begin(), local_hand_size_, local_hand_.begin());
        turn_player_id_ = game_.current_player_id();
        winner_id_ = game_.winner_id();
        top_card_ = game_.top_card();
        current_color_ = game_.current_color();
        draw_pile_size_ = game_.draw_pile_size();
    }

    if (game_ui_callback_) {
        GameUiState state;
        state.hand = local_hand_;
        state.player_ids = player_ids_;
        state.self_id = self_id_;
        state.host_id = host_id_;
        state.turn_player_id = turn_player_id_;
        state.winner_id = winner_id_;
        state.hand_size = local_hand_size_;
        state.selected_index = selected_index_;
        state.top_card = top_card_;
        state.current_color = current_color_;
        state.draw_pile_size = draw_pile_size_;
        state.player_count = player_count_;
        state.selected_wild_color = selected_wild_color_;
        state.choosing_wild_color = choosing_wild_color_;
        game_ui_callback_(state);
        return;
    }

    std::string status = "DUEL  顶牌 " + FoloToyUnoDuel::CardName(top_card_);
    if (FoloToyUnoDuel::CardColor(top_card_) == 4) {
        status += "  当前色 ";
        status += ColorName(current_color_);
    }
    std::string message;
    if (winner_id_ != 0) {
        message = winner_id_ == self_id_ ? "你赢了！" : PlayerDisplayName(winner_id_) + " 获胜";
    } else if (choosing_wild_color_) {
        message = "上下选择红、黄、绿、蓝，确认出牌";
    } else {
        message = turn_player_id_ == self_id_ ? "轮到你" : "等待 " + PlayerDisplayName(turn_player_id_);
        message += "｜手牌 " + std::to_string(local_hand_size_);
        message += "｜牌堆 " + std::to_string(draw_pile_size_);
        message += "\n选择：";
        message += local_hand_size_ == 0 ? "等待自动摸牌"
                                         : FoloToyUnoDuel::CardName(local_hand_[selected_index_]);
        message += "（上下选择，确认出牌）";
    }
    Notify(status, "system", message);
}

void FoloToyLocalRoom::Notify(const std::string& status, const std::string& role,
                              const std::string& message) {
    if (ui_callback_) {
        ui_callback_(status, role, message);
    }
}