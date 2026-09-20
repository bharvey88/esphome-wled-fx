/* wled_fx effect gallery.
 *
 * Everything on the page comes out of effects.json, which the simulator writes.
 * A card shows the still frame until it scrolls into view, then swaps to the
 * animated file and swaps back when it leaves, so a few dozen previews animate
 * at a time rather than all 223. */

const PREVIEWS = "previews/";
const MAPPINGS = ["Pixels", "Bar", "Arc", "Corner", "Pinwheel"];

const state = {
  effects: [],
  query: "",
  groups: new Set(),
  flags: new Set(),
  animate: true,
  open: null,
  opener: null,
};

const el = {
  grid: document.getElementById("grid"),
  empty: document.getElementById("empty"),
  count: document.getElementById("count"),
  search: document.getElementById("q"),
  chipsGroup: document.getElementById("chips-group"),
  chipsFlags: document.getElementById("chips-flags"),
  motion: document.getElementById("motion-toggle"),
  sheet: document.getElementById("sheet"),
  sheetPanel: document.getElementById("sheet-panel"),
  sheetTitle: document.getElementById("sheet-title"),
  sheetBody: document.getElementById("sheet-body"),
  sheetClose: document.getElementById("sheet-close"),
  sheetScrim: document.getElementById("sheet-scrim"),
  theme: document.getElementById("theme-toggle"),
  themeLabel: document.getElementById("theme-label"),
  template: document.getElementById("card-template"),
};

const reduceMotion = window.matchMedia("(prefers-reduced-motion: reduce)");

/* Plays and pauses the previews that are on screen. An animated WebP cannot be
 * paused once it is in the document, so playing is swapping the source. */
const inView = new Set();
const observer = new IntersectionObserver((entries) => {
  for (const entry of entries) {
    if (entry.isIntersecting) inView.add(entry.target);
    else inView.delete(entry.target);
    paint(entry.target);
  }
}, { rootMargin: "400px 0px" });

/* Nothing is fetched until a card is near the viewport, which keeps the first
 * paint to the few dozen previews somebody can actually see. A card that has
 * scrolled away keeps its still, so coming back to it is instant. */
function paint(card) {
  const img = card.querySelector("img");
  const visible = inView.has(card);
  if (!visible && !img.getAttribute("src")) return;
  const wanted = state.animate && visible ? card.dataset.anim : card.dataset.still;
  if (img.getAttribute("src") !== wanted) img.setAttribute("src", wanted);
}

function flagsOf(effect) {
  const out = [];
  if (effect.dims.includes("1D")) out.push("1D");
  if (effect.dims.includes("2D")) out.push("2D");
  if (effect.audio) out.push("Audio");
  if (effect.particle) out.push("Particle");
  return out;
}

function matches(effect) {
  if (state.query) {
    const hay = (effect.name + " " + effect.family + " " + effect.group).toLowerCase();
    if (!hay.includes(state.query)) return false;
  }
  if (state.groups.size && !state.groups.has(effect.group)) return false;
  if (state.flags.size && !flagsOf(effect).some((f) => state.flags.has(f))) return false;
  return true;
}

function buildCard(effect) {
  const card = el.template.content.firstElementChild.cloneNode(true);
  const preview = card.querySelector(".preview");
  const img = card.querySelector("img");
  const strip = effect.preview.h === 1;

  card.dataset.name = effect.name;
  card.dataset.anim = PREVIEWS + effect.preview.stem + ".webp";
  card.dataset.still = PREVIEWS + effect.preview.stem + ".still.webp";
  card.setAttribute("aria-label", effect.name + ", " + flagsOf(effect).join(", "));

  preview.classList.toggle("is-1d", strip);
  preview.style.setProperty("--cols", effect.preview.w);
  preview.style.setProperty("--rows", effect.preview.h);
  img.width = effect.preview.w;
  img.height = effect.preview.h;
  img.style.aspectRatio = effect.preview.w + " / " + effect.preview.h;

  card.querySelector(".card-name").textContent = effect.name;
  const tags = card.querySelector(".card-tags");
  for (const flag of flagsOf(effect)) {
    const tag = document.createElement("span");
    tag.className = "tag" + (flag === "2D" ? " dim-2d" : flag === "Audio" ? " audio" : "");
    tag.textContent = flag;
    tags.append(tag);
  }
  card.addEventListener("click", () => openSheet(effect, card));
  return card;
}

