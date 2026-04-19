# メモリ確保ガイドライン (ESP32-P4 + PSRAM)

ESP32-P4 (M5Stack Tab5) のように PSRAM を積んでいるボード特有の、静的バッファ確保と動的 malloc に関する注意点をまとめる。過去の事故例と、それを踏まえた推奨パターンを記載する。

## TL;DR

- **大きな inline 固定バッファを struct に持たせた static 変数は禁物**。内部 DRAM .bss を食って起動時のメモリレイアウトを壊し、最悪「画面真っ黒・シリアルだけ正常」という症状を起こす。
- **データバイトを溜める系の可変長バッファは、必要になった時だけ `malloc()` し、使い終わったら `free()`** する。
- ESP32-P4 では `heap_pool[4 MiB]` が `EXT_RAM_BSS_ATTR` で PSRAM に置かれている。この配置に影響する変更 (大きな BSS を追加する等) は慎重に。

## 事故例: SysEx パーサで画面が起動しなくなった (2026-04-19)

### 症状

`picoruby-midi` に SysEx パーサを追加した後、M5Stack Tab5 にファームを焼くと:

- シリアル (`idf.py monitor`) を見る限り起動シーケンスは最後まで通っている
  (`MIDI setup complete - ready for PicoRuby control` まで出る)
- **しかし LCD が真っ暗のまま。UI も描画されない。**

Seaboard BLOCK を外しても再現。ハードの電源問題ではなかった。
`git stash` でこのコミットを退避してビルドし直すと正常に起動した。

### 原因

`midi.c` にこういう static 変数を追加していた:

```c
typedef struct {
    bool active;
    bool truncated;
    uint16_t len;
    uint8_t buf[MIDI_SYSEX_MAX_LEN];  // 512 byte inline buffer
} sysex_accumulator_t;

static sysex_accumulator_t g_usb_sysex = {0};  // 516 byte in .bss
static sysex_accumulator_t g_sam_sysex = {0};  // 516 byte in .bss
```

`sizeof(sysex_accumulator_t)` が 516 byte になる struct を static として 2 個置いただけで、
内部 DRAM の `.bss` が合計 ~1 KiB 増える。これが他の static allocation
(特に `EXT_RAM_BSS_ATTR uint8_t heap_pool[4*1024*1024]` など) との配置関係を壊し、
LCD ドライバあたりの初期化が破綻していたと推測。

(heap 領域自体の先頭アドレス `_heap_start_low = 0x4ff26750` は前後で変わらなかったが、
中間の BSS 配置とアライメント境界が変わり、何らかの副作用が発生。)

### 対処

static にしていた inline buffer を**ヒープ動的確保**に切り替えた:

```c
typedef struct {
    bool active;
    bool truncated;
    uint16_t len;
    uint8_t *buf;   // NULL 初期値。F0 受信時に malloc、完了時に free/所有権移譲
} sysex_accumulator_t;

static sysex_accumulator_t g_usb_sysex = {0};  // わずか 12 byte
static sysex_accumulator_t g_sam_sysex = {0};  // わずか 12 byte
```

SysEx 進行中だけ 512 byte を消費し、それ以外はゼロ。
結果として `.bss` 増分は ~1 KiB → ~24 byte に激減、LCD も正常に表示されるようになった。

コミット: `components/picoruby-esp32/picoruby/mrbgems/picoruby-midi/ports/esp32/midi.c`

## ガイドライン

### 1. static 変数に大きな inline バッファを持たせない

NG:
```c
typedef struct {
    int meta;
    uint8_t buffer[1024];  // 固定サイズ
} foo_t;
static foo_t g_foo;  // .bss を 1KB+ 使う
```

OK:
```c
typedef struct {
    int meta;
    uint8_t *buffer;  // 必要な時だけ malloc
} foo_t;
static foo_t g_foo = {0};  // .bss は sizeof(ポインタ+meta) のみ
```

