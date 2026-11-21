// Dashboard page. Talks to dashboard/server.py: three JSON endpoints for static things, one
// websocket for the running session (engine state in, start / stop / speed out).
"use strict";

const $ = (sel, root = document) => root.querySelector(sel);
const el = (tag, props = {}, ...kids) => {
  const e = Object.assign(document.createElement(tag), props);
  for (const k of kids) e.append(k);
  return e;
};
const css = (name) => getComputedStyle(document.documentElement).getPropertyValue(name).trim();
const SERIES = ["--s1", "--s2", "--s3", "--s4", "--s5", "--s6", "--s7", "--s8"];
// Colour follows the line's place in its chart, never its rank; beyond 8 a line is "other".
const colour = (i) => css(i < SERIES.length ? SERIES[i] : "--other");
const HEDGER_ORDER = ["none", "fixed_band", "time_based", "whalley_wilmott"];
const MONTHS = "JANFEBMARAPRMAYJUNJULAUGSEPOCTNOVDEC";

const btc = (v) => (v == null ? "" : (v >= 0 ? "+" : "−") + Math.abs(v).toFixed(6));
const num = (v, d = 4) => (v == null ? "" : Number(v).toFixed(d));
const px = (v) => (v == null ? "–" : v >= 100 ? Number(v).toFixed(1) : Number(v).toFixed(4));
const utc = (ns) => new Date(ns / 1e6).toISOString().replace("T", " ").slice(0, 19);
const signClass = (v) => (v > 0 ? "pos" : v < 0 ? "neg" : "");
const ago = (s) => (s < 90 ? `${Math.round(s)} s` : s < 5400 ? `${Math.round(s / 60)} min` : `${(s / 3600).toFixed(1)} h`);

// BTC-12OCT26-83000-C -> expiry, strike, call / put. null for anything else (the perpetual).
function parseInstr(name) {
  const m = /^[A-Z]+-(\d{1,2})([A-Z]{3})(\d{2})-(\d+)-([CP])$/.exec(name);
  if (!m) return null;
  return { expiry: m[1] + m[2] + m[3], when: Date.UTC(2000 + +m[3], MONTHS.indexOf(m[2]) / 3, +m[1], 8), strike: +m[4], cp: m[5] };
}

let meta = null;
let ws = null;
let session = { status: "none" };
let sessionSeen = false;    // the first session message after the page loads
let accounts = [];          // the Strategies tab's rows
let names = [];             // accounts of the running session
let xs = [], ys = [], totals = [];   // time, PnL per account, PnL summed over accounts
let idxV = [];              // BTC index over the session
let charts = [];            // one, or one per strategy when there are more than 8 accounts
let groups = [];            // [{title, idx: [account indices]}], one per chart
let last = null;            // latest state
let prevBooks = new Map();  // top of book at the previous state, to see how much moved
let moved = 0;
let selected = 0;
let fills = [];
let fillsReceived = 0, fillsShown = -1;
let seenFills = [];         // fill count per account at the last render, to flash a card
let smile = null, smileKey = "";
let frame = 0;
let dirty = false;

// ---------------------------------------------------------------- tabs and theme

function show(tab) {
  for (const b of document.querySelectorAll("#tabs button")) b.classList.toggle("on", b.dataset.tab === tab);
  for (const s of document.querySelectorAll("section")) s.hidden = s.id !== "tab-" + tab;
  if (tab === "live" || tab === "replay") placeView(tab);
  if (tab === "results") loadResults();
  redraw();
}
$("#tabs").addEventListener("click", (e) => e.target.dataset.tab && show(e.target.dataset.tab));

function placeView(tab) {
  const slot = $(`#tab-${tab} .view-slot`);
  let view = $("#view");
  if (!view) view = $("#view-template").content.firstElementChild.cloneNode(true);
  // The view belongs under the tab of the session's mode; the other tab only has its controls.
  const mode = session.mode || tab;
  if (mode === tab && view.parentElement !== slot) { slot.append(view); buildCards(); buildChart(); }
  view.hidden = !names.length;
}

function error(msg) {
  const e = $("#error");
  e.hidden = !msg;
  e.textContent = msg || "";
}

// Colours are read from the style sheet when a chart is built, so a theme change rebuilds them.
function redraw() {
  buildChart();
  buildCards();
  smileKey = "";
  curvesDrawn = false;
  if (resultsData && !$("#tab-results").hidden) loadResults();
  if (last) render();
}
$("#theme").addEventListener("click", () => {
  const dark = document.documentElement.dataset.theme
    ? document.documentElement.dataset.theme === "dark"
    : matchMedia("(prefers-color-scheme: dark)").matches;
  document.documentElement.dataset.theme = dark ? "light" : "dark";
  try { localStorage.setItem("theme", document.documentElement.dataset.theme); } catch { /* private window */ }
  redraw();
});
matchMedia("(prefers-color-scheme: dark)").addEventListener("change", redraw);
window.addEventListener("resize", () => { buildChart(); smileKey = ""; if (last) render(); });

