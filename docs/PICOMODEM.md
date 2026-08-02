# PicoModem — Web ターミナルからのファイル転送

## 概要

[PicoRuby web terminal](https://github.com/picoruby/picoruby/blob/master/mrbgems/picoruby-wasm/demo/www/terminal.html)
から Midori の SD カードへ **ファイルをアップロード / ダウンロード**する機能。
WebSerial で USB CDC を開き、R2P2 と同じ **PicoModem プロトコル**（RBTP）で転送する。

ブラウザ側は upstream の実装をそのまま使う（**改造不要**）。

対応コマンド：

| コマンド | 方向 | 対応 |
|---------|------|------|
| `FILE_READ` (0x01) | デバイス → ホスト（ダウンロード） | ○ |
| `FILE_WRITE` (0x02) | ホスト → デバイス（アップロード） | ○ |
| `DFU_START` (0x03) | ファームウェア更新 | ✕（後述） |

## 使い方

1. `./switch_board.sh <board> midi_device` でビルドし、書き込む
2. USB-C を PC に接続（`Midori CoreS3` / `Midori Tab5` などとして CDC が見える）
3. web terminal を開き、WebSerial でそのポートに接続する
4. `> ` プロンプトが出ている状態で、terminal の File Editor から Download / Upload

転送が終わるとデバイスは `[PicoModem] read /sd/app.rb` のような 1 行を出して
プロンプトに戻る。

**スクリプト実行中は転送できない**。Supervisor がスクリプト実行時に PicoRuby
タスクを差し替えるため、PicoModem を処理するメインループが存在しない。転送は
UI モード（`> ` プロンプト待ち）でのみ動作する。

## プロトコル

フレーム形式（[picoruby-picomodem](../components/picoruby-esp32/picoruby/mrbgems/picoruby-picomodem/mrblib/picomodem.rb) が実装）：

```
STX(0x02) | Length(2, BE) | Cmd(1) | Payload(N) | CRC16(2, BE)
                           ^^^^^^^^^^^^^^^^^^^^ CRC-16/CCITT の対象
```

セッション開始のハンドシェイクはフレームではなく **裸の STX 1 バイト**：

| 段 | ホスト（ブラウザ） | デバイス（Midori） |
|---|---|---|
| 1 | `0x02` を送信 | console タスクが検出 |
| 2 | `0x06` (ACK) を待つ | `0x06` を返し、raw モードへ |
| 3 | binary capture 開始、コマンドフレーム送信 | Ruby 側が `PicoModem.session` を実行 |
| 4 | DONE_ACK 受領 → capture 停止 | raw モード解除、プロンプト再表示 |

R2P2 では shell のエディタが Ctrl-B (= STX) を拾ってこれを行う
（`picoruby-shell/mrblib/shell.rb`）。Midori には shell がないので、
同じ役割を console タスクが担う。

## 実装

### なぜ C だけで完結できないか

PicoModem のファイル I/O は **PicoRuby の VFS 経由でしか行えない**。SD カードは
`picoruby-filesystem-fat`（同梱の FatFs ff14b + `ports/esp32/sd_disk.c`）が
独自にマウントしており、ESP-IDF の VFS には登録されていない。C 側の `fopen()`
では `/sd` に到達できないため、転送本体は Ruby 側で動かす必要がある。

一方、リンク層（バイトの受信・エコー・行編集）は console タスクが所有している。
そこで **バイトの流し込みだけを C が行い、プロトコルは Ruby が処理する**構成にした。

```
[midi_device モード]
  TinyUSB task --console_cdc_rx_cb()--> StreamBuffer --+
                                                       |
                                          console task (prio 4 / Core 1)
                                                       |
                        通常時: 行編集・エコー <--------+
                        raw時 : picorb_hal_stdin_push() へ丸投げ
                                                       |
                                    picoruby-machine の stdin リングバッファ
                                                       |
                        PicoModem.session の STDIN.read_nonblock <-+

  送信は逆に Ruby --> ConsoleIO#write --> console_input_write_raw()
                                          （VFS をバイパスして TinyUSB へ）
```

### C 側 — [console_input.c](../components/picoruby-esp32/console_input.c)

| 追加 API | 役割 |
|---------|------|
| `console_input_modem_pending()` | STX を受けたか（Ruby がポーリング） |
| `console_input_write_raw(data, len)` | バイナリセーフな送信 |
| `console_input_modem_exit()` | raw モード解除・プロンプト再表示 |

`console_feed()` の先頭で `0x02` を検出すると `console_modem_enter()` が走り、
以降 `console_dispatch()` が全バイトを `picorb_hal_stdin_push()` へ転送する。

`console_modem_enter()` が ACK を返す**前に**済ませていること：

- `esp_log_level_set("*", ESP_LOG_NONE)`
  ESP_LOG は同じリンクに出るため、セッション中にログが 1 行でも出るとフレームの
  中に異物が混ざる。デバイス側 `recv_frame` は非 STX バイトをスキップしないので
  即失敗する。
- `io_raw_bang(true)`（`STDIN.raw!` 相当）
  cooked モードの `picorb_hal_stdin_push()` は `0x03` / `0x1A` を SIGINT / SIGTSTP
  として**握り潰す**。これらはバイナリペイロードとして普通に出現する。
  `PicoModem.session` 自身も `STDIN.raw!` を呼ぶが、Ruby タスクが 100ms の
  ポーリングで寝ている間に最初のフレームが届きうるため、C 側で先に立てておく。

### 改行変換のバイパス

`CONFIG_LIBC_STDOUT_LINE_ENDING_CRLF=y` のため、コンソール VFS は送信時に
`\n` → `\r\n` を挿入する。`$stdout.write` は `picorb_hal_write()` → `fputc()` →
VFS を通るので、**0x0A を含むフレームは必ず壊れる**。

| モード | 送信経路 |
|-------|---------|
| `midi_device` | `tinyusb_cdcacm_write_queue()` + `write_flush()`（VFS を完全バイパス、64B チャンク） |
| `serial` / `host` | 呼び出しごとに TX を `ESP_LINE_ENDINGS_LF` に切替 → `fwrite` → 元に戻す |

受信側も `serial` / `host` ではセッション中だけ RX を LF（無変換）にする
（既定は `CONFIG_LIBC_STDIN_LINE_ENDING_CR`）。`midi_device` では CDC コールバックが
生バイトを渡すので VFS は介在しない。

**非 CDC モードの制約**: `picoruby-machine` の `stdin_reader` タスクが同じリンクを
10ms 間隔で直読みしており（`ports/esp32/machine.c`）、console タスクとバイトを
取り合う。行編集では問題にならないが、バイナリ転送ではバイト順序が乱れる
可能性がある。動作確認済みなのは `midi_device`（CDC）モードのみ。

### Ruby バインディング — [picoruby_supervisor.c](../components/picoruby-esp32/picoruby_supervisor.c)

```ruby
sm = ScriptManager.new
sm.modem_requested?   # => true なら転送要求あり
sm.modem_end          # raw モード解除

ConsoleIO.new.write(binary_string)  # バイナリセーフな出力
```

`ConsoleIO#write` は mrbc 文字列の `data` / `size` をそのまま渡すので NUL バイトでも
切れない。

### Ruby 側 — [main_task_base.rb](../components/picoruby-esp32/mrblib/main_task_base.rb)

UI モードのメインループ先頭：

```ruby
if sm.modem_requested?
  begin
    PicoModem.session($stdin, ConsoleIO.new)
  rescue => e
    puts "[PicoModem] error: #{e.message}"
  end
  sm.modem_end
  GC.start
  next
end
```

`PicoModem.session` は 1 コマンド処理して return するので、ループはすぐ元に戻る。

## gem について

`picoruby-picomodem` / `picoruby-crc` / `picoruby-pack` は `picoruby-shell` の
依存として **既にファームウェアに組み込まれている**（`conf.gembox 'shell'` 経由）。
gem 側の変更は一切していない。

## 保存先と既知の制約

### FatFs で保存される

`File.open(path, "w")` → `VFS::File.open` → `FAT::File` → `f_open(FA_CREATE_ALWAYS |
FA_WRITE)` → `f_write()` + `f_sync()` → `f_close()`。ESP-IDF の VFS は経由しない。

`c_write` は `str.string->data` / `str.string->size` を使うため**バイナリセーフ**。
`.rb` に限らず任意のファイルを転送できる。

### exFAT 非対応

`ffconf.h` は `FF_FS_EXFAT=0`。FAT16 / FAT32 のみ。64GB 以上の SDXC は出荷時
exFAT なので FAT32 での再フォーマットが必要（既存の `load` 機能と同じ制約）。
LFN は有効（`FF_USE_LFN=2` / `FF_MAX_LFN=255`）。

### File#expand のフォールバック

`PicoModem.handle_file_write` は書き込み前に `f.expand(size)` を呼び、これが
`f_expand(fp, size, 1)` = **連続クラスタの確保**になる。断片化したカードでは
`FR_DENIED` を返し、`picoruby-filesystem-fat` がそれを `RuntimeError` にするため
アップロードが失敗する。

連続配置は最適化にすぎないので、main_task_base.rb で `File#expand` を再定義し、
失敗しても通常の `f_write` で続行するようにした。本来は upstream の
picomodem.rb 側で rescue すべきなので、いずれ本家へ提案したい。

### メモリ

`handle_file_write` は受信データを Ruby String に全部溜めてから書き出す。
数十 KB のスクリプト用途では問題ないが、大きなファイルではヒープを圧迫する。

### DFU 非対応

`DFU_START` を受けると `DFU::Updater` が未定義（`picoruby-dfu` を組み込んでいない）
のため例外になり、`PicoModem.session` の rescue が ERROR フレームを返す。
ESP32 のファーム更新は OTA / NVS リスタート方式なので、RP2040 向けの DFU とは
仕組みが異なる。

## 動作確認手順

1. `./switch_board.sh m5stack midi_device`
2. `idf.py fullclean build flash`（`midi_device` では BOOT + RESET でダウンロードモード）
3. `idf.py monitor` で `> ` プロンプトを確認（既存機能の回帰チェック）
4. monitor を閉じ、web terminal から WebSerial で接続
5. Download: `/sd/app.rb` を取得 → 内容と CRC32 が一致すること
6. Upload: `/sd/test_upload.rb` を書き込み → `load /sd/test_upload.rb` で実行できること
7. 転送後に `> ` が復帰し、`load` / `heap` / `restart` が通常どおり動くこと

## 関連

- [PICORUBY_SUPERVISOR.md](PICORUBY_SUPERVISOR.md) — スクリプト切り替えの仕組み
- [MEMORY_ALLOCATION.md](MEMORY_ALLOCATION.md) — static バッファと DRAM レイアウト
