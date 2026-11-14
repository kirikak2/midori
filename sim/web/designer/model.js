// The designer's project model (docs/WEB_SIMULATOR.md §6.1) and what each
// widget takes. The model is plain JSON: it is what codegen.js turns into the
// project script and what the script carries back in its first line.
//
//   project = {
//     v: 1, name, board,
//     devices: [{ id, kind, port? }],          // kind: sam2695 | amy | usb_midi_host | usb_midi_device | web_midi
//     parts: [{ id, type, channel, voices, patch }],   // AMY parts (Tab5): type fm | synth
//     start: 0 | 'pads' | 'knobs',             // shown first: a layout's index or a screen
//     layouts: [{ title, cols, rows, widgets: [widget] }],
//     pads:  [slot],                           // the Pads screen (UI.pad)
//     knobs: [slot],                           // the Knobs screen (UI.knob), banks 0..3
//     routes: [{ from, to, channel }],         // MIDI.route: device ids ('clock' = MIDI::Clock as a source);
//                                              //   channel 'all' | 'clock' | 0..15
//     menu: { hide: [screen] },                // UI.menu(hide:): Home's tiles
//   }
//   widget = { id, kind, col, row, w, h, props: {}, action: { type, ... } }
//   slot   = { id, kind: 'pad' | 'knob', index, bank?, props: {}, action }   // index / bank 0-based
//
// The widget kinds and their settings follow the layout DSL
// (mrbgems/picoruby-ui/mrblib/ui_layout.rb) and the classes behind it.

export const COLORS = ['gray', 'red', 'green', 'blue', 'yellow', 'cyan', 'magenta', 'orange', 'purple', 'white'];

export const DEVICE_KINDS = {
  sam2695:         { label: 'SAM2695', ruby: 'MIDIDevices.sam2695' },
  amy:             { label: 'AMY (Tab5)', ruby: 'MIDIDevices.amy' },
  usb_midi_host:   { label: 'USB-MIDI Host', ruby: 'MIDIDevices.usb_midi_host' },
  usb_midi_device: { label: 'USB-MIDI Device (PC)', ruby: 'MIDIDevices.usb_midi_device' },
  web_midi:        { label: 'Web MIDI port (simulator only)', ruby: null },
};

// One field of a widget's settings: type is int | float | optnum (a number or
// blank) | text | color | choice | bool | notes (a list of note numbers) |
// device | part (an AMY part or none)
const f = (key, label, type, def, extra = {}) => ({ key, label, type, def, ...extra });