// ---------------------------------------------------------------- strategies tab

function defaultAccounts() {
  const ww = (meta.presets.whalley_wilmott) || meta.hedgers.whalley_wilmott;
  return Object.keys(meta.strategies).map((s) => account(s, "whalley_wilmott", ww));
}

function account(strategy, hedger, hedgerParams, preset) {
  const p = preset || (meta.presets.strategies || {})[strategy];
  const pick = p && (p.best_pnl_min_fills || p.best_pnl);
  return {
    strategy,
    params: { ...meta.strategies[strategy], ...(pick ? pick.params : {}) },
    quoter: { ...meta.quoter_knobs, ...(pick ? pick.quoter : {}) },
    hedger,
    hedger_params: { ...meta.hedgers[hedger], ...(hedgerParams || {}) },
  };
}

function tunedSet() {
  const ww = meta.presets.whalley_wilmott || {};
  const out = [];
  for (const s of Object.keys(meta.strategies))
    for (const h of HEDGER_ORDER) out.push(account(s, h, h === "whalley_wilmott" ? ww : null));
  return out;
}

function fields(obj, onchange) {
  const box = el("div");
  for (const key of Object.keys(obj)) {
    const input = el("input", { type: "number", step: "any", value: obj[key] });
    input.addEventListener("change", () => { obj[key] = Number(input.value); onchange(); });
    box.append(el("label", {}, key + " ", input));
  }
  if (!Object.keys(obj).length) box.append(el("span", { className: "hint", textContent: "none" }));
  return box;
}

function select(options, value, onchange) {
  const s = el("select");
  for (const o of options) s.append(el("option", { value: o, textContent: o, selected: o === value }));
  s.addEventListener("change", () => onchange(s.value));
  return s;
}

function renderEditor() {
  const body = $("#accounts-editor tbody");
  body.replaceChildren();
  accounts.forEach((a, i) => {
    const remove = el("button", { textContent: "Remove" });
    remove.addEventListener("click", () => { accounts.splice(i, 1); renderEditor(); });
    body.append(el("tr", {},
      el("td", { textContent: String(i + 1) }),
      el("td", {}, select(Object.keys(meta.strategies), a.strategy, (v) => { accounts[i] = account(v, a.hedger, a.hedger_params); renderEditor(); })),
      el("td", {}, fields(a.params, save)),
      el("td", {}, fields(a.quoter, save)),
      el("td", {}, select(Object.keys(meta.hedgers), a.hedger, (v) => { a.hedger = v; a.hedger_params = { ...meta.hedgers[v] }; renderEditor(); })),
      el("td", {}, fields(a.hedger_params, save)),
      el("td", {}, remove)));
  });
  $("#account-count").textContent = `${accounts.length} of ${meta.max_accounts} accounts`;
  $("#add-account").disabled = accounts.length >= meta.max_accounts;
  save();
}

function save() {
  try { localStorage.setItem("accounts", JSON.stringify(accounts)); } catch { /* private window */ }
}

$("#add-account").addEventListener("click", () => { accounts.push(account("fixed_spread", "none")); renderEditor(); });
$("#load-presets").addEventListener("click", () => { accounts = tunedSet(); renderEditor(); });

// ---------------------------------------------------------------- session control

function start(mode) {
  error("");
  const latency = $("#latency").value;
  ws.send(JSON.stringify({
    cmd: "start", mode, accounts,
    fill_model: $("#fill-model").value,
    latency_ms: latency === "" ? null : Number(latency),
    day: $("#replay-day").value,
    speed: Number($("#replay-speed").value),
  }));
}
// A session can be started from where its accounts are set (Strategies) or from its own tab.
const START = { live: ["#start-live", "#start-live-here"], replay: ["#start-replay", "#start-replay-here"] };
for (const [mode, ids] of Object.entries(START))
  for (const id of ids) $(id).addEventListener("click", () => start(mode));
function startNote() {
  $("#start-note").textContent = `Replay uses the day and speed set on the Replay tab (${$("#replay-day").value || "no recorded day"}, ${$("#replay-speed").value}x).`;
}
$("#replay-day").addEventListener("change", startNote);
$("#stop").addEventListener("click", () => ws.send(JSON.stringify({ cmd: "stop" })));
$("#replay-speed").addEventListener("input", (e) => {
  $("#replay-speed-out").textContent = e.target.value + "x";
  startNote();
  if (session.mode === "replay" && session.status === "running")
    ws.send(JSON.stringify({ cmd: "speed", value: Number(e.target.value) }));
});

