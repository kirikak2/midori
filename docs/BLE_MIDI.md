# BLE-MIDI（picoruby-ble_midi_host / picoruby-ble_midi_device）

2026-11-07 起草・実装（段階 0〜4）。段階 0〜3 は CoreS3 の実機で動作を確認（2026-11-07）。
段階 4（受信のタイムスタンプ補正・Ruby の `BLE` との共存）は**実機で動作を確認**（2026-11-07）。
段階 5（Tab5: ESP32-C6 を ESP-Hosted 経由のコントローラに）は **Tab5 の実機で動作を確認**（2026-11-07、§10.1）。CoreS3 / CrowPanel はビルドのみ。

Bluetooth LE MIDI（BLE-MIDI）を MIDoRI のトランスポートとして追加する。USB-MIDI と同じ名前の付け方で 2 つの gem に分ける。

| gem | BLE の役割 | 相手 | USB での対応物 |
|-----|-----------|------|----------------|
| `mrbgems/picoruby-ble_midi_device` | Peripheral（GATT サーバ） | Mac / iPad / PC / スマホの DAW | picoruby-usb_midi_device（PC 側から見える MIDI 機器になる） |
| `mrbgems/picoruby-ble_midi_host` | Central（GATT クライアント） | BLE-MIDI 鍵盤・WIDI などの機器 | picoruby-usb_midi_host（機器をつなぐ側） |