export const WIDGETS = {
  pad: {
    label: 'Pad', size: [1, 1],
    props: [
      f('label', 'Label', 'text', 'Pad'),
      f('color', 'Color', 'color', 'gray'),
      f('type', 'Type', 'choice', 'trigger', { choices: ['trigger', 'toggle', 'momentary'] }),
    ],
    actions: ['note', 'cc', 'program', 'ruby', 'none'],
  },
  knob: {
    label: 'Knob', size: [1, 1],
    props: [
      f('label', 'Label', 'text', 'Knob'),
      f('color', 'Color', 'color', 'cyan'),
      f('value', 'Initial value', 'int', 64, { min: 0, max: 127 }),
      f('origin', 'Origin', 'choice', 'min', { choices: ['min', 'center'] }),
    ],
    actions: ['cc', 'program', 'pitch_bend', 'ruby', 'none'],
  },
  keys: {
    label: 'Keys', size: [4, 1], single: true,
    props: [
      f('octaves', 'Octaves', 'int', 2, { min: 1, max: 4 }),
      f('low', 'Lowest note', 'int', 48, { min: 0, max: 108 }),
      f('channel', 'Channel (0-15)', 'int', 0, { min: 0, max: 15 }),
      f('velocity', 'Velocity', 'choice', '100', { choices: ['100', '127', 'position'] }),
      f('device', 'Device', 'device', 'synth'),
    ],
  },
  xypad: {
    label: 'XYPad', size: [2, 2], single: true,
    props: [
      f('scale', 'Scale (notes)', 'notes', [48, 50, 52, 55, 57, 60]),
      f('glide_range', 'Glide range', 'int', 2, { min: 0, max: 12 }),
      f('y_cc', 'Y CC', 'int', 74, { min: 0, max: 127 }),
      f('channel', 'Channel (0-15)', 'int', 0, { min: 0, max: 15 }),
      f('hold', 'Hold', 'bool', false),
      f('device', 'Device', 'device', 'synth'),
    ],
  },
  tombola: {
    label: 'Tombola', size: [2, 2], single: true,
    props: [
      f('sides', 'Sides', 'int', 6, { min: 3, max: 12 }),
      f('rotation', 'Rotation', 'float', 12),
      f('gravity', 'Gravity', 'float', 0.5),
      f('balls', 'Balls', 'int', 1, { min: 0, max: 8 }),
      f('scale', 'Scale (notes)', 'notes', [36, 38, 42, 45, 46, 49]),
      f('channel', 'Channel (0-15)', 'int', 9, { min: 0, max: 15 }),
      f('device', 'Device', 'device', 'synth'),
    ],
  },
  tempo: { label: 'Tempo', size: [2, 1], single: true, props: [] },
  // An ADSR editor (docs/ENVELOPE.md): on an AMY part (Synth: amp / filter,
  // FM: an operator), or only its Ruby block. Blank stages start from the part.
  envelope: {
    label: 'Envelope', size: [2, 1],
    props: [
      f('label', 'Label', 'text', 'Env'),
      f('color', 'Color', 'color', 'cyan'),
      f('to', 'AMY part', 'part', ''),
      f('stage', 'Envelope', 'choice', 'amp', { choices: ['amp', 'filter', 'op1', 'op2', 'op3', 'op4', 'op5', 'op6'] }),
      f('max_time', 'Max time (ms)', 'int', 2000, { min: 100, max: 20000 }),
      f('attack', 'Attack (ms)', 'optnum', ''),
      f('decay', 'Decay (ms)', 'optnum', ''),
      f('sustain', 'Sustain (0-1)', 'optnum', ''),
      f('release', 'Release (ms)', 'optnum', ''),
    ],
    actions: ['none', 'ruby'],
  },
  // The Sequencer screen in the cell (docs/SEQUENCER.md): the song as it is
  sequencer: { label: 'Sequencer', size: [4, 2], single: true, props: [] },
};

// AMY parts (docs/AMY_SYNTH.md §6): played on their MIDI channel through the
// amy device; FM presets are DX7 128..255, Synth (Juno) 0..127
export const PART_TYPES = {
  fm:    { label: 'FM', patch: [128, 255], def: 128 },
  synth: { label: 'Synth', patch: [0, 127], def: 0 },
};

// What a pad or knob does (the block's body)
export const ACTIONS = {
  note:       { label: 'Note', fields: [f('device', 'Device', 'device', 'synth'), f('note', 'Note', 'int', 60, { min: 0, max: 127 }),
                                         f('velocity', 'Velocity', 'int', 110, { min: 1, max: 127 }), f('duration', 'Length (ms)', 'int', 200, { min: 1, max: 10000 }),
                                         f('channel', 'Channel (0-15)', 'int', 0, { min: 0, max: 15 })] },
  cc:         { label: 'Control change', fields: [f('device', 'Device', 'device', 'synth'), f('cc', 'CC number', 'int', 74, { min: 0, max: 127 }),
                                                  f('value', 'Value (pads)', 'int', 127, { min: 0, max: 127 }), f('channel', 'Channel (0-15)', 'int', 0, { min: 0, max: 15 })] },
  program:    { label: 'Program change', fields: [f('device', 'Device', 'device', 'synth'), f('program', 'Program (pads)', 'int', 0, { min: 0, max: 127 }),
                                                  f('channel', 'Channel (0-15)', 'int', 0, { min: 0, max: 15 })] },
  pitch_bend: { label: 'Pitch bend', fields: [f('device', 'Device', 'device', 'synth'), f('channel', 'Channel (0-15)', 'int', 0, { min: 0, max: 15 })] },
  ruby:       { label: 'Ruby', fields: [f('body', 'Block body (knobs: v is the value)', 'code', 'puts "hello"')] },
  none:       { label: 'Nothing', fields: [] },
};