function onSession(s) {
  const fresh = s.status === "running" && session.status !== "running" && session.status !== "stopping";
  // A page opened after a session has ended still shows where it ended, under its own tab.
  const arrived = !sessionSeen && !!s.mode;
  sessionSeen = true;
  session = s;
  const busy = ["starting", "running", "stopping"].includes(s.status);
  $("#stop").hidden = !busy;
  for (const id of [...START.live, ...START.replay]) $(id).disabled = busy;
  const what = s.mode === "replay" ? `replay of ${s.day} at ${s.speed}x` : s.mode === "live" ? "live" : "";
  $("#status-text").textContent = s.status === "none" ? "no session" : `${what}: ${s.status}`;
  $("#pill").className = "pill " + (s.status === "running" ? "running" : busy ? "busy" : "idle");
  if (s.mode === "replay" && busy) { $("#replay-speed").value = s.speed; $("#replay-speed-out").textContent = s.speed + "x"; }
  if (s.error) error(s.error);
  if (fresh) reset();
  if (fresh || (arrived && !location.hash)) show(s.mode);
  if (s.status === "ended" && s.dir) $("#status-text").textContent += ` (run folder ${s.dir}/run)`;
  if (s.hello && names.length && !$("#cards").children.length) buildCards();
}

function reset() {
  names = []; xs = []; ys = []; totals = []; idxV = []; fills = []; last = null; selected = 0;
  fillsReceived = 0; fillsShown = -1; seenFills = []; prevBooks = new Map(); moved = 0; smileKey = "";
  for (const c of charts) c.destroy();
  charts = [];
  if ($("#cards")) $("#cards").replaceChildren();
}

// ---------------------------------------------------------------- websocket

function connect() {
  ws = new WebSocket(`ws://${location.hostname}:${meta.ws_port}`);
  ws.onmessage = (e) => {
    const m = JSON.parse(e.data);
    if (m.type === "state") onState(m);
    else if (m.type === "session") onSession(m);
    else if (m.type === "history") onHistory(m);
    else if (m.type === "error") error(m.message);
  };
  ws.onclose = () => {
    $("#status-text").textContent = "server not reachable; retrying…";
    $("#pill").className = "pill idle";
    setTimeout(connect, 2000);
  };
}

function onHistory(h) {
  names = h.names;
  xs = h.series.length ? h.series[0].t.slice() : [];
  ys = h.series.map((s) => s.pnl.slice());
  totals = xs.map((_, k) => ys.reduce((sum, y) => sum + (y[k] || 0), 0));
  idxV = h.index ? h.index.v.slice() : [];
  fills = h.fills.slice().reverse();
  fillsReceived = fills.length;
  placeView(session.mode);
  buildCards();
  buildChart();
}

function onState(s) {
  if (!names.length) {
    names = s.accounts.map((a) => a.name);
    ys = names.map(() => []);
    placeView(session.mode);
    buildCards();
  }
  const t = s.t / 1e9;
  if (!xs.length || t > xs[xs.length - 1]) {
    xs.push(t);
    s.accounts.forEach((a, i) => ys[i].push(a.pnl.total));
    totals.push(s.accounts.reduce((sum, a) => sum + a.pnl.total, 0));
    if (s.index != null) idxV.push(s.index);
    if (xs.length > 20000) thin();
  }
  for (const f of s.fills) fills.unshift(f);
  fillsReceived += s.fills.length;
  fills.length = Math.min(fills.length, 200);
  // How many of the books we watch changed their best bid or ask since the previous state.
  moved = 0;
  const now = new Map();
  for (const b of s.books) {
    const key = b[1] + "|" + b[2];
    if (prevBooks.has(b[0]) && prevBooks.get(b[0]) !== key) ++moved;
    now.set(b[0], key);
  }
  prevBooks = now;
  last = s;
  if (!dirty) { dirty = true; setTimeout(render, 250); }
}

// Keep the charts light on long sessions: halve the resolution of the older half.
function thin() {
  const half = xs.length >> 1;
  const keep = (arr) => arr.filter((_, i) => i >= half || i % 2 === 0);
  xs = keep(xs);
  ys = ys.map(keep);
  totals = keep(totals);
  if (idxV.length > 20000) idxV = idxV.filter((_, i) => i % 2 === 0);
}

// ---------------------------------------------------------------- small drawings

function sample(vals, n) {
  if (vals.length <= n) return vals;
  const out = [];
  for (let i = 0; i < n; ++i) out.push(vals[Math.round(i * (vals.length - 1) / (n - 1))]);
  return out;
}

