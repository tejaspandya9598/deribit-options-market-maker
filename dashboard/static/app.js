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
// Colour follows the account (its index), never its rank; beyond 8 an account is "other".
const colour = (i) => css(i < SERIES.length ? SERIES[i] : "--other");
const HEDGER_ORDER = ["none", "fixed_band", "time_based", "whalley_wilmott"];

const btc = (v) => (v == null ? "" : (v >= 0 ? "+" : "−") + Math.abs(v).toFixed(6));
const num = (v, d = 4) => (v == null ? "" : Number(v).toFixed(d));
const px = (v) => (v == null ? "" : v >= 100 ? Number(v).toFixed(1) : Number(v).toFixed(4));
const utc = (ns) => new Date(ns / 1e6).toISOString().replace("T", " ").slice(0, 19);
const signed = (td, v) => { td.textContent = btc(v); td.className = v > 0 ? "pos" : v < 0 ? "neg" : ""; return td; };

let meta = null;
let ws = null;
let session = { status: "none" };
let accounts = [];          // the Strategies tab's rows
let names = [];             // accounts of the running session
let xs = [], ys = [];       // PnL chart data
let charts = [];            // one, or one per strategy when there are more than 8 accounts
let groups = [];            // [{title, idx: [account indices]}], one per chart
let last = null;            // latest state
let selected = 0;
let fills = [];
let dirty = false;

// ---------------------------------------------------------------- tabs

function show(tab) {
  for (const b of document.querySelectorAll("#tabs button")) b.classList.toggle("on", b.dataset.tab === tab);
  for (const s of document.querySelectorAll("section")) s.hidden = s.id !== "tab-" + tab;
  if (tab === "live" || tab === "replay") placeView(tab);
  if (tab === "results") loadResults();
  resize();
}
$("#tabs").addEventListener("click", (e) => e.target.dataset.tab && show(e.target.dataset.tab));

function placeView(tab) {
  const slot = $(`#tab-${tab} .view-slot`);
  let view = $("#view");
  if (!view) view = $("#view-template").content.firstElementChild.cloneNode(true);
  // The view belongs under the tab of the session's mode; the other tab only has its controls.
  const mode = session.mode || tab;
  if (mode === tab && view.parentElement !== slot) { slot.append(view); buildChart(); }
  view.hidden = !names.length;
}

function error(msg) {
  const e = $("#error");
  e.hidden = !msg;
  e.textContent = msg || "";
}

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
  session = s;
  const busy = ["starting", "running", "stopping"].includes(s.status);
  $("#stop").hidden = !busy;
  for (const id of [...START.live, ...START.replay]) $(id).disabled = busy;
  const what = s.mode === "replay" ? `replay of ${s.day} at ${s.speed}x` : s.mode === "live" ? "live" : "";
  $("#status-text").textContent = s.status === "none" ? "no session" : `${what}: ${s.status}`;
  if (s.mode === "replay" && busy) { $("#replay-speed").value = s.speed; $("#replay-speed-out").textContent = s.speed + "x"; }
  if (s.error) error(s.error);
  if (fresh) { reset(); show(s.mode); }
  if (s.status === "ended" && s.dir) $("#status-text").textContent += ` (run folder ${s.dir}/run)`;
}

function reset() {
  names = []; xs = []; ys = []; fills = []; last = null; selected = 0;
  for (const c of charts) c.destroy();
  charts = [];
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
  ws.onclose = () => { $("#status-text").textContent = "server not reachable; retrying…"; setTimeout(connect, 2000); };
}

function onHistory(h) {
  names = h.names;
  xs = h.series.length ? h.series[0].t.slice() : [];
  ys = h.series.map((s) => s.pnl.slice());
  fills = h.fills.slice().reverse();
  placeView(session.mode);
  buildChart();
}

function onState(s) {
  if (!names.length) {
    names = s.accounts.map((a) => a.name);
    ys = names.map(() => []);
    placeView(session.mode);
  }
  const t = s.t / 1e9;
  if (!xs.length || t > xs[xs.length - 1]) {
    xs.push(t);
    s.accounts.forEach((a, i) => ys[i].push(a.pnl.total));
    if (xs.length > 20000) thin();
  }
  for (const f of s.fills) fills.unshift(f);
  fills.length = Math.min(fills.length, 200);
  last = s;
  if (!dirty) { dirty = true; setTimeout(render, 250); }
}

// Keep the chart light on long sessions: halve the resolution of the older half.
function thin() {
  const half = xs.length >> 1;
  const keep = (arr) => arr.filter((_, i) => i >= half || i % 2 === 0);
  xs = keep(xs);
  ys = ys.map(keep);
}

// ---------------------------------------------------------------- session view

