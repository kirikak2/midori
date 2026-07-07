# ヒープ破壊バグ 調査記録（真因: FatFs static ボリュームテーブルのダングリング書き込み）

**日付**: 2026-07-04 〜 2026-07-07
**状態**: **解決済み**（2026-07-07）。真因を特定し修正。
**ブランチ**: `picoruby-usb_midi_device`

> 旧タイトルは「ビルトインクラス メソッドチェーン Use-After-Free 調査記録」。
> 調査当初は mruby/c のビルトインメソッドチェーン UAF が原因と考えていたが、
> それは**症状**であり、真因は別だった。以下に結論・真因・経緯・修正を記す。

---

## TL;DR（結論）

長期間別々の不具合だと思っていた 2 症状は、**同一の 1 バグの別々の顔**だった:

1. **`mrbc_find_method()` で `0x0103016d` Load access fault**（`Array.method_link` 経由・再現性 100%）
2. **`usb_midi_device_pad.rb` が起動直後だと無出力でハング**（別スクリプトで priming すると動く）

**真因**: FatFs の `static FATFS* FatFs[FF_VOLUMES]`（`ff.c`）は mruby/c ヒープ上に
`mrbc_instance_new(sizeof(FATFS))` で確保された FATFS を指す。スクリプト切替時の
`cleanup_vm()` → `mrbc_cleanup()` で**ヒープ全体がリセット**されても、この **C static は残存**し
**ダングリングポインタ**になる。次の `f_mount()` が

```c
cfs = FatFs[vol];
if (cfs) { ...; cfs->fs_type = 0; }   // ff.c f_mount() 冒頭
```

で**古いアドレスへ 0 を書き込む**。そのアドレスは既に別オブジェクトに再利用されており、
**生きたヒープブロックのヘッダ（16 バイトブロックの size フィールドの低位バイト `0x13`→`0x00`）を潰す**。

**修正**: VM ヒープを消す前に `FatFs[]` を**デリファレンスせずに**クリアする。

```c
// ff.c
void ff_clear_volumes(void) { for (int i = 0; i < FF_VOLUMES; i++) FatFs[i] = 0; }
```

を追加し、`picoruby_supervisor.c` の `cleanup_vm()` で `mrbc_cleanup()` の**前**に呼ぶ。

**closure fix・no_free ワークアラウンド・USB/TinyUSB DMA はすべて無罪**だった。これらはヒープの
レイアウトを変えることで「破壊アドレスが致命的なブロックに当たるかどうか」を左右していただけ。
これが「closure fix を入れると壊れる」「no_free を外すと壊れる」「priming すると直る」
「USB device スクリプト特有」といった**無数の赤ニシン**を生んでいた。

---

## 1 バグが 2 症状に化けた理由（レイアウト依存）

`cfs->fs_type = 0` は 1 バイトのゼロ書き込みだが、**どこに当たるか**でヒープの壊れ方＝症状が変わる:

- **症状 2（ハング）**: ダングリングアドレスが 16 バイトブロックの size 低位バイトに当たると、
  size が `0x00000000` になる。以降 `mrbc_raw_alloc_no_free()` の物理ブロック走査
  （`ff14b` の symbol ロード時など、`flag_permanence=1` の VM で毎回通る）が
  `PHYS_NEXT(blk) == blk` で**無限ループ**。Script Mode は `Watchdog.disable` のため
  WDT も出ず、**クラッシュせず無言でハング**（=「無出力」）。
- **症状 1（クラッシュ）**: 破壊が head 領域のブロック管理を混乱させ、巡り巡って
  ビルトインクラス表（`Array` の `.data`）の `method_link` フィールドが別ノード
  （永続 `sleep_ms` メソッドノード。sym 259, `c_sleep_ms`）の中身で上書きされると、
  `mrbc_find_method()` がそのフィールドを次ノードポインタとして deref して
  **`0x0103016d`（= メソッドノード先頭ワードの中身）で Load fault**。

破壊アドレス X（= UI モードでマウントした時の FATFS のアドレス）が、スクリプトモードの
ヒープ状態で「どのブロックのどのバイト」に一致するかは、次の要素で変わる:

- **UI モードで SD をマウント済みか**（X が確定する）
- **`script_to_run` の文字列長**（`get_autorun_script` が返す String のサイズでレイアウトがずれる）
- **closure fix の有無**（`mrbc_proc_new` の captured_regs 追加確保でレイアウトがずれる）
- **no_free ワークアラウンドの有無**（Array のメソッドノードを head→tail 領域へ動かす）

→ 「特定スクリプトだけ」「priming で直る」「closure fix で悪化」「no_free で止まる」は
すべて**同じ破壊の当たり所がずれていただけ**。

---

## 真因の特定（決め手となった観測）