// A line through `vals` in a 100 x 28 box; with a dashed zero line when zero is in range.
function spark(svg, vals, zero) {
  const v = sample(vals, 160);
  if (v.length < 2) { svg.innerHTML = ""; return; }
  let lo = Math.min(...v), hi = Math.max(...v);
  if (zero) { lo = Math.min(lo, 0); hi = Math.max(hi, 0); }
  const span = hi - lo || 1;
  const y = (x) => (26 - ((x - lo) / span) * 24).toFixed(2);
  const pts = v.map((x, i) => `${(i / (v.length - 1) * 100).toFixed(2)},${y(x)}`).join(" ");
  svg.innerHTML = (zero ? `<line x1="0" x2="100" y1="${y(0)}" y2="${y(0)}"/>` : "") + `<polyline points="${pts}"/>`;
}

function chartOptions(box, labels, colours, height, x) {
  const axis = { stroke: css("--muted"), grid: { stroke: css("--grid"), width: 1 }, ticks: { stroke: css("--axis"), width: 1 } };
  const time = !x;
  return {
    width: Math.max(280, box.clientWidth), height,
    tzDate: (ts) => uPlot.tzDate(new Date(ts * 1e3), "Etc/UTC"),  // every time on the page is UTC
    // A flat series (no fills yet) would get an arbitrary range; give it a small one around its value.
    scales: { x: { time }, y: { range: (u, lo, hi) => (hi - lo < 1e-9 ? [lo - 1e-4, hi + 1e-4] : [lo - (hi - lo) * 0.08, hi + (hi - lo) * 0.08]) } },
    // Times as 24 h UTC; dates instead once the chart spans more than a day and a half.
    axes: [time ? { ...axis, values: (u, vals) => { const days = vals.length > 1 && vals[vals.length - 1] - vals[0] > 129600;
      return vals.map((v) => (days ? utc(v * 1e9).slice(5, 10) : utc(v * 1e9).slice(11, 16))); } }
      : { ...axis, values: (u, vals) => vals.map(x.tick) },
      { ...axis, size: x ? 46 : 86, values: (u, vals) => vals.map(x ? x.ytick : (v) => v.toFixed(6)) }],
    cursor: { y: false },
    series: [{ label: x ? x.label : "UTC", value: (u, v) => (v == null ? "" : x ? x.tick(v) : utc(v * 1e9)) },
      ...labels.map((label, i) => ({ label, stroke: colours[i], width: 2, spanGaps: true,
        points: { show: !!x, size: 5 }, value: (u, v) => (v == null ? "" : x ? x.ytick(v) : btc(v)) }))],
  };
}

// ---------------------------------------------------------------- PnL charts

// Up to 8 accounts share one chart. More than that is one small chart per strategy, and a
// strategy with more than 8 accounts is split again, so no chart ever needs more lines than
// there are colours. A colour then means "n-th account of its chart" (the hedger, in the tuned
// 4 x 4 set).
const strategyOf = (i) => (session.hello && session.hello.accounts[i] ? session.hello.accounts[i].strategy : "accounts");
const hedgerOf = (i) => (session.hello && session.hello.accounts[i] ? session.hello.accounts[i].hedger : names[i]);

function makeGroups() {
  const max = SERIES.length;
  if (names.length <= max) return [{ title: "", idx: names.map((_, i) => i) }];
  const by = new Map();
  names.forEach((_, i) => { const k = strategyOf(i); if (!by.has(k)) by.set(k, []); by.get(k).push(i); });
  const out = [];
  for (const [title, idx] of by)
    for (let k = 0; k < idx.length; k += max) {
      const part = idx.slice(k, k + max);
      const range = part.length > 1 ? `accounts ${part[0] + 1}–${part[part.length - 1] + 1}` : `account ${part[0] + 1}`;
      out.push({ title: idx.length > max ? `${title} (${range})` : title, idx: part });
    }
  return out;
}
const slot = (i) => { for (const g of groups) { const k = g.idx.indexOf(i); if (k >= 0) return k; } return i; };

function buildChart() {
  const box = $("#pnl-chart");
  if (!box || !names.length || !box.clientWidth) return;
  for (const c of charts) c.destroy();
  groups = makeGroups();
  box.replaceChildren();
  box.classList.toggle("multiples", groups.length > 1);
  const cells = groups.map((g) => {
    const cell = el("div");
    if (g.title) cell.append(el("h2", { textContent: g.title }));
    box.append(cell);
    return cell;
  });
  charts = groups.map((g, k) => new uPlot(
    chartOptions(cells[k], g.idx.map((i) => (groups.length > 1 ? hedgerOf(i) : names[i])), g.idx.map((_, n) => colour(n)), groups.length > 1 ? 170 : 300),
    [xs, ...g.idx.map((i) => ys[i])], cells[k]));
}

// ---------------------------------------------------------------- account cards