function render() {
  const visible = state.effects.filter(matches);
  observer.disconnect();
  inView.clear();
  el.grid.replaceChildren(...visible.map((e) => e.card));
  for (const effect of visible) observer.observe(effect.card);
  el.empty.hidden = visible.length > 0;
  el.count.textContent =
    visible.length === state.effects.length
      ? state.effects.length + " effects"
      : visible.length + " of " + state.effects.length + " effects";
}

function chip(label, count, pressed, onToggle) {
  const button = document.createElement("button");
  button.type = "button";
  button.className = "chip";
  button.setAttribute("aria-pressed", String(pressed));
  button.append(document.createTextNode(label));
  if (count !== null) {
    const n = document.createElement("span");
    n.className = "n";
    n.textContent = count;
    button.append(n);
  }
  button.addEventListener("click", () => {
    const next = button.getAttribute("aria-pressed") !== "true";
    button.setAttribute("aria-pressed", String(next));
    onToggle(next);
  });
  return button;
}

function buildChips() {
  for (const flag of ["1D", "2D", "Audio", "Particle"]) {
    const count = state.effects.filter((e) => flagsOf(e).includes(flag)).length;
    el.chipsFlags.append(chip(flag, count, false, (on) => {
      on ? state.flags.add(flag) : state.flags.delete(flag);
      render();
    }));
  }
  // The registry groups, which are the translation units the effects live in and
  // the batches BATCHES.md hands out, so they are worth filtering by even though
  // they are not how somebody picks a light show.
  const groups = [];
  for (const effect of state.effects) {
    const found = groups.find((g) => g.id === effect.group);
    if (found) found.count++;
    else groups.push({ id: effect.group, count: 1, family: effect.family });
  }
  for (const group of groups) {
    const button = chip(group.id, group.count, false, (on) => {
      on ? state.groups.add(group.id) : state.groups.delete(group.id);
      render();
    });
    button.title = group.family;
    el.chipsGroup.append(button);
  }
}

/* The detail panel */

function row(label, value, dim) {
  const tr = document.createElement("tr");
  const th = document.createElement("td");
  th.textContent = label;
  const td = document.createElement("td");
  if (dim) td.className = "off";
  td.textContent = value;
  tr.append(th, td);
  return tr;
}

function specTable(title, rows) {
  const section = document.createElement("section");
  section.className = "spec";
  const h = document.createElement("h3");
  h.textContent = title;
  const table = document.createElement("table");
  table.append(...rows);
  section.append(h, table);
  return section;
}