シリアルが使えない Tab5 で、`ui_add_log()` による LCD 段階トレースと、
ヒープ物理走査で size=0 ブロックを検出する `mrbc_heap_first_bad()` +
「0→1 遷移時だけ 1 行出す」`mrbc_heap_check(ctx)` を用いて、破壊の**発生段階を二分探索**した:

```
compile(pad.rb) OK → EXE-preload → mrbc_load_mrb でハング
  → シンボルロードの mrbc_raw_alloc_no_free 内、物理走査が size=0 ブロックで無限ループ
破壊ブロック: @+52868 sz=00000000, 直前 @+52852 psz=0x13 (16B, 無傷)
  → 大きなバッファのオーバーフローではなく、精密な 4/1 バイトのゼロ書き込み
段階トレース: after-SPI.new(健全) → f_mount(破壊) → dio:pre-SD_init(既に破壊)
  → f_mount の disk_initialize 呼び出し前 = ff.c:3620-3629 の cfs->fs_type=0
FATFS インスタンス: fs@+46164 sizeof=4156 end@+50320
  → 破壊 @+52868 は FATFS の外（+2548 先）= fs 自身のオーバーフローではない
  → f_mount vol=2 cfs@+<非NULL>（= FatFs[2] にダングリングが残っていた）
```

**修正検証**: `ff_clear_volumes()` を `cleanup_vm()` に入れると `f_mount vol=2 cfs=NULL` となり、
ダングリング書き込みが起きず、**priming 無しの fresh 起動で `usb_midi_device_pad.rb` が完動**・
ハング解消。さらに **no_free ワークアラウンドを外したビルドでも `0x0103016d` クラッシュが
再現しなくなった**ことをユーザーが確認 → 症状 1・2 が同一原因であることが確定。

---

## 修正（恒久・コミット対象）

| ファイル | 変更 |
|---|---|
| `picoruby-filesystem-fat/lib/ff14b/source/ff.c` | `ff_clear_volumes()` 追加（`FatFs[]` をデリファレンスせずゼロクリア） |
| `picoruby-filesystem-fat/lib/ff14b/source/ff.h` | `ff_clear_volumes()` プロトタイプ宣言 |
| `components/picoruby-esp32/picoruby_supervisor.c` | `cleanup_vm()` の `mrbc_cleanup()` **直前**で `ff_clear_volumes()` を呼ぶ |

```c
// picoruby_supervisor.c cleanup_vm()
{ extern void ff_clear_volumes(void); ff_clear_volumes(); }
mrbc_cleanup();
```

**設計原則**: **C の static/global がVMヒープ上のオブジェクトを指す場合、`mrbc_cleanup()` の
前に必ずそのポインタを捨てる（デリファレンス禁止）**。FatFs の `FatFs[]` はその代表例。
将来 littlefs 等の別 FS や、VM ヒープを指す他の C static を足すときも同じ配慮が要る。

---

## 後始末・フォローアップ

- **no_free ワークアラウンド（`sub_op_def`/`op_alias` で `flag_builtin` 時に永続確保）は
  真因修正後は不要**の可能性が高い（外しても `0x0103016d` が再現しないことを確認済み）。
  ただしビルトインへの monkey-patch ノードのライフサイクルという別論点も含むため、
  撤去は影響確認の上で別途判断する。
- 本調査で入れた診断コード（`ui_add_log` トレース各所、`mrbc_heap_first_bad` /
  `mrbc_heap_check` / `mrbc_pool_offset`、`mrbc_raw_alloc_no_free` のハングガード、
  sandbox の vm_code ハッシュ probe 等）は**すべて撤去済み**。恒久修正のみ残置。
- 教訓: **midori-local gem（mrbgems/picoruby-*）の mrblib(.rb) や C を変更したら
  `idf.py fullclean`**。`idf.py build` だけでは gem の .rb が再コンパイルされないことがある。

## 反証された旧仮説（記録として）

真因にたどり着くまでに否定した説。同じ罠を踏まないために残す:

1. **「サブ VM 終了時の `mrbc_free_all` がリンク中の 'M' ノードを解放して dangling」** — 誤り。
   このビルドは **`MRBC_ALLOC_VMID` 未定義**で `mrbc_free_all` は no-op マクロ。
   vm_id タグ機構自体が存在せず、この筋書きは丸ごと成立しない。
2. **「`UI.set_screen(LOGS)` しないと描画されない（active 画面説）」** — 赤ニシン。
   撤去しても動く。`UI.log` は `UI.process` と無関係に描画される。
3. **「no_free が stale で未反映」「incremental build 起因」** — 部分的に真
   （fullclean は実際に必要）だが、無出力の**真因ではなかった**。
4. **「closure fix が captured_regs で refcount を壊す」「reset_vm のレジスタ初期化漏れ」** —
   いずれも無罪。closure fix はレイアウトを変えて破壊の当たり所をずらしていただけ。
5. **「USB transport / TinyUSB DMA 特有」** — 無関係。USB device スクリプト群が
   たまたま「UI モードで SD マウント → 特定パス長のスクリプト」という再現条件を踏んでいた。

