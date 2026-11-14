// The simulator's and the designer's two languages (Japanese, English).
//
// The language: ?lang=ja|en, else the one chosen before (localStorage), else
// the browser's (ja for Japanese, en otherwise). The site's Japanese pages
// link with ?lang=ja.
//
//   t('日本語', 'English')                    in the scripts
//   <b data-en="English">日本語</b>           in the pages (title / placeholder /
//   data-en-title, data-en-placeholder)     aria-label: data-en-aria-label)

const STORE = 'midori-sim/lang';

function pick() {
  // Outside a page (the Node tests) there is no location / navigator: English
  if (typeof location === 'undefined') return 'en';
  const q = new URLSearchParams(location.search).get('lang');
  if (q === 'ja' || q === 'en') {
    try { localStorage.setItem(STORE, q); } catch (e) {}
    return q;
  }
  try {
    const s = localStorage.getItem(STORE);
    if (s === 'ja' || s === 'en') return s;
  } catch (e) {}
  return ((typeof navigator !== 'undefined' && navigator.language) || '').toLowerCase().startsWith('ja') ? 'ja' : 'en';
}

export const LANG = pick();
export const t = (ja, en) => (LANG === 'ja' ? ja : en);

// The page's static text in LANG, and a link to the other language
export function localizePage(root = document) {
  document.documentElement.lang = LANG;
  if (LANG === 'en') {
    for (const el of root.querySelectorAll('[data-en]')) el.textContent = el.dataset.en;
    for (const [attr, data] of [['title', 'enTitle'], ['placeholder', 'enPlaceholder'], ['aria-label', 'enAriaLabel']]) {
      for (const el of root.querySelectorAll(`[data-en-${attr}]`)) el.setAttribute(attr, el.dataset[data]);
    }
  }
  for (const a of root.querySelectorAll('a[data-lang-switch]')) {
    const other = LANG === 'ja' ? 'en' : 'ja';
    const u = new URL(location.href);
    u.searchParams.set('lang', other);
    a.href = u.pathname + u.search + u.hash;
    a.textContent = other === 'ja' ? '日本語' : 'English';
    a.hreflang = other;
  }
}
