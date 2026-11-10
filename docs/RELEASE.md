# リリースの作り方

タグを push すると GitHub Actions がボードごとのファームウェアをビルドし、GitHub Release に
載せ、GitHub Pages の Web インストーラを最新版に更新する。

## 構成

| ファイル | 役割 |
|---------|------|
| [.github/workflows/firmware.yml](../.github/workflows/firmware.yml) | 配布する全ターゲットのビルド。main への push / PR でも走る（壊れたボードをタグの前に見つけるため） |
| [.github/workflows/release.yml](../.github/workflows/release.yml) | `v*` タグで firmware.yml を呼び、Release を作って pages.yml を呼ぶ |
| [.github/workflows/pages.yml](../.github/workflows/pages.yml) | `site/` と最新リリースの `.bin` を Pages へ。`site/` を変えて main に push したときも走る |
| [tools/release/build.sh](../tools/release/build.sh) | 1 ターゲットのビルドと梱包（CI とローカル共用） |
| [tools/release/sd_card.sh](../tools/release/sd_card.sh) | microSD カード用 zip（examples と plugins/bundle.mrb） |
| [tools/release/make_manifests.py](../tools/release/make_manifests.py) | ESP Web Tools のマニフェストと `firmware/index.json` |
| [site/](../site/) | LP（`/`、`/ja/`）とインストールガイド（`/install/`、`/ja/install/`） |

配布するターゲット（`firmware.yml` の matrix）:

| ボード | モード |
|-------|-------|
| freenove | host / midi_device |
| m5stack | host / midi_device |
| m5stack_tab5 | midi_device |
| crowpanel | host / midi_device |

serial モードは開発用なので配らない。

## Release の中身

- `midori-<tag>-<board>-<mode>.bin` … 全体を 1 つにまとめたイメージ（0x0 に書く。Web インストーラもこれを使う）
- `midori-<tag>-<board>-<mode>.zip` … bootloader / パーティションテーブル / アプリと `flash_args`
- `midori-<tag>-sd-card.zip` … SD カードのルートに展開するもの
- `SHA256SUMS.txt`

バージョンは `MIDORI_VERSION`（CI ではタグ名）を `version.txt` に書いて ESP-IDF の
`PROJECT_VER` にする。設定画面の「Version」はそれを `esp_app_get_description()` で読む。
ローカルのビルドでは `version.txt` が無いので `git describe` の値になる。

## 最初に 1 回だけやること

1. **Settings → Pages → Build and deployment → Source を「GitHub Actions」**にする
2. **Settings → Environments → github-pages → Deployment branches and tags** に
   タグのルール `v*` を足す（既定では main からしかデプロイできず、タグから呼ばれた
   pages.yml が「not allowed to deploy」で止まる）
3. （任意）リポジトリの About の Website に `https://kirikak2.github.io/midori/` を入れる

## タグを打つ前に

- [ ] サブモジュールの参照しているコミットがすべて push 済み（CI はクリーンなクローンから取る）
  - `components/midori-runtime/picoruby` は `kirikak2/picoruby` の `midori-ble` ブランチ。
    `.gitmodules` は `picoruby/picoruby` を指したままだが、GitHub はフォークのコミットも
    親リポジトリ経由で取らせるので取得はできる（2026-11-08 に確認）。**`midori-ble` を消すと
    過去のタグもビルドできなくなる**ので、PR #427 が入るまでは消さない
  - `mrbgems/picoruby-amy` のネストした `lib/amy`（`kirikak2/amy` の `midori` ブランチ）も同様
- [ ] main の Firmware ワークフローが全ターゲットで緑
- [ ] 実機での確認（下の「実機チェック」）
- [ ] 必要なら `docs/releases/<tag>.md` にリリースノートを書く（あれば自動生成の前に入る）

## タグを打つ

```bash
git tag -a v1.0.0 -m "MIDoRI 1.0.0"
git push origin v1.0.0
```

- バージョンは SemVer。`v1.1.0-rc.1` のように `-` を含むタグは **pre-release** になり、
  Pages（Web インストーラ）は最新の安定版を配る。安定版がまだ 1 つも無い間だけ、
  最新の pre-release を配る（1.0.0 の前に RC を Web インストーラで試せるように）
- 特定のリリースを配り直すときは Actions → Pages → Run workflow で `tag` を指定する
- 失敗したら Release とタグを消して打ち直す:
  `gh release delete v1.0.0 --cleanup-tag`

## ローカルでビルドする

ESP-IDF の環境で:

```bash
MIDORI_VERSION=v1.0.0-local tools/release/build.sh m5stack host dist
tools/release/sd_card.sh dist
python3 tools/release/make_manifests.py dist v1.0.0-local
```

`build.sh` は `switch_board.sh` を呼ぶので、作業ツリーの `sdkconfig` / `build/` は消える。

CI と同じ環境（`espressif/idf:v5.5.5` に `ruby` と `rake` を足したもの）で試すなら、
スクラッチのクローンを Docker に渡す:

```bash
docker run --rm -v "$PWD":/work -w /work espressif/idf:v5.5.5 bash -c '
  apt-get update && apt-get install -y ruby rake &&
  git config --global --add safe.directory "*" &&
  git submodule update --init --recursive --depth 1 &&
  . $IDF_PATH/export.sh && tools/release/build.sh m5stack host dist'
```

## Web インストーラ

- [ESP Web Tools](https://esphome.github.io/esp-web-tools/) 10.4.0（ESP32-S3 / ESP32-P4 対応）を unpkg から読む
- Release のアセットは CORS ヘッダが付かずブラウザから読めないので、pages.yml が最新リリースの
  `.bin` を `site/firmware/` にコピーして同じオリジンから配る
- CoreS3 と Freenove はどちらも ESP32-S3 でチップだけでは区別できないので、マニフェストは
  ボード × モードごとに 1 つ（`firmware/<board>-<mode>.json`）。ページは `firmware/index.json` を読んで選択肢を有効にする
- 初回は「Erase device」を勧める（`new_install_prompt_erase`）。別のビルドの NVS（パッチ・設定）を持ち越さないため

ページをローカルで見る:

```bash
mkdir -p site/firmware && cp dist/*.bin site/firmware/
python3 tools/release/make_manifests.py site/firmware v1.0.0-local
python3 -m http.server -d site 8000   # http://localhost:8000/install/（localhost は Web Serial が使える）
```

`site/firmware/` はコミットしない（.gitignore 済み）。

## 実機チェック

Web インストーラで書いたもの（= Release のイメージそのもの）で確かめる。

| ボード | モード | 書き込み | 起動・Home | USB MIDI | BLE-MIDI | 音 |
|-------|-------|---------|-----------|---------|---------|----|
| CoreS3 | host | | | | | SAM2695 |
| CoreS3 | midi_device | | | | | |
| Tab5 | midi_device | | | USB-A / USB-C | | AMY |
| CrowPanel | host | | | | — | AMY |
| CrowPanel | midi_device | | | | — | AMY |
| Freenove | host | | シリアル | | | |
| Freenove | midi_device | | シリアル | | | |

あわせて、インストールガイドのボードごとの手順（ダウンロードモードの入り方、ポートの名前）が
実物と合っているかを確かめる。
