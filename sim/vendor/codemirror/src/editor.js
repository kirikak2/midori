// The simulator's Ruby editor: CodeMirror 6 with Ruby highlighting, bundled
// into sim/web/vendor/codemirror.js (npm run build). Colours come from the
// page's CSS variables (--cm-*, --fg, --code-bg ...), so it follows the
// page's light / dark theme.
//
//   const ed = createEditor(parent, { doc, label, onChange, onRun });
//   ed.value            // the text
//   ed.value = text     // replace it (one undo step)
//   ed.focus()

import { EditorState } from '@codemirror/state';
import {
  EditorView, keymap, lineNumbers, highlightActiveLine, highlightActiveLineGutter,
  highlightSpecialChars, drawSelection, dropCursor, rectangularSelection, crosshairCursor,
} from '@codemirror/view';
import { defaultKeymap, history, historyKeymap, indentWithTab } from '@codemirror/commands';
import {
  StreamLanguage, HighlightStyle, syntaxHighlighting, indentOnInput, bracketMatching,
  foldGutter, foldKeymap, indentUnit,
} from '@codemirror/language';
import { closeBrackets, closeBracketsKeymap } from '@codemirror/autocomplete';
import { search, searchKeymap, highlightSelectionMatches } from '@codemirror/search';
import { ruby } from '@codemirror/legacy-modes/mode/ruby';
import { tags as t } from '@lezer/highlight';

const v = name => `var(--cm-${name})`;

const highlight = HighlightStyle.define([
  { tag: [t.keyword, t.controlKeyword, t.definitionKeyword, t.moduleKeyword], color: v('keyword') },
  { tag: [t.string, t.special(t.string), t.regexp], color: v('string') },
  { tag: [t.number, t.bool, t.null], color: v('number') },
  { tag: [t.atom], color: v('symbol') },   // :symbols
  { tag: [t.comment, t.lineComment, t.blockComment, t.docComment], color: v('comment'), fontStyle: 'italic' },
  { tag: [t.variableName, t.propertyName], color: v('name') },
  { tag: [t.definition(t.variableName), t.function(t.variableName)], color: v('def') },
  { tag: [t.typeName, t.className, t.namespace], color: v('type') },
  { tag: [t.special(t.variableName), t.self], color: v('ivar') },   // @ivars, $globals, self
  { tag: [t.operator, t.punctuation, t.bracket], color: v('punct') },
  { tag: [t.meta], color: v('comment') },
]);

const theme = EditorView.theme({
  '&': { color: 'var(--fg)', backgroundColor: 'var(--code-bg)', height: '100%' },
  '.cm-scroller': { fontFamily: 'ui-monospace, SFMono-Regular, Menlo, monospace', fontSize: '13px', lineHeight: '1.45' },
  '.cm-content': { caretColor: 'var(--fg)', padding: '8px 0' },
  '.cm-cursor, .cm-dropCursor': { borderLeftColor: 'var(--fg)' },
  '&.cm-focused': { outline: 'none' },
  '.cm-gutters': { backgroundColor: 'var(--code-bg)', color: 'var(--muted)', border: 'none', borderRight: '1px solid var(--line)' },
  '.cm-activeLine': { backgroundColor: v('active') },
  '.cm-activeLineGutter': { backgroundColor: v('active'), color: 'var(--fg)' },
  '&.cm-focused .cm-selectionBackground, .cm-selectionBackground, .cm-content ::selection': { backgroundColor: v('selection') },
  '.cm-selectionMatch': { backgroundColor: v('match') },
  '&.cm-focused .cm-matchingBracket': { backgroundColor: v('match'), outline: '1px solid var(--muted)' },
  '.cm-searchMatch': { backgroundColor: v('match'), outline: '1px solid var(--muted)' },
  '.cm-panels': { backgroundColor: 'var(--panel)', color: 'var(--fg)' },
  '.cm-panels.cm-panels-bottom': { borderTop: '1px solid var(--line)' },
  '.cm-textfield': { backgroundColor: 'var(--code-bg)', color: 'var(--fg)', border: '1px solid var(--line)', borderRadius: '4px' },
  '.cm-button': { backgroundImage: 'none', backgroundColor: 'var(--panel)', color: 'var(--fg)', border: '1px solid var(--line)', borderRadius: '4px' },
  '.cm-foldPlaceholder': { backgroundColor: 'var(--panel)', color: 'var(--muted)', border: '1px solid var(--line)' },
});

export function createEditor(parent, { doc = '', label = 'Ruby script', onChange = () => {}, onRun = () => {} } = {}) {
  const view = new EditorView({
    parent,
    state: EditorState.create({
      doc,
      extensions: [
        lineNumbers(), highlightActiveLineGutter(), highlightSpecialChars(), history(), foldGutter(),
        drawSelection(), dropCursor(), EditorState.allowMultipleSelections.of(true), indentOnInput(),
        syntaxHighlighting(highlight), bracketMatching(), closeBrackets(), rectangularSelection(),
        crosshairCursor(), highlightActiveLine(), highlightSelectionMatches(), search({ top: false }),
        StreamLanguage.define(ruby), indentUnit.of('  '), EditorState.tabSize.of(2),
        keymap.of([
          // Ctrl/Cmd+Enter runs the script (before the default Enter bindings)
          { key: 'Mod-Enter', run: () => { onRun(); return true; }, preventDefault: true },
          ...closeBracketsKeymap, ...defaultKeymap, ...searchKeymap, ...historyKeymap, ...foldKeymap,
          indentWithTab,
        ]),
        EditorView.updateListener.of(u => { if (u.docChanged) onChange(); }),
        EditorView.contentAttributes.of({ 'aria-label': label, spellcheck: 'false', autocapitalize: 'off', autocorrect: 'off' }),
        theme,
      ],
    }),
  });
  return {
    view,
    get value() { return view.state.doc.toString(); },
    set value(text) {
      view.dispatch({ changes: { from: 0, to: view.state.doc.length, insert: String(text ?? '') }, selection: { anchor: 0 } });
      view.scrollDOM.scrollTop = 0;
    },
    focus() { view.focus(); },
  };
}
