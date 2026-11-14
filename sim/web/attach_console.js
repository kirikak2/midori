// The Attach panel (docs/ATTACH.md): Ruby typed here runs inside a running
// VM - the simulator's, or the connected MIDoRI's - the next time its
// script calls UI.process / MIDI.sleep_ms. It sees the script's globals,
// constants and classes, and its own locals stay from one run to the next.
//
//   const ui = mountAttachConsole(el, { targets });
//   ui.refresh();   // a target came or went (the device connected, a new VM)
//
// targets: [{ id, label, state() -> { available, text }, evaluate(code, { onOutput })
//             -> Promise<{ ok, value }>, cancel() }]

import { t } from './i18n.js';

const HISTORY_STORE = 'midori-sim/attach-history';
const HISTORY_MAX = 100;
const WAIT_NOTICE_MS = 3000;

export function mountAttachConsole(el, { targets }) {
  el.innerHTML = `
    <style>
      .ac-head { display: flex; flex-wrap: wrap; gap: 6px; align-items: center; }
      .ac-head .hint { margin: 0; }
      .ac-out { height: 18vh; min-height: 100px; overflow: auto; margin: 8px 0 6px; padding: 6px 8px;
                background: var(--code-bg); border: 1px solid var(--line); border-radius: 6px;
                font: 12px/1.4 ui-monospace, monospace; white-space: pre-wrap; word-break: break-all; }
      .ac-out .in { color: var(--muted); }
      .ac-out .val { color: var(--accent); }
      .ac-out .err { color: var(--err); }
      .ac-out .note { color: var(--muted); font-style: italic; }
      .ac-row { display: flex; gap: 6px; align-items: flex-start; }
      .ac-row textarea { flex: 1; min-width: 0; min-height: 2.2em; resize: vertical; font: 12px/1.4 ui-monospace, monospace;
                         padding: 4px 6px; border: 1px solid var(--line); border-radius: 6px; background: var(--code-bg); color: var(--fg); }
      .ac-row textarea:focus { outline: none; border-color: var(--accent); }
      .ac-row button:disabled, .ac-head select:disabled { opacity: .45; cursor: default; }
    </style>
    <div class="ac-head">
      <label>${t('対象', 'Target')} <select class="ac-target" aria-label="${t('attach する VM', 'The VM to attach to')}"></select></label>
      <span class="ac-state hint"></span>
      <button class="ac-clear small" style="margin-left:auto">${t('クリア', 'Clear')}</button>
    </div>
    <div class="ac-out" role="log" aria-live="polite"></div>
    <div class="ac-row">
      <textarea class="ac-in" rows="2" spellcheck="false" placeholder="${t('Ruby（Enter で実行、Shift+Enter で改行、↑↓ で履歴）', 'Ruby (Enter runs, Shift+Enter for a new line, ↑↓ history)')}"></textarea>
      <button class="ac-run primary">${t('実行', 'Run')}</button>
      <button class="ac-cancel" hidden>${t('取り消す', 'Cancel')}</button>
    </div>
    <p class="hint">${t('スクリプトが UI.process / MIDI.sleep_ms を呼んだときに、その VM の中で実行します。スクリプトのグローバル変数・定数・クラスが見えます（スクリプトのファイルのローカル変数は見えません）。',
                         'Runs inside the VM when its script calls UI.process / MIDI.sleep_ms. The script\'s globals, constants and classes are there (not the script file\'s locals).')}</p>`;
  const $ = sel => el.querySelector(sel);
  const out = $('.ac-out'), input = $('.ac-in'), select = $('.ac-target');
  let busy = null;   // the target running an eval

  let history = [];
  try { history = JSON.parse(localStorage.getItem(HISTORY_STORE) || '[]'); } catch (e) {}
  let hpos = history.length;
  let draft = '';

  for (const tg of targets) select.add(new Option(tg.label, tg.id));
  const target = () => targets.find(x => x.id === select.value) || targets[0];

  function line(text, cls) {
    const d = document.createElement('div');
    if (cls) d.className = cls;
    d.textContent = text;
    out.append(d);
    while (out.childElementCount > 1000) out.firstChild.remove();
    out.scrollTop = out.scrollHeight;
    return d;
  }

  function refresh() {
    for (const opt of select.options) {
      const tg = targets.find(x => x.id === opt.value);
      opt.disabled = !tg.state().available;
    }
    // The selected target went away: the first one that is there
    if (select.selectedOptions[0]?.disabled) {
      const ok = [...select.options].find(o => !o.disabled);
      if (ok) select.value = ok.value;
    }
    const st = target().state();
    $('.ac-state').textContent = st.text;
    $('.ac-run').disabled = !!busy || !st.available;
    select.disabled = !!busy;
    $('.ac-cancel').hidden = !busy;
  }

  async function run() {
    const code = input.value;
    if (!code.trim() || busy) return;
    const tg = target();
    if (!tg.state().available) return;
    if (history[history.length - 1] !== code) history.push(code);
    if (history.length > HISTORY_MAX) history = history.slice(-HISTORY_MAX);
    try { localStorage.setItem(HISTORY_STORE, JSON.stringify(history)); } catch (e) {}
    hpos = history.length;
    draft = '';
    input.value = '';
    code.split('\n').forEach((l, i) => line(`${i ? '… ' : '› '}${l}`, 'in'));
    busy = tg;
    refresh();
    let note = null;
    const timer = setTimeout(() => {
      note = line(t('（スクリプトが UI.process / MIDI.sleep_ms を呼ぶのを待っています。取り消しもできます）',
                    '(waiting for the script to call UI.process / MIDI.sleep_ms; you can cancel)'), 'note');
    }, WAIT_NOTICE_MS);
    try {
      const r = await tg.evaluate(code, { onOutput: s => { if (note) { note.remove(); note = null; } line(s); } });
      if (r.ok) line(`=> ${r.value}`, 'val');
      else line(r.value, 'err');
    } catch (e) {
      line(e.cancelled ? t('取り消しました', 'Cancelled') : e.message, e.cancelled ? 'note' : 'err');
    } finally {
      clearTimeout(timer);
      if (note) note.remove();
      busy = null;
      refresh();
      input.focus();
    }
  }

  input.addEventListener('keydown', e => {
    if (e.key === 'Enter' && !e.shiftKey && !e.isComposing) { e.preventDefault(); run(); return; }
    // History: at the first line going up, at the last line going down
    const before = input.value.slice(0, input.selectionStart);
    const after = input.value.slice(input.selectionEnd);
    if (e.key === 'ArrowUp' && !before.includes('\n') && hpos > 0) {
      e.preventDefault();
      if (hpos === history.length) draft = input.value;
      input.value = history[--hpos];
    } else if (e.key === 'ArrowDown' && !after.includes('\n') && hpos < history.length) {
      e.preventDefault();
      input.value = ++hpos === history.length ? draft : history[hpos];
    }
  });
  $('.ac-run').onclick = run;
  $('.ac-cancel').onclick = () => { if (busy) busy.cancel(); };
  $('.ac-clear').onclick = () => { out.textContent = ''; };
  select.onchange = refresh;

  refresh();
  return { refresh };
}
