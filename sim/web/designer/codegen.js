// The project script (docs/WEB_SIMULATOR.md §6.2): one Ruby file that sets up
// the whole project and runs as an ordinary script on the device
// (/sd/<name>.rb). Its first line carries the project model, so the designer
// can open the file again; the device ignores the comment.

import { WIDGETS, DEVICE_KINDS } from './model.js';

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

const devId = (project, id) => (project.devices.some(d => d.id === id) ? id : (project.devices[0] || { id: 'synth' }).id);

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

function widgetLines(project, M, w, area) {
  const p = w.props;
  const at = `area: ${sym(area)}`;
  switch (w.kind) {
    case 'pad':
    case 'knob': {
      const args = [at, `label: ${rubyStr(p.label)}`, `color: ${sym(p.color || 'gray')}`];
      if (w.kind === 'pad') args.push(`type: ${sym(p.type || 'trigger')}`);
      if (w.kind === 'knob') {
        args.push(`value: ${int(p.value, 0, 127)}`);
        if (p.origin === 'center') args.push('origin: :center');
      }
      const block = actionBlock(project, M, w);
      if (!block) return [`${w.kind} ${args.join(', ')}`];
      const [params, body] = block;
      return [
        `${w.kind} ${args.join(', ')} do${params ? ` |${params}|` : ''}`,
        ...body.map(l => `  ${l}`),
        'end',
      ];
    }
    case 'keys': {
      const vel = p.velocity === 'position' ? ':position' : String(int(p.velocity, 1, 127));
      return [`keys ${at}, octaves: ${int(p.octaves, 1, 4)}, low: ${int(p.low, 0, 108)}, channel: ${int(p.channel, 0, 15)}, ` +
              `velocity: ${vel}, device: ${M}.${devId(project, p.device)}_transport`];
    }
    case 'xypad':
      return [`xypad ${at}, pad: ${M}.xypad`];
    case 'tombola':
      return [`tombola ${at}, tombola: ${M}.tombola`];
    case 'tempo':
      return [`tempo ${at}`];
    default:
      return [`# unknown widget: ${w.kind}`];
  }
}

// XYPad / Tombola have one model on the device: one instance in the project,
// made once and placed by whichever layout has it
function instanceMethods(project, M) {
  const out = [];
  const find = kind => { for (const l of project.layouts) for (const w of l.widgets) if (w.kind === kind) return w; return null; };
  const xy = find('xypad');
  if (xy) {
    const p = xy.props;
    out.push(
      `  def self.xypad`,
      `    @xypad ||= UI::XYPad.new(scale: ${notes(p.scale)}, glide_range: ${int(p.glide_range, 0, 12)}, ` +
        `y_cc: ${int(p.y_cc, 0, 127)}, channel: ${int(p.channel, 0, 15)}, hold: ${p.hold ? 'true' : 'false'}, ` +
        `device: ${devId(project, p.device)})`,
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

function layoutMethod(project, M, layout, index) {
  const out = [`  def self.layout_${index + 1}`,
               `    UI.layout(${rubyStr(layout.title)}, cols: ${int(layout.cols, 1, 16)}, rows: ${int(layout.rows, 1, 16)}) do`];
  if (layout.widgets.length) {
    const { rows, names } = areas(layout);
    out.push('      areas <<~GRID', ...rows.map(r => `        ${r}`), '      GRID');
    for (const w of layout.widgets) {
      for (const l of widgetLines(project, M, w, names.get(w))) out.push(`      ${l}`);
    }
  }
  out.push('    end', '  end', '');
  return out;
}

// opts.show: the layout to show first (default project.start); the
// designer's preview shows the one being edited
export function generate(project, opts = {}) {
  const M = moduleName(project.name);
  const show = Math.max(0, Math.min(project.layouts.length - 1, opts.show ?? project.start ?? 0));
  const lines = [
    HEADER + encodeModel(project),
    `# ${project.name} -- made with the MIDoRI Designer. Edit it there: this file is generated.`,
    `require 'midi'`,
    `require 'ui'`,
    ``,
    `module ${M}`,
    ...deviceMethods(project, M),
    ...instanceMethods(project, M),
    ...project.layouts.flatMap((l, i) => layoutMethod(project, M, l, i)),
    `  def self.setup`,
    `    @layouts = [${project.layouts.map((_, i) => `layout_${i + 1}`).join(', ')}]`,
    `    UI.show(@layouts[${show}]) if @layouts[${show}]`,
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