function buildCards() {
  const box = $("#cards");
  if (!box || !names.length) return;
  if (!groups.length) groups = makeGroups();
  const by = new Map();
  names.forEach((_, i) => { const k = strategyOf(i); if (!by.has(k)) by.set(k, []); by.get(k).push(i); });
  box.replaceChildren();
  for (const [strategy, idx] of by) {
    const row = el("div", { className: "cardrow" });
    for (const i of idx) {
      const card = el("button", { className: "card" + (i === selected ? " sel" : ""), title: names[i] });
      card.dataset.i = i;
      card.style.setProperty("--c", colour(slot(i)));
      card.innerHTML = `<div class="who"></div><div class="val num">+0.000000</div>
        <svg viewBox="0 0 100 28" preserveAspectRatio="none"></svg>
        <div class="meta"><span class="f">0 fills</span><span class="d"></span></div>
        <div class="meter" title="vega in use, as a share of the account's limit"><i></i></div>`;
      $(".who", card).textContent = session.hello ? hedgerOf(i) : names[i];
      card.addEventListener("click", () => { selected = i; fillsShown = -1; render(); });
      card.addEventListener("animationend", () => card.classList.remove("flash"));
      row.append(card);
    }
    box.append(el("div", { className: "cardgroup" }, el("div", { className: "gname", textContent: strategy }), row));
  }
}

function renderCards() {
  const maxVega = (i) => (session.hello && session.hello.accounts[i] ? session.hello.accounts[i].quoter.max_vega_usd_per_vp : 100);
  for (const card of document.querySelectorAll("#cards .card")) {
    const i = +card.dataset.i, a = last.accounts[i];
    if (!a) continue;
    card.classList.toggle("sel", i === selected);
    const val = $(".val", card);
    val.textContent = btc(a.pnl.total);
    val.className = "val num " + signClass(a.pnl.total);
    spark($("svg", card), ys[i].slice(-600), true);
    $(".f", card).textContent = `${a.fills} fill${a.fills === 1 ? "" : "s"}`;
    $(".d", card).textContent = `Δ ${a.delta >= 0 ? "+" : "−"}${Math.abs(a.delta).toFixed(3)}`;
    $(".meter i", card).style.width = Math.min(100, Math.abs(a.vega) / maxVega(i) * 100) + "%";
    if (seenFills[i] != null && a.fills > seenFills[i]) card.classList.add("flash");
    seenFills[i] = a.fills;
  }
}

// ---------------------------------------------------------------- the session view

const td = (text, cls) => el("td", { textContent: text, className: cls || "" });
function rows(tbody, items, make) { tbody.replaceChildren(...items.map(make)); }

function renderHeadline() {
  const A = last.accounts;
  const total = A.reduce((s, a) => s + a.pnl.total, 0);
  $("#k-pnl").textContent = btc(total);
  $("#k-pnl").className = "big " + signClass(total);
  $("#k-pnl-sub").textContent = (last.index ? `${total >= 0 ? "+" : "−"}$${Math.abs(total * last.index).toFixed(2)} · ` : "") + `${A.length} account${A.length === 1 ? "" : "s"}, after fees`;
  spark($("#s-pnl"), totals.slice(-1200), true);

  if (last.index) {
    $("#k-index").textContent = last.index.toFixed(1);
    const chg = idxV.length ? (last.index / idxV[0] - 1) * 100 : 0;
    const shown = Math.abs(chg).toFixed(2);
    $("#k-index-sub").textContent = `${shown === "0.00" ? "" : chg > 0 ? "+" : "−"}${shown}% since the session began`;
    spark($("#s-index"), idxV.slice(-1200), false);
  }

  const nFills = A.reduce((s, a) => s + a.fills, 0);
  const elapsed = xs.length ? xs[xs.length - 1] - xs[0] : 0;
  $("#k-fills").textContent = nFills;
  $("#k-fills-sub").textContent = nFills && fills.length
    ? `last ${ago((last.t - fills[0].t) / 1e9)} ago · ${(nFills / Math.max(elapsed / 3600, 1 / 60)).toFixed(1)} per hour`
    : "none yet";

  let orders = 0;
  const quoted = new Set();
  for (const a of A) for (const q of a.quotes) { orders += (q[1] != null) + (q[3] != null); quoted.add(q[0]); }
  $("#k-quotes").textContent = orders;
  $("#k-quotes-sub").textContent = `on ${quoted.size} options · ${moved} of ${last.books.length} books moved`;

  const delta = A.reduce((s, a) => s + a.delta, 0), vega = A.reduce((s, a) => s + a.vega, 0);
  $("#k-delta").textContent = `${delta >= 0 ? "+" : "−"}${Math.abs(delta).toFixed(4)}`;
  $("#k-risk-sub").textContent = `BTC · vega ${vega >= 0 ? "+" : "−"}${Math.abs(vega).toFixed(1)} $/vp`;

  // Flat lines are the normal state before the first fill; say so instead of looking broken.
  const wait = $("#waiting");
  wait.hidden = nFills > 0 || session.status !== "running";
  if (!wait.hidden) {
    wait.innerHTML = `<b>No fills yet.</b> ${orders} paper orders are resting on ${quoted.size} options. A paper fill happens only when a
      public trade prints at one of those prices, so PnL stays at zero until then. ` + (session.mode === "live"
      ? "Live, expect about one fill per account per hour, fewer when the market is quiet."
      : "On a replay, raise the speed to get there sooner.");
  }

  const prog = $("#progress");
  prog.hidden = session.mode !== "replay";
  if (!prog.hidden && session.day) {
    const frac = Math.min(1, Math.max(0, (last.t / 1e9 - Date.parse(session.day + "T00:00:00Z") / 1e3) / 86400));
    $("#progress-fill").style.width = (frac * 100).toFixed(2) + "%";
    $("#progress-label").textContent = `${utc(last.t).slice(11, 16)} of 24:00 on ${session.day} · ${session.speed}x`;
  }
}