// The Pads / Knobs screens' grid per board (main/ui/ui_common.h: UI_PAD_* /
// UI_KNOB_*; Knobs have 4 banks A-D)
export const SCREEN_GRID = {
  tab5:   { cols: 4, rows: 3 },
  cores3: { cols: 3, rows: 2 },
};
export const KNOB_BANKS = 4;
export const screenGrid = board => SCREEN_GRID[board] || SCREEN_GRID.tab5;
export const slotCount = board => screenGrid(board).cols * screenGrid(board).rows;

// Home's tiles UI.menu(hide:) can take away, in Home's order
// (main/ui/ui_manager.cpp MENU_ORDER). :home and :scripts always stay.
// :layout is every layout's tile at once.
export const MENU_SCREENS = [
  ['pads', 'Pads'], ['knobs', 'Knobs'], ['xypad', 'XYPad'], ['keys', 'Keys'], ['sequencer', 'Sequencer'],
  ['tombola', 'Tombola'], ['layout', 'Layouts (all)'], ['tempo', 'Tempo'], ['midi_devices', 'MIDI Devices'],
  ['bluetooth', 'Bluetooth'], ['logs', 'Logs'], ['settings', 'Settings'],
];

// Where a route can start: devices that receive (AMY only plays), and the
// clock (Tempo = MIDI::Clock)
export const ROUTE_CLOCK = 'clock';
export const canRouteFrom = kind => kind !== 'amy';

export function newProject(board = 'tab5') {
  return {
    v: 1,
    name: 'My Stage',
    board,
    devices: [{ id: 'synth', kind: 'sam2695' }],
    parts: [],
    sendInitial: true,      // knobs / envelopes send their starting values at start
    start: 0,
    layouts: [newLayout('Perform')],
    pads: [],
    knobs: [],
    routes: [],
    menu: { hide: [] },
  };
}

// Fill in what projects from older versions of the designer lack
export function upgrade(project) {
  project.parts ||= [];
  project.pads ||= [];
  project.knobs ||= [];
  project.routes ||= [];
  project.menu ||= { hide: [] };
  project.menu.hide ||= [];
  return project;
}

// A Pads / Knobs screen slot: a pad / knob with the widget's settings
export function newSlot(kind, index, bank = 0) {
  const w = newWidget(kind, 0, 0, 1, 1);
  const slot = { id: w.id, kind, index, props: w.props, action: w.action };
  if (kind === 'knob') slot.bank = bank;
  slot.props.label = `${WIDGETS[kind].label} ${index + 1}`;
  return slot;
}

export function newLayout(title) {
  return { title, cols: 4, rows: 3, widgets: [] };
}

let nextId = 1;
export function newWidget(kind, col, row, w, h) {
  const spec = WIDGETS[kind];
  const props = {};
  for (const p of spec.props) props[p.key] = structuredClone(p.def);
  const widget = { id: `w${Date.now().toString(36)}${(nextId++).toString(36)}`, kind, col, row, w, h, props };
  if (spec.actions) widget.action = newAction(spec.actions[0]);
  return widget;
}

export function newAction(type) {
  const a = { type };
  for (const fld of ACTIONS[type].fields) a[fld.key] = structuredClone(fld.def);
  return a;
}

// Which widget covers (col, row), or null
export function widgetAt(layout, col, row) {
  return layout.widgets.find(w => col >= w.col && col < w.col + w.w && row >= w.row && row < w.row + w.h) || null;
}