目安: 数十 byte までなら inline でも OK。数百 byte〜を超えるならヒープへ。
特に同種のインスタンスを複数 static で持つとき (`g_usb_xxx` と `g_sam_xxx` みたいな並列構成) は、
自乗で効いてくるのでより注意。

### 2. 溜め込み系バッファはライフサイクルを明示

- **開始**: 初めてデータが来たタイミングで `malloc()` (ESP-IDF の通常 malloc で OK)
- **追記**: `len` をインクリメントしつつ `cap` を超えないようガード
- **完了**: consumer にポインタを所有権ごと渡すか、`free()` してポインタを `NULL` に
- **リセット/異常終了**: `free()` を忘れずに

`picoruby-midi/ports/esp32/midi.c` の `sysex_reset()` / `sysex_begin()` /
`finalize_sysex_event()` のセットが参考になる。

### 3. malloc はデフォルトで内部 DRAM に落ちる (< 4 KiB の場合)

このプロジェクトの `sdkconfig`:

```
CONFIG_SPIRAM_USE_MALLOC=y
CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096
```

なので:

- `malloc(N)` (N < 4 KiB) → 内部 DRAM (高速、DMA 可)
- `malloc(N)` (N ≥ 4 KiB) → PSRAM (低速、DMA 制約あり)
- 明示的に PSRAM へ: `heap_caps_malloc(N, MALLOC_CAP_SPIRAM)`
- 明示的に 内部 RAM へ: `heap_caps_malloc(N, MALLOC_CAP_INTERNAL)`
- DMA 用: `heap_caps_malloc(N, MALLOC_CAP_DMA)`

MIDI 入力のように低レイテンシが欲しい用途では、内部 DRAM に落ちるサイズ (< 4 KiB)
を単位にするのが無難。大きいバッファを使うなら `heap_caps_*` を明示する。

### 4. PSRAM に置きたい場合は `EXT_RAM_BSS_ATTR`

大きな static バッファ (数十 KiB 以上) は PSRAM に置く:

```c
EXT_RAM_BSS_ATTR uint8_t huge_buffer[256 * 1024];
```

- 内部 DRAM を圧迫しない
- ただしアクセス速度は遅い、DMA 用途には不適
- 起動後 PSRAM 初期化 (`esp_psram: SPI SRAM memory test OK` のログ) が終わるまでは使用不可

プロジェクト内の例: `components/picoruby-esp32/picoruby-esp32.c` の
```c
EXT_RAM_BSS_ATTR uint8_t heap_pool[HEAP_SIZE];  // ESP32-P4 は 4 MiB
```

### 5. 変更の影響を確認するコマンド

内部 DRAM 使用量や BSS の内訳を調べるとき:

```bash
# 各セクションのサイズ
riscv32-esp-elf-size -A build/midori.elf

# 特定の .o の中の大きな BSS 変数を探す
riscv32-esp-elf-objdump -h build/esp-idf/.../your_file.c.obj | grep -E "bss|data"

# 特定シンボルの最終アドレス
riscv32-esp-elf-nm build/midori.elf | grep my_global
```

起動ログ中の以下の値も参考:

```
I (1437) heap_init: At 4FF26750 len 00014870 (82 KiB): RAM   ← 内部ヒープ先頭と容量
I (1438) esp_psram: Adding pool of 28672K of PSRAM memory to heap allocator
```

static 変数を追加/削除した後、`_heap_start_low` のアドレスがどう動いたかを前後比較すると、
内部 DRAM への影響度がわかる。

## 関連ドキュメント

- [PICORUBY_SUPERVISOR.md](PICORUBY_SUPERVISOR.md) - PicoRuby VM のヒーププール (`heap_pool[4 MiB]`) を PSRAM に配置している件
- ESP-IDF 公式: [Support for External RAM](https://docs.espressif.com/projects/esp-idf/en/latest/esp32p4/api-guides/external-ram.html)