`0x0103016d` が「メソッドノード先頭ワードの中身（`sleep_ms`）」である点、`pack` が 'M'
（`mrbc_raw_alloc`）ノードである点、no_free でクラッシュが止まる点、といった**生データの
観測は正しかった**。誤っていたのは「誰が壊すか」の解釈（mruby/c 内部と決めつけ、
FatFs の C static を疑わなかった）。

---

## 調査を支えた診断テクニック（再利用のために）

Tab5 は TinyUSB が USB-C を占有し**シリアルログが取れない**。また coredump は
**タスク限定**で PSRAM ヒープ・`.bss` は含まれない。ハングはクラッシュしないため
coredump すら出ない。この制約下で有効だった手法:

1. **LCD への段階トレース（`ui_add_log`）**: `puts`（USB-CDC）が使えない環境で、
   C 側から直接 `ui_add_log()` を呼んで LCD の LOGS 画面に段階マーカーを出す。
   ロードパイプライン（Sandbox.new → compile → execute → mrbc_load_mrb → symbol load）を
   段階名で刻み、**どこで止まるか / どこで例外か**を可視化。
2. **ヒープ整合チェッカ + 遷移ログ**: 物理ブロックを `PHYS_NEXT` で走査し
   size=0（= `PHYS_NEXT(blk) <= blk`）を検出する `mrbc_heap_first_bad()`。
   これを多数の地点から呼び、**0→1（健全→破壊）遷移の瞬間だけ 1 行ログ**する
   `mrbc_heap_check(ctx)` で、スクロールする LCD でも破壊発生段階を確実に捕捉。
   段階を細分化して二分探索することで、Ruby の require → SD 初期化 → `Shell.setup_sdcard`
   → `f_mount` → `cfs->fs_type=0` まで追い込めた。
3. **無限ループを「検出→ログ→フォールバック脱出」に変える**: ハングを起こす
   走査ループに「異常検出時はログを出して安全に抜ける」ガードを一時的に入れると、
   ハングが**観測可能なログ + 継続実行**に変わり、その先の挙動（＝破壊が致命的でない事実）まで分かる。
4. **破壊ブロックの近傍情報を採る**: 破壊ブロックだけでなく**直前・直前々ブロックの
   size / used フラグ**も報告させ、「大きなバッファのオーバーフロー」か「精密な少バイト書き込み」かを判別。
   隣接が無傷の 16 バイトブロックだった事実が「オーバーフローではなくダングリング書き込み」の決め手になった。
5. **coredump パーティションの直接読み出し**（クラッシュ時のみ有効）:
   ```bash
   python -m esptool --chip esp32p4 -p PORT erase_region 0x200000 0x10000  # 事前クリア
   python -m esptool --chip esp32p4 -p PORT read_flash 0x200000 0x10000 core_live.bin
   python -m esp_coredump --chip esp32p4 info_corefile --core core_live.bin --core-format raw build/midori.elf
   ```
6. **タスクスタックへのスナップショット**: 見たい PSRAM 領域を診断関数内で `memcpy` して
   クラッシュタスクのスタック上ローカルに置き `abort()`。coredump に確実に載る。
   最適化除去を防ぐため `asm volatile("" :: "r"(p) : "memory")`。
7. **coredump の DRAM は「古い」**: 直近ストアは L1 に dirty のまま残り DRAM 読みに
   反映されないことがある。「DRAM は clean なのにレジスタは破壊値」は診断関数内の
   **volatile 再読**で解消。

---

## 関連ファイル

- `picoruby-filesystem-fat/lib/ff14b/source/ff.c` / `ff.h`
  - `f_mount()`（`cfs->fs_type = 0` のダングリング書き込み地点）、`ff_clear_volumes()`（**修正**）
- `picoruby-filesystem-fat/src/mrubyc/fat.c`
  - `c__mount()`: `mrbc_instance_new(sizeof(FATFS))` で FATFS を VM ヒープに確保 → `f_mount` に渡す
- `components/picoruby-esp32/picoruby_supervisor.c`
  - `cleanup_vm()`（`mrbc_cleanup` 前に `ff_clear_volumes` を呼ぶ）
- `components/picoruby-esp32/mrblib/main_task_base.rb`
  - `try_init_sd_card` / `Shell.setup_sdcard` 経由で UI モード・スクリプトモード両方で `f_mount` する
- `picoruby-mrubyc/lib/mrubyc/src/alloc.c`
  - `mrbc_raw_alloc_no_free()`（物理走査が size=0 ブロックで無限ループ＝ハングの発現点）、`mrbc_cleanup`
- `picoruby-mrubyc/lib/mrubyc/src/class.c`
  - `mrbc_find_method`（`0x0103016d` クラッシュの発現点）
- `docs/MRUBYC_CLOSURE_FIX.md`
  - closure fix（本件では無罪だが、同時期に疑った別修正）