function chartOptions(box, labels, colours, height) {
  const axis = { stroke: css("--muted"), grid: { stroke: css("--grid"), width: 1 }, ticks: { stroke: css("--axis"), width: 1 } };
  return {
    width: Math.max(320, box.clientWidth - 16), height,
    tzDate: (ts) => uPlot.tzDate(new Date(ts * 1e3), "Etc/UTC"),  // every time on the page is UTC
    // A flat series (no fills yet) would get an arbitrary range; give it a small one around its value.
    scales: { x: { time: true }, y: { range: (u, lo, hi) => (hi - lo < 1e-9 ? [lo - 1e-4, hi + 1e-4] : [lo - (hi - lo) * 0.08, hi + (hi - lo) * 0.08]) } },
    axes: [axis, { ...axis, size: 86, values: (u, vals) => vals.map((v) => v.toFixed(6)) }],
    cursor: { y: false },
    series: [{ label: "UTC", value: (u, v) => (v == null ? "" : utc(v * 1e9)) },
      ...labels.map((label, i) => ({ label, stroke: colours[i], width: 2, value: (u, v) => btc(v) }))],
  };
}

// Up to 8 accounts share one chart. More than that is one small chart per strategy, and a
// strategy with more than 8 accounts is split again, so no chart ever needs more lines than
// there are colours. A colour then means "n-th account of its chart" (the hedger, in the tuned
// 4 x 4 set).
function makeGroups() {
  const max = SERIES.length;
  if (names.length <= max) return [{ title: "", idx: names.map((_, i) => i) }];
  const strategy = (i) => (session.hello && session.hello.accounts[i] ? session.hello.accounts[i].strategy : "accounts");
  const by = new Map();
  names.forEach((_, i) => { const k = strategy(i); if (!by.has(k)) by.set(k, []); by.get(k).push(i); });
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
    chartOptions(cells[k], g.idx.map((i) => names[i]), g.idx.map((_, n) => colour(n)), groups.length > 1 ? 200 : 300),
    [xs, ...g.idx.map((i) => ys[i])], cells[k]));
}

function resize() {
  if (charts.length) buildChart();
}
window.addEventListener("resize", resize);
matchMedia("(prefers-color-scheme: dark)").addEventListener("change", () => { buildChart(); curvesDrawn = false; if (resultsData) loadResults(); });

function rows(tbody, items, make) {
  tbody.replaceChildren(...items.map(make));
}
const td = (text, cls) => el("td", { textContent: text, className: cls || "" });

function render() {
  dirty = false;
  if (!last || !$("#view")) return;
  $("#clock").textContent = utc(last.t) + " UTC" + (last.index ? `  index ${last.index.toFixed(0)}` : "");
  if (charts.length) charts.forEach((c, k) => c.setData([xs, ...groups[k].idx.map((i) => ys[i])]));
  else buildChart();

  rows($("#accounts tbody"), last.accounts, (a) => {
    const i = last.accounts.indexOf(a);
    const sw = el("td", {}, el("span", { className: "sw", style: `background:${colour(slot(i))}` }));
    const tr = el("tr", { className: i === selected ? "sel" : "" }, sw, td(a.name, "l"),
      signed(el("td"), a.pnl.total), td(btc(a.pnl.spread)), td(btc(a.pnl.fees)), td(btc(a.pnl.adverse)),
      td(btc(a.pnl.option_mtm)), td(btc(a.pnl.hedge)), td(num(a.delta)), td(num(a.gamma, 6)),
      td(num(a.vega, 2)), td(a.fills), td(a.quotes.length), td(a.positions.length));
    tr.addEventListener("click", () => { selected = i; render(); });
    return tr;
  });

  const acc = last.accounts[Math.min(selected, last.accounts.length - 1)];
  const book = new Map(last.books.map((b) => [b[0], b]));
  $("#quotes-title").textContent = `Quotes of ${acc.name}`;
  rows($("#quotes tbody"), acc.quotes, (q) => {
    const b = book.get(q[0]) || [];
    return el("tr", {}, td(q[0], "l"), td(px(q[1])), td(px(b[1])), td(px(b[2])), td(px(q[3])), td(b[3] == null ? "" : b[3].toFixed(1)));
  });
  rows($("#positions tbody"), acc.positions, (p) => el("tr", {}, td(p[0], "l"), td(num(p[1], 1)), td(px(p[2]))));
  rows($("#fills tbody"), fills.slice(0, 60), (f) => el("tr", {}, td(utc(f.t).slice(11), "l"), td(names[f.a] || f.a, "l"),
    td(f.i, "l"), td(f.side), td(px(f.px)), td(f.amt), td(f.maker ? "maker" : "taker")));
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
    $("#curves-note").textContent = `No run folder at ${data.run}: the curves need the M5 run output (runs/ is not in the repository). The tables below do not.`;
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
  meta = await (await fetch("/api/meta")).json();
  for (const d of meta.days) $("#replay-day").append(el("option", { value: d, textContent: d }));
  for (const f of meta.fill_models) $("#fill-model").append(el("option", { value: f, textContent: f }));
  // Without a recording there is no measured latency: live sessions then need one typed in.
  $("#latency").placeholder = meta.default_latency_ms != null ? `${meta.default_latency_ms} (measured)` : "required for live";
  if (meta.default_latency_ms == null) $("#latency").title = "No recorded latency probes on this machine. Enter your one-way latency to Deribit in ms; half a ping to www.deribit.com is a fair start.";
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
