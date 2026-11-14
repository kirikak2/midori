// The simulator's "MIDoRI" panel (docs/WEB_SIMULATOR.md §13): connect to a
// real MIDoRI over USB CDC (midori_link.js) and work with its scripts. The
// Scripts part works only while connected; there is no simulated /sd.
//
//   /app.rb      the startup script (internal flash): runs once after power-up
//   /sd/*.rb     the Scripts screen's scripts
//   /plugins, /sd/plugins   loaded at every start, in that order
//
// mountDevicePanel(el, { link, print, getCode, setCode, getName, onDeviceFiles, onScriptList })
//   onDeviceFiles(files | null): the device's plugins and /app.rb
//   ([{ path, data }]) for the simulator's start-up, or null when they are gone
//   onScriptList(names | null): the device's /sd/*.rb names for the
//   simulator's Scripts screen, or null when disconnected
//   startupFiles: offer "use the device's plugins and /app.rb" (default true)
//   beforeSave(path, existing): async; false keeps the device's file (the
//   designer will not overwrite a hand-written script); existing is the
//   device's current text or null
//   setCode(text, path): may return false when the page cannot take the file

import { PACES } from './midori_link.js';
import { t } from './i18n.js';

const PLUGIN_DIRS = ['/plugins', '/sd/plugins'];   // main_task_base.rb PLUGIN_DIRS
export const APP_SCRIPT = '/app.rb';
const dec = new TextDecoder();
const isRuby = n => /\.(rb|mrb)$/i.test(n) && !n.startsWith('_');

const MODES = { ui: t('UI モード', 'UI mode'), script: t('スクリプト実行中', 'running a script'), irb: t('irb 実行中', 'in irb'), stopping: t('停止中', 'stopping') };

