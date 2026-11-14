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

const PLUGIN_DIRS = ['/plugins', '/sd/plugins'];   // main_task_base.rb PLUGIN_DIRS
export const APP_SCRIPT = '/app.rb';
const dec = new TextDecoder();
const isRuby = n => /\.(rb|mrb)$/i.test(n) && !n.startsWith('_');

const MODES = { ui: 'UI モード', script: 'スクリプト実行中', irb: 'irb 実行中', stopping: '停止中' };

export function mountDevicePanel(el, { link, print, getCode, setCode, getName, onDeviceFiles, onScriptList = () => {} }) {
  el.innerHTML = `
    <div class="row">
      <button id="dp-connect" class="primary">実機に接続</button>
      <span id="dp-status" class="hint" style="margin:0"></span>
    </div>
    <div id="dp-body" hidden>
      <div class="row">
        <button id="dp-refresh">一覧を更新</button>
        <button id="dp-stop">実機のスクリプトを停止</button>
        <label class="hint" style="margin:0"><input type="checkbox" id="dp-use"> シミュレーターの起動にも実機のプラグインと /app.rb を使う</label>
      </div>
      <div id="dp-busy" class="hint" hidden></div>
      <table id="dp-files"><tbody></tbody></table>
      <div class="row">
        <label>名前 <input type="text" id="dp-name" style="width:12em" placeholder="my_script"></label>
        <button id="dp-save-sd">/sd に保存</button>
        <button id="dp-save-app" title="起動時に自動で実行されるスクリプトになります">/app.rb として保存</button>
        <label class="hint" style="margin:0"><input type="checkbox" id="dp-run-after"> 保存したら実機で実行</label>
      </div>
    </div>`;
  const $ = id => el.querySelector(`#${id}`);
  let files = [];      // [{ path, dir, size, kind: 'app' | 'script' | 'plugin' }]

  const setBusy = msg => { $('dp-busy').hidden = !msg; $('dp-busy').textContent = msg || ''; };

  function renderStatus() {
    const d = link.device;
    $('dp-connect').textContent = link.connected ? '切断' : '実機に接続';
    $('dp-connect').classList.toggle('primary', !link.connected);
    $('dp-body').hidden = !link.connected;
    $('dp-status').textContent = !link.connected ? 'USB-C（midi_device モード）の MIDoRI に Web Serial で接続します'
      : d ? `${d.board || '?'}: ${MODES[d.mode] || d.mode}${d.script ? ` (${d.script})` : ''}` : '接続中…';
  }
  link.onState = renderStatus;

  function renderFiles() {
    const body = $('dp-files').querySelector('tbody');
    body.textContent = '';
    const groups = [
      ['起動時のスクリプト', files.filter(f => f.kind === 'app')],
      ['/sd のスクリプト', files.filter(f => f.kind === 'script')],
      ['プラグイン（読み込み順）', files.filter(f => f.kind === 'plugin')],
    ];
    for (const [title, list] of groups) {
      const h = body.insertRow();
      const th = document.createElement('th');
      th.colSpan = 3; th.textContent = title;
      h.append(th);
      if (!list.length) {
        const r = body.insertRow();
        const c = r.insertCell(); c.colSpan = 3; c.className = 'hint';
        c.textContent = title.startsWith('起動時') ? 'なし（/app.rb として保存すると、電源を入れたときに実行されます）' : 'なし';
      }
      for (const f of list) {
        const r = body.insertRow();
        r.insertCell().textContent = f.path;
        r.insertCell().textContent = `${f.size} B`;
        const act = r.insertCell();
        const btn = (label, fn, title) => {
          const b = document.createElement('button');
          b.className = 'small'; b.textContent = label; if (title) b.title = title;
          b.onclick = () => guard(fn);
          act.append(b);
        };
        if (/\.rb$/i.test(f.path)) btn('開く', () => open(f), 'エディタに読み込む');
        if (f.kind !== 'plugin') btn('実行', () => runOnDevice(f.path), '実機で実行する');
        btn('削除', () => remove(f));
      }
    }
  }

  // One operation at a time, with errors in the log
  let working = false;
  async function guard(fn) {
    if (working) return;
    working = true;
    el.querySelectorAll('button').forEach(b => { if (b.id !== 'dp-connect') b.disabled = true; });
    try { await fn(); } catch (e) { print(`[MIDoRI] ${e.message}`, 'err'); }
    finally {
      working = false;
      el.querySelectorAll('button').forEach(b => { b.disabled = false; });
    }
  }

  async function refresh() {
    const d = await link.info();
    if (d.mode !== 'ui') {
      files = [];
      renderFiles();
      setBusy(`実機は${MODES[d.mode] || d.mode}です。ファイルは UI モードでしか扱えません（「実機のスクリプトを停止」で戻ります）。`);
      return;
    }
    setBusy('');
    const out = [];
    const root = await link.list('/');
    if (root.some(e => !e.dir && e.name === 'app.rb')) out.push({ path: APP_SCRIPT, size: root.find(e => e.name === 'app.rb').size, kind: 'app' });
    try {
      const sd = (await link.list('/sd')).sort((a, b) => (a.name.toLowerCase() < b.name.toLowerCase() ? -1 : 1));
      for (const e of sd) {
        if (!e.dir && /\.rb$/i.test(e.name)) out.push({ path: `/sd/${e.name}`, size: e.size, kind: 'script' });
      }
    } catch (e) { setBusy(`/sd: ${e.message}`); }
    for (const dir of PLUGIN_DIRS) {
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
    for (const f of want) {
      out.push({ path: f.path, data: await link.readFile(f.path) });
    }
    print(`[MIDoRI] シミュレーターの起動に使うファイル: ${out.map(f => f.path).join(', ') || 'なし'}`, 'sys');
    onDeviceFiles(out);
  }

  async function open(f) {
    const text = dec.decode(await link.readFile(f.path));
    setCode(text);
    const base = f.path.split('/').pop().replace(/\.rb$/i, '');
    $('dp-name').value = f.kind === 'app' ? '' : base;
    print(`[MIDoRI] ${f.path} を開きました（${text.length} 文字）`, 'sys');
  }

  async function runOnDevice(path) {
    await link.load(path);
    print(`[MIDoRI] 実機で実行: ${path}`, 'sys');
    setTimeout(() => guard(refresh), 800);
  }

  async function remove(f) {
    if (!confirm(`${f.path} を実機から削除しますか？${f.kind === 'app' ? '\n（起動時のスクリプトがなくなります）' : ''}`)) return;
    await link.remove(f.path);
    print(`[MIDoRI] 削除: ${f.path}`, 'sys');
    if (f.kind === 'script') await link.rescan();
    await refresh();
  }

  async function save(path) {
    const code = getCode();
    const exists = files.find(f => f.path === path);
    if (exists && !confirm(`${path} は実機にあります。上書きしますか？`)) return;
    await link.writeFile(path, code);
    print(`[MIDoRI] 保存: ${path}（${new TextEncoder().encode(code).length} バイト）`, 'sys');
    // The Scripts screen's list on the device (a new file under /sd)
    if (path.startsWith('/sd/') && !exists) await link.rescan();
    if ($('dp-run-after').checked) await runOnDevice(path);
    else await refresh();
  }

  const fileName = () => {
    const n = ($('dp-name').value || (getName && getName()) || '').trim()
      .toLowerCase().replace(/\.rb$/, '').replace(/[^a-z0-9_]+/g, '_').replace(/^_+|_+$/g, '');
    if (!n) throw new Error('保存する名前を入れてください（英数字と _）');
    return n;
  };

  $('dp-connect').onclick = async () => {
    if (link.connected) { await link.disconnect(); files = []; onScriptList(null); onDeviceFiles(null); renderStatus(); return; }
    try {
      const d = await link.connect();
      print(`[MIDoRI] 接続しました: ${d.board} (${MODES[d.mode] || d.mode})`, 'sys');
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
    $('dp-status').textContent = 'このブラウザは Web Serial に対応していません（Chrome / Edge で使えます）';
  }
  return {
    refresh: () => guard(refresh),
    // A script's text from the device (the simulator's Scripts screen runs it)
    readText: async path => dec.decode(await link.readFile(path)),
  };
}
