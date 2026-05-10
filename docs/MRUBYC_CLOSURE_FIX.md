# mruby/c クロージャ use-after-free + 同期 block 伝播の調査記録 (2026-05-10)

`midi_monitor.rb` の起動時クラッシュから始まり、それを応急修理したあと `bach_air.rb` の MML 再生がずれる二次症状に発展し、最終的に mrubyc の **OP_GETUPVAR / OP_SETUPVAR + c_proc_call** を一括で再設計するに至った経緯の記録。

## TL;DR

- **症状 1 (crash / silent garbage)**: `Proc.new` で作ったクロージャが、生成元の関数が return した後に呼ばれると、tag=`#<Proc:...>` のような silent garbage を返したり、`OP_GETCONST`/`mrbc_find_method` で Load access fault でクラッシュする。
- **症状 2 (MML drift)**: 症状 1 を partial 修正した直後、`loop do clocks += tie_clocks end` のような同期 block が **block 内の rebind を外側に伝えなくなり**、MML タイ短縮 (`f+1&8`) や付点 (`a8.`) の長さが反映されず再生がずれる。
- **原因**:
  - 症状 1 = `proc->callinfo` / `proc->callinfo_self` が `mrbc_pop_callinfo` で free された callinfo を指しっぱなし (use-after-free)。slab が再利用されると silent garbage または crash。
  - 症状 2 = bc2ee1d (症状 1 の中間修正) が **`OP_GETUPVAR` / `OP_SETUPVAR` を snapshot 経由のみ** に倒したため、同期 block の write が **親フレームの live regs に伝わらなくなった** (回帰)。MRI Ruby なら通る `loop do x += 1 end` パターンが効かなくなった。
- **最終修正** (mrubyc 1 コミット):
  1. `mrbc_proc_new` で親 scope の regs を proc 内バッファに snapshot。`OP_GETUPVAR` は snapshot から読む。
  2. `OP_SETUPVAR` は snapshot に書く **AND** 親 callinfo がまだ active chain にいれば live regs にもミラー書き込み。
  3. `c_proc_call` は `callinfo_self` を一切 deref しない (`method_id=0`, `own_class=0`)。`OP_GETCONST` は `find_class_by_object(self)` にフォールバック。
- **構造体サイズ変更なし**。LCD 黒画面リスクを回避。

## 発生事象

### Phase 1: SysEx 由来と思われた

最初に踏んだ症状:

> midi_monitor.rb を立ち上げて、YAMAHA SEQTRAK から MIDI-DIN 経由で SysEx パケットを読み込むと強制終了する

スタックトレースは SysEx パーサ周りを指していて、`midi.c` の SysEx 処理がメモリ破壊しているのかと思った。実際 [docs/MEMORY_ALLOCATION.md](MEMORY_ALLOCATION.md) の "事故例" もまさに SysEx 関連だった。

### Phase 2: SysEx 無関係と判明

調査を進めるとクラッシュは SysEx 無しでも再現:

- Roland J-6 の通常 note でも落ちる
- 再生ボタン (= MIDI Clock 受信開始) で必ず落ちる
- ハンドラ数を減らすと頻度が下がる

クロージャがクラッシュ要因ではないかと推測し、クロージャ周りから問題を修正する方針に絞った。

### Phase 3: 「聞こえ方が変わった」(同期 block 回帰)

