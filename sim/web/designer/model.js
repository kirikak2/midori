// The designer's project model (docs/WEB_SIMULATOR.md §6.1) and what each
// widget takes. The model is plain JSON: it is what codegen.js turns into the
// project script and what the script carries back in its first line.
//
//   project = {
//     v: 1, name, board,
//     devices: [{ id, kind, port? }],          // kind: sam2695 | amy | usb_midi_host | usb_midi_device | web_midi
//     start: 0,                                // the layout shown first
//     layouts: [{ title, cols, rows, widgets: [widget] }],
//   }
//   widget = { id, kind, col, row, w, h, props: {}, action: { type, ... } }
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

// One field of a widget's settings: type is int | float | text | color |
// choice | bool | notes (a list of note numbers) | device
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

export function newProject(board = 'tab5') {
  return {
    v: 1,
    name: 'My Stage',
    board,
    devices: [{ id: 'synth', kind: board === 'tab5' ? 'sam2695' : 'sam2695' }],
    start: 0,
    layouts: [newLayout('Perform')],
  };
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
  // XYPad / Tombola / Keys: one model on the device, so one in the whole project
  const kinds = {};
  project.layouts.forEach((l, li) => l.widgets.forEach(w => {
    if (!WIDGETS[w.kind].single || w.kind === 'tempo') return;
    if (kinds[w.kind] !== undefined && kinds[w.kind] !== li) {
      out.push({ layout: li, widget: w.id, message: `${WIDGETS[w.kind].label} is already in another layout (one per project)` });
    }
    kinds[w.kind] = li;
  }));
  return out;
}
