# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## プロジェクト概要

ESP32-S3向けのESP-IDF USB Host MIDIサンプルプロジェクト。USB MIDIデバイス（例：Roland J-6）を検出し通信するUSBホスト機能を実装。FreeRTOSによるマルチタスク構成。

## ビルドコマンド

最初にESP-IDF環境をソースする必要あり：
```bash
source ~/esp-idf/export.sh
```

ビルド・書き込みコマンド：
- `idf.py build` - プロジェクトをコンパイル
- `idf.py flash` - ファームウェアをデバイスに書き込み
- `idf.py monitor` - シリアル出力をモニタ（Ctrl+]で終了）
- `idf.py flash monitor` - 書き込み後すぐにモニタ開始
- `idf.py fullclean` - ビルド成果物を全削除
- `idf.py menuconfig` - プロジェクト設定（sdkconfig）

## アーキテクチャ

### アクションベースのステートマシン

メインアプリケーション（`main/freenove-usbhost-sample.c`）は、`driver_obj.actions`にビットマスクで保留中の操作を格納し、ループで処理するパターンを使用：

```
ACTION_OPEN_DEV → ACTION_GET_DEV_INFO → ACTION_GET_DEV_DESC →
ACTION_GET_CONFIG_DESC → ACTION_GET_STR_DESC → ACTION_CHECK_MIDI →
ACTION_SETUP_MIDI → ACTION_SEND_NOTE（繰り返し）
```

切断時：`ACTION_CLOSE_DEV`でクリーンアップし、再接続に備えて状態をリセット。

### スレッドモデル

- **メインタスク**（`app_main`）：USBホストライブラリのイベントループを実行
- **クラスドライバタスク**（`class_driver_task`）：デバイス固有の処理を担当、コア0に固定

### USB MIDI検出

MIDIデバイスはインターフェースディスクリプタでAudioクラス（0x01）かつMIDI Streamingサブクラス（0x03）をチェックして検出。検出後、インターフェースをクレームしMIDI IN/OUTバルクエンドポイントを探索。

### 転送コールバック

- `midi_in_transfer_callback()`：受信MIDIデータを処理、メッセージをデコード、転送を再サブミット
- `midi_out_transfer_callback()`：MIDI送信完了を処理、遅延後に次のノートをトリガー

### ホットプラグ対応

グローバル静的変数（`g_in_transfer`、`g_midi_enabled`等）で接続/切断サイクルをまたいでデバイス状態を追跡。`reset_midi_static_vars()`でデバイス取り外し時に状態をクリーンアップ。

## 主要データ構造

```c
typedef struct {
    usb_host_client_handle_t client_hdl;
    uint8_t dev_addr;
    usb_device_handle_t dev_hdl;
    uint32_t actions;           // 保留中アクションのビットマスク
    uint8_t midi_in_ep;         // MIDI INエンドポイントアドレス
    uint8_t midi_out_ep;        // MIDI OUTエンドポイントアドレス
    bool is_midi_device;
    uint8_t note_counter;       // C4-C5を循環するカウンタ
    uint8_t num_interfaces;
} class_driver_t;
```

## USB MIDIパケットフォーマット

4バイトパケット：`[ケーブル番号 + CIN][MIDIバイト1][MIDIバイト2][MIDIバイト3]`
- Note On CIN: 0x09
- Note Off CIN: 0x08
