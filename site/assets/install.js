// Install page: pick a board and a USB mode, then flash from the browser with
// ESP Web Tools. Both languages share this file; the page supplies the text.
//
// <body data-root="../">     path from the page to the site root
// [data-board-only="a b"]    shown only for those boards ("none": nothing picked)
// [data-mode-only="host"]    shown only for that mode
// [data-text-*] on #install  strings this script fills in

(function () {
  "use strict";

  var REPO = "https://github.com/kirikak2/midori";
  var root = document.body.getAttribute("data-root") || "./";
  var box = document.getElementById("install");
  var btn = document.getElementById("install-button");
  var meta = document.getElementById("install-meta");
  var state = { board: null, mode: null, index: null };

  function t(name) { return box.getAttribute("data-text-" + name) || ""; }

  function load(key) {
    try { return window.localStorage.getItem("midori-install-" + key); } catch (e) { return null; }
  }
  function save(key, value) {
    try { window.localStorage.setItem("midori-install-" + key, value); } catch (e) { /* private mode */ }
  }

  function radios(name) {
    return Array.prototype.slice.call(document.querySelectorAll('input[name="' + name + '"]'));
  }

  function images() { return (state.index && state.index.images) || []; }

  function image(board, mode) {
    return images().filter(function (i) { return i.board === board && i.mode === mode; })[0];
  }

  function toggle(attr, value) {
    document.querySelectorAll("[" + attr + "]").forEach(function (el) {
      var list = el.getAttribute(attr).split(/\s+/);
      // "none": shown until something is picked.
      el.classList.toggle("show", value === null ? list.indexOf("none") >= 0 : list.indexOf(value) >= 0);
    });
  }

  function mb(bytes) { return (bytes / 1048576).toFixed(1) + " MB"; }

  function update() {
    // Modes the selected board has an image for.
    radios("mode").forEach(function (r) {
      var ok = !state.index || !state.board || !!image(state.board, r.value);
      r.disabled = !ok;
      if (!ok && r.checked) { r.checked = false; state.mode = null; }
    });
    if (state.board && !state.mode) {
      var def = document.querySelector('input[name="board"][value="' + state.board + '"]').getAttribute("data-default-mode");
      var r = document.querySelector('input[name="mode"][value="' + def + '"]');
      if (r && !r.disabled) { r.checked = true; state.mode = def; }
    }

    toggle("data-board-only", state.board);
    toggle("data-mode-only", state.mode);

    var img = state.board && state.mode && image(state.board, state.mode);
    if (img) {
      btn.setAttribute("manifest", root + "firmware/" + img.manifest);
      btn.hidden = false;
      meta.innerHTML = "";
      meta.appendChild(document.createTextNode("MIDoRI " + state.index.version + " · " + img.chip + " · " + mb(img.size) + " · "));
      var a = document.createElement("a");
      a.href = root + "firmware/" + img.file;
      a.textContent = t("download");
      a.setAttribute("download", "");
      meta.appendChild(a);
    } else {
      btn.hidden = true;
      meta.textContent = state.index && images().length ? t("choose") : "";
    }
  }

  function bind(name) {
    radios(name).forEach(function (r) {
      r.addEventListener("change", function () {
        state[name] = r.value;
        save(name, r.value);
        update();
      });
    });
    var saved = load(name);
    var r = saved && document.querySelector('input[name="' + name + '"][value="' + saved + '"]');
    if (r) { r.checked = true; state[name] = saved; }
  }

  bind("board");
  bind("mode");

  if (!("serial" in navigator)) {
    document.getElementById("browser-unsupported").hidden = false;
  }

  fetch(root + "firmware/index.json", { cache: "no-cache" })
    .then(function (res) { if (!res.ok) throw new Error(res.status); return res.json(); })
    .then(function (index) {
      state.index = index;
      if (!index.version || !index.images.length) {
        document.getElementById("no-release").hidden = false;
      } else {
        var sd = REPO + "/releases/download/" + index.version + "/midori-" + index.version + "-sd-card.zip";
        document.querySelectorAll("[data-sd-link]").forEach(function (a) { a.href = sd; });
        document.querySelectorAll("[data-version]").forEach(function (el) { el.textContent = index.version; });
      }
      update();
    })
    .catch(function () {
      document.getElementById("no-release").hidden = false;
      update();
    });

  update();
})();