function renderDetail() {
  const acc = last.accounts[Math.min(selected, last.accounts.length - 1)];
  const book = new Map(last.books.map((b) => [b[0], b]));

  // PnL parts: one neutral hue; which side of the zero line a bar is on carries its sign.
  $("#parts-title").replaceChildren("Where the PnL comes from ", el("span", { className: "hint", textContent: acc.name }));
  const parts = [["Spread earned", acc.pnl.spread], ["Fees", acc.pnl.fees], ["Adverse 60 s", acc.pnl.adverse],
    ["Option MTM", acc.pnl.option_mtm], ["Hedge", acc.pnl.hedge]];
  const scale = Math.max(1e-9, ...parts.map((p) => Math.abs(p[1])));
  $("#parts").replaceChildren(...parts.map(([name, v]) => {
    const w = Math.abs(v) / scale * 50;
    const bar = el("i", { className: v < 0 ? "left" : "" });
    bar.style.width = w + "%";
    bar.style.left = (v < 0 ? 50 - w : 50) + "%";
    return el("div", { className: "part" }, el("span", { className: "name", textContent: name }),
      el("span", { className: "track" }, bar), el("span", { className: "v num", textContent: btc(v) }));
  }), el("div", { className: "part total" }, el("span", { className: "name", textContent: "Total" }), el("span"),
    el("span", { className: "v num " + signClass(acc.pnl.total), textContent: btc(acc.pnl.total) })));

  rows($("#positions tbody"), acc.positions, (p) => el("tr", {}, td(p[0], "l"), td(num(p[1], 1)), td(px(p[2]))));
  if (!acc.positions.length) $("#positions tbody").append(el("tr", {}, el("td", { className: "empty l", colSpan: 3, textContent: "No positions." })));

  // Quote ladder: each option's market bid-to-ask as a band, our bid and ask as marks on it.
  $("#ladder-title").replaceChildren("Quotes against the market ", el("span", { className: "hint", textContent: acc.name }));
  const quotes = acc.quotes.map((q) => ({ q, i: parseInstr(q[0]) })).filter((x) => x.i)
    .sort((a, b) => a.i.when - b.i.when || a.i.strike - b.i.strike || (a.i.cp < b.i.cp ? -1 : 1));
  const out = [];
  let expiry = "";
  for (const { q, i } of quotes) {
    if (i.expiry !== expiry) { expiry = i.expiry; out.push(el("div", { className: "ladder-exp", textContent: `expires ${expiry} 08:00 UTC` })); }
    const b = book.get(q[0]) || [];
    const vals = [b[1], b[2], q[1], q[3]].filter((v) => v != null);
    const lo = Math.min(...vals), hi = Math.max(...vals), pad = (hi - lo) * 0.25 || lo * 0.1 || 1e-4;
    const x = (v) => ((v - lo + pad) / (hi - lo + 2 * pad) * 100).toFixed(2) + "%";
    const track = el("span", { className: "track" });
    if (b[1] != null && b[2] != null) {
      const m = el("i", { className: "mkt" });
      m.style.left = x(b[1]);
      m.style.width = `calc(${x(b[2])} - ${x(b[1])})`;
      track.append(m);
    }
    for (const [v, cls] of [[q[1], "bid"], [q[3], "ask"]]) {
      if (v == null) continue;
      const t = el("i", { className: "tick " + cls });
      t.style.left = x(v);
      track.append(t);
    }
    const rung = el("div", { className: "rung", title: `${q[0]}: market ${px(b[1])} to ${px(b[2])}, our bid ${px(q[1])}, our ask ${px(q[3])}` },
      el("span", { textContent: `${i.strike} ${i.cp}` }), track,
      el("span", { className: "px num", textContent: `${px(q[1])} / ${px(q[3])}` }),
      el("span", { className: "iv num", textContent: b[3] == null ? "" : b[3].toFixed(1) }));
    out.push(rung);
  }
  if (!out.length) out.push(el("div", { className: "empty", textContent: "No quotes resting for this account." }));
  $("#ladder").replaceChildren(...out);

  if (fillsShown !== fillsReceived) {
    const fresh = fillsShown < 0 ? 0 : fillsReceived - fillsShown;
    // One line per fill: what was traded, then who. Taker fills are the hedges on the perpetual.
    rows($("#fills tbody"), fills.slice(0, 60), (f) => {
      const who = (names[f.a] || String(f.a)).replace(/^\d+-/, "");
      const what = `${f.side} ${f.amt} ${f.i.replace(/^BTC-/, "")} @ ${px(f.px)}` + (f.maker ? "" : " (hedge)");
      return el("tr", { className: fills.indexOf(f) < fresh ? "new" : "", title: `${names[f.a] || f.a}: ${what}, fee ${f.fee} BTC` },
        td(utc(f.t).slice(11), "l"), td(what, "l num"), td(who, "l who"));
    });
    if (!fills.length) $("#fills tbody").append(el("tr", {}, el("td", { className: "empty l", colSpan: 3, textContent: "No fills yet." })));
    fillsShown = fillsReceived;
  }
}