BLE のスタックは picoruby-ble の ESP32 ポート（NimBLE）を使う。upstream ではまだ PR
[picoruby/picoruby#427](https://github.com/picoruby/picoruby/pull/427)（bash0C7/picoruby
`picoruby-ble-esp32-port`、2026-11-07 時点で 7529064、OPEN）なので、それをベースにする。
インターフェースは変わらない見込み。

### 実装の状態（2026-11-07）

| 段階 | 状態 |
|------|------|
| 0 picoruby-ble（PR #427）の組み込み | 済。サブモジュールのローカルブランチ `midori-ble`（§9.1） |
| 1 コーデック・共有オーナー・BLE-MIDI Device | 済。コーデックはホストテスト済み |
| 2 BLE-MIDI Host | 済。**登録表の 32 ビット化は見送り**（§12） |
| 3 画面（Bluetooth 画面・MIDI Devices・Tempo の BLE・設定画面） | 済 |
| 4 タイムスタンプ補正・Ruby の `BLE` との共存 | 済。実機で確認。補正は §8.1、共存は §3.2。ペアリングは見直しのみ（§5.2） |
| 5 Tab5（ESP-Hosted + C6） | 済。Tab5 の実機で確認（§10.1） |

設計からの変更点:

- 共有オーナーに `configure`（再起動しない ble_hs_cfg の調整。SM の設定はこれで入れる）と
  `picoruby_nimble_synced()` を足した。host 側は GATT を持たないので、device が先に動いていても
  host を有効にして device の接続が切れることはない（逆に host が先だと、device を有効にしたときに
  1 回だけホストが立ち上げ直される）
- 外部クロックの BPM を測るため、picoruby-midi に `MIDI_Input_track_clock(bit)` を足した
  （パケットソースにもパーサを付ける。最大 4 つ）
- MIDI Devices のキーは `b` + アドレスの下位 4 バイト（`UI_MIDIDEV_KEY_LEN` が 12 なので 6 バイト全部は入らない）
- 利用者が切った（Disconnect した）機器は、その起動中は自動で再接続しない（もう一度タップすれば戻る）
- スクリプト終了時のクリーンアップ（All Notes Off など）は、USB Device と同じく **BLE-MIDI Device には送らない**
  （相手が PC / iPad なので）。BLE-MIDI Host の機器には送る
- picoruby-ble を入れる・外すと libmruby の作り直しが要るので、CMake が gem セットのスタンプ
  （IDF のビルドディレクトリの `midori_gemset.txt`）を見て rake を走らせる

---

## 1. 方針の要約

1. **MIDI のデータは Ruby を通さない**。BLE-MIDI のパケットの分解・組み立ては C で行い、
   USB-MIDI と同じ 4 バイトの USB-MIDI パケットとして picoruby-midi のトランスポート登録表に載せる。
   ルート（`MIDI.route`）・filter・スケジューラ・クリーンアップは今のまま BLE にも効く
2. **picoruby-ble からは NimBLE ホストの所有（`nimble_owner`）だけを共有する**。
   Ruby の `BLE` クラス（イベントループ・GATT データベースの DSL）は BLE-MIDI には使わない
3. **プロセス全体で生きている C の状態**（USB host と同じ）。スクリプトの切り替えで接続は切れない。
   gem の `*_gem_final` は何もしない
4. **パケットの符号化・復号は OS 非依存の C**（ホストでテストする）。置き場所は picoruby-midi
5. 最初の対象は **ESP32-S3（CoreS3 / Freenove）**。ESP32-P4（Tab5 / CrowPanel）は無線を持たず、
   ESP32-C6 を ESP-Hosted 経由で使う必要があるので後の段階（§10）

### なぜ picoruby-ble の Ruby API に乗せないか

PR #427 の `BLE` は「`BLE#start` / `BLE#scan` が回すイベントループに VM が入りっぱなしになる」作りで、
NimBLE のイベントは `picoruby_nimble_pump()`（VM のスケジューラのサービス）から Ruby の
`Task::Queue` に積まれ、`packet_callback` で 1 件ずつ処理される。MIDI に使うと:

- 受信の遅れが VM の都合で決まる（UI モードのループは 100 ms、スクリプトは何をしているか分からない）
- スクリプトが終わる・切り替わるたびに VM ごと消え、接続も切れる（UI モードはスクリプトのたびに起動し直す）
- 1 VM に `BLE` は 1 つ（`registered_ble` が 1 個だけピン留め）、接続も 1 本（`con_handle` が 1 つ）

MIDoRI の他のトランスポート（USB host / device、UART、AMY）は全部「C のタスクが受けて、入力タスクが
ルートする」形なので、BLE-MIDI もそれに揃える。

---

## 2. PR #427 の ESP32 ポートの構造（前提の整理）

`mrbgems/picoruby-ble/ports/esp32/`:

| ファイル | 中身 |
|---------|------|
| `nimble_owner.c/.h` | NimBLE ホストの起動・停止（`nvs_flash_init` → `nimble_port_init` → `ble_hs_cfg` 設定 → `setup()` → ホストタスク → sync を 2 秒待つ）、イベントキュー（32 × 104 byte）、書き込みキュー（32 × 260 byte）、1 秒の heartbeat タイマ、VM 側のポンプ |
| `ble.c` | BTstack 形式の GATT データベース（バイト列）を `ble_gatt_svc_def` に変換して登録、GAP / GATT のイベントを BTstack 形式のパケットに合成して Ruby へ。`BLE_init()` |
| `ble_peripheral.c` | 広告（31 byte、`ble_gap_adv_start`）、notify |
| `ble_central.c` | スキャン、接続 |
| `pump_mruby.c` / `pump_mrubyc.c` | `picorb_scheduler_service_add()` で VM のポンプを登録 |

`nimble_owner.h` の公開 API:

```c
typedef int (*picoruby_nimble_setup_fn)(void);
int  picoruby_nimble_start(picoruby_nimble_setup_fn setup);  /* setup で GATT サービスを登録する */
int  picoruby_nimble_stop(void);
bool picoruby_nimble_started(void);
uint8_t picoruby_nimble_own_addr_type(void);
```

BLE-MIDI と一緒に使うときに問題になるところ:

| # | 箇所 | 問題 |
|---|------|------|
| P1 | `BLE_init()` | 起動済みなら `picoruby_nimble_stop()` してから立ち上げ直す。Ruby で `BLE.new` すると BLE-MIDI の接続が全部切れる |
| P2 | `picoruby_nimble_start(setup)` | `setup` は 1 つだけ。GATT サービスはホスト起動前にしか登録できないので、BLE-MIDI と Ruby の GATT を両方載せる手段がない |
| P3 | `ble_hs_cfg.sync_cb` / `reset_cb` | owner が持っていて、他のクライアントに伝える経路がない（コントローラのリセットで BLE-MIDI が接続を失ったことを知れない） |
| P4 | `ble_hs_cfg.gatts_register_cb` | `picoruby_ble_gatts_register_cb` は記述子の `arg` を `attr_map_t *` とみなして書き込む。他のサービスが記述子に `arg` を付けるとメモリを壊す |
| P5 | `ble.c` / `ble_peripheral.c` | `uint16_t con_handle` が static でないグローバル（名前の衝突、1 接続前提） |
| P6 | `nimble_owner.c` | `ble_hs_cfg.sm_bonding = 0` など SM の設定を固定（ペアリングを使う BLE-MIDI と食い違う） |
| P7 | `src/mruby/ble.c` | `_mrb` を static に持ち、`gem_final` でだけ外す。Supervisor が 5 秒タイムアウトで VM を閉じずに捨てると残る |
| P8 | .bss | `evq` / `wrq` / read mirror / 属性表で約 17 KB を static に持つ（Ruby の `BLE` を使わなくても確保される） |

P1〜P4・P6 は §3 の「共有オーナー」で、P7 は Supervisor 側（§9.5）で扱う。P5・P8 は害が出るまで触らない
（P8 は §11 のメモリの見積もりに含める）。

---

## 3. picoruby-ble への変更: NimBLE の共有オーナー

`nimble_owner` を「複数のクライアントが使うホスト」にする最小の変更。PR #427 が入った後の upstream への
フォローアップとして出す想定で、それまでは fork のブランチに載せる（§9.1）。

### 3.1 API

```c
/* nimble_owner.h に追加 */
typedef struct picoruby_nimble_client {
    const char *name;                 /* ログ用 */
    int  (*setup)(void);              /* ホスト起動前。GATT サービスの登録 */
    void (*configure)(void);          /* ble_hs_cfg の調整（SM など）。起動のたびと、動いているホストを
                                       * acquire したときに呼ぶ。再起動はしない */
    void (*on_sync)(void);            /* ホストとコントローラの同期後（起動時・リセットからの復帰時） */
    void (*on_reset)(int reason);     /* コントローラのリセット、またはオーナーがホストを止めた
                                       * （PICORUBY_NIMBLE_REASON_STOPPED）。接続はすべて失われている */
    bool exclusive;                   /* ホストを独り占めする（今は誰も使わない） */
    int  priority;                    /* 手続きの取り合いで上の者が勝つ（BLE クラス 1、BLE-MIDI 0） */
    void (*yield)(int proc);          /* 手続きを取り上げられた。すぐ止める */
    void (*proc_free)(int proc);      /* 断られた / 取り上げられた手続きが空いた */
    bool provides_gap;                /* setup が NimBLE の GAP / GATT サービスを登録する */
    bool held, set_up;                /* オーナーが使う */
    uint8_t waiting;
    struct picoruby_nimble_client *next;
} picoruby_nimble_client_t;

/* ホストを使う。動いていなければ起動し、setup がまだ走っていなければ立ち上げ直す
 * （GATT はホスト起動前にしか足せないため）。0 / -1（起動しない）/ -2（排他のクライアントがいる） */
int  picoruby_nimble_acquire(picoruby_nimble_client_t *client);
/* 外す。最後のクライアントならホストを止める */
void picoruby_nimble_release(picoruby_nimble_client_t *client);
/* 自分以外のクライアントがホストを使っているか（NULL: BLE クラス以外） */
bool picoruby_nimble_shared(const picoruby_nimble_client_t *self);
bool picoruby_nimble_synced(void);

/* 1 つずつしか動かない手続き（§3.2）: PICORUBY_NIMBLE_PROC_ADV / _CENTRAL */
bool picoruby_nimble_claim(picoruby_nimble_client_t *client, int proc);   /* NULL: BLE クラス */
void picoruby_nimble_unclaim(picoruby_nimble_client_t *client, int proc);
bool picoruby_nimble_holds(const picoruby_nimble_client_t *client, int proc);
bool picoruby_nimble_gap_provided(void);
```

- 既存の `picoruby_nimble_start(setup)` / `stop()` は「picoruby-ble 自身をクライアントとして acquire / release する」
  薄いラッパとして残す（PR のインターフェースは変えない）
- `setup` はクライアントを足した順に全部呼ぶ。`ble_gatts_count_cfg()` / `ble_gatts_add_svcs()` は複数回呼んでよい
- `gatts_register_cb`（P4）は owner が受けて、「picoruby-ble が登録した記述子か」で振り分ける
  （`ble.c` の `dsc_slots[]` の範囲かどうか）。BLE-MIDI の定義は記述子を持たない（CCCD は NimBLE が自動で付ける）
- SM（P6）は `setup` の中でクライアントが上書きしてよい。BLE-MIDI が `sm_bonding = 1` にした後に
  picoruby-ble の setup が 0 に戻さないよう、owner の既定値の設定は `setup` 群より前で 1 回だけ行う

### 3.2 Ruby の `BLE` との共存（段階 4）

段階 1〜3 は**排他**だった（BLE-MIDI がホストを使っている間の `BLE.new` は失敗）。段階 4 で**共有**にした
（2026-11-07、実機で確認）。BLE クラスも普通のクライアント（`exclusive = false`）で、走っているスクリプトの
ものなので**優先度が上**（1。BLE-MIDI は 0）。

- **GATT**: `BLE.new` のプロファイルにサービスがあれば、ホストを立ち上げ直して足す（`picoruby_nimble_start(setup)`、
  サービスがなければ `NULL` で再起動しない）。BLE-MIDI の接続はそこで切れる（Device は広告し直し、Host は自動で
  再接続する）。スクリプトが終わると（`picoruby_nimble_stop()`）、他のクライアントが残っていればもう一度
  立ち上げ直してサービスを外す
- **GAP / GATT サービス（0x1800 / 0x1801）**: BLE-MIDI Device は NimBLE の `ble_svc_gap` / `ble_svc_gatt` を登録する
  （`provides_gap`）。プロファイルにも 0x1800 / 0x1801 があると二重になるので、そのときは BLE クラスの側を外して
  登録する（`ble.c` の `register_services`）。デバイス名は BLE-MIDI Device のもの（`BLE_MIDI_DEVICE.name`）になる
- **1 つずつしか動かない手続き**は取り合いにする（`picoruby_nimble_claim` / `unclaim`）
  - `PROC_ADV`（広告）: レガシー広告はホストに 1 セットだけ。BLE クラスのペリフェラル / ブロードキャスタが広告する間、
    BLE-MIDI Device の広告は止まる（状態は `:starting`）。BLE クラスが止める・接続されると Device が広告を再開する
  - `PROC_CENTRAL`（スキャンと接続の開始）: NimBLE はスキャン中に接続を始められず、スキャンも接続の開始も
    1 つずつ。BLE クラスのスキャン / 接続が BLE-MIDI Host のスキャン（ユーザのスキャン・再接続のスキャン）と
    接続の開始を止める。止められた接続は BLE クラスが手放した後にやり直す
  - 取り上げは要求した側のタスクで `yield` を呼ぶ（`ble_gap_adv_stop` / `ble_gap_disc_cancel` /
    `ble_gap_conn_cancel` は同期的）。空いたら待っていた側に `proc_free`（各 gem のタスクを起こす）。
    状態はスピンロックで持ち、オーナーの再帰ミューテックスは取らない（GAP のイベントはホストタスクから来るので、
    `host_stop()` が待つホストタスクを止めないため）。ホストの停止・コントローラのリセットで全部空く
- **接続**: GAP のイベントは手続きを始めたクライアントのコールバックに届くので混ざらない。BLE クラスの
  `con_handle`（1 本）は自分の接続だけ。ホストが他のクライアントのために立ち上げ直されたときは、BLE クラスに
  切断イベント（理由 0x16）を合成し、同期後に広告・スキャンを再開する（`picoruby_ble_on_reset` / `on_sync`）。
  その間の VM とイベントキューはそのまま（`host_start()` は BLE クラスがいないときだけ消す）
- **スクリプトの終了**: `ble_midi_settings_restore()` → `picoruby_nimble_stop()` が BLE クラスの広告・スキャン・
  接続を止め（`picoruby_ble_teardown`）、ホストの持ち分を放す。VM ごと捨てられたときも同じ
- 制限: 同時に広告できるのは 1 つだけ（拡張広告は使っていない。`CONFIG_BT_NIMBLE_EXT_ADV` にするとレガシーの
  API が無くなり PR #427 も BLE-MIDI も書き換えになる）。BLE クラスのペリフェラルが広告している間、Mac / iPad から
  BLE-MIDI Device は見えない

---

## 4. BLE-MIDI のプロトコル（コーデック）

仕様: MMA / AMEI "Specification for MIDI over Bluetooth Low Energy (BLE-MIDI) 1.0"。

### 4.1 GATT

| 項目 | 値 |
|------|-----|
| MIDI Service | `03B80E5A-EDE8-4B33-A751-6CE34EC4C700` |
| MIDI I/O Characteristic | `7772E5DB-3868-4112-A1A9-F2669D106BF3` |
| プロパティ | Read（空を返す）/ Write Without Response / Notify |
| 方向 | Central → Peripheral は Write Without Response、Peripheral → Central は Notify |

### 4.2 パケット

```
[header][ts][status][data...][ts][status][data...] ...
header = 1 0 t12 t11 t10 t9 t8 t7     タイムスタンプ（13 bit, ms）の上位 6 bit
ts     = 1 t6 .. t0                    下位 7 bit。各メッセージの前に置く
```

- 1 パケット = MTU − 3 バイト以内（既定 MTU 23 なら 20 バイト）
- ランニングステータス可（タイムスタンプの後にステータスを省いてデータが来る）
- 下位 7 bit が前のメッセージより小さければ上位が 1 進んだとみなす（パケット内の折り返し）
- リアルタイム（F8〜FF）は他のメッセージの途中にも、自分のタイムスタンプ付きで割り込める
- SysEx（F0〜F7）だけはパケットをまたいでよい。続きのパケットは header の直後にデータが続き（ts なし）、
  終わりの F7 の前にはタイムスタンプを置く
- SysEx 以外のメッセージはパケットをまたがない

### 4.3 コーデックの置き場所と API

両 gem が使うので **picoruby-midi** に置く（`src/ble_midi_codec.c` + `include/ble_midi_codec.h`）。
OS にもスタックにも依存しない純粋な C で、`make -C mrbgems/picoruby-midi/host_test` でテストする。
`midi_transport.h` に `MIDI_TRANSPORT_ID_BLE` がもうあるので、picoruby-midi に BLE-MIDI の層を置くのは
自然な位置づけ。

API は [include/ble_midi_codec.h](../mrbgems/picoruby-midi/include/ble_midi_codec.h):

- 復号: `ble_midi_decode(&dec, buf, len, out, arg)` が 1 パケット（キャラクタリスティックの値 1 回分）を読み、
  メッセージごとに `out(arg, usb_pkt[4], ts)` を呼ぶ。状態（ランニングステータス・パケットをまたぐ SysEx）は
  `ble_midi_decoder_t` に持つ。不正なバイトは捨てて `errors` を数え、読めた分は出す。
  SysEx は 3 バイトたまったらすぐ出す（中に挟まったリアルタイムの順番が前後しないように）。
  メッセージの途中に挟まったリアルタイムは受ける（タイムスタンプの後のステータスで判断する）
- 符号化: `ble_midi_encoder_begin(&enc, buf, cap, ts)` で 1 パケットを始め、`ble_midi_encode(&enc, usb_pkt, ts, &done)`
  を USB-MIDI パケットごとに。入らなければ false（送ってから begin し直して同じ `done` でもう一度）。
  SysEx 以外は丸ごと、SysEx はパケットが埋まったところで分ける。開いたままの SysEx は次のパケットへ続く。
  タイムスタンプは受け手が追える範囲（同じ上位ビットで後、または次の上位ビットで下位が小さい）なら書き、
  時刻が戻ったら直前の時刻にし、それ以外は新しいパケットにする
- UUID（リトルエンディアン）: `BLE_MIDI_SERVICE_UUID_LE` / `BLE_MIDI_CHAR_UUID_LE`

- USB-MIDI ↔ MIDI の変換は CIN で: 0x8〜0xE はステータスの上位 4 bit、2 / 3 はシステムコモン 2 / 3 byte、
  4〜7 は SysEx（4 = 続き 3 byte、5 / 6 / 7 = 1 / 2 / 3 byte で終わる。5 は F6 などの 1 byte コモンにも使う）、
  0xF は 1 byte（リアルタイム）。ケーブル番号は送信では無視し、受信では 0
- **符号化ではランニングステータスを使わない**（バイト数は増えるが、受け手の実装差で事故が起きない）
- 受信の SysEx は USB-MIDI の SysEx パケット（CIN 4〜7）の列に戻す。組み立て直すのは今どおり
  picoruby-midi の入力タスク（USB host と同じ経路）

---

## 5. picoruby-ble_midi_device（Peripheral）

### 5.1 振る舞い

- 有効なら広告する。広告データ: Flags + 128 bit の MIDI Service UUID（Complete List）。名前は
  スキャン応答（31 byte に UUID 18 byte と名前は入り切らないことが多いため）
- 名前は USB と同じアイデンティティをビルド定義で入れる（`BLE_MIDI_DEVICE_NAME`、既定 `"PicoRuby MIDI"`。
  MIDoRI は `USB_MIDI_DEVICE_PRODUCT` と同じ `"Midori CoreS3"` など）
- 接続は **1 本**（Central は 1 台）。接続中は広告を止め、切れたら再開
- 接続後に:
  - 接続間隔の更新を要求（`ble_gap_update_params`、7.5〜15 ms。iOS / macOS が受ける最小値は要確認。
    受けてもらえなければそのまま）
  - MTU は Central からの交換を待つ（`CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU = 247`）。交換された MTU を
    送信の 1 パケットの上限にする
- Read には空を返す（仕様どおり）
- CCCD（notify の購読）が立つまでは送らない（送信キューは捨てる。§7）

### 5.2 セキュリティ

macOS / iOS の Bluetooth MIDI は暗号化を求めることがある（機器によっては接続直後にペアリングを要求する）。
最初は **Just Works のペアリング + ボンディング**（`BLE_HS_IO_NO_INPUT_OUTPUT`、`sm_bonding = 1`、
`sm_sc = 1`）、ボンド情報は NimBLE の NVS ストア（`CONFIG_BT_NIMBLE_NVS_PERSIST=y`、`ble_store_config_init()`）。
キャラクタリスティックのパーミッションに暗号化を要求するかは `BLE_MIDI_DEVICE_REQUIRE_ENCRYPTION`（既定 0）。
実機（macOS の Audio MIDI 設定、iPadOS の GarageBand / AUM、Windows の MIDI Services）で詰める。

段階 4 の見直し（2026-11-07）: 段階 1〜3 の実装で次が揃っていて、CoreS3 の実機で接続できたので、それ以上は足していない。

- 相手がボンドを失って再ペアリングしてきたら、こちらの古いボンドを消してやり直す（`BLE_GAP_EVENT_REPEAT_PAIRING`、両 gem）
- Host: 相手が暗号化を求めてきたら（Insufficient Authentication / Encryption）ペアリングを始めて購読し直す
- ボンドは両方の役割で 1 つのストア（`CONFIG_BT_NIMBLE_MAX_BONDS=8`、溢れたら古いものから `ble_store_util_status_rr`）。
  Host の `forget` は自分のボンドも消す
- 残っていること: こちらがボンドを失った（NVS を消した）のに Mac / iPad が覚えているときは、相手の側で
  「このデバイスを削除」が要る（こちらからはどうにもできない）。Device のボンドだけを消す API は持っていない

### 5.3 C API

[mrbgems/picoruby-ble_midi_device/include/ble_midi_device.h](../mrbgems/picoruby-ble_midi_device/include/ble_midi_device.h)。
`init`（トランスポートの登録だけ）/ `enable(on)`（非同期。gem のタスクが acquire / release）/ `is_connected`
（接続済みかつ notify 購読中）/ `send_packet` / `read_packet` / `bytes_available` / `disconnect` / `set_name` /
`stats`（状態・ピア・暗号化・MTU・接続間隔・RX / TX / 捨てた数・復号エラー）。ビルド時の設定は
`include/ble_midi_device_config.h`（名前・接続間隔・広告間隔・キューの大きさ・タスク）。

トランスポートは `MIDI_transport_register()` の動的ビット + `MIDI_Input_add_packet_source(bit)`
（USB host のスロット 1〜 と同じ。入力タスクはルート・filter・watch がある間だけ読む）+
`MIDI_Input_track_clock(bit)`（外部クロックの BPM）。`transport_id = MIDI_TRANSPORT_ID_BLE`。
購読されたときに、以前ルートのために入力タスクが起こされていて止まっているなら起こし直す（USB host と同じ）。

### 5.4 Ruby API

```ruby
require 'ble_midi_device'
dev = BLE_MIDI_DEVICE.instance          # transport_id / connected? / send_packet / read_available
BLE_MIDI_DEVICE.enable = true           # スクリプト限り（終了時に保存済みの設定へ戻る）
BLE_MIDI_DEVICE.state                   # :off / :starting / :advertising / :linked / :connected / :error
BLE_MIDI_DEVICE.peer                    # => "AA:BB:CC:DD:EE:FF" / nil
BLE_MIDI_DEVICE.info                    # 状態・ピア・MTU・interval_ms・カウンタの Hash
BLE_MIDI_DEVICE.name = "My Synth"       # 次の広告から
BLE_MIDI_DEVICE.disconnect
MIDI::Device.new(MIDIDevices.ble_midi_device)
```

---

## 6. picoruby-ble_midi_host（Central）

### 6.1 振る舞い

- **スキャン**: 要求されたときだけ（UI の Scan、`BLE_MIDI_HOST.scan`）。アクティブスキャンで
  MIDI Service UUID を広告している機器を集める（UUID が広告に無くスキャン応答側にある機器があるので、
  アクティブにして両方見る）。結果は C のリスト（アドレス・アドレス種別・名前・RSSI、最大 16 件）
- **接続**: 選ばれた機器へ `ble_gap_connect`。つながったら
  1. MTU 交換（`ble_gattc_exchange_mtu`）
  2. MIDI Service → I/O Characteristic → CCCD を探索（`ble_gattc_disc_svc_by_uuid` / `disc_chrs_by_uuid` / `disc_all_dscs`）
  3. CCCD に 0x0001 を書いて notify を購読
  4. 暗号化を求められたら（ATT エラー Insufficient Authentication / Encryption）`ble_gap_security_initiate` して 3 をやり直す
  5. ここで初めて「接続済み」
- **複数台**: 最大 `BLE_MIDI_HOST_MAX_DEVICES`（既定 2、§12 の登録表の残りビットに注意）。
  USB host と同じく機器ごとに**スロット**（RX リング・TX キュー・コーデックの状態・統計）。
  スロットは最初に使うときに登録表の動的ビットを取り、同じアドレスの機器は前のスロットに戻る
- **既知の機器と自動再接続**: 一度つないだ機器は NVS（`ble_midi` 名前空間の `known`、アドレス・種別・名前、最大 8 件。
  あふれたら一番古いものから捨てる）に覚える。Host を有効にしている間、空きスロットがあって既知の機器で
  未接続のものがあれば、8 秒ごとに 4 秒のパッシブスキャン（interval 100 ms / window 30 ms）で探し、
  見つけたら接続する（NimBLE の接続処理は同時に 1 つなので、1 台ずつ直列に）。切れた直後は 1 秒後に探し直す。
  利用者が Disconnect した機器はその起動中は探さない（自分でつなぎ直せば戻る）
- 切断されたらスロットの `is_connected` を落とし、MIDoRI 側でそのビットへ送る音は止める
  （ルートを外したときと同じく、`MIDI_route_*` の「ノートを通したチャンネル」の記録は残っているので、
  再接続時に All Notes Off を送るかは要検討）

### 6.2 C API

[mrbgems/picoruby-ble_midi_host/include/ble_midi_host.h](../mrbgems/picoruby-ble_midi_host/include/ble_midi_host.h)。
`init` / `enable(on)` / `is_ready` / `set_auto_connect` / `scan_start(ms)` / `scan_stop` / `scanning` /
`scan_results` / `connect(addr, type)`（非同期、スロット番号を返す）/ `disconnect(slot)` / `known` / `forget(addr)`、
スロットごとに `dev_status` / `dev_bit` / `dev_connected` / `dev_info` / `dev_stats` / `dev_send_packet` /
`dev_read_packet` / `dev_bytes_available`、`find(name)`（接続中から名前の部分一致、大文字小文字を区別しない）。
アドレスは上位バイトが先（表示と同じ順）。ビルド時の設定は `include/ble_midi_host_config.h`。

スロットが別の機器に渡ったときのルートの始末は、USB host と同じく一覧を作る側（MIDoRI では
`ui_midi_devices.cpp` の `sync_ble_host`）が行う。gem はしない。

### 6.3 Ruby API

```ruby
require 'ble_midi_host'
BLE_MIDI_HOST.scan(timeout_ms: 5000)    # ブロックする。=> [{name:, address:, type:, rssi:, known:, connected:}, ...]
h = BLE_MIDI_HOST.connect("WIDI")       # 名前（部分一致。前回のスキャン・既知の一覧になければスキャンする）
h = BLE_MIDI_HOST.connect(address: "AA:BB:CC:DD:EE:FF")   # 接続済みになるまで待つ（timeout_ms: 10000）。失敗は nil
BLE_MIDI_HOST.instance(slot = 0)        # USB_MIDI_HOST と同じ
BLE_MIDI_HOST.find("WIDI")              # 接続中から名前で
BLE_MIDI_HOST.devices                   # 接続中の一覧
BLE_MIDI_HOST.known / forget(address)
BLE_MIDI_HOST.enable = false            # すべて切る（止まるまで待つ）。スクリプト限り
h.status / h.info / h.name / h.address / h.disconnect
MIDI::Device.new(MIDIDevices.ble_midi_host("WIDI"))
```

---

## 7. 送信

`send_packet` は色々なタスクから呼ばれる（入力タスクのルート、UI タスクの鍵盤、スケジューラの esp_timer、
Ruby VM、クリーンアップ）。NimBLE の API はスレッドセーフだが、1 メッセージごとに notify / write すると
1 接続間隔に何パケットも積むことになり、mbuf も足りなくなる。

- `send_packet` は **接続ごとの TX リング**（4 byte パケット + 積んだ時刻の ms、既定 256 件、PSRAM 可）に
  積むだけで返る（ブロックしない。満杯なら捨てて `tx_dropped` を数える。リアルタイムメッセージは
  最優先で残したいが、最初は単純に捨てる）
- **gem ごとのタスク**（`ble_midi_dev` / `ble_midi_host`。NimBLE のホストタスクと同じコア、優先度 5）が
  リングを読み、コーデックで MTU − 3 バイトに詰めて送る（Peripheral は `ble_gatts_notify_custom`、
  Central は `ble_gattc_write_no_rsp_flat`）
  - 積まれたら起こされ（タスク通知）、1 ms 待って後続をまとめてから送る（コード・和音を 1 パケットにする）。
    待ちは esp_timer の one-shot（`CONFIG_FREERTOS_HZ=100` では 1 ms を待てない）
  - タイムスタンプは**積んだときの時刻**（受け手がジッタを吸収できるように）
  - `BLE_HS_ENOMEM` など送れなかったときはパケットを戻し、次の接続イベント（`BLE_GAP_EVENT_NOTIFY_TX` /
    数 ms 後）に再送
- USB Device の送信と同じく、購読前・未接続のスロットに積まれたものは捨てる

---

## 8. 受信

- Peripheral: GATT の書き込みアクセスコールバック、Central: `BLE_GAP_EVENT_NOTIFY_RX`。どちらも
  **NimBLE のホストタスク上**で、mbuf を平らにしてコーデックで復号し、USB-MIDI パケットを接続ごとの
  RX リング（picoruby-midi の `midi_timed_ring`、§8.1）へ、渡してよい時刻つきで積むだけ
- 入力タスクがパケットソースとして読む（`MIDI_Input_add_packet_source(bit)`）。ルート・filter・listener・
  `MIDI.on` は USB と同じに効く
- **タイムスタンプで間隔を戻す**（段階 4、§8.1）。段階 1〜3 は届いた時点で流していた
- 外部クロックの BPM: picoruby-midi に `MIDI_Input_track_clock(bit)` を足し、パケットソースにもパーサを
  付けて `MIDI_Input_get_external_bpm_from(bit)` で測れるようにした。Tempo 画面の送信元 `BLE` は
  BLE-MIDI Device、次に Host のスロットの順で、BPM が出ている最初のもの（`ui_manager.cpp` の
  `ble_external_bpm`）。表示中・Sync 中はそれらを watch する。パーサは入力タスクが読んだ時刻で測るので、
  §8.1 の補正が効いていれば F8 の間隔も送り手のものに戻る

### 8.1 受信のタイムスタンプ補正（段階 4 / 2026-11-07、実機で確認）

BLE は接続間隔のあいだに送られたものを次の接続イベントでまとめて届けるので、届く時刻は送られた時刻より
0〜接続間隔（再送があればもっと）遅れ、その遅れがメッセージごとに違う。送り手の 13 bit のタイムスタンプ
（ms）を自分の時計に写し、**送られた間隔のまま、一定の遅延（budget）だけ後に**流す:

    渡す時刻 = 送り手の時刻 + offset + budget

- **offset**（自分の時計 − 送り手の時計）は**見た中で一番小さい遅れ**（接続イベントの直前に送られたもの）。
  送り手の時計が速ければ遅れが小さくなるのですぐ追従し、遅ければ `BLE_MIDI_SYNC_RELAX_MS`（500 ms）ごとに
  1 ms ずつ offset を上げて追う（2000 ppm まで）。13 bit の折り返しは「今そうであるはずの送り手の時刻」に
  一番近い 8192 ms の候補を選んで外す。5 秒メッセージが来なければ最初からやり直す
- 渡す時刻は**後戻りしない**（順序を保つ）、**届いた時刻より前にしない**（遅れすぎたものはすぐ流して `rx_late` に数える）、
  **届いてから budget より後にしない**。なのでタイムスタンプを入れない送り手でも、遅延が budget 増えるだけで
  順序は崩れない
- **budget** の既定は自動 = 接続間隔 + 4 ms（10〜60 ms。15 ms 間隔なら 19 ms）。`BLE_MIDI_DEVICE.rx_latency =` /
  `BLE_MIDI_HOST.rx_latency =`（ms、`:auto`、`0` = 補正なし）でスクリプトの間だけ変えられる（終了時に自動へ戻す）。
  ビルド時の既定は `BLE_MIDI_DEVICE_RX_LATENCY_US` / `BLE_MIDI_HOST_RX_LATENCY_US`（-1 = 自動）。
  補正は遅延とジッタの引き換え: 鍵盤を弾くだけなら `0` の方が速い
- 計算は picoruby-midi の `ble_midi_sync_release()`（OS 非依存、`ble_midi_codec.h`。ホストテストで間隔の再現・
  折り返し・遅れ・タイムスタンプなし・時計のずれを確かめている）
- **時刻どおりに流す仕組み**: picoruby-midi の `midi_timed_ring`（ESP32、`include/midi_timed_ring.h`）。
  パケットに時刻を付けて積み、読む側には時刻を過ぎたものだけ見せる。次のパケットの時刻に esp_timer を
  仕掛け、その時刻に `MIDI_Input_kick()` で入力タスクを起こす（入力タスクのポーリングは 1 tick = 10 ms なので、
  起こさないと 10 ms 刻みになる）。`MIDI_Input_kick()` は入力タスクの待ちをセマフォにしたもの
  （タスクは止まったり作り直されたりするので、タスク通知ではなくプロセスに 1 つのセマフォ）
- `info` に `rx_late` と `rx_latency_ms`（今使っている budget）を足した

---

## 9. MIDoRI への組み込み

### 9.1 サブモジュール

picoruby サブモジュールは upstream の master を直接参照している（2026-10-21〜）。PR #427 が入るまでは
**fork にブランチを切って `.gitmodules` を戻す**（CLAUDE.md の方針どおり）:

- サブモジュールのローカルブランチ **`midori-ble`** = upstream master（ec253ec2）+ PR #427（bash0C7/picoruby
  `picoruby-ble-esp32-port` 7529064 のマージ）+ §3 の共有オーナー（`nimble_owner.c/.h` と `ble.c` の 2 か所）
- **まだ push していない**。`kirikak2/picoruby` に push して `.gitmodules` の url / branch をそちらへ向けるまで、
  MIDoRI のコミットが指すサブモジュールのコミットは手元にしかない
- PR #427 が入ったら共有オーナーだけを upstream に出し、入ったら master 直参照に戻す

### 9.2 ビルド

- sdkconfig の断片 [sdkconfig.defaults.ble](../sdkconfig.defaults.ble)（R2P2-ESP32 PR #135 の `sdkconfigs/bt_nimble` が基。
  NimBLE・4 ロール・接続 3・MTU 247・SM（Legacy + SC）・NVS のボンド 8・ホストタスク 8 KB・ホストの確保は PSRAM・
  `SW_COEXIST` なし・NimBLE のログは WARNING）。`switch_board.sh` が ESP32-S3 のボードで「ボード + USB モード」の後ろにつなぐ
- menuconfig: `USB_MIDI_BLE_MIDI`（`BT_NIMBLE_ENABLED` に依存、既定 y）。MIDoRI 側はこれだけを見る
- `components/midori-runtime/CMakeLists.txt`:
  - `bt` を PRIV_REQUIRES（早い段階で CONFIG_ が無いので AMY と同じく `IDF_TARGET STREQUAL "esp32s3"` で）
  - 両 gem の `ports/esp32/*.c` は**いつでも**コンパイルする（`BLE_MIDI_*_ENABLED` が無ければスタブ。UI と
    Ruby のバインディングがいつでもリンクできるように）
  - `CONFIG_USB_MIDI_BLE_MIDI` のとき picoruby-ble の `ports/esp32/{ble,ble_peripheral,ble_central,nimble_owner,pump_mruby}.c`
    を足し、`BLE_MIDI_DEVICE_ENABLED=1` / `BLE_MIDI_HOST_ENABLED=1` / `BLE_MIDI_DEVICE_NAME`（USB の製品名と同じ）を定義、
    rake に `USE_BLE=1` を渡す。`HAS_BLE_MIDI` を board_config.rb へ
  - gem セットのスタンプ（`${CMAKE_BINARY_DIR}/midori_gemset.txt`、中身は `ble` か空）を libmruby の依存にして、
    BLE の有無が変わったら rake を走らせる（rake の後で libmruby に touch）
- `build_config/picoruby_gems.rb`: 両 gem はいつでも、`picoruby-ble` は `ENV['USE_BLE']` のときだけ
- gem の mrblib / src を変えたら従来どおり `rm -rf components/midori-runtime/picoruby/build/esp32-picoruby`
- `picoruby-ble-uart` は入れない

### 9.3 起動

- [components/midori-runtime/ble_midi_settings.c](../components/midori-runtime/ble_midi_settings.c):
  NVS（`ble_midi`: `dev_on` / `host_on`、どちらも既定 on）の設定。`app_main` が supervisor の前に
  `ble_midi_settings_start()` を呼び、両 gem の init（トランスポートの登録）と保存済みの設定での enable をする。
  enable は非同期で、NimBLE の起動（sync 待ち最大 2 秒）は gem のタスクで走る
- `main_task_base.rb`: `MIDIDevices.ble_midi_device` / `ble_midi_host(name)` / `ble_midi_hosts`。
  `setup_midi_devices` で BLE-MIDI Device を `register_builtin(:ble_dev, "BLE", bit, color: :blue)`
- Host の機器は UI タスクが載せる（`ui_midi_devices.cpp` の `sync_ble_host`。キーは `b` + アドレスの下位 4 バイト、
  名前は機器名、色は青）。保存済みパッチは機器ごと

### 9.4 画面

- **Bluetooth 画面**（[main/ui/screen_bluetooth.cpp](../main/ui/screen_bluetooth.cpp)、`UI_SCREEN_BLUETOOTH` = 13、
  Ruby は `:bluetooth` / `UI.bluetooth`。BLE のビルドだけ。Home の状態グループ、MIDI Info の次）:
  - 上に Device / Host の On / Off（NVS に保存）
  - Device の名前と状態（advertising / connected と相手のアドレス）
  - 機器の一覧: スロットにある機器（接続中は紺）→ 既知の機器（`*`）→ スキャンで見つかった機器（RSSI）。
    タップで接続 / 切断、既知の機器の右端の `x` で Forget。縦にドラッグでスクロール
  - ナビバーの中央は Scan（8 秒。スキャン中に押すと止める）
  - タイルの状態: 「2 linked」/「On」/「Off」
  - 一覧の写しはヒープ（UI タスクのスタックが小さいため）。300 ms ごとに写し直し、変わったときだけ合成で描き直す
- **設定画面**の BLE-MIDI 欄（以前は「Not implemented」）: Device / Host の状態。欄のタップとナビバーの
  「Bluetooth」で Bluetooth 画面へ
- **MIDI Devices**: 行・列に `BLE`（Device）と Host の機器
- **Tempo** の `BLE` ボタン（§8）

### 9.5 スクリプトの終了・Supervisor

- `picoruby_esp32_midi_cleanup()` の All Sound Off / All Notes Off / MIDI Stop は、BLE-MIDI **Device には送らない**
  （USB Device と同じ扱い）。Host の機器には送る
- 接続・広告・自動再接続はスクリプトをまたいで残す。cleanup の最後で `ble_midi_settings_restore()` が
  BLE クラスの分のホストを放し（P7: VM が捨てられても）、保存済みの設定で両方を enable し直す（変わっていなければ何もしない）

---

## 10. ボード

| ボード | SoC | BLE | 段階 |
|-------|-----|-----|------|
| M5Stack CoreS3 | ESP32-S3 | 内蔵 | 1〜 |
| Freenove ESP32-S3 | ESP32-S3 | 内蔵 | 1〜 |
| M5Stack Tab5 | ESP32-P4（無線なし）+ ESP32-C6（SDIO） | C6 経由 | 5 |
| CrowPanel Advanced 7" | ESP32-P4 + ESP32-C6 | C6 経由（SDIO のピンは基板リビジョン依存、docs/CROWPANEL.md） | 5 以降 |

P4 では NimBLE のホストを P4 で動かし、コントローラは C6 にある（ESP-Hosted の Hosted-HCI / VHCI）。

### 10.1 Tab5（段階 5 / 2026-11-07、Tab5 の実機で確認）

実機: 出荷時の C6 は **ESP-Hosted 1.4.1 で BT コントローラ入り**（起動ログ `BOARD_BLE: BLE through ESP32-C6, ESP-Hosted
firmware 1.4.1`）。C6 の書き換えは要らない。BLE-MIDI Device の広告・Host の準備完了を確認。

- **4 bit バスの D1 は `CONFIG_ESP_HOSTED_SDIO_PRIV_PIN_D1_4BIT_BUS`**（`CONFIG_ESP_HOSTED_SDIO_PIN_D1` はそこから導出される
  表に出ない値で、sdkconfig.defaults に書いても効かない）。P4 の既定の 15 は C6 のリセットピンで、そのままだと
  カードの初期化までは通り、直後の `sdmmc_io_read_byte(CCCR_FN_ENABLE)` が失敗して esp_hosted が `abort()` し、
  起動のたびに落ちた（2026-11-07）。起動ログの `sdio_wrapper: GPIOs: CLK[12] CMD[13] D0[11] D1[10] ...` で確かめる

- **esp_hosted 1.4.0 + esp_wifi_remote 0.8.5**（`main/idf_component.yml`）。M5Stack の Tab5 のファームウェア
  （m5stack/M5Tab5-UserDemo）が C6 の出荷時ファームと組み合わせている版。ホストとスレーブ（C6）の版は合わせるのが
  ESP-Hosted の前提なので、新しい 2.x / 3.x には上げていない（上げるなら C6 の書き換えも要る。ESP-Hosted の
  co-processor OTA でホストから書ける）。esp_hosted 1.4.0 の C6 用スレーブの既定は `CONFIG_BT_CONTROLLER_ONLY=y`
  （BT コントローラ入り）。**出荷時の C6 ファームに BT が入っているかは実機で確かめる**（起動ログ、下記）
- **Tab5 だけ**に入れる: `rules: if: "$CONFIG{USB_MIDI_BOARD_M5STACK_TAB5} == True"`（Component Manager は
  Kconfig を見て 2 回解く）。esp_wifi_remote を入れると P4 では ESP-Hosted が**既定で有効**になり、P4 EV ボードの
  ピン（リセット GPIO 54 = Tab5 の Port A など）で C6 を探しに行くので、他の P4 ボード（CrowPanel）には入れない
- **マニフェストを変えると Component Manager が解き直し、固定していないものを最新にする**（tinyusb 0.19 → 0.21、
  M5GFX 0.2.24 → 0.2.36 になった）。`main/idf_component.yml` で tinyusb と M5GFX をロックの版に固定した
- 設定: `sdkconfig.defaults.ble`（NimBLE ホスト、共通）+ `sdkconfig.defaults.ble.m5stack_tab5`
  （`CONFIG_BT_CONTROLLER_DISABLED`、`CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE` / `_NIMBLE_HCI_VHCI`、
  `CONFIG_ESP_WIFI_REMOTE_LIBRARY_HOSTED`、`CONFIG_SLAVE_IDF_TARGET_ESP32C6`、SDIO の設定は M5Tab5-UserDemo の sdkconfig
  から: 4 bit・40 MHz、CLK 12 / CMD 13 / D0 11 / D1 10 / D2 9 / D3 8、リセット 15（Low アクティブ）、SDMMC スロット 1）。
  ESP32-S3 は `sdkconfig.defaults.ble.esp32s3`（コントローラ有効・coexist なし）。`switch_board.sh` がつなぐ
- 起動の流れ: esp_hosted 1.4.0 はコンストラクタ（app_main の前）で SDIO のタスクを作るだけ。C6 の電源は
  IO エキスパンダ 0x44 の P0（WLAN_PWR_EN）で、M5GFX が `platform_init()` で入れる。最初の `nimble_port_init()`
  （共有オーナー、BLE-MIDI の gem のタスク上）が `ble_transport_ll_init()` → C6 をリセットしてリンクが上がるまで待つ。
  なので picoruby-ble・BLE-MIDI のコードは S3 と同じ（コントローラの初期化を呼ばない）
- `ble_transport_ll_deinit()` は esp_hosted 1.4.0 に無い（`nimble_port_deinit()` がリンクで要求する）ので
  `main/platform/board_ble.c` に空で置いた。共有オーナーはサービスが変わるたびにホストを止めて起こし直すが、
  SDIO のリンクは上げたまま（次の init は「Transport is already up」、ホストの HCI Reset で C6 のコントローラを戻す）
- 起動ログ: `board_ble_report()`（`board_ble.c`）が BLE が上がったら `BOARD_BLE: BLE through ESP32-C6, ESP-Hosted
  firmware x.y.z`、20 秒上がらなければ「C6 のファームに BT コントローラが無いのでは」と警告する
- **SD カードと SDMMC ホストを共有する**: C6 は SDMMC スロット 1、SD カードはスロット 0。IDF 5.5 の
  `sdmmc_host_init()` は初期化済みなら何もせず OK を返し、`sdmmc_host_deinit_slot()` は使っているスロットを数える。
  picoruby-filesystem-fat が失敗時に `sdmmc_host_deinit()`（ホストごと）を呼んでいたので、C6 のリンクごと落とさないよう
  スロットだけ外すようにした（IDF 5.4 未満は従来どおり）
- 注意: C6 がまったく応答しないと、esp_hosted 1.4.0 は 1 秒ごとに約 1000 回待ってから `ESP_ERROR_CHECK` で止まる
  （BLE-MIDI の gem のタスクの中なので UI は動くが、十数分後に再起動する）。BT の無い C6 ファームなら、SDIO の
  リンクは上がって HCI に応答がなく、ホストの同期待ち（2 秒）で失敗して BLE-MIDI が `:error` になると見ている（未確認）
- Tab5（midi_device、BLE あり）の大きさ: イメージ 3.32 MB（アプリ領域 3.94 MB）、DIRAM 229 KB / 576 KB（40%）
- Tab5 の USB パッドの事情（docs/AMY_SYNTH.md）とは無関係（SDIO なので）
- CrowPanel は未対応のまま（C6 の SDIO のピンが基板リビジョン依存、docs/CROWPANEL.md）。足すなら
  `sdkconfig.defaults.ble.crowpanel` と `main/idf_component.yml` の rule

ESP32-S3 は USB コネクタの役割（host / serial / midi_device）にかかわらず BLE を使える。
`host` モードの CoreS3 なら「USB で機器、BLE で iPad」が同時にできる。

---

## 11. メモリとフラッシュ

CoreS3（host モード、BLE あり）のビルド（2026-11-07、`idf.py size`）: イメージ 2.52 MB（アプリ領域 3.94 MB）、
DIRAM 178.7 KB / 341.8 KB（52%、.bss 46 KB / .data 39 KB / .text 94 KB）。BLE なしとの差と、起動後の
内部 RAM の空き（`heap_caps_get_free_size(MALLOC_CAP_INTERNAL)`・最大ブロック、UI の帯バッファが内部 RAM に
入ったか＝起動ログの `UI_STRIP`）は実機で確かめる。

BLE-MIDI のバッファ（リング・スロット・送信バッファ）はすべて init のときに `heap_caps_calloc(MALLOC_CAP_8BIT)`
（PSRAM に落ちてよい）。picoruby-ble の static（P8、約 17 KB）は .bss。

- MIDoRI の mruby ヒープは PSRAM（S3 + PSRAM で 2 MB）なので、R2P2-ESP32 PR #135 の
  「DRAM のヒープを 172 KB に縮める」問題は起きない
- それでも内部 RAM は UI の帯バッファ（CoreS3 40 KB）や .bss の配置に敏感
  （docs/MEMORY_ALLOCATION.md の「static の大きなバッファで LCD が真っ暗」）。BLE-MIDI 側のバッファは
  全部 malloc にし、起動ログに `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)` と最大ブロックを出して
  BLE あり / なしで比べる
- アプリの領域は 0x3F0000（約 3.94 MB）。S3 のビルドは AMY を含まないので余裕はあるはずだが、
  ビルドで確かめる

---

## 12. 登録表のビット

トランスポート登録表は 16 ビット。今の内訳（ボードに関係なく登録されるもの）:

| ビット | 中身 |
|-------|------|
| 0x01 / 0x02 / 0x04 | USB host（スロット 0）/ UART / USB device |
| 動的 | Clock、AMY、Keys、Tombola、XYPad、Script、Sequencer（7） |
| 動的 | USB host スロット 1〜3（機器が来たときに 3） |

合わせて最大 13。BLE-MIDI Device 1 + Host 2 で **16 ちょうど**になる。

**実装では広げるのを見送った**（2026-11-07）。ESP32-S3 のボードでは AMY が登録されない（スタブ）ので最大 15。
**Tab5（段階 5）は AMY があるので最大 16 ちょうど**で、空きが無い（ハブ越しの USB 機器 4 台 + BLE Host 2 台 +
内蔵すべてを同時に使ったとき）。溢れた分は登録できずトランスポートにならない（ログ「registry full」）。
Tab5 の実機で余裕が要るとわかったら広げる。広げるときの注意:

- 32 ビットにすると `MIDI_ROUTE_DST_RUBY`（= `1 << 登録表の大きさ`）が `uint32_t` から溢れる。
  現実的なのは **24 ビット**（`midi_tmask_t` を `uint32_t`、DST_RUBY = 1 << 24、DST_VIRTUAL = 1 << 25）
- `uint16_t` で決め打ちしている所がある: `ui_sequencer.cpp` のトークン（mask | route << 16）、
  `screen_midi_devices.cpp` のハッシュ、`UI_MIDIDEV_MAX`（16）、`AMY_GEM_transport_bit()` /
  `USB_MIDI_HOST_dev_bit()` の戻り値、Ruby の `DST_RUBY` / `DST_VIRTUAL` / `ALL_TRANSPORTS` と
  `PAIR_KEY_STRIDE`（mruby/c の整数に収まるか）

---

## 13. 段階

| 段階 | 内容 | 確認 |
|------|------|------|
| 0 | fork のブランチ（master + PR #427）、sdkconfig 断片、CMake / rake の組み込み。Ruby の `BLE` の examples が CoreS3 で動く | 実機 |
| 1 | コーデック（picoruby-midi、ホストテスト）+ 共有オーナー + **picoruby-ble_midi_device**。macOS の Audio MIDI 設定 / iPad から見えて、送受信できる。`MIDI.route` で USB 機器 ⇄ BLE | ホストテスト・実機 |
| 2 | 登録表の 32 ビット化（§12）+ **picoruby-ble_midi_host**（スキャン・接続・複数台・既知の機器・自動再接続）、Ruby API | 実機（WIDI、BLE-MIDI 鍵盤） |
| 3 | 画面: Bluetooth 画面、MIDI Devices の行・列と詳細、Tempo の BLE、NVS の設定 | 実機 |
| 4 | 受信のタイムスタンプ補正（§8.1）、ペアリング・暗号化の詰め（§5.2）、Ruby の `BLE` との共存（§3.2） | 実機（確認済み） |
| 5 | Tab5（ESP-Hosted + C6、§10.1） | 実機（Tab5 で確認） |

---

## 14. 未決事項・リスク

- **実機で何も確かめていない**。とくに: 共有オーナーの起動順（device と host のタスクが同時に acquire する）、
  macOS / iPadOS からの接続と購読、ペアリング、自動再接続、NimBLE の mbuf が足りるか（大量の CC や SysEx）
- **picoruby-ble の変更を upstream が受けるか**（§3）。受けられなければ fork に持ち続けるか、
  BLE-MIDI 側で NimBLE を直接所有して picoruby-ble をビルドから外す（そのときは Ruby の `BLE` は使えない）
- **iOS / macOS の要件**: 接続間隔の最小値、暗号化の要否、広告の形（UUID を広告とスキャン応答のどちらに置くか）。
  段階 1 の実機で決める
- **Central と Peripheral の同時動作**: S3 のコントローラで「Peripheral として接続中に広告しつつ Central として接続」
  が問題なく動くか（NimBLE・コントローラとも対応のはずだが、接続間隔のスケジューリングで MIDI の遅れが増えないか）
- **受信のジッタ**: 段階 1〜3 は接続間隔（7.5〜15 ms、受けてもらえなければ 30 ms 以上）ぶんのジッタが乗る。
  段階 4 で補正し（§8.1）、実機で動作を確認した（2026-11-07）
- **共存**（§3.2）: 段階 4 で共有にし、実機で動作を確認した（2026-11-07）。接続は 4 本まで（`CONFIG_BT_NIMBLE_MAX_CONNECTIONS`）
- **NimBLE のホストタスクのコア**: 既定は Core 0。S3 の Core 0 は USB host のクラスドライバ・app_main（UI）が
  いる。BLE の TX / RX 処理が UI 描画に止められないよう、ホストタスクと TX タスクの優先度を UI より上にする
- **Wi-Fi との共存**: 今は Wi-Fi を使っていないので `SW_COEXIST` を切る。picoruby-network（shell の依存）を
  実際に使うようになったら見直す
- **PR #427 の制限**: 値イベントは 92 byte まで、広告は 31 byte まで（拡張広告なし）。BLE-MIDI は自前の
  GATT・GAP コールバックを持つので影響しない

---

## 15. テスト

- ホスト（`make -C mrbgems/picoruby-midi/host_test`）: コーデック
  - 仕様の例（ノート・ランニングステータス・タイムスタンプの折り返し・リアルタイムの割り込み・
    パケットをまたぐ SysEx・不正なバイト列）
  - 符号化 → 復号の往復（MTU 20 / 244 byte、SysEx の長さを変えて）
  - USB-MIDI の CIN との対応（全 CIN）
- 実機
  - macOS の Audio MIDI 設定 → Bluetooth 設定で接続、MIDI モニタで送受信
  - iPadOS（GarageBand / AUM の Bluetooth MIDI デバイス）
  - Windows 11（Windows MIDI Services）
  - Host: Yamaha MD-BT01 / CME WIDI Master などの BLE-MIDI アダプタ、BLE-MIDI 鍵盤
  - スクリプトの切り替え・強制停止で接続が残ること、ノートが残らないこと
  - 起動ログの内部 RAM の空き（BLE あり / なし）