クラッシュ修正 (試行 4 まで進めて bc2ee1d としていったんマージした状態) を実機に焼いて `bach_air.rb` を再生してみたら、聞こえ方が以前と違う。`bach_air.rb` は J.S. バッハ Air on G string の MML 5 パート同時演奏 (約 1300 イベント)。耳では「どこが変か」を特定できない。**ログ + 理論値 diff の枠組み**を組んで原因切り分けに進んだ ([詳細は次節](#4-実機ログ-vs-理論値-を-diff-する枠組みmml-drift-用))。

## 原因特定までの調査方法

### 1. 最小再現コード (closure UAF 用)

`midi_monitor.rb` には MIDI ハードウェア・USB ホスト・FreeRTOS タスク・大量のハンドラ proc が絡んでいて切り分けが厳しい。MIDI を一切使わない最小再現を書いた:

```ruby
# examples/closure_repro.rb (抜粋)
def make_handler(tag)
  Proc.new { puts "tag=#{tag}" }
end

h = make_handler("HELLO")
h.call    # 期待: tag=HELLO
```

これがクラッシュ (selfbuild 当時) または `tag=#<Proc:...>` を出力 (upstream rebase 後) するなら、原因は MIDI ではなく **クロージャ実装そのもの**。

→ 実際 `tag=#<Proc:...>` が出た。**MIDI 一切無しでクロージャが壊れている** ことが確定。これで以降は ESP32 を起動しなくてもバグの本質を議論できるようになった。

### 2. mrubyc の Proc / callinfo のライフサイクルを読む

`OP_GETUPVAR` の実装 ([vm.c:823](../components/picoruby-esp32/picoruby/mrbgems/picoruby-mrubyc/lib/mrubyc/src/vm.c)):

```c
static inline void op_getupvar( mrbc_vm *vm, mrbc_value *regs EXT )
{
  ...
  mrbc_callinfo *callinfo = regs[0].proc->callinfo;
  ...
  p_val = callinfo->cur_regs + callinfo->reg_offset + b;
  ...
}
```

`proc->callinfo` を辿って親フレームの reg を読む。一方 `mrbc_pop_callinfo` ([vm.c:237](../components/picoruby-esp32/picoruby/mrbgems/picoruby-mrubyc/lib/mrubyc/src/vm.c)) は:

```c
void mrbc_pop_callinfo( mrbc_vm *vm )
{
  ...
  mrbc_callinfo *callinfo = vm->callinfo_tail;
  ...
  mrbc_free(vm, callinfo);   // ← 親 return で callinfo を free!
}
```

親関数が return すると `callinfo` が freed。Proc がそれを `proc->callinfo` で参照し続けるので、Proc が親より長生きすると free 後の変数をクロージャ越しに読みに行く use-after-free。

スラブが再利用されると、隣の領域にあった任意の値が `callinfo->cur_regs[reg_offset+b]` として読めてしまう。`tag=#<Proc:...>` はまさにそれで、たまたまその場所が新しく確保された Proc 値だった、というオチ。

### 3. もう一つの dangling: `c_proc_call`

最初は `OP_GETUPVAR` だけ見ていた。snapshot 方式 (後述) を入れて closure_repro.rb は通るようになったが、`midi_monitor.rb` は別の場所で落ちた:

```
Guru Meditation Error: Core 1 panic'ed (Load access fault)
MEPC : 0x4003547e   <-- op_getconst
MTVAL: 0x00425357   <-- "USB" + 2 (not a valid pointer!)
```

`OP_GETCONST` で読んでいるのは `vm->callinfo_tail->own_class`。同様にダングリング。`c_proc_call` ([c_proc.c:125](../components/picoruby-esp32/picoruby/mrbgems/picoruby-mrubyc/lib/mrubyc/src/c_proc.c)):

```c
static void c_proc_call(mrbc_vm *vm, mrbc_value v[], int argc)
{
  mrbc_callinfo *callinfo_self = v[0].proc->callinfo_self;     // ← dangling
  mrbc_callinfo *callinfo = mrbc_push_callinfo(vm,
                                (callinfo_self ? callinfo_self->method_id : 0),
                                ...);
  if( callinfo_self ) {
    callinfo->own_class = callinfo_self->own_class;  // ← garbage を伝播!
  }
  ...
}
```

`callinfo_self` が freed callinfo を指していて、そこから読んだガベージ `own_class` を新 `callinfo->own_class` に書き込む。後続の `OP_GETCONST` がこれを class struct としてデリファレンス → クラッシュ。

`closure_repro.rb` がこの 2 つ目の dangling を踏まなかったのは、`puts "tag=#{tag}"` が定数参照を含まないため `op_getconst` が呼ばれないから。`midi_monitor.rb` のハンドラは `UI.log(...)` を呼ぶので、`UI` 定数の lookup が起きそこで初めて踏む。

### 4. 「実機ログ vs 理論値」を diff する枠組み (MML drift 用)

bc2ee1d 統合後の MML drift は「聞こえ方が変」という曖昧な情報からスタート。耳で原因特定はできないので、**「いま実機が何を鳴らしているか」をクロック単位で取れる仕組み**を最初に組んだ。

- 実機側: `MIDI::Device` を `LoggingDevice` という薄いラッパで包み、`CombinedPlayer` が呼ぶ `trigger_batch` (note_on) と `note_off` を `[c=<clock> sc=<seq_clock>] on/off ch=N n=NN ...` 形式で `puts`。`bpm_loop` ブロックの先頭で現在の clock をラッパに食わせて、シリアルログに最初の 480 clock (≒48 sec) ぶん吐き出す。
- ホスト側: 既存の `tools/debug_mml_events.rb` を拡張。CRuby 上で同じ MML を `Sequence` にパースし、`CombinedPlayer` の送出順 (同一クロック内 note_off → note_on、シーケンス追加順) で全イベントをダンプ。フォーマットは実機ログと同じ。

これで `diff /tmp/expected.log /tmp/actual.log` が走る。

### 5. MML drift の原因特定: snapshot-only write が block→parent rebind を握りつぶしていた

実機ログを取ると、最初のイベント c=48 から既にずれていた:

| ch | MML 先頭 | 期待 clocks | 期待 d | 実機 d | 差 |
|----|---------|-------------|--------|--------|----|
| 0 | `f+1&8` | 96+12 = **108** | 10800 | **9600** | `&8` (=12) 欠落 |
| 1 | `d1&4`  | 96+24 = **120** | 12000 | **9600** | `&4` (=24) 欠落 |
| 1 | `a2&8`  | 48+12 = **60**  | 6000  | **4800** | `&8` (=12) 欠落 |

ch=0 を追跡すると、すべての note_on/off が **常に 12 clock 早い**。`&8` 分が冒頭で 1 回落ちただけで以降のイベントが順送りに前倒しになっていた。完璧に「タイ短縮で加算されるはずの長さがゼロ」と仮定したときの挙動と一致。

該当箇所はパーサの:

```ruby
clocks = length_to_clocks(length, dots)
loop do
  ...
  clocks += tie_clocks   # ← block 内 rebind
  ...
end
```

ホスト CRuby では `clocks` が正しく加算され 108 になるが、実機では 96 のまま素通り。**block 内の `+=` rebind が外側に書き戻されない** という挙動。同じ系統で `dots.times do total += dot_value end` (付点処理) も加算されず、`a8.` が `a8` として再生されていた (第 2 のずれ、c=222 で発覚)。

最初は「mrubyc の `Kernel#loop` が pure-Ruby (`def loop; while true; yield; end; end`) で、yield + closure rebind に問題がある」と仮説を立てたが、本当の原因は **bc2ee1d の OP_SETUPVAR が snapshot のみに書き込み、live parent regs を更新していない** ことだった。snapshot は `OP_GETUPVAR` でも読まれるので block 内の繰り返し参照は通るが、loop が return した後 parent が `clocks` を読むと **snapshot に行かず live regs を読む** ので、block の write は失われていた。

## 試行手順

### 試行 1: callinfo を refcount して延命する + 親の regs を save_regs に保存

最初に思いついた "ちゃんとした" 解。`mrbc_callinfo` に `refcount` と `saved_regs` フィールドを追加して、

- `mrbc_proc_new` で `callinfo->refcount++`
- `mrbc_pop_callinfo` で `refcount > 1` なら regs を heap にコピーしてから vm から外す
- `proc_delete` で `refcount--`、0 になったら free

実装は綺麗に書けたが、**`puts "Hello"` すら出なくなった**。

#### 原因

`op_return` は `regs[0] = regs[a]` で **返り値を `regs[0]` に置く**。直後に `mrbc_pop_callinfo` が走る。私の save loop は `regs[0..nregs-1]` を全部 saved_regs にコピーして元の slot を nil にしていた。**返り値を消してしまっていた**。

→ **教訓**: `pop_callinfo` 時点では `regs[0]` は既に return value で上書きされている。スコープ本来の self は失われている。snapshot を pop_callinfo で取るのは原理的に手遅れ。

### 試行 2: Proc 作成時に snapshot を取る (採用 — UAF 対策)

`mrbc_proc_new` の時点で `vm->cur_regs[0..nregs-1]` を Proc 内バッファに snapshot。`OP_GETUPVAR` / `OP_SETUPVAR` はこれを読む / 書く。

→ **`closure_repro.rb` は完璧に動作**。`tag=HELLO` × 73 回 + test PASSED。

しかし `midi_monitor.rb` は別の場所で落ちた (= 試行 3 の dangling `callinfo_self`)。さらに後の Phase 3 で **同期 block の rebind が parent に伝わらない** 回帰も発覚 (= 試行 7 で対処)。

### 試行 3: `captured_method_id` / `captured_own_class` を proc 構造体に追加

試行 2 と同じノリで `c_proc_call` 用にも snapshot を取る。`mrbc_proc` に 2 フィールド追加 (`mrbc_sym captured_method_id` + `struct RClass *captured_own_class`)。

ビルドして書き込んだら、LCD 表示が壊れた。

→ 以前 [docs/MEMORY_ALLOCATION.md](MEMORY_ALLOCATION.md) で類似ケース (SysEx 用 inline buffer で .bss レイアウトが変わって LCD が真っ黒) を経験していたので、同じパターン (struct サイズ変更でメモリレイアウトが変わって他の場所を破壊) と判断。`captured_method_id` / `captured_own_class` フィールド追加は破棄。試行 2 の状態へ戻した。

### 試行 4: `c_proc_call` で `callinfo_self` を一切 deref しない (採用)

構造体サイズを変えずに `c_proc_call` のダングリング読みだけ消す:

```c
mrbc_callinfo *callinfo = mrbc_push_callinfo(vm, 0, v - vm->cur_regs, argc);
if( !callinfo ) return;
callinfo->is_called_block = 1;
```

`callinfo->own_class` は `mrbc_push_callinfo` の初期値 0 のまま。`OP_GETCONST` ([vm.c:705](../components/picoruby-esp32/picoruby/mrbgems/picoruby-mrubyc/lib/mrubyc/src/vm.c)) は `vm->callinfo_tail->own_class == 0` なら `find_class_by_object(self)` フォールバックに落ちる。midori のハンドラ proc は self = top-level Object なので Object のクラスチェーンから `UI` 定数が引ける。**正常に解決される**。

→ `midi_monitor.rb` 動作。`closure_repro.rb` も引き続き動作。LCD も無事。**ここまでが bc2ee1d としていったんマージ**。

### 試行 5: midi_mml.rb の `loop do` / `dots.times do` を `while` に書き換え (workaround、KEEP)

Phase 3 (MML drift 発覚) の最初の対処。**修正対象を mrubyc ではなく picoruby-midi-mml の Ruby 側に限定** する workaround:

```ruby
# loop do ... clocks += tie_clocks ... end
while true
  ...
  clocks += tie_clocks
  ...
end

# dots.times do total += dot_value; dot_value /= 2 end
i = 0
while i < dots
  total += dot_value
  dot_value /= 2
  i += 1
end
```

`while` は method スコープの制御構造でクロージャ境界が無いので、`+=` がそのまま method ローカルに当たる。

→ 実機で MML drift 解消、bach_air.rb 再生 OK。**ただし根本原因は mrubyc 側にあるので、後で試行 7 でちゃんと直した**。

`while` 化自体は **call stack を浅く保つ** という別の利点もあるので、mrubyc 修正後も **midi_mml.rb の `while` rewrite は採用継続** ([picoruby commit `d6da5220`](../components/picoruby-esp32/picoruby/mrbgems/picoruby-midi-mml/mrblib/midi_mml.rb))。

### 試行 6: mrubyc に refcount tombstone を仕込む (失敗、ボツ)

「`mrbc_callinfo` に `refcount` + `is_dead` を持たせ、proc が掴んでいる callinfo は free を遅延、slab 再利用を防ぐ。OP_GETUPVAR/SETUPVAR は `is_dead` を見て live or snapshot を切替」という設計。

実装はクリーンに書けたが、**bach_air.rb が起動直後に `mrbc_find_method` で Load access fault**:

```
Guru Meditation Error: Core 1 panic'ed (Load access fault)
MEPC : 0x40031e44   <-- mrbc_find_method
MTVAL: 0x00000015   <-- cls=0x13 + offsetof(flag_module)
```

`send_by_name → find_class_by_object(recv) → cls=0x13 (garbage)`。`recv` が壊れた mrbc_value を読んでいる。

原因は完全には追えなかったが、refcount 周りの細かい lifecycle ミス (proc_new での 2 重 incref vs proc_delete での 2 重 decref のアンバランス、または OP_BREAK 等で `proc->callinfo` を pointer 比較する箇所が tombstoned 版に対応していない、etc.) のいずれか。設計を完全に詰めるには OP_BREAK / OP_BREAKERR / OP_ARGARY / OP_BLKPUSH の callinfo 触る箇所も全部 is_dead 対応にする必要があり、変更面積が大きすぎた。

→ **撤回**。試行 7 のシンプル路線へ。

### 試行 7: OP_SETUPVAR に live regs write-through を追加 (採用 — MML drift)

bc2ee1d (試行 2 + 試行 4) の snapshot 設計を温存しつつ、**`OP_SETUPVAR` の write 時だけ live parent regs にもミラー書き込み**を追加:

```c
// op_setupvar の末尾
if( callinfo_is_live(vm, proc->callinfo) ) {  // pointer walk of vm->callinfo_tail->prev
  mrbc_value *p_live = proc->callinfo->cur_regs + proc->callinfo->reg_offset + b;
  if( p_live != p_val ) {
    mrbc_decref( p_live );
    mrbc_incref( &regs[a] );
    *p_live = regs[a];
  }
}
```

| 経路 | bc2ee1d | 試行 7 |
|------|---------|--------|
| `OP_GETUPVAR` (read) | snapshot のみ | **snapshot のみ** ← 変更なし |
| `OP_SETUPVAR` (write) | snapshot のみ | **snapshot + (parent active なら) live regs** |
| 構造体サイズ | 変更なし | **変更なし** |
| `mrbc_pop_callinfo` | 即 free | **即 free** ← 変更なし |
| `mrbc_proc_new` / `delete` | 既存 | **変更なし** |

`callinfo_is_live` の slab 再利用リスクは write path だけで使うので影響限定的: 同期 block では parent は stack 上 → callinfo は free されてない → slab 再利用は起きえない。非同期 closure からの async write は理論上誤動作しうるが、async closure から outer に rebind する Ruby はレア。

→ 実機で **bach_air.rb (MML drift 解消) + closure_repro.rb (22 handlers 全て tag=H0..H21) + midi_monitor.rb (クラッシュなし)** すべて通る。

## 最終的な対策

mrubyc 1 コミット (`8ffe16b`) に統合済み:

```
Fix use-after-free of callinfo when a closure outlives its parent
```

### コード変更 (mrubyc)

- `src/c_proc.h`: `mrbc_proc` に `captured_regs_size` (uint16_t) と `captured_regs` (mrbc_value*) 追加。`callinfo` / `callinfo_self` は legacy として残し may-dangle と注記。
- `src/c_proc.c`:
  - `mrbc_proc_new`: 親 `cur_regs[0..nregs-1]` を heap snapshot し、各値を incref。
  - `mrbc_proc_delete`: snapshot を decref + free。
  - `c_proc_call`: `callinfo_self` を deref しない (`method_id=0`, `own_class=0`)。
- `src/vm.c`:
  - `OP_GETUPVAR`: snapshot から読む。チェーン walk も `proc->captured_regs[0]` 経由。
  - `OP_SETUPVAR`: snapshot に書く + `callinfo_is_live(vm, proc->callinfo)` なら live regs にもミラー書き込み。

### 補足変更 (picoruby-midi-mml)

- `loop do` → `while true` ([commit `d6da5220`](../components/picoruby-esp32/picoruby/mrbgems/picoruby-midi-mml/mrblib/midi_mml.rb))。mrubyc 修正でセマンティクス的には不要だが、`while` の方が **call stack が浅く** 安全という別の理由で採用継続。
- `dots.times do` → `while i < dots` (同上)。

### トレードオフ

| 項目 | bc2ee1d 前 | bc2ee1d (中間) | 試行 7 後 (現状) |
|---|---|---|---|
| read-only クロージャ | crash / silent garbage | 正常 | 正常 |
| 同期 block の `outer += x` 伝播 | (crash しなければ) parent に届く | **届かない** ← 回帰 | parent に届く |
| 非同期 closure の `outer += x` 伝播 | crash / silent garbage | snapshot に閉じる | snapshot に閉じる (live への mirror は parent 消滅で skip) |
| ブロック内の定数参照 (`CONST` 形式) | dangling own_class で crash | `self` のクラスから lookup | `self` のクラスから lookup |
| `OP_SETUPVAR` の slab 再利用誤動作 | — | — | 理論上あり (async write 限定、稀) |

`Class::CONST` 明示形式は引き続き動く。

### upstream 報告

mrubyc upstream (`github.com/mrubyc/mrubyc`) への PR を予定。ブランチ `fix/closure-dangling-callinfo` を fork に push して PR を立てる。PR description は `git log -1 8ffe16b` のコミットメッセージをそのまま使える状態。

### midori 側適用

| 階層 | コミット | 内容 |
|---|---|---|
| mrubyc | `8ffe16b` | 上記の修正本体 (snapshot + write-through + c_proc_call スキップ) |
| picoruby | `d6da5220` | `picoruby-midi-mml: avoid mrubyc closure rebind in MML parser` (`while` rewrite、stack 浅化) |
| picoruby | `db2e2e1d` | `mrubyc: bump to closure use-after-free fix` |
| picoruby-esp32 | `ac49861` | `Bump picoruby for mrubyc closure use-after-free fix and MML while-rewrite` |
| midori | `e69fe1e` | `Bump picoruby-esp32 for closure use-after-free fix` (+ `tools/debug_mml_events.rb` 拡張) |

## 学び

1. **「特定のデバイス/入力で壊れる」と疑った時点で安易に決めつけない**。最小再現を独立に作って原因スコープを限定する。今回 SysEx と MIDI clock を疑って数時間溶かしたが、最終原因は MIDI 完全無関係だった。
2. **メモリレイアウト変更は実機でしか出ない症状を生む**。コードレビューで OK でも LCD/起動シーケンスが壊れることがある ([docs/MEMORY_ALLOCATION.md](MEMORY_ALLOCATION.md) の事故と全く同じ轍)。`mrbc_proc` への field 追加 (試行 3) も `mrbc_callinfo` への refcount/is_dead 追加 (試行 6) も実機で踏んだ。**struct サイズを変えるなら、それが本当に必要か再確認**する。
3. **Use-after-free は dangling な場所が複数あることが多い**。`OP_GETUPVAR` を直しただけでは `c_proc_call` の同根バグが残った (試行 2 → 試行 4)。ある dangling pointer を見つけたら、その struct field の **全使用箇所** を grep して総点検する。
4. **`mrbc_pop_callinfo` のタイミングで snapshot を取ろうとすると `regs[0]` が return value で上書き済み**。スコープのライフサイクルに沿った snapshot 採取は **作成時** が正解。pop 時は遅すぎる (試行 1 の失敗)。
5. **「修正の修正」の回帰に注意**。bc2ee1d は UAF を直したが、snapshot-only writes に倒したことで MRI 互換セマンティクスを失い、同期 block の rebind 伝播が壊れた (Phase 3)。**snapshot は読み専用、書きは live にも mirror** というハイブリッド設計でようやく両立した (試行 7)。
6. **「聞こえ方が変わった」レベルの曖昧な症状はログ + 理論値 diff の枠組みを最初に作る**。`LoggingDevice` ラッパと `debug_mml_events.rb` の timeline ダンプを作るのに 30 分、出力 diff から原因特定まで数分。ホスト CRuby と実機 mrubyc を同じ Ruby ソースで走らせて diff できる枠組みは応用が効く。
7. **完璧な意味論より crash しない方がマシ**。`OP_SETUPVAR` の slab 再利用誤動作 (試行 7 のトレードオフ) は理論上残るが、async write が稀な現実では実用上問題なし。完璧解 (refcount + 完全な closure semantics) は将来の仕事として upstream に委ねる。
8. **`while` を選ぶ別の理由**。`loop do` / `n.times do` を `while` に書き換えるのは mrubyc 修正で意味論的には不要になったが、**call stack を浅く保つ** という独立した利点があるので keep ([picoruby-midi-mml の `commit d6da5220`](../components/picoruby-esp32/picoruby/mrbgems/picoruby-midi-mml/mrblib/midi_mml.rb))。組込み環境では block 呼び出しが多いと再帰で stack を食いやすい。