// Mark IV by strike, one line per expiry, from the options some account quotes or holds.
function renderSmile() {
  const box = $("#smile");
  if (!box || !box.clientWidth) return;
  const by = new Map();
  for (const b of last.books) {
    const i = parseInstr(b[0]);
    if (!i || b[3] == null) continue;
    if (!by.has(i.expiry)) by.set(i.expiry, { when: i.when, iv: new Map() });
    const at = by.get(i.expiry).iv;
    at.set(i.strike, at.has(i.strike) ? (at.get(i.strike) + b[3]) / 2 : b[3]);   // call and put share a strike
  }
  const expiries = [...by].sort((a, b) => a[1].when - b[1].when).slice(0, SERIES.length);
  if (!expiries.length) { box.replaceChildren(el("div", { className: "empty", textContent: "No option quotes yet." })); smile = null; smileKey = ""; return; }
  const strikes = [...new Set(expiries.flatMap(([, e]) => [...e.iv.keys()]))].sort((a, b) => a - b);
  const data = [strikes, ...expiries.map(([, e]) => strikes.map((k) => (e.iv.has(k) ? e.iv.get(k) : null)))];
  const key = expiries.map(([name]) => name).join(",");
  if (key !== smileKey || !smile) {
    box.replaceChildren();
    smile = new uPlot(chartOptions(box, expiries.map(([name]) => name), expiries.map((_, n) => colour(n)), 190,
      { label: "strike", tick: (v) => (v / 1000).toFixed(v % 1000 ? 1 : 0) + "k", ytick: (v) => v.toFixed(1) }), data, box);
    smileKey = key;
  } else {
    smile.setData(data);
  }
}

function render() {
  dirty = false;
  if (!last || !$("#view") || $("#view").hidden) return;
  $("#clock").textContent = utc(last.t) + " UTC";
  renderHeadline();
  if (charts.length) charts.forEach((c, k) => c.setData([xs, ...groups[k].idx.map((i) => ys[i])]));
  else buildChart();
  if (!$("#cards").children.length) buildCards();
  renderCards();
  renderDetail();
  if (frame++ % 4 === 0 || !smile) renderSmile();
}

// ---------------------------------------------------------------- results tab

let resultsData = null;
let curvesDrawn = false;

async function loadResults() {
  if (!resultsData) {
    resultsData = (await (await fetch("/api/results")).json()).tables;
    const pick = $("#results-table");
    for (const name of Object.keys(resultsData)) pick.append(el("option", { value: name, textContent: name.replace("_", " ") }));
    pick.addEventListener("change", renderResults);
    $("#results-filter").addEventListener("input", renderResults);
  }
  renderResults();
  if (!curvesDrawn && !$("#tab-results").hidden) drawCurves();
}

// What was run and what came of it first; every other condition follows to the right.
const LEAD = ["strategy", "tuning_pick", "hedger", "fill_model", "md_latency_ms", "strategy_params", "pnl_btc", "pnl_usd",
  "spread_btc", "fees_btc", "adverse_60s_btc", "option_mtm_btc", "hedge_btc", "max_drawdown_btc", "maker_fills", "hedge_orders"];

