// The MIDoRI Designer page (docs/WEB_SIMULATOR.md §6, §7): a grid editor and
// an inspector over the project model (model.js), the project script
// generated from it (codegen.js), and a preview that runs that script on the
// simulated device (../device.js), started again after each change.

const $ = id => document.getElementById(id);
const STORE = 'midori-designer/project';
const PREVIEW_DELAY_MS = 350;

export async function startDesigner(build) {
  const v = `?v=${encodeURIComponent(build)}`;
  const [model, codegen, dev, usb, synthMod, amyMod, linkMod, panelMod, i18n] = await Promise.all([
    import(`./model.js${v}`), import(`./codegen.js${v}`), import(`../device.js${v}`),
    import(`../usb_midi.js${v}`), import(`../synth.js${v}`), import(`../amy_audio.js${v}`),
    import(`../midori_link.js${v}`), import(`../device_panel.js${v}`), import(`../i18n.js${v}`),
  ]);
  const { t } = i18n;
  i18n.localizePage();
  $('to-simulator').href = `./index.html?lang=${i18n.LANG}`;
  const { WIDGETS, ACTIONS, COLORS, DEVICE_KINDS, PART_TYPES, MENU_SCREENS, ROUTE_CLOCK, KNOB_BANKS } = model;
  const { MidoriDevice, BOARDS, DEFAULT_BOARD } = dev;

  // ---- state

  let project = loadSaved() || model.newProject(DEFAULT_BOARD);
  if (!BOARDS[project.board]) project.board = DEFAULT_BOARD;
  model.upgrade(project);   // projects from older versions
  let current = 0;          // layout being edited
  let view = 'layout';      // what the grid edits: 'layout' | 'pads' | 'knobs'
  let bank = 0;             // the Knobs bank being edited (0..3)
  let selected = null;      // widget / slot id
  openAtStart();
  let paletteKind = 'pad';

  function loadSaved() {
    try { const s = localStorage.getItem(STORE); return s ? JSON.parse(s) : null; } catch (e) { return null; }
  }
  function save() {
    try { localStorage.setItem(STORE, JSON.stringify(project)); } catch (e) {}
  }
  const layout = () => project.layouts[current];
  const inLayout = () => view === 'layout';
  // The Pads / Knobs screen's slots being edited (Knobs: this bank)
  const slots = () => (view === 'pads' ? project.pads : project.knobs.filter(k => (k.bank || 0) === bank));
  const slotAt = i => slots().find(sl => sl.index === i) || null;
  const widget = () => (inLayout() ? layout().widgets : project[view]).find(w => w.id === selected) || null;
  // What the edited view is as project.start / generate's show
  const viewKey = () => (inLayout() ? current : view === 'pads' ? 'pads' : `knobs:${bank}`);
  // Open the project where it starts
  function openAtStart() {
    const st = project.start ?? 0;
    if (typeof st === 'string') {
      const [v, b] = st.split(':');
      view = v === 'knobs' ? 'knobs' : 'pads';
      bank = clamp(Number(b) || 0, 0, KNOB_BANKS - 1);
      current = 0;
    } else {
      view = 'layout';
      current = clamp(st, 0, project.layouts.length - 1);
    }
  }

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
    if (!confirm(t('今のプロジェクトを捨てて新しく始めますか？（保存していない変更は消えます）', 'Throw this project away and start a new one? (Unsaved changes are lost)'))) return;
    project = model.newProject(project.board);
    current = 0; view = 'layout'; bank = 0; selected = null;
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
      alert(t('MIDoRI Designer で作ったファイルではありません（先頭に # midori-designer: がありません）。\nシミュレーターのエディタで開いてください。', 'This file was not made by the MIDoRI Designer (no # midori-designer: line at the top).\nOpen it in the simulator\'s editor.'));
      return;
    }
    project = model.upgrade(m);
    openAtStart();
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
    // The device's own Pads / Knobs screens, then the layouts
    for (const [v, label] of [['pads', t('Pads 画面', 'Pads screen')], ['knobs', t('Knobs 画面', 'Knobs screen')]]) {
      const b = document.createElement('button');
      b.className = 'screen';
      b.textContent = label;
      b.setAttribute('aria-selected', String(view === v));
      b.onclick = () => { view = v; selected = null; changed(); };
      tabs.append(b);
    }
    const sep = document.createElement('span');
    sep.className = 'sep';
    tabs.append(sep);
    project.layouts.forEach((l, i) => {
      const b = document.createElement('button');
      b.textContent = l.title || `Layout ${i + 1}`;
      b.setAttribute('aria-selected', String(inLayout() && i === current));
      b.onclick = () => { view = 'layout'; current = i; selected = null; changed(); };
      tabs.append(b);
    });
    const add = document.createElement('button');
    add.textContent = '+';
    add.title = t('レイアウトを追加', 'Add a layout');
    add.onclick = () => {
      project.layouts.push(model.newLayout(`Layout ${project.layouts.length + 1}`));
      view = 'layout'; current = project.layouts.length - 1; selected = null;
      changed();
    };
    tabs.append(add);
    $('layout-fields').hidden = !inLayout();
    $('screen-fields').hidden = inLayout();
    $('palette').hidden = !inLayout();
    $('layout-hint').hidden = !inLayout();
    $('slot-hint').hidden = inLayout();
    $('start').checked = String(project.start ?? 0) === String(viewKey());
    $('send-initial').checked = project.sendInitial !== false;
    if (!inLayout()) { renderScreenFields(); return; }
    const l = layout();
    $('title').value = l.title;
    $('cols').value = l.cols;
    $('rows').value = l.rows;
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
  $('start').onchange = () => { project.start = viewKey(); changed({ preview: false }); };
  // The Pads / Knobs screen's line: its size on this board, Knobs' banks
  function renderScreenFields() {
    const { cols, rows } = model.screenGrid(project.board);
    const what = view === 'pads' ? t('パッド', 'pads') : t('ノブ', 'knobs');
    $('screen-info').textContent = t(`${BOARDS[project.board].name}: ${what} ${cols * rows} 個（${cols} x ${rows}）`, `${BOARDS[project.board].name}: ${cols * rows} ${what} (${cols} x ${rows})`) +
      (view === 'knobs' ? t(' × 4 バンク', ' × 4 banks') : '');
    const banks = $('banks');
    banks.textContent = '';
    if (view !== 'knobs') return;
    for (let b = 0; b < KNOB_BANKS; b++) {
      const btn = document.createElement('button');
      btn.className = 'small';
      const n = project.knobs.filter(k => (k.bank || 0) === b).length;
      btn.textContent = 'ABCD'[b] + (n ? ` (${n})` : '');
      btn.setAttribute('aria-selected', String(b === bank));
      btn.onclick = () => { bank = b; selected = null; changed(); };
      banks.append(btn);
    }
  }
  $('send-initial').onchange = () => { project.sendInitial = $('send-initial').checked; changed(); };
  $('del-layout').onclick = () => {
    if (project.layouts.length <= 1 || !confirm(t(`レイアウト「${layout().title}」を削除しますか？`, `Delete the layout "${layout().title}"?`))) return;
    project.layouts.splice(current, 1);
    if (typeof project.start === 'number' && project.start >= project.layouts.length) project.start = 0;
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

  // What a pointer on the grid is doing:
  //   { mode: 'place', c0, r0, c1, r1 }               dragging out a new widget on empty cells
  //   { mode: 'move' | 'resize', id, c0, r0, col, row, w, h, moved }   a widget (col..h: where it would go)
  let drag = null;
  function renderGrid() {
    if (!inLayout()) { renderSlots(); return; }
    const g = $('grid'), l = layout();
    g.dataset.board = project.board;
    g.style.gridTemplateColumns = `repeat(${l.cols}, 1fr)`;
    g.style.gridTemplateRows = `repeat(${l.rows}, 1fr)`;
    g.textContent = '';
    const bad = new Set(model.problems(project).filter(p => p.layout === current).map(p => p.widget));
    const moving = drag && drag.mode !== 'place' ? drag : null;
    for (let r = 0; r < l.rows; r++) {
      for (let c = 0; c < l.cols; c++) {
        const under = model.widgetAt(l, c, r);
        if (under && !(moving && under.id === moving.id)) continue;
        const cell = document.createElement('div');
        cell.className = 'cell';
        cell.style.gridColumn = `${c + 1}`;
        cell.style.gridRow = `${r + 1}`;
        if (drag && drag.mode === 'place' && inRect(c, r, drag)) cell.classList.add('pending');
        g.append(cell);
      }
    }
    for (const w of l.widgets) {
      const m = moving && moving.id === w.id ? moving : null;
      const at = m || w;
      const el = document.createElement('div');
      el.className = 'widget' + (w.id === selected ? ' sel' : '') + (bad.has(w.id) && !m ? ' bad' : '') +
        (m ? ' dragging' : '') + (m && !fits(m.col, m.row, m.w, m.h, w.id) ? ' blocked' : '');
      el.style.gridColumn = `${at.col + 1} / span ${at.w}`;
      el.style.gridRow = `${at.row + 1} / span ${at.h}`;
      el.style.background = widgetColor(w);
      el.textContent = widgetCaption(w);
      el.title = `${WIDGETS[w.kind].label}: col ${at.col}, row ${at.row}, ${at.w}x${at.h}`;
      el.onpointerdown = ev => startWidgetDrag(ev, w, 'move');
      if (w.id === selected) {
        const handle = document.createElement('div');
        handle.className = 'resize';
        handle.title = t('ドラッグで大きさを変える', 'Drag to resize');
        handle.onpointerdown = ev => startWidgetDrag(ev, w, 'resize');
        el.append(handle);
      }
      g.append(el);
    }
  }
  // The Pads / Knobs screen: its fixed grid, a slot per cell
  function renderSlots() {
    const g = $('grid'), { cols, rows } = model.screenGrid(project.board);
    g.dataset.board = project.board;
    g.style.gridTemplateColumns = `repeat(${cols}, 1fr)`;
    g.style.gridTemplateRows = `repeat(${rows}, 1fr)`;
    g.textContent = '';
    const bad = new Set(model.problems(project).filter(p => p.screen === view).map(p => p.widget));
    const what = view === 'pads' ? 'Pad' : 'Knob';
    for (let i = 0; i < cols * rows; i++) {
      const sl = slotAt(i);
      const el = document.createElement('div');
      el.style.gridColumn = `${(i % cols) + 1}`;
      el.style.gridRow = `${Math.floor(i / cols) + 1}`;
      const target = drag && drag.mode === 'slot' && drag.moved && drag.to === i && drag.from !== i;
      if (sl) {
        el.className = 'widget' + (sl.id === selected ? ' sel' : '') + (bad.has(sl.id) ? ' bad' : '') + (target ? ' target' : '') +
          (drag && drag.mode === 'slot' && drag.id === sl.id && drag.moved ? ' dragging' : '');
        el.style.background = widgetColor(sl);
        el.textContent = `${i + 1}: ${widgetCaption(sl)}`;
        el.title = `${what} ${i + 1}${view === 'knobs' ? ` (bank ${'ABCD'[bank]})` : ''}`;
        el.onpointerdown = ev => startSlotDrag(ev, sl);
      } else {
        el.className = 'cell slot-empty' + (target ? ' target' : '');
        el.textContent = `+ ${what} ${i + 1}`;
      }
      g.append(el);
    }
  }
  function startSlotDrag(ev, sl) {
    ev.stopPropagation();
    ev.preventDefault();
    drag = { mode: 'slot', id: sl.id, from: sl.index, to: sl.index, moved: false };
    $('grid').setPointerCapture(ev.pointerId);
    if (selected !== sl.id) { selected = sl.id; changed({ preview: false }); }
  }
  // Move a slot to another number, swapping with the one there
  function moveSlot(sl, to) {
    const count = model.slotCount(project.board);
    if (to < 0 || to >= count || to === sl.index) return false;
    const other = slotAt(to);
    if (other) other.index = sl.index;
    sl.index = to;
    return true;
  }
  const gridDims = () => (inLayout() ? layout() : model.screenGrid(project.board));
  const inRect = (c, r, d) => c >= Math.min(d.c0, d.c1) && c <= Math.max(d.c0, d.c1) && r >= Math.min(d.r0, d.r1) && r <= Math.max(d.r0, d.r1);
  // The cell under the pointer from the grid's geometry (widgets cover their
  // cells, so not from the element there); clamped to the grid
  function cellAt(ev) {
    const g = $('grid'), l = gridDims(), rc = g.getBoundingClientRect();
    const gap = parseFloat(getComputedStyle(g).columnGap) || 0;
    const cw = (rc.width + gap) / l.cols, ch = (rc.height + gap) / l.rows;
    return { c: clamp(Math.floor((ev.clientX - rc.left) / cw), 0, l.cols - 1),
             r: clamp(Math.floor((ev.clientY - rc.top) / ch), 0, l.rows - 1) };
  }
  function startWidgetDrag(ev, w, mode) {
    ev.stopPropagation();
    ev.preventDefault();
    const at = cellAt(ev);
    drag = { mode, id: w.id, c0: at.c, r0: at.r, col: w.col, row: w.row, w: w.w, h: w.h, moved: false };
    $('grid').setPointerCapture(ev.pointerId);
    if (selected !== w.id) { selected = w.id; changed({ preview: false }); }
  }
  $('grid').addEventListener('pointerdown', ev => {
    if (ev.button !== 0) return;
    const at = cellAt(ev);
    if (!inLayout()) {
      // A click on an empty slot puts a pad / knob there
      const i = at.r * gridDims().cols + at.c;
      if (slotAt(i)) return;
      const sl = model.newSlot(view === 'pads' ? 'pad' : 'knob', i, bank);
      project[view].push(sl);
      selected = sl.id;
      changed();
      return;
    }
    if (model.widgetAt(layout(), at.c, at.r)) return;
    $('grid').setPointerCapture(ev.pointerId);
    drag = { mode: 'place', c0: at.c, r0: at.r, c1: at.c, r1: at.r };
    renderGrid();
  });
  $('grid').addEventListener('pointermove', ev => {
    if (!drag) return;
    const at = cellAt(ev), l = gridDims();
    if (drag.mode === 'slot') {
      const to = at.r * l.cols + at.c;
      if (to !== drag.to) { drag.to = to; drag.moved = true; renderGrid(); }
      return;
    }
    if (drag.mode === 'place') {
      if (at.c !== drag.c1 || at.r !== drag.r1) { drag.c1 = at.c; drag.r1 = at.r; renderGrid(); }
      return;
    }
    const w = l.widgets.find(x => x.id === drag.id);
    if (!w) { drag = null; return; }
    let { col, row, w: ww, h } = w;
    if (drag.mode === 'move') {
      col = clamp(w.col + at.c - drag.c0, 0, l.cols - w.w);
      row = clamp(w.row + at.r - drag.r0, 0, l.rows - w.h);
    } else {
      ww = Math.max(1, at.c - w.col + 1);
      h = Math.max(1, at.r - w.row + 1);
    }
    if (col !== drag.col || row !== drag.row || ww !== drag.w || h !== drag.h) {
      Object.assign(drag, { col, row, w: ww, h, moved: true });
      renderGrid();
    }
  });
  function endDrag(commit) {
    if (!drag) return;
    const d = drag;
    drag = null;
    if (d.mode === 'slot') {
      const sl = project[view].find(x => x.id === d.id);
      if (commit && sl && d.moved && moveSlot(sl, d.to)) changed();
      else renderGrid();
      return;
    }
    if (d.mode !== 'place') {
      const w = layout().widgets.find(x => x.id === d.id);
      // Only where it fits: a move or resize onto another widget springs back
      if (commit && w && d.moved && (d.col !== w.col || d.row !== w.row || d.w !== w.w || d.h !== w.h) &&
          fits(d.col, d.row, d.w, d.h, w.id)) {
        Object.assign(w, { col: d.col, row: d.row, w: d.w, h: d.h });
        changed();
      } else {
        renderGrid();
      }
      return;
    }
    if (!commit) { renderGrid(); return; }
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
  }
  $('grid').addEventListener('pointerup', () => endDrag(true));
  $('grid').addEventListener('pointercancel', () => endDrag(false));
  // Whether a widget of w x h fits at (col, row): inside the grid and on no
  // widget but `ignore` (the one being moved)
  function fits(col, row, w, h, ignore = null) {
    const l = layout();
    if (col < 0 || row < 0 || col + w > l.cols || row + h > l.rows) return false;
    return !l.widgets.some(o => o.id !== ignore &&
      col < o.col + o.w && o.col < col + w && row < o.row + o.h && o.row < row + h);
  }
  // The keyboard: Delete removes the selected widget, arrows move it, Shift +
  // arrows resize it, Escape drops a drag
  document.addEventListener('keydown', ev => {
    if (/INPUT|TEXTAREA|SELECT/.test(document.activeElement.tagName)) return;
    if (ev.key === 'Escape' && drag) { endDrag(false); return; }
    const w = selected && widget();
    if (!w) return;
    if (ev.key === 'Delete' || ev.key === 'Backspace') {
      ev.preventDefault();
      removeSelected();
      return;
    }
    const step = { ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowUp: [0, -1], ArrowDown: [0, 1] }[ev.key];
    if (!step) return;
    ev.preventDefault();
    const [dc, dr] = step;
    if (!inLayout()) {
      // The neighbour in the screen's grid (rows wrap only within the grid)
      const { cols } = gridDims();
      const c = (w.index % cols) + dc;
      if (c < 0 || c >= cols) return;
      if (moveSlot(w, w.index + dc + dr * cols)) changed();
      return;
    }
    const to = ev.shiftKey ? { col: w.col, row: w.row, w: w.w + dc, h: w.h + dr }
                           : { col: w.col + dc, row: w.row + dr, w: w.w, h: w.h };
    if (to.w < 1 || to.h < 1 || !fits(to.col, to.row, to.w, to.h, w.id)) return;
    Object.assign(w, to);
    changed();
  });
  function removeSelected() {
    if (inLayout()) layout().widgets = layout().widgets.filter(w => w.id !== selected);
    else project[view] = project[view].filter(w => w.id !== selected);
    selected = null;
    changed();
  }

  const RGB = { gray: '#7b7b7b', red: '#d83b3b', green: '#2e9e4e', blue: '#3456d6', yellow: '#c9a90c', cyan: '#1aa7b8',
                magenta: '#c43bbf', orange: '#e0821b', purple: '#7c3aa8', white: '#bdbdbd' };
  const KIND_RGB = { keys: '#555b66', xypad: '#2d6a8a', tombola: '#6a4a8a', tempo: '#244f9e', sequencer: '#3d5a3a' };
  const widgetColor = w => (w.props.color ? RGB[w.props.color] : KIND_RGB[w.kind]) || '#666';
  const widgetCaption = w => (w.props.label !== undefined ? `${w.props.label}` : WIDGETS[w.kind].label);

  // ---- inspector

  function renderInspector() {
    const box = $('inspector');
    box.textContent = '';
    const w = widget();
    if (!w) { box.innerHTML = `<p class="hint">${t('部品を選ぶと設定が出ます。', 'Select a widget for its settings.')}</p>`; return; }
    const spec = WIDGETS[w.kind];
    const head = document.createElement('div');
    head.className = 'row';
    head.innerHTML = `<strong>${spec.label}</strong>`;
    const del = document.createElement('button');
    del.className = 'small'; del.textContent = t('削除', 'Delete'); del.onclick = removeSelected;
    head.append(del);
    box.append(head);
    if (!inLayout()) {
      // The number on the screen (1-based as UI.pad / UI.knob); another
      // slot there swaps
      box.append(numberField(view === 'knobs' ? t(`番号（バンク ${'ABCD'[bank]}）`, `Number (bank ${'ABCD'[bank]})`) : t('番号', 'Number'), w.index + 1, 1, model.slotCount(project.board),
        val => { if (moveSlot(w, val - 1)) changed(); }));
    } else {
      // Position: col / row / span, 0-based like the DSL
      for (const [key, label] of [['col', t('列 (0〜)', 'Column (0-)')], ['row', t('行 (0〜)', 'Row (0-)')], ['w', t('幅', 'Width')], ['h', t('高さ', 'Height')]]) {
        box.append(numberField(label, w[key], key === 'w' || key === 'h' ? 1 : 0, 12, val => { w[key] = val; changed(); }));
      }
    }
    for (const fld of spec.props) box.append(field(fld, w.props[fld.key], val => { w.props[fld.key] = val; changed(); }, w));
    if (w.kind === 'knob') {
      // The value on the preview's screen (turned there), and taking it as
      // the starting value
      const span = document.createElement('span');
      span.id = 'live-value';
      span.textContent = w.id in liveValues ? String(Math.round(liveValues[w.id] * 100) / 100) : '—';
      const take = document.createElement('button');
      take.className = 'small'; take.textContent = t('初期値にする', 'Use as the start');
      take.onclick = () => { if (w.id in liveValues) { w.props.value = Math.round(liveValues[w.id] * 100) / 100; changed(); } };
      const row = document.createElement('div');
      row.className = 'row';
      row.append(span, take);
      box.append(labeled(t('画面の値', 'On screen'), row));
    }
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
      box.append(labeled(t('動作', 'Action'), sel));
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
  function field(fld, value, set, w = null) {
    switch (fld.type) {
      case 'keycolors': return keyColorsField(fld, value || [], set, w);
      case 'xyslots': return xySlotsField(fld, value || [], set);
      case 'int': return numberField(fld.label, value, fld.min ?? -99999, fld.max ?? 99999, set);
      case 'optnum': {
        // A number, or blank for "as the target has it"
        const i = document.createElement('input');
        i.type = 'number'; i.step = 'any'; i.value = value ?? ''; i.placeholder = '(from the part)';
        i.onchange = () => set(i.value === '' ? '' : Number(i.value));
        return labeled(fld.label, i);
      }
      case 'part': {
        const s = document.createElement('select');
        s.add(new Option('(none: only the Ruby block)', ''));
        for (const p of project.parts) s.add(new Option(`${p.id} (${PART_TYPES[p.type].label}, ch ${p.channel + 1})`, p.id));
        s.value = value || '';
        s.onchange = () => set(s.value);
        return labeled(fld.label, s);
      }
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

  // Keys: colour rows (a colour and its notes), and presets (a scale or a
  // chord from a root, on this keyboard's range)
  function keyColorsField(fld, rows, set, w) {
    const box = document.createElement('div');
    const commit = () => set(rows.map(r => ({ color: r.color, notes: [...r.notes] })));
    rows.forEach((r, i) => {
      const line = document.createElement('div');
      line.className = 'row';
      const col = document.createElement('select');
      for (const c of COLORS) col.add(new Option(c, c));
      col.value = r.color || 'blue';
      col.onchange = () => { r.color = col.value; commit(); };
      const ns = document.createElement('input');
      ns.type = 'text'; ns.value = (r.notes || []).join(' '); ns.style.flex = '1';
      ns.setAttribute('aria-label', 'Notes');
      ns.onchange = () => { r.notes = ns.value.split(/[\s,]+/).filter(Boolean).map(n => clamp(parseInt(n, 10) || 0, 0, 127)); commit(); };
      const del = document.createElement('button');
      del.className = 'small'; del.textContent = '×';
      del.onclick = () => { rows.splice(i, 1); commit(); };
      line.append(col, ns, del);
      box.append(line);
    });
    const add = document.createElement('div');
    add.className = 'row';
    const preset = document.createElement('select');
    for (const k of Object.keys(model.KEY_PRESETS)) preset.add(new Option(k, k));
    const root = document.createElement('select');
    model.NOTE_NAMES.forEach((n, i) => root.add(new Option(n, String(i))));
    const color = document.createElement('select');
    for (const c of COLORS) color.add(new Option(c, c));
    color.value = 'blue';
    const btn = document.createElement('button');
    btn.className = 'small'; btn.textContent = t('追加', 'Add');
    btn.onclick = () => {
      const low = (w && Number(w.props.low)) || 48, oct = (w && Number(w.props.octaves)) || 2;
      rows.push({ color: color.value, notes: model.presetNotes(preset.value, Number(root.value), low, oct) });
      commit();
    };
    add.append(root, preset, color, btn);
    box.append(add);
    return labeled(fld.label, box);
  }

  // XYPad: what each finger (1-5) does differently from the pad
  function xySlotsField(fld, slots, set) {
    const box = document.createElement('div');
    const commit = () => set(slots.filter(Boolean).map(x => ({ ...x })));
    for (let n = 1; n <= 5; n++) {
      let sl = slots.find(x => x.slot === n);
      const det = document.createElement('details');
      const sum = document.createElement('summary');
      sum.textContent = t(`指 ${n}${sl ? '（変更あり）' : ''}`, `Finger ${n}${sl ? ' (changed)' : ''}`);
      det.append(sum);
      const on = document.createElement('input');
      on.type = 'checkbox'; on.checked = !!sl;
      on.onchange = () => {
        if (on.checked) slots.push({ slot: n, x_mode: '', x_cc: 70, y_cc: '', note: '', channel: '', hold: '', device: '' });
        else slots.splice(slots.indexOf(slots.find(x => x.slot === n)), 1);
        commit();
      };
      det.append(labeled(t('この指を変える', 'Change this finger'), on));
      if (sl) {
        det.open = true;
        const sel = (label, key, opts) => {
          const s = document.createElement('select');
          for (const [v, t] of opts) s.add(new Option(t, v));
          s.value = String(sl[key] ?? '');
          s.onchange = () => { sl[key] = s.value === 'true' ? true : s.value === 'false' ? false : s.value; commit(); };
          return labeled(label, s);
        };
        const num = (label, key, lo, hi) => {
          const i = document.createElement('input');
          i.type = 'number'; i.min = lo; i.max = hi; i.value = sl[key] ?? ''; i.placeholder = t('(パッドと同じ)', '(as the pad)');
          i.onchange = () => { sl[key] = i.value === '' ? '' : clamp(parseInt(i.value, 10) || 0, lo, hi); commit(); };
          return labeled(label, i);
        };
        det.append(
          sel('X', 'x_mode', [['', t('(パッドと同じ)', '(as the pad)')], ['scale', t('スケールの音程', 'Notes of the scale')], ['cc', 'CC']]),
          num(t('X の CC', 'X CC'), 'x_cc', 0, 127), num(t('Y の CC', 'Y CC'), 'y_cc', 0, 127), num(t('ノート（X が CC のとき）', 'Note (when X is a CC)'), 'note', 0, 127),
          num(t('チャンネル (0-15)', 'Channel (0-15)'), 'channel', 0, 15),
          sel('Hold', 'hold', [['', t('(パッドと同じ)', '(as the pad)')], ['true', 'on'], ['false', 'off']]),
          sel(t('デバイス', 'Device'), 'device', [['', t('(パッドと同じ)', '(as the pad)')], ...project.devices.map(d => [d.id, d.id])]));
      }
      box.append(det);
    }
    return labeled(fld.label, box);
  }

  // ---- devices

  function renderDevices() {
    const box = $('devices');
    box.textContent = '';
    project.devices.forEach((d, i) => {
      const row = document.createElement('div');
      row.className = 'row';
      const id = document.createElement('input');
      id.type = 'text'; id.value = d.id; id.style.width = '7em'; id.title = t('Ruby の名前（英小文字・数字・_）', 'Its Ruby name (a-z, 0-9, _)');
      id.onchange = () => {
        const nid = id.value.toLowerCase().replace(/[^a-z0-9_]/g, '_').replace(/^[^a-z]+/, '') || d.id;
        if (project.devices.some(o => o !== d && o.id === nid) || project.parts.some(p => p.id === nid)) { id.value = d.id; return; }
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
        del.onclick = () => {
          project.routes = project.routes.filter(r => r.from !== d.id && r.to !== d.id);
          renameDevice(d.id, project.devices[i === 0 ? 1 : 0].id);
          project.devices.splice(i, 1);
          changed();
        };
        row.append(del);
      }
      box.append(row);
    });
  }
  // Point every widget and action at another device id
  function renameDevice(from, to) {
    for (const d of project.devices) if (d.id === from) d.id = to;
    const all = [...project.layouts.flatMap(l => l.widgets), ...project.pads, ...project.knobs];
    for (const w of all) {
      if (w.props.device === from) w.props.device = to;
      if (w.action && w.action.device === from) w.action.device = to;
      for (const sl of w.props.slots || []) if (sl.device === from) sl.device = to;
    }
    for (const r of project.routes) {
      if (r.from === from) r.from = to;
      if (r.to === from) r.to = to;
    }
  }

  // ---- routes (MIDI.route)

  function renderRoutes() {
    const box = $('routes');
    box.textContent = '';
    const deviceLabel = d => `${d.id} (${DEVICE_KINDS[d.kind].label})`;
    project.routes.forEach((r, i) => {
      const row = document.createElement('div');
      row.className = 'row';
      const from = document.createElement('select');
      from.setAttribute('aria-label', 'From');
      for (const d of project.devices) if (model.canRouteFrom(d.kind)) from.add(new Option(deviceLabel(d), d.id));
      from.add(new Option('Clock (Tempo)', ROUTE_CLOCK));
      from.value = r.from;
      from.onchange = () => {
        r.from = from.value;
        if (r.from === ROUTE_CLOCK) r.channel = 'all';   // the clock has no channels
        changed();
      };
      const to = document.createElement('select');
      to.setAttribute('aria-label', 'To');
      for (const d of project.devices) to.add(new Option(deviceLabel(d), d.id));
      to.value = r.to;
      to.onchange = () => { r.to = to.value; changed(); };
      const ch = document.createElement('select');
      ch.setAttribute('aria-label', 'Channel');
      ch.add(new Option(t('全部', 'Everything'), 'all'));
      if (r.from !== ROUTE_CLOCK) {
        ch.add(new Option(t('Clock だけ', 'Clock only'), 'clock'));
        for (let c = 0; c < 16; c++) ch.add(new Option(t(`ch ${c + 1} だけ`, `ch ${c + 1} only`), String(c)));
      }
      ch.value = String(r.channel ?? 'all');
      ch.onchange = () => { r.channel = /^\d+$/.test(ch.value) ? Number(ch.value) : ch.value; changed(); };
      const del = document.createElement('button');
      del.className = 'small'; del.textContent = '×';
      del.onclick = () => { project.routes.splice(i, 1); changed(); };
      const arrow = document.createElement('span');
      arrow.textContent = '→';
      const addFilter = document.createElement('button');
      addFilter.className = 'small'; addFilter.textContent = t('+ フィルタ', '+ Filter');
      addFilter.title = t('名前付き filter（MIDI.register_filter。プラグインが登録するもの）を通す', 'Pass a named filter (MIDI.register_filter, as plugins register them)');
      addFilter.onclick = () => { (r.chain ||= []).push({ name: '', params: '' }); changed({ preview: false }); };
      row.append(from, arrow, to, ch, addFilter, del);
      box.append(row);
      // Its chain: the filters in order (Route#chain)
      (r.chain || []).forEach((f, fi) => {
        const line = document.createElement('div');
        line.className = 'row';
        line.style.paddingLeft = '1.5em';
        const n = document.createElement('input');
        n.type = 'text'; n.value = f.name || ''; n.placeholder = 'filter name'; n.style.width = '8em';
        n.setAttribute('list', 'known-filters');
        n.onchange = () => { f.name = n.value.trim().toLowerCase(); changed(); };
        const ps = document.createElement('input');
        ps.type = 'text'; ps.value = f.params || ''; ps.placeholder = 'rate=1/8 octaves=2'; ps.style.flex = '1';
        ps.title = t('パラメータ（名前=値 をスペース区切り。数・true / false 以外はシンボル）', 'Parameters (name=value, separated by spaces; anything but numbers and true / false is a Symbol)');
        ps.onchange = () => { f.params = ps.value; changed(); };
        const up = document.createElement('button');
        up.className = 'small'; up.textContent = '↑'; up.disabled = fi === 0; up.title = t('前へ', 'Earlier');
        up.onclick = () => { [r.chain[fi - 1], r.chain[fi]] = [r.chain[fi], r.chain[fi - 1]]; changed(); };
        const x = document.createElement('button');
        x.className = 'small'; x.textContent = '×';
        x.onclick = () => { r.chain.splice(fi, 1); changed(); };
        line.append(`${fi + 1}.`, n, ps, up, x);
        box.append(line);
      });
    });
    if (!$('known-filters')) {
      const dl = document.createElement('datalist');
      dl.id = 'known-filters';
      for (const name of model.KNOWN_FILTERS) dl.append(new Option(name));
      document.body.append(dl);
    }
  }
  $('add-route').onclick = () => {
    const src = project.devices.find(d => model.canRouteFrom(d.kind));
    const from = src ? src.id : ROUTE_CLOCK;
    const dst = project.devices.find(d => d.id !== from) || project.devices[0];
    project.routes.push({ from, to: dst.id, channel: 'all' });
    changed();
  };

  // ---- Home's menu (UI.menu)

  function renderMenu() {
    const box = $('menu');
    box.textContent = '';
    const hide = new Set(project.menu.hide);
    for (const [key, label] of MENU_SCREENS) {
      const l = document.createElement('label');
      const i = document.createElement('input');
      i.type = 'checkbox'; i.checked = !hide.has(key);
      i.onchange = () => {
        project.menu.hide = MENU_SCREENS.map(([k]) => k)
          .filter(k => (k === key ? !i.checked : hide.has(k)));
        changed();
      };
      l.append(i, ` ${label}`);
      box.append(l);
    }
  }
  // ---- AMY parts (Tab5)

  function renderParts() {
    const box = $('parts');
    box.textContent = '';
    $('parts-card').hidden = project.board !== 'tab5' && project.parts.length === 0;
    project.parts.forEach((p, i) => {
      const row = document.createElement('div');
      row.className = 'row';
      const id = document.createElement('input');
      id.type = 'text'; id.value = p.id; id.style.width = '6em'; id.title = t('Ruby の名前', 'Its Ruby name');
      id.onchange = () => {
        const nid = id.value.toLowerCase().replace(/[^a-z0-9_]/g, '_').replace(/^[^a-z]+/, '') || p.id;
        if (nid === p.id || project.parts.some(o => o.id === nid) || project.devices.some(d => d.id === nid)) { id.value = p.id; return; }
        for (const l of project.layouts) for (const w of l.widgets) if (w.props.to === p.id) w.props.to = nid;
        p.id = nid;
        changed();
      };
      const type = document.createElement('select');
      for (const [k, t] of Object.entries(PART_TYPES)) type.add(new Option(t.label, k));
      type.value = p.type;
      type.onchange = () => { p.type = type.value; p.patch = PART_TYPES[p.type].def; changed(); };
      const num = (label, key, lo, hi, show = v => v, store = v => v) => {
        const l = document.createElement('label');
        l.textContent = `${label} `;
        const i = document.createElement('input');
        i.type = 'number'; i.min = show(lo); i.max = show(hi); i.value = show(p[key]); i.style.width = '4.5em';
        i.onchange = () => { const n = parseInt(i.value, 10); if (Number.isFinite(n)) { p[key] = clamp(store(n), lo, hi); changed(); } };
        l.append(i);
        return l;
      };
      const [plo, phi] = PART_TYPES[p.type].patch;
      row.append(id, type,
        num('ch', 'channel', 0, 15, v => v + 1, v => v - 1),
        num('voices', 'voices', 1, 16),
        num('patch', 'patch', plo, phi));
      const del = document.createElement('button');
      del.className = 'small'; del.textContent = '×';
      del.onclick = () => {
        for (const l of project.layouts) for (const w of l.widgets) if (w.props.to === p.id) w.props.to = '';
        project.parts.splice(i, 1);
        changed();
      };
      row.append(del);
      box.append(row);
    });
  }
  $('add-part').onclick = () => {
    let n = project.parts.length + 1;
    while (project.parts.some(p => p.id === `part${n}`)) n++;
    const used = new Set(project.parts.map(p => p.channel));
    let ch = 0;
    while (used.has(ch) && ch < 15) ch++;
    project.parts.push({ id: `part${n}`, type: 'fm', channel: ch, voices: 6, patch: PART_TYPES.fm.def });
    changed();
  };

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
      const where = p.screen ? (p.screen === 'pads' ? 'Pads' : 'Knobs')
        : p.widget && project.layouts[p.layout] ? project.layouts[p.layout].title : '';
      li.textContent = where ? `${where}: ${p.message}` : p.message;
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
    renderParts();
    renderRoutes();
    renderMenu();
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
  // What the running preview was made from: structure (codegen.structureKey)
  // and the project then. A change that keeps the structure is patched into
  // the running VM (live); anything else starts the preview again.
  let running = null;
  const liveQueue = [];
  let wantValues = false;
  let liveValues = {};          // widget / slot id -> the knob's value on screen
  const live = {
    take: () => {
      if (liveQueue.length) return liveQueue.shift();
      if (wantValues && running) { wantValues = false; return `${codegen.moduleName(project.name)}.live_values`; }
      return null;
    },
    failed: msg => {
      print(t(`[designer] 反映できなかったので作り直します（${msg}）`, `[designer] Could not patch it in (${msg}); starting again`), 'sys');
      running = null;
      preview();
    },
    values: json => {
      try { liveValues = JSON.parse(json); } catch (e) { return; }
      const el = document.getElementById('live-value');
      if (el && selected && selected in liveValues) el.textContent = String(Math.round(liveValues[selected] * 100) / 100);
    },
  };
  // The selected knob's value on screen, twice a second
  setInterval(() => {
    const w = widget();
    if (w && w.kind === 'knob' && running) wantValues = true;
  }, 500);

  function schedulePreview() {
    clearTimeout(timer);
    timer = setTimeout(update, PREVIEW_DELAY_MS);
  }

  // Patch or start again
  function update() {
    if (device && running && !starting && codegen.structureKey(project) === running.key) patch();
    else preview();
  }

  // The knobs / envelopes whose starting value changed (or that are new):
  // they take the new value; the others keep what is on screen
  const valueKeys = ['value', 'min', 'max', 'attack', 'decay', 'sustain', 'release'];
  function freshIds(before, after) {
    const old = new Map();
    const all = p => [...p.layouts.flatMap(l => l.widgets), ...p.pads, ...p.knobs];
    for (const w of all(before)) old.set(w.id, w);
    const out = [];
    for (const w of all(after)) {
      const o = old.get(w.id);
      if (!o || valueKeys.some(k => JSON.stringify(o.props[k]) !== JSON.stringify(w.props[k]))) out.push(w.id);
    }
    return out;
  }

  function patch() {
    const code = codegen.generate(project, { live: { fresh: freshIds(running.snap, project) }, show: viewKey() });
    liveQueue.push(code);
    running.snap = structuredClone(project);
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
        script: codegen.generate(project, { show: viewKey(), preview: true }),
        onOutput: print, onMidiOut: midiOut, amyAudio,
        onScriptEnd: () => print('[designer] the script ended', 'sys'),
        live,
      });
      liveQueue.length = 0;
      running = { key: codegen.structureKey(project), snap: structuredClone(project) };
      device.M.simVolume = (out, percent) => { if (out === 0) { synth.setVolume(percent); amyAudio.setVolume(percent); } };
      print(`[designer] ${BOARDS[project.board].name}: started in ${Math.round(performance.now() - t0)} ms`, 'sys');
    })();
    try { await starting; } catch (e) { print(`[designer] ${e.message}`, 'err'); }
    starting = null;
    if (again) { again = false; preview(); }
  }

  // ---- the real MIDoRI (USB CDC): open a project from it, save one to it

  const quiet = /^(#[<>]|#info |[fd]\t\d+\t|(info|ls|rm|mv|rescan)( |$)|[IWD] \(|\[PicoModem\])/;
  const link = new linkMod.MidoriLink({ onLine: line => { if (line.trim() && !quiet.test(line)) print(`[MIDoRI] ${line}`, 'sys'); } });
  panelMod.mountDevicePanel($('midori-panel-body'), {
    link, print, startupFiles: false,
    getCode: () => codegen.generate(project),
    getName: () => codegen.fileName(project.name).replace(/\.rb$/, ''),
    // A designer file: its project; anything else goes to the simulator's editor
    setCode: (text, path) => {
      let m = null;
      try { m = codegen.decodeModel(text); } catch (e) { m = null; }
      if (!m || !Array.isArray(m.layouts)) {
        if (confirm(t(`${path} はデザイナーで作ったファイルではありません（先頭に # midori-designer: がありません）。\nシミュレーターのエディタで開きますか？`, `${path} was not made by the designer (no # midori-designer: line at the top).\nOpen it in the simulator's editor?`))) {
          try { localStorage.setItem('midori-sim/script', text); } catch (e) {}
          window.open('./index.html', '_blank');
        }
        return false;
      }
      project = model.upgrade(m);
      openAtStart();
      selected = null;
      changed();
      return true;
    },
    // Never over a hand-written script; ask before replacing a project
    beforeSave: async (path, existing) => {
      if (existing === null) return true;
      if (!existing.startsWith(codegen.HEADER)) {
        alert(t(`${path} はデザイナーで作ったファイルではない（手書きのスクリプト）ので、上書きしません。名前を変えて保存してください。`, `${path} is a hand-written script (not made by the designer), so it is not replaced. Save under another name.`));
        return false;
      }
      return confirm(t(`${path} は実機にあります。上書きしますか？`, `${path} is on the device. Replace it?`));
    },
  });

  renderAll();
  preview();
}

function clamp(n, lo, hi) { return Math.max(lo, Math.min(hi, n)); }
