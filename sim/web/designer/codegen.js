// The project script (docs/WEB_SIMULATOR.md §6.2): one Ruby file that sets up
// the whole project and runs as an ordinary script on the device
// (/sd/<name>.rb). Its first line carries the project model, so the designer
// can open the file again; the device ignores the comment.

import { WIDGETS, DEVICE_KINDS, PART_TYPES, ROUTE_CLOCK, upgrade } from './model.js';

export const HEADER = '# midori-designer: ';

// ---- Ruby literals

export function rubyStr(s) {
  return '"' + String(s)
    .replace(/\\/g, '\\\\').replace(/"/g, '\\"').replace(/#(?=[{$@])/g, '\\#')
    .replace(/\n/g, '\\n').replace(/\t/g, '\\t') + '"';
}
const int = (v, lo = -Infinity, hi = Infinity) => Math.max(lo, Math.min(hi, Math.round(Number(v) || 0)));
const num = v => { const n = Number(v); return Number.isFinite(n) ? String(n) : '0'; };
const sym = s => ':' + String(s).replace(/[^A-Za-z0-9_]/g, '_');
const notes = a => '[' + (Array.isArray(a) ? a : []).map(n => int(n, 0, 127)).join(', ') + ']';

// The module the project lives in: a constant from its name ("My Stage" -> MyStage)
export function moduleName(name) {
  const words = String(name || '').match(/[A-Za-z0-9]+/g) || [];
  let m = words.map(w => w[0].toUpperCase() + w.slice(1)).join('');
  if (!/^[A-Z]/.test(m)) m = 'Project' + m;
  return m;
}

// The file name on /sd ("My Stage" -> my_stage.rb)
export function fileName(name) {
  const base = String(name || '').toLowerCase().replace(/[^a-z0-9]+/g, '_').replace(/^_+|_+$/g, '') || 'project';
  return `${base}.rb`;
}

// ---- the model in the header (base64 of UTF-8 JSON)

export function encodeModel(project) {
  const bytes = new TextEncoder().encode(JSON.stringify(project));
  let bin = '';
  for (const b of bytes) bin += String.fromCharCode(b);
  return btoa(bin);
}

export function decodeModel(text) {
  const line = String(text).split('\n').find(l => l.startsWith(HEADER));
  if (!line) return null;
  const bin = atob(line.slice(HEADER.length).trim());
  const bytes = Uint8Array.from(bin, c => c.charCodeAt(0));
  return JSON.parse(new TextDecoder().decode(bytes));
}

// ---- generation

function deviceMethods(project, M) {
  const out = [];
  for (const d of project.devices) {
    const kind = DEVICE_KINDS[d.kind];
    const transport = d.kind === 'web_midi'
      ? `MIDIDevices.respond_to?(:web_midi) ? MIDIDevices.web_midi(${rubyStr(d.port || '')}) : nil`
      : kind.ruby;
    out.push(
      `  # ${kind.label}`,
      `  def self.${d.id}_transport`,
      `    ${transport}`,
      `  end`,
      ``,
      `  def self.${d.id}`,
      `    @${d.id} ||= (t = ${d.id}_transport) && MIDI::Device.new(t)`,
      `  end`,
      ``,
    );
  }
  return out;
}

// AMY parts: made once, the first time anything asks (setup does, so they
// play from the start); nil where there is no AMY
function partMethods(project) {
  const out = [];
  for (const p of project.parts || []) {
    const t = PART_TYPES[p.type] || PART_TYPES.fm;
    const patch = int(p.patch ?? t.def, t.patch[0], t.patch[1]);
    out.push(
      `  # AMY ${t.label} part on channel ${int(p.channel, 0, 15) + 1}`,
      `  def self.${p.id}`,
      `    @${p.id} ||= (amy = MIDIDevices.amy) && amy.add_${p.type === 'synth' ? 'synth' : 'fm'}(` +
        `channel: ${int(p.channel, 0, 15)}, voices: ${int(p.voices, 1, 16)}, patch: ${patch})`,
      `  end`,
      ``,
    );
  }
  return out;
}

const devId = (project, id) => (project.devices.some(d => d.id === id) ? id : (project.devices[0] || { id: 'synth' }).id);

// A knob's value / range keywords (only what differs from UI.knob's defaults)
function knobArgs(p) {
  const n = (v, d) => (Number.isFinite(Number(v)) ? Number(v) : d);
  const min = n(p.min, 0), max = n(p.max, 127), step = n(p.step, 1), sens = n(p.sensitivity, 1);
  const lo = Math.min(min, max), hi = Math.max(min, max);
  const out = [`value: ${num(Math.max(lo, Math.min(hi, n(p.value, 64))))}`];
  if (p.origin === 'center') out.push('origin: :center');
  if (min !== 0) out.push(`min: ${num(min)}`);
  if (max !== 127) out.push(`max: ${num(max)}`);
  if (step !== 1) out.push(`step: ${num(step)}`);
  if (sens !== 1) out.push(`sensitivity: ${num(sens)}`);
  return out;
}

// The block of a pad / knob: [params, body lines]
function actionBlock(project, M, widget) {
  const a = widget.action || { type: 'none' };
  const dev = `${M}.${devId(project, a.device)}`;
  const ch = `channel: ${int(a.channel, 0, 15)}`;
  const isKnob = widget.kind === 'knob';
  const latched = !isKnob && widget.props.type !== 'trigger';   // momentary / toggle get the state
  switch (a.type) {
    case 'note': {
      const n = int(a.note, 0, 127), vel = int(a.velocity, 1, 127);
      if (latched) return ['on', [`d = ${dev}`, `next unless d`, `on ? d.note_on(${n}, ${vel}, ${ch}) : d.note_off(${n}, 0, ${ch})`]];
      return ['', [`${dev}&.trigger(${n}, ${vel}, duration: ${int(a.duration, 1, 60000)}, ${ch})`]];
    }
    case 'cc':
      if (isKnob) return ['v', [`${dev}&.control_change(${int(a.cc, 0, 127)}, v.to_i, ${ch})`]];
      if (latched) return ['on', [`${dev}&.control_change(${int(a.cc, 0, 127)}, on ? ${int(a.value, 0, 127)} : 0, ${ch})`]];
      return ['', [`${dev}&.control_change(${int(a.cc, 0, 127)}, ${int(a.value, 0, 127)}, ${ch})`]];
    case 'program':
      if (isKnob) return ['v', [`${dev}&.program_change(v.to_i, ${ch})`]];
      return [latched ? 'on' : '', [`${dev}&.program_change(${int(a.program, 0, 127)}, ${ch})`]];
    case 'pitch_bend':
      // The knob's 0..127 over the whole bend range
      return ['v', [`${dev}&.pitch_bend((v.to_i * 16383 / 127) - 8192, ${ch})`]];
    case 'ruby': {
      const body = String(a.body || '').split('\n');
      return [isKnob ? 'v' : (latched ? 'on' : ''), body];
    }
    default:
      return null;
  }
}

// preview: the designer's preview names knobs, pads and envelopes after the
// designer's widget ids (id: :w...), so live patching finds them again
function widgetLines(project, M, w, area, preview = false) {
  const p = w.props;
  const at = `area: ${sym(area)}`;
  const wid = preview && ['pad', 'knob', 'envelope'].includes(w.kind) ? [`id: ${sym(w.id)}`] : [];
  switch (w.kind) {
    case 'pad':
    case 'knob': {
      const args = [at, ...wid, `label: ${rubyStr(p.label)}`, `color: ${sym(p.color || 'gray')}`];
      if (w.kind === 'pad') args.push(`type: ${sym(p.type || 'trigger')}`);
      if (w.kind === 'knob') args.push(...knobArgs(p));
      const block = actionBlock(project, M, w);
      if (!block) return [`${w.kind} ${args.join(', ')}`];
      const [params, body] = block;
      return [
        `${w.kind} ${args.join(', ')} do${params ? ` |${params}|` : ''}`,
        ...body.map(l => `  ${l}`),
        'end',
      ];
    }
    case 'keys':
      return [`keys ${at}, keys: ${M}.keys`];
    case 'xypad':
      return [`xypad ${at}, pad: ${M}.xypad`];
    case 'tombola':
      return [`tombola ${at}, tombola: ${M}.tombola`];
    case 'tempo':
      return [`tempo ${at}`];
    case 'sequencer':
      return [`sequencer ${at}`];
    case 'envelope': {
      const args = [at, ...wid, `label: ${rubyStr(p.label)}`, `color: ${sym(p.color || 'cyan')}`, `max_time: ${int(p.max_time, 100, 20000)}`];
      for (const k of ['attack', 'decay', 'sustain', 'release']) {
        if (p[k] !== '' && p[k] !== null && p[k] !== undefined && Number.isFinite(Number(p[k]))) args.push(`${k}: ${num(p[k])}`);
      }
      const part = (project.parts || []).find(x => x.id === p.to);
      if (part) {
        const op = /^op([1-6])$/.exec(p.stage || '');
        if (part.type === 'fm') args.push(`to: ${M}.${part.id}&.op(${op ? op[1] : 1})`);
        else args.push(`to: ${M}.${part.id}`, `env: ${sym(p.stage === 'filter' ? 'filter' : 'amp')}`);
      }
      const a = w.action || { type: 'none' };
      if (a.type !== 'ruby') return [`envelope ${args.join(', ')}`];
      return [`envelope ${args.join(', ')} do |e|`, ...String(a.body || '').split('\n').map(l => `  ${l}`), 'end'];
    }
    default:
      return [`# unknown widget: ${w.kind}`];
  }
}

// The Pads / Knobs screens: UI.pad / UI.knob with the same blocks as the
// layout's pads and knobs
function slotLines(project, M, sl) {
  const p = sl.props;
  const args = [String(sl.index + 1)];
  if (sl.kind === 'knob') args.push(`bank: ${int(sl.bank, 0, 3) + 1}`);
  args.push(`label: ${rubyStr(p.label)}`, `color: ${sym(p.color || 'gray')}`);
  if (sl.kind === 'pad') args.push(`type: ${sym(p.type || 'trigger')}`);
  else args.push(...knobArgs(p));
  const call = `UI.${sl.kind}(${args.join(', ')})`;
  const block = actionBlock(project, M, sl);
  if (!block) return [call];
  const [params, body] = block;
  return [`${call} do${params ? ` |${params}|` : ''}`, ...body.map(l => `  ${l}`), 'end'];
}

function screenMethods(project, M) {
  const out = [];
  const bySlot = (a, b) => (a.bank || 0) - (b.bank || 0) || a.index - b.index;
  for (const [key, name] of [['pads', 'setup_pads'], ['knobs', 'setup_knobs']]) {
    const slots = [...(project[key] || [])].sort(bySlot);
    if (!slots.length) continue;
    out.push(`  # The ${key === 'pads' ? 'Pads' : 'Knobs'} screen`, `  def self.${name}`);
    for (const sl of slots) for (const l of slotLines(project, M, sl)) out.push(`    ${l}`);
    out.push('  end', '');
  }
  return out;
}

// The source / destination of a route as Ruby: a device's transport, or
// the clock (what Tempo sends)
const routeEnd = (M, id) => (id === ROUTE_CLOCK ? 'MIDI::Clock' : `${M}.${id}_transport`);
const routeChannel = ch => (ch === 'clock' ? ':clock' : Number.isInteger(ch) ? String(int(ch, 0, 15)) : 'nil');

// A filter's parameters "rate=1/8 octaves=2 on=true" as a Ruby Hash body:
// numbers, booleans, anything else a Symbol (enum choices)
function filterParams(text) {
  const out = [];
  for (const m of String(text || '').matchAll(/([A-Za-z_][A-Za-z0-9_]*)\s*[=:]\s*("[^"]*"|\S+)/g)) {
    let v = m[2].replace(/^"|"$/g, '');
    if (/^-?\d+$/.test(v)) v = String(parseInt(v, 10));
    else if (/^-?\d*\.\d+$/.test(v)) v = String(Number(v));
    else if (v === 'true' || v === 'false') v = v;
    else v = `:${rubyStr(v.replace(/^:/, ''))}`;
    out.push(`${m[1]}: ${v}`);
  }
  return out.join(', ');
}

// Route#chain's arguments: [:chord, [:arp, { rate: :"1/8" }]]
function chainArg(chain) {
  const items = (chain || []).filter(f => f && /^[a-z][a-z0-9_]{0,15}$/.test(f.name || ''))
    .map(f => { const p = filterParams(f.params); return p ? `[:${f.name}, { ${p} }]` : `:${f.name}`; });
  return items.length ? `[${items.join(', ')}]` : '';
}

function routeMethods(project, M) {
  const routes = (project.routes || []).filter(r => r.from && r.to && r.from !== r.to);
  if (!routes.length) return [];
  return [
    `  # MIDI.route: skipped where a device is absent on this board`,
    `  # chain: named filters (MIDI.register_filter, e.g. by a plugin), in order`,
    `  def self.route(from, to, channel = nil, chain = nil)`,
    `    return unless from && to`,
    `    route = MIDI.route(from, to, channel: channel)`,
    `    return puts("route: the route table is full") unless route`,
    `    route.chain(*chain) if chain`,
    `  rescue ArgumentError => e`,
    `    puts "route: #{e.message}"`,
    `  end`,
    ``,
    `  def self.setup_routes`,
    ...routes.map(r => {
      const ch = routeChannel(r.channel), chain = chainArg(r.chain);
      const args = [routeEnd(M, r.from), routeEnd(M, r.to)];
      if (ch !== 'nil' || chain) args.push(ch);
      if (chain) args.push(chain);
      return `    route(${args.join(', ')})`;
    }),
    `  end`,
    ``,
  ];
}

// The Tempo widget's BPM at start (the clock is one for the whole device)
function tempoLines(project) {
  for (const l of project.layouts) for (const w of l.widgets) {
    if (w.kind === 'tempo' && w.props.bpm !== '' && w.props.bpm != null && Number.isFinite(Number(w.props.bpm))) {
      return [`    UI::Tempo.bpm = ${num(Math.max(20, Math.min(300, Number(w.props.bpm))))}`];
    }
  }
  return [];
}

// What setup shows first: a layout, or the Pads / Knobs screen
function showLine(project, show) {
  if (show === 'pads') return `    UI.show(:pads)`;
  if (typeof show === 'string' && show.startsWith('knobs')) {
    const bank = Number(show.split(':')[1]) || 0;
    return `    UI.show(:knobs, bank: ${int(bank, 0, 3) + 1})`;
  }
  const i = Math.max(0, Math.min(project.layouts.length - 1, Number(show) || 0));
  return `    UI.show(@layouts[${i}]) if @layouts[${i}]`;
}

// XYPad / Tombola have one model on the device: one instance in the project,
// made once and placed by whichever layout has it
function instanceMethods(project, M) {
  const out = [];
  const find = kind => { for (const l of project.layouts) for (const w of l.widgets) if (w.kind === kind) return w; return null; };
  const kb = find('keys');
  if (kb) {
    const p = kb.props;
    const vel = p.velocity === 'position' ? ':position' : String(int(p.velocity, 1, 127));
    const colors = (Array.isArray(p.colors) ? p.colors : []).filter(c => c && Array.isArray(c.notes) && c.notes.length);
    out.push(
      `  def self.keys`,
      `    @keys ||= begin`,
      `      k = UI::Keys.new(octaves: ${int(p.octaves, 1, 4)}, low: ${int(p.low, 0, 108)}, channel: ${int(p.channel, 0, 15)}, ` +
        `velocity: ${vel}, device: ${devId(project, p.device)}_transport)`,
      ...colors.map(c => `      k.set_color(${notes(c.notes)}, ${sym(c.color || 'blue')})`),
      `      k`,
      `    end`,
      `  end`, ``);
  }
  const xy = find('xypad');
  if (xy) {
    const p = xy.props;
    const slots = (Array.isArray(p.slots) ? p.slots : []).filter(sl => sl && sl.slot >= 1 && sl.slot <= 5);
    out.push(
      `  def self.xypad`,
      `    @xypad ||= begin`,
      `      pad = UI::XYPad.new(scale: ${notes(p.scale)}, glide_range: ${int(p.glide_range, 0, 12)}, ` +
        `y_cc: ${int(p.y_cc, 0, 127)}, channel: ${int(p.channel, 0, 15)}, hold: ${p.hold ? 'true' : 'false'}, ` +
        `device: ${devId(project, p.device)})`,
      ...slots.map(sl => {
        // Only what the finger changes; the rest stays the pad's
        const a = [];
        if (sl.x_mode === 'cc') a.push('x_mode: :cc', `x_cc: ${int(sl.x_cc ?? 70, 0, 127)}`);
        if (sl.x_mode === 'scale') a.push('x_mode: :scale');
        if (sl.y_cc !== '' && sl.y_cc !== undefined && sl.y_cc !== null) a.push(`y_cc: ${int(sl.y_cc, 0, 127)}`);
        if (sl.note !== '' && sl.note !== undefined && sl.note !== null) a.push(`note: ${int(sl.note, 0, 127)}`);
        if (sl.channel !== '' && sl.channel !== undefined && sl.channel !== null) a.push(`channel: ${int(sl.channel, 0, 15)}`);
        if (sl.hold === true || sl.hold === false) a.push(`hold: ${sl.hold}`);
        if (sl.device) a.push(`device: ${devId(project, sl.device)}`);
        return a.length ? `      pad.slot(${int(sl.slot, 1, 5)}, ${a.join(', ')})` : null;
      }).filter(Boolean),
      `      pad`,
      `    end`,
      `  end`, ``);
  }
  const tb = find('tombola');
  if (tb) {
    const p = tb.props;
    out.push(
      `  def self.tombola`,
      `    @tombola ||= begin`,
      `      t = UI::Tombola.new(sides: ${int(p.sides, 3, 12)}, rotation: ${num(p.rotation)}, gravity: ${num(p.gravity)}, ` +
        `scale: ${notes(p.scale)}, channel: ${int(p.channel, 0, 15)}, device: ${devId(project, p.device)})`,
      `      ${int(p.balls, 0, 8)}.times { t.add_ball }`,
      `      t.start`,
      `      t`,
      `    end`,
      `  end`, ``);
  }
  return out;
}

// The areas text of a layout: one name per widget, "." for empty cells
function areas(layout) {
  const prefix = { pad: 'p', knob: 'k', keys: 'kb', xypad: 'xy', tombola: 'tb', tempo: 'tp' };
  const names = new Map(layout.widgets.map((w, i) => [w, `${prefix[w.kind] || 'w'}${i + 1}`]));
  const width = Math.max(...layout.widgets.map(w => names.get(w).length), 1);
  const rows = [];
  for (let r = 0; r < layout.rows; r++) {
    const cells = [];
    for (let c = 0; c < layout.cols; c++) {
      const w = layout.widgets.find(x => c >= x.col && c < x.col + x.w && r >= x.row && r < x.row + x.h);
      cells.push((w ? names.get(w) : '.').padEnd(width));
    }
    rows.push(cells.join(' ').trimEnd());
  }
  return { rows, names };
}

function layoutMethod(project, M, layout, index, preview = false) {
  const out = [`  def self.layout_${index + 1}`,
               `    UI.layout(${rubyStr(layout.title)}, cols: ${int(layout.cols, 1, 16)}, rows: ${int(layout.rows, 1, 16)}) do`];
  if (layout.widgets.length) {
    const { rows, names } = areas(layout);
    out.push('      areas <<~GRID', ...rows.map(r => `        ${r}`), '      GRID');
    for (const w of layout.widgets) {
      for (const l of widgetLines(project, M, w, names.get(w), preview)) out.push(`      ${l}`);
    }
  }
  out.push('    end', '  end', '');
  return out;
}

// The designer's live patch (docs/WEB_SIMULATOR.md §6.3): the module again
// (its methods redefined; devices, parts and the XYPad / Tombola / Keys stay,
// being memoised), then live_update: the layouts redefined in place (same
// titles), the Pads / Knobs screens again, and the knobs and envelopes on
// screen keep their values unless their starting value changed (fresh ids).
// live_values hands the knobs' values on screen to the page.
function liveScript(project, M, opts) {
  const fresh = new Set(opts.live.fresh || []);
  const keep = [], values = [];
  project.layouts.forEach((l, i) => l.widgets.forEach(w => {
    if (w.kind !== 'knob' && w.kind !== 'envelope') return;
    if (!fresh.has(w.id)) keep.push(`[${i}, ${sym(w.id)}]`);
    if (w.kind === 'knob') values.push(`[${i}, ${sym(w.id)}, ${rubyStr(w.id)}]`);
  }));
  const screenKnobs = project.knobs.map(k => `[${int(k.bank, 0, 3) + 1}, ${k.index + 1}, ${rubyStr(k.id)}, ${fresh.has(k.id)}]`);
  return [
    `module ${M}`,
    ...deviceMethods(project, M),
    ...partMethods(project),
    ...instanceMethods(project, M),
    ...project.layouts.flatMap((l, i) => layoutMethod(project, M, l, i, true)),
    ...screenMethods(project, M),
    ...liveMethods(project, M, opts, keep, values, screenKnobs),
    `end`,
    `${M}.live_update`,
    ``,
  ].join('\n');
}

// live_update / live_values (in the preview's script too, so the page can
// ask for values before the first patch)
function liveMethods(project, M, opts, keep, values, screenKnobs) {
  return [
    `  def self.live_update`,
    `    old = @layouts || []`,
    `    kept = {}`,
    `    [${keep.join(', ')}].each do |i, id|`,
    `      h = old[i] && old[i][id]`,
    `      kept[[i, id]] = (h.is_a?(UI::Envelope) ? h.values : h.value) if h`,
    `    end`,
    `    knobs = {}`,
    `    [${screenKnobs.join(', ')}].each do |b, n, _, fresh|`,
    `      knobs[[b, n]] = UI.knob_value(n, bank: b) if !fresh && UI._knob_assigned(b - 1, n - 1)`,
    `    end`,
    `    @layouts = [${project.layouts.map((_, i) => `layout_${i + 1}`).join(', ')}]`,
    `    kept.each do |(i, id), v|`,
    `      h = @layouts[i] && @layouts[i][id]`,
    `      next unless h`,
    `      if v.is_a?(Hash)`,
    `        h.set(attack: v[:attack], decay: v[:decay], sustain: v[:sustain], release: v[:release], notify: false)`,
    `      else`,
    `        h.set_value(v, notify: false)`,
    `      end`,
    `    end`,
    `    UI.pad_clear_all`,
    ...(project.pads.length ? ['    setup_pads'] : []),
    `    UI.knob_clear_all`,
    ...(project.knobs.length ? ['    setup_knobs'] : []),
    `    knobs.each { |(b, n), v| UI.knob_set(n, v, bank: b, notify: false) }`,
    showLine(project, opts.show ?? project.start ?? 0),
    `  end`,
    ``,
    `  def self.live_values`,
    `    out = {}`,
    `    [${values.join(', ')}].each do |i, id, key|`,
    `      h = @layouts && @layouts[i] && @layouts[i][id]`,
    `      out[key] = h.value if h`,
    `    end`,
    `    [${screenKnobs.join(', ')}].each do |b, n, key, _|`,
    `      out[key] = UI.knob_value(n, bank: b) if UI._knob_assigned(b - 1, n - 1)`,
    `    end`,
    `    JS.global[:__midoriSim].liveValues(JSON.generate(out))`,
    `  end`,
    ``,
  ];
}

// liveMethods' lists for a project (no fresh ids: the preview's start)
function liveLists(project) {
  const keep = [], values = [];
  project.layouts.forEach((l, i) => l.widgets.forEach(w => {
    if (w.kind !== 'knob' && w.kind !== 'envelope') return;
    keep.push(`[${i}, ${sym(w.id)}]`);
    if (w.kind === 'knob') values.push(`[${i}, ${sym(w.id)}, ${rubyStr(w.id)}]`);
  }));
  const screenKnobs = project.knobs.map(k => `[${int(k.bank, 0, 3) + 1}, ${k.index + 1}, ${rubyStr(k.id)}, false]`);
  return [keep, values, screenKnobs];
}

// What, if changed, needs the preview started again rather than patched:
// the device-wide things (devices, parts, routes, menu, board, the one XYPad /
// Tombola / Keys, Tempo's BPM) and the layouts' titles and number
export function structureKey(project) {
  const single = {};
  for (const l of project.layouts) for (const w of l.widgets) {
    if (['xypad', 'tombola', 'keys', 'tempo'].includes(w.kind)) single[w.kind] = w.props;
  }
  return JSON.stringify({
    name: moduleName(project.name), board: project.board, devices: project.devices, parts: project.parts,
    routes: project.routes, menu: project.menu, sendInitial: project.sendInitial,
    layouts: project.layouts.map(l => l.title), single,
  });
}

// opts.show: what to show first (default project.start): a layout's index,
// 'pads', or 'knobs' / 'knobs:<bank>'; the designer's preview shows what is
// being edited
export function generate(project, opts = {}) {
  upgrade(project);
  const M = moduleName(project.name);
  if (opts.live) return liveScript(project, M, opts);
  const show = opts.show ?? project.start ?? 0;
  const hide = (project.menu.hide || []).filter(Boolean);
  const hasKnobs = project.knobs.length > 0;
  const sendInitial = project.sendInitial !== false;
  const lines = [
    HEADER + encodeModel(project),
    `# ${project.name} -- made with the MIDoRI Designer. Edit it there: this file is generated.`,
    `require 'midi'`,
    `require 'ui'`,
    ``,
    `module ${M}`,
    ...deviceMethods(project, M),
    ...partMethods(project),
    ...instanceMethods(project, M),
    ...project.layouts.flatMap((l, i) => layoutMethod(project, M, l, i, !!opts.preview)),
    ...screenMethods(project, M),
    ...routeMethods(project, M),
    ...(opts.preview ? liveMethods(project, M, opts, ...liveLists(project)) : []),
    `  def self.setup`,
    ...(project.parts.length ? [`    ${project.parts.map(p => p.id).join('; ')}   # the AMY parts`] : []),
    `    @layouts = [${project.layouts.map((_, i) => `layout_${i + 1}`).join(', ')}]`,
    ...(project.pads.length ? ['    setup_pads'] : []),
    ...(hasKnobs ? ['    setup_knobs'] : []),
    ...(routeMethods(project, M).length ? ['    setup_routes'] : []),
    ...(hide.length ? [`    UI.menu(hide: [${hide.map(sym).join(', ')}])   # Home's tiles`] : []),
    ...tempoLines(project),
    ...(sendInitial ? [
      `    # Knobs and envelopes send their starting values (defining them sends nothing)`,
      `    @layouts.each { |l| l.send_all }`,
      ...(hasKnobs ? ['    UI.knob_send_all(bank: :all)'] : [])] : []),
    showLine(project, show),
    `  end`,
    ``,
    `  def self.run`,
    `    sm = ScriptManager.new`,
    `    until sm.stop_requested?`,
    `      UI.process`,
    `      MIDI.sleep_ms 10`,
    `    end`,
    `  end`,
    `end`,
    ``,
    `${M}.setup`,
    `${M}.run`,
    ``,
  ];
  return lines.join('\n');
}
