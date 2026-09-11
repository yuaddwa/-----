# Open Source Review

Scope, audit trail, and known gaps for `FoloToy DUEL`.

## What this project is

A standalone firmware for the FoloToy AI Passport ESP32-C3 device. Two
devices pair over ESP-NOW and play a trimmed-down, two-player only card
duel. No internet, no MQTT, no cloud SDK, no AI service.

## What this project is NOT

* Not a port of the upstream `folotoy/ai-passport` repository. We do not
  fork that baseline; the BSP and LVGL glue are reimplemented from the
  upstream `Acolevia/ai-passport-uno` reference so the build is
  self-contained.
* Not compatible on the wire with `ai-passport-uno` v3. The protocol
  version was bumped to 4 because the snapshot payload no longer carries
  the `uno_target_id` and `wild_draw_four_target_id` fields, and the
  action enum dropped the two `*WildDrawFour` opcodes.

## Files reviewed line-by-line

| Path | Review notes |
|---|---|
| `main/folotoy_uno_duel.h` / `.cc` | Pure game logic. No hardware or FreeRTOS dependency. Reviewed for stale state across `Play` / `Draw` / `AutoDrawCurrentPlayerIfBlocked`; manual cards-on-table trace shows no leakage of `uno_target_id` because the field was deleted. |
| `main/folotoy_local_room.h` / `.cc` | ESP-NOW protocol, room state machine, packet checksum. Verified the `kProtocolVersion = 4` is consistent across all call sites. Verified the third-member rejection path in `AddOrRefreshMember`. |
| `main/local_room_ui.h` / `.cpp` | LVGL renderer. All LVGL create calls are inside `lvgl_port_lock`/`unlock`. Wildcard card pips use 4 corner dots; verified the rotation / arc-drop math for fan layouts. |
| `main/passport_board.h` / `.cpp` | ST7789 panel bring-up + ADC button driver. GPIO numbers match upstream. |
| `main/turn_tone.h` / `.cpp` | ES8311 codec over I2C + I2S. Sine envelope math is unchanged from upstream. |
| `main/app_main.cpp` | FreeRTOS queue + event dispatch + turn-boundary chime trigger. Removed `OnDoubleConfirm` because the duel no longer needs a quick-start helper. |
| `main/lv_font_folotoy_cn_18.c` | Placeholder font. Falls back to `lv_font_montserrat_14` so the firmware boots cleanly. |
| `main/CMakeLists.txt` / `idf_component.yml` | Matches upstream dependency set exactly. |

## Behavioural changes vs. upstream UNO

1. **Two-player hard cap.** `FoloToyLocalRoom::AddOrRefreshMember` rejects
   a third device. Behaviour change: a third device that presses Confirm
   in the lobby sees "房间已满（仅支持两人）" instead of joining silently.
2. **No Reverse.** `FoloToyUnoDuel::Play` does not branch on
   `kReverse`; the rank value is reserved but never produced by
   `Start`.
3. **No UNO call window.** `FoloToyUnoDuel` no longer tracks
   `uno_target_id`. `FoloToyLocalRoom::Tick` no longer schedules a
   `CloseUnoWindow` deadline. The UI no longer shows the "喊 UNO / 抓漏喊"
   button.
4. **No WildDrawFour challenge.** `FoloToyUnoDuel::Play` for
   `kWildDrawFour` draws 4 to the target and skips them. There is no
   `ResolveWildDrawFour` method. The action enum no longer carries
   `kActionAcceptWildDrawFour` / `kActionChallengeWildDrawFour`. The UI
   no longer shows the "接受 +4 / 质疑" button row.

## Known limitations / unresolved gaps

* The Chinese font shipped in `main/lv_font_folotoy_cn_18.c` is a
  placeholder that falls back to Montserrat 14. Run
  `python tools/generate_ui_font.py` (Pillow + a TrueType font) or use
  the LVGL online font converter to populate real Chinese glyphs.
* No offline / drop detection. If a peer disappears mid-duel the room
  stays in its last state until the host times out and clears the session.
* No NVS persistence. Boot state is rebuilt from scratch on every reset.
* The protocol has no packet acknowledgement or replay protection. A
  noisy channel can produce a stale action being applied. Mitigated by the
  game class rejecting out-of-turn plays, but no retry / dedupe at the
  transport layer.
* Only ESP-IDF 5.5.3 has been compiled. `idf: ">=5.5,<6.1"` is the
  declared range; 6.0.x has not been verified.

## Validation status

* Game logic: traced by hand for all branches of `Start`, `Play`, `Draw`,
  `AutoDrawCurrentPlayerIfBlocked`.
* Build system: `idf.py set-target esp32c3` and `idf.py build` succeed on
  ESP-IDF 5.5.3 in CI for the upstream project. Not yet executed for
  this fork on a fresh machine.
* Hardware: not bench-tested. The two-device ESP-NOW pairing, button
  responsiveness, audio chime, and panel refresh rate have not been
  validated end-to-end. Treat this firmware as experimental.