export function mountDevicePanel(el, { link, print, getCode, setCode, getName, onDeviceFiles = () => {}, onScriptList = () => {},
                                      startupFiles = true, beforeSave = null }) {
  el.innerHTML = `
    <style>
      #dp-progress { display: flex; align-items: center; gap: 8px; margin: 6px 0; }
      #dp-progress[hidden] { display: none; }
      #dp-progress progress { flex: 1; max-width: 360px; height: 8px; accent-color: var(--accent); }
      #dp-body button:disabled, #dp-body input:disabled { opacity: .45; cursor: default; }
    </style>
    <div class="row">
      <button id="dp-connect" class="primary">${t('実機に接続', 'Connect')}</button>
      <span id="dp-status" class="hint" style="margin:0"></span>
    </div>
    <div id="dp-body" hidden>
      <div class="row">
        <button id="dp-refresh">${t('一覧を更新', 'Refresh')}</button>
        <button id="dp-stop">${t('実機のスクリプトを停止', 'Stop the device\'s script')}</button>
        <label class="hint" style="margin:0" ${startupFiles ? '' : 'hidden'}><input type="checkbox" id="dp-use"> ${t('シミュレーターの起動にも実機のプラグインと /app.rb を使う', 'Start the simulator with the device\'s plugins and /app.rb')}</label>
      </div>
      <div id="dp-progress" hidden><progress></progress><span id="dp-progress-label" class="hint" style="margin:0"></span></div>
      <div id="dp-busy" class="hint" hidden></div>
      <table id="dp-files"><tbody></tbody></table>
      <div class="row">
        <label>${t('名前', 'Name')} <input type="text" id="dp-name" style="width:12em" placeholder="my_script"></label>
        <button id="dp-save-sd">${t('/sd に保存', 'Save to /sd')}</button>
        <button id="dp-save-app" title="${t('起動時に自動で実行されるスクリプトになります', 'It runs by itself when the device is switched on')}">${t('/app.rb として保存', 'Save as /app.rb')}</button>
        <label class="hint" style="margin:0"><input type="checkbox" id="dp-run-after"> ${t('保存したら実機で実行', 'Run it on the device after saving')}</label>
      </div>
    </div>`;
  const $ = id => el.querySelector(`#${id}`);
  let files = [];      // [{ path, dir, size, kind: 'app' | 'script' | 'plugin' }]

  // The progress bar while an operation runs: step(label) moves it on (with
  // a total from steps(n)); without a total it runs indeterminate
  const progress = {
    total: 0, done: 0,
    show() { $('dp-progress').hidden = false; this.total = this.done = 0; this.draw(''); },
    hide() { $('dp-progress').hidden = true; },
    steps(n) { this.total = n; this.done = 0; this.draw(''); },
    more(n) { this.total += n; this.draw(); },
    step(label) { if (this.total) this.done = Math.min(this.done + 1, this.total); this.draw(label); },
    draw(label) {
      const bar = $('dp-progress').querySelector('progress');
      if (this.total) { bar.max = this.total; bar.value = this.done; } else bar.removeAttribute('value');
      if (label !== undefined) $('dp-progress-label').textContent = label;
    },
  };
  const setBusy = msg => { $('dp-busy').hidden = !msg; $('dp-busy').textContent = msg || ''; };

  function renderStatus() {
    const d = link.device;
    $('dp-connect').textContent = link.connected ? t('切断', 'Disconnect') : t('実機に接続', 'Connect');
    $('dp-connect').classList.toggle('primary', !link.connected);
    $('dp-body').hidden = !link.connected;
    $('dp-status').textContent = !link.connected ? t('USB-C（midi_device モード）の MIDoRI に Web Serial で接続します', 'Connects to a MIDoRI on USB-C (midi_device mode) with Web Serial')
      : d ? `${d.board || '?'}: ${MODES[d.mode] || d.mode}${d.script ? ` (${d.script})` : ''}` : t('接続中…', 'Connecting…');
  }
  link.onState = renderStatus;
  link.onPace = (pace, e) => print(t(`[MIDoRI] 転送に失敗したので（${e.message}）、遅い速さでやり直します（${PACES[pace].piece} バイト / ${PACES[pace].gap} ms）`, `[MIDoRI] The transfer failed (${e.message}); trying again slower (${PACES[pace].piece} bytes / ${PACES[pace].gap} ms)`), 'sys');

  function renderFiles() {
    const body = $('dp-files').querySelector('tbody');
    body.textContent = '';
    const groups = [
      [t('起動時のスクリプト', 'Startup script'), files.filter(f => f.kind === 'app'), 'app'],
      [t('/sd のスクリプト', 'Scripts on /sd'), files.filter(f => f.kind === 'script'), 'script'],
      [t('プラグイン（読み込み順）', 'Plugins (in load order)'), files.filter(f => f.kind === 'plugin'), 'plugin'],
    ];
    for (const [title, list, kind] of groups) {
      const h = body.insertRow();
      const th = document.createElement('th');
      th.colSpan = 3; th.textContent = title;
      h.append(th);
      if (!list.length) {
        const r = body.insertRow();
        const c = r.insertCell(); c.colSpan = 3; c.className = 'hint';
        c.textContent = kind === 'app' ? t('なし（/app.rb として保存すると、電源を入れたときに実行されます）', 'None (a script saved as /app.rb runs when the device is switched on)') : t('なし', 'None');
      }
      for (const f of list) {
        const r = body.insertRow();
        r.insertCell().textContent = f.path;
        r.insertCell().textContent = `${f.size} B`;
        const act = r.insertCell();
        const btn = (label, fn, title) => {
          const b = document.createElement('button');
          b.className = 'small'; b.textContent = label; if (title) b.title = title;
          b.disabled = working;   // rows drawn while a refresh is still running
          b.onclick = () => guard(fn);
          act.append(b);
        };
        if (/\.rb$/i.test(f.path)) btn(t('開く', 'Open'), () => open(f), t('エディタに読み込む', 'Into the editor'));
        if (f.kind !== 'plugin') btn(t('実行', 'Run'), () => runOnDevice(f.path), t('実機で実行する', 'Run it on the device'));
        // Plugins between the SD card and the internal flash (/plugins load first)
        if (f.kind === 'plugin' && f.path.startsWith('/sd/plugins/')) btn(t('flash に移動', 'To flash'), () => move(f, '/plugins'), t('内蔵フラッシュの /plugins へ移す（SD カードが無くても読まれ、/sd/plugins より先に読まれる）', 'Move it to /plugins on the internal flash (loaded without an SD card, before /sd/plugins)'));
        if (f.kind === 'plugin' && f.path.startsWith('/plugins/')) btn(t('SD に移動', 'To SD'), () => move(f, '/sd/plugins'), t('SD カードの /sd/plugins へ移す', 'Move it to /sd/plugins on the SD card'));
        btn(t('削除', 'Delete'), () => remove(f));
      }
    }
  }

  // One operation at a time, with errors in the log
  let working = false;
  async function guard(fn) {
    if (working) return;
    working = true;
    // Everything but Connect / Disconnect, so nothing runs twice
    el.querySelectorAll('#dp-body button, #dp-body input').forEach(b => { b.disabled = true; });
    progress.show();
    try { await fn(); } catch (e) { print(`[MIDoRI] ${e.message}`, 'err'); }
    finally {
      working = false;
      progress.hide();
      el.querySelectorAll('#dp-body button, #dp-body input').forEach(b => { b.disabled = false; });
    }
  }

  async function refresh() {
    // info, /, /sd, the plugin directories (and the start-up files, below)
    progress.steps(3 + PLUGIN_DIRS.length);
    progress.step(t('実機の状態を確認中…', 'Asking the device…'));
    const d = await link.info();
    if (d.mode !== 'ui') {
      files = [];
      renderFiles();
      setBusy(t(`実機は${MODES[d.mode] || d.mode}です。ファイルは UI モードでしか扱えません（「実機のスクリプトを停止」で戻ります）。`, `The device is ${MODES[d.mode] || d.mode}. Files work in UI mode only ("Stop the device's script" goes back to it).`));
      return;
    }
    setBusy('');
    const out = [];
    progress.step(t('一覧を読み込み中: /', 'Listing /'));
    const root = await link.list('/');
    if (root.some(e => !e.dir && e.name === 'app.rb')) out.push({ path: APP_SCRIPT, size: root.find(e => e.name === 'app.rb').size, kind: 'app' });
    progress.step(t('一覧を読み込み中: /sd', 'Listing /sd'));
    try {
      const sd = (await link.list('/sd')).sort((a, b) => (a.name.toLowerCase() < b.name.toLowerCase() ? -1 : 1));
      for (const e of sd) {
        if (!e.dir && /\.rb$/i.test(e.name)) out.push({ path: `/sd/${e.name}`, size: e.size, kind: 'script' });
      }
    } catch (e) { setBusy(`/sd: ${e.message}`); }
    for (const dir of PLUGIN_DIRS) {
      progress.step(t(`一覧を読み込み中: ${dir}`, `Listing ${dir}`));
      let entries = [];
      try { entries = await link.list(dir); } catch (e) { continue; }   // no such directory
      for (const e of entries.filter(x => !x.dir && isRuby(x.name)).sort((a, b) => (a.name < b.name ? -1 : 1))) {
        out.push({ path: `${dir}/${e.name}`, size: e.size, kind: 'plugin' });
      }
    }
    files = out;
    renderFiles();
    onScriptList(files.filter(f => f.kind === 'script').map(f => f.path.slice('/sd/'.length)));
    if ($('dp-use').checked) await syncFiles();
  }

  // The device's start-up files for the simulator (plugins: foo.mrb hides
  // foo.rb, as on the device; the simulator's loader decides the same)
  async function syncFiles() {
    const want = files.filter(f => f.kind === 'app' || f.kind === 'plugin');
    const out = [];
    progress.more(want.length);
    for (const f of want) {
      progress.step(t(`読み込み中: ${f.path}`, `Reading ${f.path}`));
      out.push({ path: f.path, data: await link.readFile(f.path) });
    }
    print(`[MIDoRI] ${t('シミュレーターの起動に使うファイル', 'Files for the simulator\'s start')}: ${out.map(f => f.path).join(', ') || t('なし', 'none')}`, 'sys');
    onDeviceFiles(out);
  }

  async function open(f) {
    const text = dec.decode(await link.readFile(f.path));
    if (setCode(text, f.path) === false) return;
    const base = f.path.split('/').pop().replace(/\.rb$/i, '');
    $('dp-name').value = f.kind === 'app' ? '' : base;
    print(t(`[MIDoRI] ${f.path} を開きました（${text.length} 文字）`, `[MIDoRI] Opened ${f.path} (${text.length} characters)`), 'sys');
  }

  async function runOnDevice(path) {
    await link.load(path);
    print(`[MIDoRI] ${t('実機で実行', 'Running on the device')}: ${path}`, 'sys');
    setTimeout(() => guard(refresh), 800);
  }

  async function remove(f) {
    if (!confirm(t(`${f.path} を実機から削除しますか？`, `Delete ${f.path} from the device?`) + (f.kind === 'app' ? t('\n（起動時のスクリプトがなくなります）', '\n(The device will have no startup script)') : ''))) return;
    await link.remove(f.path);
    print(`[MIDoRI] ${t('削除', 'Deleted')}: ${f.path}`, 'sys');
    if (f.kind === 'script') await link.rescan();
    await refresh();
  }

  async function move(f, dir) {
    const name = f.path.split('/').pop();
    const to = `${dir}/${name}`;
    if (files.some(x => x.path === to) && !confirm(t(`${to} は実機にあります。上書きしますか？`, `${to} is on the device. Replace it?`))) return;
    await link.move(f.path, to);
    print(t(`[MIDoRI] 移動: ${f.path} → ${to}（次の起動から有効）`, `[MIDoRI] Moved ${f.path} → ${to} (from the next start)`), 'sys');
    await refresh();
  }

  async function save(path) {
    const code = getCode();
    const exists = files.find(f => f.path === path);
    if (beforeSave) {
      const existing = exists ? dec.decode(await link.readFile(path)) : null;
      if (!(await beforeSave(path, existing))) return;
    } else if (exists && !confirm(t(`${path} は実機にあります。上書きしますか？`, `${path} is on the device. Replace it?`))) return;
    await link.writeFile(path, code);
    print(t(`[MIDoRI] 保存: ${path}（${new TextEncoder().encode(code).length} バイト）`, `[MIDoRI] Saved ${path} (${new TextEncoder().encode(code).length} bytes)`), 'sys');
    // The Scripts screen's list on the device (a new file under /sd)
    if (path.startsWith('/sd/') && !exists) await link.rescan();
    if ($('dp-run-after').checked) await runOnDevice(path);
    else await refresh();
  }

  const fileName = () => {
    const n = ($('dp-name').value || (getName && getName()) || '').trim()
      .toLowerCase().replace(/\.rb$/, '').replace(/[^a-z0-9_]+/g, '_').replace(/^_+|_+$/g, '');
    if (!n) throw new Error(t('保存する名前を入れてください（英数字と _）', 'Give it a name (letters, digits and _)'));
    return n;
  };

  $('dp-connect').onclick = async () => {
    if (link.connected) { await link.disconnect(); files = []; onScriptList(null); onDeviceFiles(null); renderStatus(); return; }
    try {
      const d = await link.connect();
      print(`[MIDoRI] ${t('接続しました', 'Connected')}: ${d.board} (${MODES[d.mode] || d.mode})`, 'sys');
      await guard(refresh);
    } catch (e) {
      if (e.name !== 'NotFoundError') print(`[MIDoRI] ${e.message}`, 'err');   // NotFoundError: the chooser was closed
    }
  };
  $('dp-refresh').onclick = () => guard(refresh);
  $('dp-stop').onclick = () => guard(async () => {
    await link.stop();
    // The supervisor waits up to 5 s for the script to stop by itself
    for (let i = 0; i < 14; i++) {
      await new Promise(r => setTimeout(r, 500));
      if ((await link.info()).mode === 'ui') break;
    }
    await refresh();
  });
  $('dp-use').onchange = () => guard(async () => {
    if ($('dp-use').checked) await syncFiles();
    else onDeviceFiles(null);
  });
  $('dp-save-sd').onclick = () => guard(() => save(`/sd/${fileName()}.rb`));
  $('dp-save-app').onclick = () => guard(() => save(APP_SCRIPT));

  renderStatus();
  if (!link.constructor.supported) {
    $('dp-connect').disabled = true;
    $('dp-status').textContent = t('このブラウザは Web Serial に対応していません（Chrome / Edge で使えます）', 'This browser has no Web Serial (Chrome and Edge do)');
  }
  return {
    refresh: () => guard(refresh),
    // A script's text from the device (the simulator's Scripts screen runs it)
    readText: async path => dec.decode(await link.readFile(path)),
  };
}
