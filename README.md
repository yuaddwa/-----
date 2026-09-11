# FoloToy AI Passport — UNO DUEL

> 双人简化版 UNO 固件，基于 ESP32-C3 (FoloToy AI Passport) + LVGL v9 + ESP-IDF v5.5.3。
> 纯本地多人对战，无 AI 依赖，离线可用。

## 派生说明

| 仓库 | 关系 |
|---|---|
| [`folotoy/ai-passport`](https://github.com/folotoy/ai-passport) | 上游框架（BSP、UI、构建系统参考） |
| [`Acolevia/ai-passport-uno`](https://github.com/Acolevia/ai-passport-uno) | 上游完整版 UNO（2-6 人，含 UNO 喊话与 +4 质疑） |
| 本仓库 (`uno`) | **下游双人简化版**（基于上游 API 改造） |

## 玩法改动 vs 上游 UNO

| 维度 | 上游 UNO | DUEL 版 |
|---|---|---|
| 玩家 | 2–6 | **锁死 2** |
| 牌组 | 108（4 色 × 25 + Wild×8） | **52**（4 色 × 13 + Wild×4，去 0/重） |
| 起手 | 7 | **5** |
| 牌型 | Skip / Reverse / +2 / Wild / +4 | **Skip / +2 / Wild / +4**（删 Reverse） |
| UNO 喊话窗口 | 3 秒 | **删** |
| +4 质疑 | 有 | **删**（直接摸 4） |
| 协议版本 | 3 | 4（精简包） |

> 完整的改动审计见 [`OPEN_SOURCE_REVIEW.md`](./OPEN_SOURCE_REVIEW.md)。

---

## 目录结构

```
uno-duel/
├── CMakeLists.txt                顶层构建
├── sdkconfig.defaults            ESP-IDF 配置
├── partitions.csv                8MB Flash 分区
├── dependencies.lock             依赖锁定
├── .gitignore
├── LICENSE                       MIT
├── README.md                     本文件
├── OPEN_SOURCE_REVIEW.md         改造审计
├── THIRD_PARTY_NOTICES.md        第三方声明
├── .github/workflows/
│   ├── build.yml                 ★ GitHub Actions 自动编译
│   └── lint.yml                    CI 代码检查
├── scripts/
│   ├── push_to_github.sh         一键推送（Git Bash / WSL）
│   ├── push_to_github.ps1        一键推送（Windows PowerShell）
│   └── check_build.sh            查询构建状态
├── tools/
│   └── generate_ui_font.py       中文字体生成脚本
└── main/                         源代码
    ├── CMakeLists.txt
    ├── idf_component.yml
    ├── app_main.cpp              FreeRTOS 入口
    ├── app_types.h
    ├── passport_board.{h,cpp}    ST7789 LCD + 3 路 ADC 按钮
    ├── turn_tone.{h,cpp}         ES8311 I2S 提示音
    ├── folotoy_uno_duel.{h,cc}   ★ 纯游戏逻辑（无硬件依赖）
    ├── folotoy_local_room.{h,cc} ESP-NOW 房间 + 协议 v4
    ├── local_room_ui.{h,cpp}     LVGL v9 渲染
    └── lv_font_folotoy_cn_18.c   占位中文字体（回退 Montserrat 14）
```

---

## 三种拿到 .bin 的路径

### 路径 A：GitHub Actions 自动编译（**推荐，零本地依赖**）

完整流程图：

```
[本地 uno-duel/ 源码]
        │
        │  push (需要 PAT)
        ▼
[GitHub 仓库 yuaddwa/uno]
        │
        │  触发 workflow
        ▼
[GitHub Actions runner (Ubuntu)]
   ├─ 装 ESP-IDF v5.5.3
   ├─ idf.py set-target esp32c3
   ├─ idf.py build
   └─ 产出 build/*.bin
        │
        ▼
[Artifact: uno-firmware.zip]
   ├─ bootloader.bin
   ├─ partition-table.bin
   ├─ folotoy_uno_duel.bin
   ├─ merged.bin             ← 一键合并烧录用
   └─ SHA256SUMS
```

#### A.1 在 GitHub 网页建空仓

打开 https://github.com/new：
- Repository name: `uno`
- Visibility: Public 或 Private 均可
- **不要**勾选 "Add a README"
- **不要**勾选 "Add .gitignore"
- **不要**勾选 "Choose a license"

点 Create repository。

#### A.2 生成 GitHub PAT

打开 https://github.com/settings/tokens/new （必须登录 yuaddwa）：
- Note: `uno-build`
- Expiration: 选个短期（7 天足够）
- Scopes:
  - ☑ **`repo`**（完整仓库访问，必选）
  - 其他都不要

点 Generate token → **复制 token（形如 `ghp_xxxxxxxxxxxxxxxxxxxx`）**，**只显示一次**。

#### A.3 一键推送

在项目根目录打开 Git Bash 或 PowerShell：

**Git Bash / WSL：**
```bash
cd "C:/Users/ysc02/Desktop/新建文件夹/uno-duel"
bash scripts/push_to_github.sh yuaddwa
# 提示输入 PAT 时粘贴 ghp_xxx...
```

**PowerShell：**
```powershell
cd "C:\Users\ysc02\Desktop\新建文件夹\uno-duel"
powershell -ExecutionPolicy Bypass -File scripts\push_to_github.ps1 -GitHubUser yuaddwa
# 提示输入 PAT 时粘贴 ghp_xxx...
```

脚本会自动：
1. 检查仓库是否存在
2. `git init` + 提交
3. `git push` 用 PAT 认证
4. 推送成功后**立刻把 remote 改回公开 URL**（不保存凭据）

#### A.4 等编译 + 下载 .bin

打开 https://github.com/yuaddwa/uno/actions

- 第一次会显示 'Build ESP32-C3 Firmware' 正在跑
- 编译约 6-10 分钟
- 完成后页面底部 Artifacts 区下载 `uno-firmware.zip`
- 解压拿到：
  - `bootloader.bin` ~ 20KB
  - `partition-table.bin` ~ 3KB
  - `folotoy_uno_duel.bin` ~ 700KB
  - `merged.bin` ~ 720KB（推荐用这个）
  - `SHA256SUMS` 校验文件

也可以用脚本查询构建状态（无需认证）：
```bash
bash scripts/check_build.sh yuaddwa uno
```

#### A.5 烧录到设备

拿到 `merged.bin` 后，**在本机**（或任意带 esptool.py 的机器）烧录：

```bash
# 一次性安装 esptool（只需要这一行）
pip install esptool

# 烧录（合并文件最方便）
esptool.py --chip esp32c3 -p COM18 write_flash 0x0 merged.bin

# 或分批烧录
esptool.py --chip esp32c3 -p COM18 \
  write_flash 0x0     bootloader.bin \
  write_flash 0x8000  partition-table.bin \
  write_flash 0x10000 folotoy_uno_duel.bin
```

> COM 口在 Windows 设备管理器里看（带 "USB-SERIAL CH340" 字样的）。

---

### 路径 B：本机装 ESP-IDF（适合长期开发）

```bash
# 1. 下载 ESP-IDF 离线安装器（约 1.2GB）
#    https://dl.espressif.com/dl/esp-idf/
#    Windows: 跑 esp-idf-tools-setup-x.x.x.exe
#    Linux/macOS: git clone -b v5.5.3 --recursive https://github.com/espressif/esp-idf.git

# 2. 启动 ESP-IDF PowerShell 环境（Windows 安装器创建的快捷方式）
#    或在 bash 里: source $IDF_PATH/export.sh

# 3. 编译
cd uno-duel
idf.py set-target esp32c3
idf.py build

# 4. 烧录
idf.py -p COM18 flash

# 5. 监视日志
idf.py -p COM18 monitor
```

### 路径 C：PlatformIO（备选）

未在本项目验证，但理论上可移植（需将 `main/` 移至 `src/` 并写 `platformio.ini`）。

---

## 在硬件上玩

设备：FoloToy AI Passport（ESP32-C3 + ST7789 240×280 LCD + 3 个 ADC 按钮 + ES8311 音频 + 18650 电池）

1. 长按中间键开机
2. 选 `Host`（房主）或 `Client`（加入）
4. 房主按中间键开始
5. **轮到你时**：左右键翻牌、中间键出牌（无合法牌时长按 = 摸牌）
6. 摸到 Wild/Wild+4 时，按中间键切颜色再出

---

## 本机代码自测

`folotoy_uno_duel.cc` 是**纯 C++ 逻辑**，无任何 ESP-IDF 依赖，可在 PC 上用 g++ 编译测试：

```bash
# Linux / macOS / WSL
g++ -std=c++17 -DFOLOTOY_DUEL_UNIT_TEST \
    main/folotoy_uno_duel.cc \
    -o /tmp/uno_duel_test

# 或者配合 gtest
```

TODO：单元测试代码后续补充。

---

## 故障排查

| 现象 | 原因 | 解法 |
|---|---|---|
| `idf.py` 找不到 | 没装 ESP-IDF 或没 `source export.sh` | 见路径 B 步骤 2 |
| `merged.bin` 烧录后屏幕黑 | LCD 复位脚没拉高 | 检查 `passport_board.cpp` 里 `lcd_io_init` |
| ESP-NOW 找不到对手 | 两个设备没在同一频道 | 两个 `sdkconfig.defaults` 里 `CONFIG_ESP_WIFI_CHANNEL=1` 必须一致 |
| 牌面中文显示成方块 | 占位字体问题 | `python tools/generate_ui_font.py` 生成真字体 |

---

## License

MIT — 见 [`LICENSE`](./LICENSE)。

上游版权归属 [`Acolevia/ai-passport-uno`](https://github.com/Acolevia/ai-passport-uno)（MIT）和 [`folotoy/ai-passport`](https://github.com/folotoy/ai-passport)（MIT）。
完整第三方声明见 [`THIRD_PARTY_NOTICES.md`](./THIRD_PARTY_NOTICES.md)。