function renderResults() {
  const t = resultsData[$("#results-table").value];
  if (!t) return;
  const words = $("#results-filter").value.toLowerCase().split(/\s+/).filter(Boolean);
  const shown = t.rows.filter((r) => { const s = r.join(" ").toLowerCase(); return words.every((w) => s.includes(w)); });
  const order = [...LEAD.map((c) => t.columns.indexOf(c)).filter((i) => i >= 0)];
  t.columns.forEach((_, i) => order.includes(i) || order.push(i));
  const cell = (c, v) => (c.endsWith("_btc") && c !== "starting_equity_btc" && v !== "" ? Number(v).toFixed(6) : c === "pnl_usd" ? Number(v).toFixed(2) : v);
  $("#results thead").replaceChildren(el("tr", {}, ...order.map((i) => el("th", { textContent: t.columns[i] }))));
  rows($("#results tbody"), shown.slice(0, 400), (r) => el("tr", {}, ...order.map((i) => td(cell(t.columns[i], r[i])))));
  $("#results-count").textContent = `${shown.length} of ${t.rows.length} rows` + (shown.length > 400 ? " (first 400 shown)" : "");
}

// 16 accounts are too many lines for one chart: one small chart per strategy, four lines each,
// and the colour follows the hedger in every chart.
async function drawCurves() {
  const box = $("#curves");
  if (!box.clientWidth) return;
  const data = await (await fetch("/api/curves")).json();
  curvesDrawn = true;
  box.replaceChildren();
  if (!data.curves.length) {
    $("#curves-note").textContent = `No run folder at ${data.run}: the curves need the run output (runs/ is not in the repository). The tables below do not.`;
    return;
  }
  $("#curves-note").textContent = "One chart per strategy; the four lines are the four hedgers. Hover for values.";
  const by = new Map();
  for (const c of data.curves) {
    const [strategy, hedger] = c.account.split("--");
    if (!by.has(strategy)) by.set(strategy, []);
    by.get(strategy).push({ hedger, ...c });
  }
  box.classList.add("multiples");
  // Lay out every cell first, so each chart is sized to its final cell.
  const cells = [...by].map(([strategy, list]) => {
    list.sort((a, b) => HEDGER_ORDER.indexOf(a.hedger) - HEDGER_ORDER.indexOf(b.hedger));
    const cell = el("div", {}, el("h2", { textContent: strategy.replace("-active", " (min-20-fills set)") }));
    box.append(cell);
    return [cell, list];
  });
  for (const [cell, list] of cells) {
    // The four accounts of a run share their sample times only roughly: align on the first.
    const t = list[0].t;
    const series = list.map((c) => t.map((x) => at(c, x)));
    new uPlot(chartOptions(cell, list.map((c) => c.hedger), list.map((c) => colour(HEDGER_ORDER.indexOf(c.hedger))), 220),
      [t, ...series], cell);
  }
}

// Value of a curve at time x: its last point at or before x.
function at(c, x) {
  let lo = 0, hi = c.t.length - 1;
  if (x < c.t[0]) return null;
  while (lo < hi) { const mid = (lo + hi + 1) >> 1; if (c.t[mid] <= x) lo = mid; else hi = mid - 1; }
  return c.pnl[lo];
}

// ---------------------------------------------------------------- start

(async function init() {
  try { const t = localStorage.getItem("theme"); if (t) document.documentElement.dataset.theme = t; } catch { /* private window */ }
  meta = await (await fetch("/api/meta")).json();
  for (const d of meta.days) $("#replay-day").append(el("option", { value: d, textContent: d }));
  for (const f of meta.fill_models) $("#fill-model").append(el("option", { value: f, textContent: f }));
  // Without a recording there is no measured latency: live sessions then need one typed in.
  $("#latency").placeholder = meta.default_latency_ms != null ? `${meta.default_latency_ms} (measured)` : "required for live";
  if (meta.default_latency_ms == null) $("#latency").title = "No recorded latency probes on this machine. Enter the one-way latency to Deribit in ms; half a ping to www.deribit.com is a fair start.";
  if (!meta.engine_built) error("build/release/mm_engine is missing: build the engine first (see README).");
  try { accounts = JSON.parse(localStorage.getItem("accounts") || "null") || []; } catch { accounts = []; }
  if (!accounts.length) accounts = defaultAccounts();
  renderEditor();
  startNote();
  if (!meta.days.length) for (const id of START.replay) { $(id).disabled = true; $(id).title = "No recorded day with a book checkpoint and a fit cache."; }
  // A link can open a tab directly, e.g. http://127.0.0.1:8765/#results
  const tab = location.hash.slice(1);
  if (["live", "replay", "strategies", "results"].includes(tab)) show(tab);
  connect();
})();