// Problems the designer can see before the VM does (ui_layout.rb checks the rest)
export function problems(project) {
  const out = [];
  project.layouts.forEach((l, li) => {
    const seen = {};
    for (const w of l.widgets) {
      if (w.col < 0 || w.row < 0 || w.col + w.w > l.cols || w.row + w.h > l.rows) {
        out.push({ layout: li, widget: w.id, message: `${WIDGETS[w.kind].label} is outside the ${l.cols}x${l.rows} grid` });
      }
      for (const o of l.widgets) {
        if (o === w || o.id < w.id) continue;
        if (w.col < o.col + o.w && o.col < w.col + w.w && w.row < o.row + o.h && o.row < w.row + w.h) {
          out.push({ layout: li, widget: w.id, message: `${WIDGETS[w.kind].label} overlaps ${WIDGETS[o.kind].label}` });
        }
      }
      if (WIDGETS[w.kind].single) {
        if (seen[w.kind]) out.push({ layout: li, widget: w.id, message: `Only one ${WIDGETS[w.kind].label} per project (the device has one)` });
        seen[w.kind] = true;
      }
    }
  });
  // Envelopes on AMY parts
  project.layouts.forEach((l, li) => l.widgets.forEach(w => {
    if (w.kind !== 'envelope' || !w.props.to) return;
    const part = (project.parts || []).find(p => p.id === w.props.to);
    if (!part) out.push({ layout: li, widget: w.id, message: `Envelope: there is no AMY part "${w.props.to}"` });
    else if (part.type === 'synth' && /^op/.test(w.props.stage)) out.push({ layout: li, widget: w.id, message: 'Envelope: a Synth part has amp / filter, not operators' });
    else if (part.type === 'fm' && !/^op/.test(w.props.stage)) out.push({ layout: li, widget: w.id, message: 'Envelope: an FM part has operator envelopes (op1-op6)' });
  }));
  if ((project.parts || []).length && project.board !== 'tab5') {
    out.push({ layout: 0, widget: null, message: 'AMY parts play on the Tab5 only (this board has no AMY)' });
  }
  // The Pads / Knobs screens
  const count = slotCount(project.board);
  for (const [key, label] of [['pads', 'Pad'], ['knobs', 'Knob']]) {
    for (const sl of project[key] || []) {
      if (sl.index >= count) {
        out.push({ screen: key, widget: sl.id, message: `${label} ${sl.index + 1} is not on this board (${count} per screen)` });
      }
    }
  }
  // Routes
  const devs = new Map(project.devices.map(d => [d.id, d]));
  for (const r of project.routes || []) {
    const from = r.from === ROUTE_CLOCK ? { kind: 'clock' } : devs.get(r.from), to = devs.get(r.to);
    if (!from || !to) out.push({ route: r, message: `Route ${r.from} -> ${r.to}: no such device` });
    else if (r.from === r.to) out.push({ route: r, message: `Route ${r.from} -> ${r.to}: the same device` });
    else if (!canRouteFrom(from.kind)) out.push({ route: r, message: `Route ${r.from} -> ${r.to}: ${DEVICE_KINDS[from.kind].label} cannot be a source` });
    else if (from.kind !== 'web_midi' && to.kind !== 'web_midi' && from.kind === to.kind && from.kind !== 'clock') {
      out.push({ route: r, message: `Route ${r.from} -> ${r.to}: both are the same transport` });
    }
  }
  // Home's menu
  const hide = new Set((project.menu || {}).hide || []);
  const start = project.start ?? 0;
  const startScreen = typeof start === 'number' ? 'layout' : String(start).split(':')[0];
  if (hide.has(startScreen)) {
    out.push({ message: `The screen shown first (${startScreen === 'layout' ? 'a layout' : startScreen}) is hidden from the menu, so it cannot be shown` });
  }
  // XYPad / Tombola / Keys: one model on the device, so one in the whole project
  const kinds = {};
  project.layouts.forEach((l, li) => l.widgets.forEach(w => {
    if (!WIDGETS[w.kind].single || w.kind === 'tempo' || w.kind === 'sequencer') return;   // one clock / one song, shown anywhere
    if (kinds[w.kind] !== undefined && kinds[w.kind] !== li) {
      out.push({ layout: li, widget: w.id, message: `${WIDGETS[w.kind].label} is already in another layout (one per project)` });
    }
    kinds[w.kind] = li;
  }));
  return out;
}