function quoted(value) {
  return '"' + String(value).replace(/"/g, '\\"') + '"';
}

function controlLines(effect, indent) {
  const lines = [];
  if (effect.palette) lines.push(indent + "palette: " + quoted(effect.paletteName));
  for (const slider of effect.sliders) {
    lines.push(indent + slider.key + ": " + slider.default + "  # " + slider.label);
  }
  for (const check of effect.checks) {
    lines.push(indent + check.key + ": " + (check.default ? "true" : "false") + "  # " + check.label);
  }
  if (effect.name === "Scrolling Text") lines.push(indent + 'text: "WLED FX"');
  return lines;
}

function lightYaml(effect) {
  const lines = [
    "wled_fx:",
    "",
    "light:",
    "  - platform: esp32_rmt_led_strip",
    "    id: strip",
    "    name: Strip",
    "    # your own pin, chipset, rgb_order and num_leds",
    "    effects:",
    "      - wled_fx:",
    "          name: " + quoted(effect.name),
    "          effect: " + quoted(effect.name),
  ];
  if (!effect.dims.includes("1D")) {
    lines.push(
      "          # 2D only: wire the strip as a matrix",
      "          width: 16",
      "          height: 16",
      "          serpentine: true"
    );
  }
  lines.push(...controlLines(effect, "          "));
  if (effect.audio) {
    lines.push(
      "",
      "# Audio reactive. With no audio: block it runs on WLED's simulated sound.",
      "# Point one wled_fx entry at a microphone to run it on real sound."
    );
  }
  return lines.join("\n");
}

function displayYaml(effect) {
  const lines = [
    "display:",
    "  - platform: hub75",
    "    id: matrix",
    "    # your own board and panel size",
    "    panel_width: 64",
    "    panel_height: 32",
    "    update_interval: never",
    "    auto_clear_enabled: false",
    "",
    "wled_fx:",
    "  id: fx",
    "  display_id: matrix",
    "  update_interval: 23ms",
    "  effect: " + quoted(effect.name),
  ];
  lines.push(...controlLines(effect, "  "));
  if (effect.audio) {
    lines.push(
      "  audio:",
      "    microphone: board_mic  # leave the block out for simulated sound"
    );
  }
  return lines.join("\n");
}

function snippet(title, yaml) {
  const section = document.createElement("section");
  section.className = "snippet";
  const head = document.createElement("div");
  head.className = "snippet-head";
  const h = document.createElement("h3");
  h.textContent = title;
  const copy = document.createElement("button");
  copy.type = "button";
  copy.className = "copy";
  copy.textContent = "Copy";
  copy.addEventListener("click", async () => {
    try {
      await navigator.clipboard.writeText(yaml);
      copy.textContent = "Copied";
      copy.dataset.done = "yes";
    } catch {
      copy.textContent = "Press Ctrl and C";
      window.getSelection().selectAllChildren(pre);
    }
    setTimeout(() => {
      copy.textContent = "Copy";
      delete copy.dataset.done;
    }, 2200);
  });
  head.append(h, copy);
  const pre = document.createElement("pre");
  pre.textContent = yaml;
  section.append(head, pre);
  return section;
}

function openSheet(effect, card) {
  state.open = effect;
  state.opener = card || null;
  el.sheetTitle.textContent = effect.name;
  el.sheetBody.replaceChildren();

  const strip = effect.preview.h === 1;
  const figure = document.createElement("div");
  figure.className = "sheet-preview" + (strip ? " is-1d" : "");
  figure.style.setProperty("--cols", effect.preview.w);
  figure.style.setProperty("--rows", effect.preview.h);
  const img = document.createElement("img");
  img.alt = "Simulator render of the " + effect.name + " effect";
  img.src = state.animate
    ? PREVIEWS + effect.preview.stem + ".webp"
    : PREVIEWS + effect.preview.stem + ".still.webp";
  const leds = document.createElement("span");
  leds.className = "leds";
  figure.append(img, leds);

  const note = document.createElement("p");
  note.className = "sheet-note";
  const swapped = effect.preview.paletteName;
  note.textContent =
    "Simulator render on a " + effect.preview.w + " by " + effect.preview.h +
    (strip ? " pixel strip" : " pixel panel") + ", five seconds at 20 frames per second" +
    (swapped ? ", on the " + swapped + " palette because the default leaves this one flat" : "") + ".";

  el.sheetBody.append(figure, note);

  if (!state.animate) {
    const play = document.createElement("button");
    play.type = "button";
    play.className = "copy";
    play.style.marginTop = "10px";
    play.textContent = "Play this preview";
    play.addEventListener("click", () => {
      img.src = PREVIEWS + effect.preview.stem + ".webp";
      play.remove();
    });
    el.sheetBody.append(play);
  }

  const about = [
    row("Group", effect.family + " (" + effect.group + ")"),
    row("Runs on", effect.dims.join(" and ")),
    row("Audio", effect.audio ? effect.audio + " reactive" : "no", !effect.audio),
    row("Particle system", effect.particle ? "yes" : "no", !effect.particle),
    row("Default palette", effect.paletteName + " (" + effect.palette + ")"),
  ];
  if (swapped) about.push(row("Palette in this preview", swapped));
  if (effect.dims.includes("1D") && effect.dims.includes("2D")) {
    about.push(row("1D on a matrix", MAPPINGS[effect.m12] || String(effect.m12)));
  }
  if (effect.colors.length) about.push(row("Colours used", effect.colors.join(", ")));
  el.sheetBody.append(specTable("The effect", about));

  const controls = effect.sliders.map((s) => row(s.label, String(s.default)))
    .concat(effect.checks.map((c) => row(c.label, c.default ? "on" : "off", !c.default)));
  el.sheetBody.append(specTable(
    controls.length ? "Controls and their defaults" : "Controls",
    controls.length ? controls : [row("This effect takes no sliders", "")]
  ));

  el.sheetBody.append(snippet("Light front end", lightYaml(effect)));
  el.sheetBody.append(snippet("Display front end", displayYaml(effect)));

  el.sheet.hidden = false;
  document.body.style.overflow = "hidden";
  for (const other of el.grid.children) other.removeAttribute("aria-current");
  if (card) card.setAttribute("aria-current", "true");
  el.sheetClose.focus();
  history.replaceState(null, "", "#" + encodeURIComponent(effect.name));
}

function closeSheet() {
  if (el.sheet.hidden) return;
  el.sheet.hidden = true;
  document.body.style.overflow = "";
  if (state.opener) {
    state.opener.removeAttribute("aria-current");
    state.opener.focus();
  }
  state.open = null;
  state.opener = null;
  history.replaceState(null, "", location.pathname + location.search);
}

function trapTab(event) {
  if (event.key !== "Tab" || el.sheet.hidden) return;
  const focusable = el.sheetPanel.querySelectorAll("button, [href], input, select, textarea, [tabindex]:not([tabindex='-1'])");
  if (!focusable.length) return;
  const first = focusable[0];
  const last = focusable[focusable.length - 1];
  if (event.shiftKey && document.activeElement === first) {
    last.focus();
    event.preventDefault();
  } else if (!event.shiftKey && document.activeElement === last) {
    first.focus();
    event.preventDefault();
  }
}

/* Theme */

function applyTheme(theme) {
  document.documentElement.dataset.theme = theme;
  el.theme.setAttribute("aria-pressed", String(theme === "light"));
  el.themeLabel.textContent = theme === "light" ? "Dark theme" : "Light theme";
  try {
    localStorage.setItem("wled-fx-gallery-theme", theme);
  } catch {
    /* a private window is allowed to refuse, and the page still works */
  }
}

function setAnimate(on) {
  state.animate = on;
  el.motion.setAttribute("aria-pressed", String(on));
  try {
    localStorage.setItem("wled-fx-gallery-animate", on ? "yes" : "no");
  } catch {
    /* see above */
  }
  for (const card of el.grid.children) paint(card);
}

/* Start up */

async function main() {
  let stored = null;
  try {
    stored = localStorage.getItem("wled-fx-gallery-theme");
  } catch {
    /* see above */
  }
  applyTheme(stored === "light" ? "light" : "dark");
  el.theme.addEventListener("click", () => {
    applyTheme(document.documentElement.dataset.theme === "light" ? "dark" : "light");
  });

  let animate = !reduceMotion.matches;
  try {
    const saved = localStorage.getItem("wled-fx-gallery-animate");
    if (saved) animate = saved === "yes";
  } catch {
    /* see above */
  }
  el.motion.addEventListener("click", () => setAnimate(el.motion.getAttribute("aria-pressed") !== "true"));

  const response = await fetch("effects.json");
  const data = await response.json();
  state.effects = data.effects;
  for (const effect of state.effects) effect.card = buildCard(effect);

  el.search.placeholder = "Search " + state.effects.length + " effects by name";
  buildChips();
  // Open on anything wider than a phone, where the summary is hidden anyway.
  const wide = window.matchMedia("(min-width: 721px)");
  const foldFilters = () => { document.getElementById("filters").open = wide.matches; };
  foldFilters();
  wide.addEventListener("change", foldFilters);
  setAnimate(animate);
  render();

  el.search.addEventListener("input", () => {
    state.query = el.search.value.trim().toLowerCase();
    render();
  });
  el.sheetClose.addEventListener("click", closeSheet);
  el.sheetScrim.addEventListener("click", closeSheet);
  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape") closeSheet();
    trapTab(event);
    if (event.key === "/" && document.activeElement !== el.search && el.sheet.hidden) {
      event.preventDefault();
      el.search.focus();
    }
  });

  // A link to one effect works on load and when the address bar changes, which
  // is also what makes the browser's back button close the panel.
  const openFromHash = () => {
    const deep = decodeURIComponent(location.hash.slice(1));
    if (!deep) {
      closeSheet();
      return;
    }
    const effect = state.effects.find((e) => e.name.toLowerCase() === deep.toLowerCase());
    if (effect) openSheet(effect, effect.card);
  };
  window.addEventListener("hashchange", openFromHash);
  openFromHash();
}

main();
