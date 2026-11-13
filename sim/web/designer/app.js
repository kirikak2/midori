// The MIDoRI Designer page (docs/WEB_SIMULATOR.md §6, §7): a grid editor and
// an inspector over the project model (model.js), the project script
// generated from it (codegen.js), and a preview that runs that script on the
// simulated device (../device.js), started again after each change.

const $ = id => document.getElementById(id);
const STORE = 'midori-designer/project';
const PREVIEW_DELAY_MS = 350;

export async function startDesigner(build) {
  const v = `?v=${encodeURIComponent(build)}`;
  const [model, codegen, dev, usb, synthMod, amyMod] = await Promise.all([
    import(`./model.js${v}`), import(`./codegen.js${v}`), import(`../device.js${v}`),
    import(`../usb_midi.js${v}`), import(`../synth.js${v}`), import(`../amy_audio.js${v}`),
  ]);
  const { WIDGETS, ACTIONS, COLORS, DEVICE_KINDS } = model;
  const { MidoriDevice, BOARDS, DEFAULT_BOARD } = dev;

  // ---- state

  let project = loadSaved() || model.newProject(DEFAULT_BOARD);
  if (!BOARDS[project.board]) project.board = DEFAULT_BOARD;
  let current = Math.min(project.start || 0, project.layouts.length - 1);   // layout being edited
  let selected = null;     // widget id
  let paletteKind = 'pad';

  function loadSaved() {
    try { const s = localStorage.getItem(STORE); return s ? JSON.parse(s) : null; } catch (e) { return null; }
  }
  function save() {
    try { localStorage.setItem(STORE, JSON.stringify(project)); } catch (e) {}
  }
  const layout = () => project.layouts[current];
  const widget = () => layout().widgets.find(w => w.id === selected) || null;

  // Every edit goes through here: re-render, store, preview again
  function changed({ preview = true } = {}) {
    save();
    renderAll();
    if (preview) schedulePreview();
  }

  // ---- header

  for (const [id, b] of Object.entries(BOARDS)) $('board').add(new Option(`${b.name} (${b.width}x${b.height})`, id));
  $('name').oninput = () => { project.name = $('name').value; changed(); };
  $('board').onchange = () => { project.board = $('board').value; changed(); };
  $('new').onclick = () => {
    if (!confirm('今のプロジェクトを捨てて新しく始めますか？（保存していない変更は消えます）')) return;
    project = model.newProject(project.board);
    current = 0; selected = null;
    changed();
  };
  $('save').onclick = () => {
    const blob = new Blob([codegen.generate(project)], { type: 'text/plain' });
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = codegen.fileName(project.name);
    a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 1000);
  };
  $('open').onclick = () => $('file').click();
  $('file').onchange = async () => {
    const f = $('file').files[0];
    $('file').value = '';
    if (!f) return;
    let m = null;
    try { m = codegen.decodeModel(await f.text()); } catch (e) { m = null; }
    if (!m || !Array.isArray(m.layouts)) {
      alert('MIDoRI Designer で作ったファイルではありません（先頭に # midori-designer: がありません）。\nシミュレーターのエディタで開いてください。');
      return;
    }
    project = m;
    current = Math.min(project.start || 0, project.layouts.length - 1);
    selected = null;
    changed();
  };
  // The simulator page keeps its script in localStorage (index.html)
  $('to-sim').onclick = () => {
    try {
      localStorage.setItem('midori-sim/script', codegen.generate(project));
      localStorage.setItem('midori-sim/board', project.board);
    } catch (e) {}
    window.open('./index.html', '_blank');
  };

  // ---- layouts

  function renderTabs() {
    const tabs = $('tabs');
    tabs.textContent = '';
    project.layouts.forEach((l, i) => {
      const b = document.createElement('button');
      b.textContent = l.title || `Layout ${i + 1}`;
      b.setAttribute('aria-selected', String(i === current));
      b.onclick = () => { current = i; selected = null; changed(); };
      tabs.append(b);
    });
    const add = document.createElement('button');
    add.textContent = '+';
    add.title = 'レイアウトを追加';
    add.onclick = () => {
      project.layouts.push(model.newLayout(`Layout ${project.layouts.length + 1}`));
      current = project.layouts.length - 1; selected = null;
      changed();
    };
    tabs.append(add);
    const l = layout();
    $('title').value = l.title;
    $('cols').value = l.cols;
    $('rows').value = l.rows;
    $('start').checked = project.start === current;
    $('del-layout').disabled = project.layouts.length <= 1;
  }
  $('title').oninput = () => { layout().title = $('title').value; save(); renderTabs(); $('title').focus(); schedulePreview(); };
  const resize = () => {
    layout().cols = clamp(parseInt($('cols').value, 10) || 1, 1, 12);
    layout().rows = clamp(parseInt($('rows').value, 10) || 1, 1, 12);
    changed();
  };
  $('cols').onchange = resize;
  $('rows').onchange = resize;
  $('start').onchange = () => { project.start = current; changed({ preview: false }); };
  $('del-layout').onclick = () => {
    if (project.layouts.length <= 1 || !confirm(`レイアウト「${layout().title}」を削除しますか？`)) return;
    project.layouts.splice(current, 1);
    if (project.start >= project.layouts.length) project.start = 0;
    current = Math.max(0, current - 1); selected = null;
    changed();
  };

  // ---- palette and grid

  for (const [kind, spec] of Object.entries(WIDGETS)) {
    const label = document.createElement('label');
    label.innerHTML = `<input type="radio" name="kind" value="${kind}"${kind === paletteKind ? ' checked' : ''}><span>${spec.label}</span>`;
    label.querySelector('input').onchange = () => { paletteKind = kind; };
    $('palette').append(label);
  }

  let drag = null;   // { c0, r0, c1, r1 }
  function renderGrid() {
    const g = $('grid'), l = layout();
    g.dataset.board = project.board;
    g.style.gridTemplateColumns = `repeat(${l.cols}, 1fr)`;
    g.style.gridTemplateRows = `repeat(${l.rows}, 1fr)`;
    g.textContent = '';
    const bad = new Set(model.problems(project).filter(p => p.layout === current).map(p => p.widget));
    for (let r = 0; r < l.rows; r++) {
      for (let c = 0; c < l.cols; c++) {
        if (model.widgetAt(l, c, r)) continue;
        const cell = document.createElement('div');
        cell.className = 'cell';
        cell.style.gridColumn = `${c + 1}`;
        cell.style.gridRow = `${r + 1}`;
        cell.dataset.col = c; cell.dataset.row = r;
        if (drag && inRect(c, r, drag)) cell.classList.add('pending');
        g.append(cell);
      }
    }
    for (const w of l.widgets) {
      const el = document.createElement('div');
      el.className = 'widget' + (w.id === selected ? ' sel' : '') + (bad.has(w.id) ? ' bad' : '');
      el.style.gridColumn = `${w.col + 1} / span ${w.w}`;
      el.style.gridRow = `${w.row + 1} / span ${w.h}`;
      el.style.background = widgetColor(w);
      el.textContent = widgetCaption(w);
      el.onpointerdown = ev => { ev.stopPropagation(); selected = w.id; changed({ preview: false }); };
      g.append(el);
    }
  }
  const inRect = (c, r, d) => c >= Math.min(d.c0, d.c1) && c <= Math.max(d.c0, d.c1) && r >= Math.min(d.r0, d.r1) && r <= Math.max(d.r0, d.r1);
  const cellAt = ev => {
    const el = document.elementFromPoint(ev.clientX, ev.clientY);
    return el && el.classList.contains('cell') ? { c: +el.dataset.col, r: +el.dataset.row } : null;
  };
  $('grid').addEventListener('pointerdown', ev => {
    const at = cellAt(ev);
    if (!at) return;
    $('grid').setPointerCapture(ev.pointerId);
    drag = { c0: at.c, r0: at.r, c1: at.c, r1: at.r };
    renderGrid();
  });
  $('grid').addEventListener('pointermove', ev => {
    if (!drag) return;
    const at = cellAt(ev);
    if (at && (at.c !== drag.c1 || at.r !== drag.r1)) { drag.c1 = at.c; drag.r1 = at.r; renderGrid(); }
  });
  $('grid').addEventListener('pointerup', () => {
    if (!drag) return;
    const d = drag;
    drag = null;
    const col = Math.min(d.c0, d.c1), row = Math.min(d.r0, d.r1);
    let w = Math.abs(d.c1 - d.c0) + 1, h = Math.abs(d.r1 - d.r0) + 1;
    // A single click gives the widget its usual size where that fits
    if (w === 1 && h === 1) {
      const [sw, sh] = WIDGETS[paletteKind].size;
      if (fits(col, row, sw, sh)) { w = sw; h = sh; }
    }
    if (!fits(col, row, w, h)) { renderGrid(); return; }
    const nw = model.newWidget(paletteKind, col, row, w, h);
    if (nw.props.label !== undefined) nw.props.label = `${WIDGETS[paletteKind].label} ${layout().widgets.length + 1}`;
    layout().widgets.push(nw);
    selected = nw.id;
    changed();
  });
  function fits(col, row, w, h) {
    const l = layout();
    if (col + w > l.cols || row + h > l.rows) return false;
    for (let r = row; r < row + h; r++) for (let c = col; c < col + w; c++) if (model.widgetAt(l, c, r)) return false;
    return true;
  }
  document.addEventListener('keydown', ev => {
    if ((ev.key === 'Delete' || ev.key === 'Backspace') && selected && !/INPUT|TEXTAREA|SELECT/.test(document.activeElement.tagName)) {
      ev.preventDefault();
      removeSelected();
    }
  });
  function removeSelected() {
    const l = layout();
    l.widgets = l.widgets.filter(w => w.id !== selected);
    selected = null;
    changed();
  }

  const RGB = { gray: '#7b7b7b', red: '#d83b3b', green: '#2e9e4e', blue: '#3456d6', yellow: '#c9a90c', cyan: '#1aa7b8',
                magenta: '#c43bbf', orange: '#e0821b', purple: '#7c3aa8', white: '#bdbdbd' };
  const KIND_RGB = { keys: '#555b66', xypad: '#2d6a8a', tombola: '#6a4a8a', tempo: '#244f9e' };
  const widgetColor = w => (w.props.color ? RGB[w.props.color] : KIND_RGB[w.kind]) || '#666';
  const widgetCaption = w => (w.props.label !== undefined ? `${w.props.label}` : WIDGETS[w.kind].label);

  // ---- inspector

  function renderInspector() {
    const box = $('inspector');
    box.textContent = '';
    const w = widget();
    if (!w) { box.innerHTML = '<p class="hint">部品を選ぶと設定が出ます。</p>'; return; }
    const spec = WIDGETS[w.kind];
    const head = document.createElement('div');
    head.className = 'row';
    head.innerHTML = `<strong>${spec.label}</strong>`;
    const del = document.createElement('button');
    del.className = 'small'; del.textContent = '削除'; del.onclick = removeSelected;
    head.append(del);
    box.append(head);
    // Position: col / row / span, 0-based like the DSL
    for (const [key, label] of [['col', '列 (0〜)'], ['row', '行 (0〜)'], ['w', '幅'], ['h', '高さ']]) {
      box.append(numberField(label, w[key], key === 'w' || key === 'h' ? 1 : 0, 12, val => { w[key] = val; changed(); }));
    }
    for (const fld of spec.props) box.append(field(fld, w.props[fld.key], val => { w.props[fld.key] = val; changed(); }));
    if (spec.actions) {
      const sel = document.createElement('select');
      for (const t of spec.actions) sel.add(new Option(ACTIONS[t].label, t));
      sel.value = w.action.type;
      sel.onchange = () => {
        const keepDevice = w.action.device;
        w.action = model.newAction(sel.value);
        if (keepDevice && 'device' in w.action) w.action.device = keepDevice;
        changed();
      };
      box.append(labeled('動作', sel));
      for (const fld of ACTIONS[w.action.type].fields) {
        box.append(field(fld, w.action[fld.key], val => { w.action[fld.key] = val; changed(); }));
      }
    }
  }

  function labeled(text, input) {
    const div = document.createElement('div');
    div.className = 'field';
    const l = document.createElement('label');
    l.textContent = text;
    div.append(l, input);
    return div;
  }
  function numberField(text, value, min, max, set, float = false) {
    const i = document.createElement('input');
    i.type = 'number'; i.value = value; i.min = min; i.max = max; if (float) i.step = 'any';
    i.onchange = () => { const n = float ? parseFloat(i.value) : parseInt(i.value, 10); if (Number.isFinite(n)) set(clamp(n, min, max)); };
    return labeled(text, i);
  }
  function field(fld, value, set) {
    switch (fld.type) {
      case 'int': return numberField(fld.label, value, fld.min ?? -99999, fld.max ?? 99999, set);
      case 'float': return numberField(fld.label, value, -1000, 1000, set, true);
      case 'bool': {
        const i = document.createElement('input');
        i.type = 'checkbox'; i.checked = !!value; i.onchange = () => set(i.checked);
        return labeled(fld.label, i);
      }
      case 'choice': case 'color': case 'device': {
        const s = document.createElement('select');
        const opts = fld.type === 'color' ? COLORS : fld.type === 'device' ? project.devices.map(d => d.id) : fld.choices;
        for (const o of opts) s.add(new Option(fld.type === 'device' ? `${o} (${DEVICE_KINDS[project.devices.find(d => d.id === o).kind].label})` : o, o));
        s.value = value;
        s.onchange = () => set(s.value);
        return labeled(fld.label, s);
      }
      case 'notes': {
        const i = document.createElement('input');
        i.type = 'text'; i.value = (value || []).join(' ');
        i.onchange = () => set(i.value.split(/[\s,]+/).filter(Boolean).map(n => clamp(parseInt(n, 10) || 0, 0, 127)));
        return labeled(fld.label, i);
      }
      case 'code': {
        const t = document.createElement('textarea');
        t.value = value || ''; t.spellcheck = false;
        t.onchange = () => set(t.value);
        return labeled(fld.label, t);
      }
      default: {
        const i = document.createElement('input');
        i.type = 'text'; i.value = value ?? '';
        i.onchange = () => set(i.value);
        return labeled(fld.label, i);
      }
    }
  }

  // ---- devices

  function renderDevices() {
    const box = $('devices');
    box.textContent = '';
    project.devices.forEach((d, i) => {
      const row = document.createElement('div');
      row.className = 'row';
      const id = document.createElement('input');
      id.type = 'text'; id.value = d.id; id.style.width = '7em'; id.title = 'Ruby の名前（英小文字・数字・_）';
      id.onchange = () => {
        const nid = id.value.toLowerCase().replace(/[^a-z0-9_]/g, '_').replace(/^[^a-z]+/, '') || d.id;
        if (project.devices.some(o => o !== d && o.id === nid)) { id.value = d.id; return; }
        renameDevice(d.id, nid);
        changed();
      };
      const kind = document.createElement('select');
      for (const [k, spec] of Object.entries(DEVICE_KINDS)) kind.add(new Option(spec.label, k));
      kind.value = d.kind;
      kind.onchange = () => { d.kind = kind.value; changed(); };
      row.append(id, kind);
      if (d.kind === 'web_midi') {
        const port = document.createElement('input');
        port.type = 'text'; port.placeholder = 'port name'; port.value = d.port || ''; port.style.width = '9em';
        port.onchange = () => { d.port = port.value; changed(); };
        row.append(port);
      }
      if (project.devices.length > 1) {
        const del = document.createElement('button');
        del.className = 'small'; del.textContent = '×';
        del.onclick = () => { renameDevice(d.id, project.devices[i === 0 ? 1 : 0].id); project.devices.splice(i, 1); changed(); };
        row.append(del);
      }
      box.append(row);
    });
  }
  // Point every widget and action at another device id
  function renameDevice(from, to) {
    for (const d of project.devices) if (d.id === from) d.id = to;
    for (const l of project.layouts) for (const w of l.widgets) {
      if (w.props.device === from) w.props.device = to;
      if (w.action && w.action.device === from) w.action.device = to;
    }
  }
  $('add-device').onclick = () => {
    let n = project.devices.length + 1;
    while (project.devices.some(d => d.id === `dev${n}`)) n++;
    project.devices.push({ id: `dev${n}`, kind: 'usb_midi_host' });
    changed();
  };

  // ---- problems, code

  function renderProblems() {
    const ps = model.problems(project);
    $('problems-card').hidden = ps.length === 0;
    $('problems').innerHTML = '';
    for (const p of ps) {
      const li = document.createElement('li');
      li.textContent = `${project.layouts[p.layout].title}: ${p.message}`;
      $('problems').append(li);
    }
  }

  function renderAll() {
    $('name').value = project.name;
    $('board').value = project.board;
    $('device').dataset.board = project.board;
    renderTabs();
    renderGrid();
    renderInspector();
    renderDevices();
    renderProblems();
    $('code').textContent = codegen.generate(project);
  }

  // ---- preview: the generated script on the simulated device

  const log = $('log');
  function print(s, kind = 'out') {
    if (kind === 'out' && /^[IWD] \(/.test(s)) return;
    if (/initialized$/.test(s)) return;
    const line = document.createElement('div');
    if (kind !== 'out' || /^E \(|error/i.test(s)) line.className = kind === 'sys' ? 'sys' : 'err';
    line.textContent = s;
    log.append(line);
    while (log.childElementCount > 200) log.firstChild.remove();
    log.scrollTop = log.scrollHeight;
  }

  const synth = new synthMod.BuiltinSynth();
  let audioCtx = null, amyAudio = null;
  async function startAudio() {
    if (!audioCtx) {
      audioCtx = new AudioContext({ sampleRate: 48000 });
      amyAudio = new amyMod.AmyAudio(audioCtx);
    }
    synth.resume(audioCtx);
    if (audioCtx.state === 'suspended') audioCtx.resume().catch(() => {});
    await amyAudio.init().catch(e => print(`[designer] AMY audio: ${e.message}`, 'err'));
  }
  document.addEventListener('pointerdown', () => { if (audioCtx && audioCtx.state === 'suspended') audioCtx.resume(); }, true);
  const asm = [new usb.UsbMidiAssembler(), new usb.UsbMidiAssembler(), new usb.UsbMidiAssembler()];
  function midiOut(role, cin, b1, b2, b3, timeUs) {
    const bytes = asm[role].feed(cin, b1, b2, b3);
    if (bytes && role === 2 && synth.ctx) synth.message(bytes, synth.ctx.currentTime + (timeUs / 1000 + 10 - performance.now()) / 1000);
  }

  let device = null, timer = null, starting = null, again = false;
  function schedulePreview() {
    clearTimeout(timer);
    timer = setTimeout(preview, PREVIEW_DELAY_MS);
  }
  async function preview() {
    if (starting) { again = true; return; }
    starting = (async () => {
      await startAudio();
      if (device) { device.stop(); synth.allOff(); amyAudio.reset(); device = null; }
      log.textContent = '';
      const t0 = performance.now();
      device = await MidoriDevice.start({
        canvas: $('screen'), board: project.board, mode: 'script',
        script: codegen.generate(project, { show: current }),
        onOutput: print, onMidiOut: midiOut, amyAudio,
        onScriptEnd: () => print('[designer] the script ended', 'sys'),
      });
      device.M.simVolume = (out, percent) => { if (out === 0) { synth.setVolume(percent); amyAudio.setVolume(percent); } };
      print(`[designer] ${BOARDS[project.board].name}: started in ${Math.round(performance.now() - t0)} ms`, 'sys');
    })();
    try { await starting; } catch (e) { print(`[designer] ${e.message}`, 'err'); }
    starting = null;
    if (again) { again = false; preview(); }
  }

  renderAll();
  preview();
}

function clamp(n, lo, hi) { return Math.max(lo, Math.min(hi, n)); }
