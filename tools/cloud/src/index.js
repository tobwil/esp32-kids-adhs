// Eltern-Portal als Cloudflare Worker: dieselbe Schnittstelle wie tools/portal/portal.py, Speicher im R2-Bucket DATEN.
//
// Anmeldung: Eltern mit Passwort (Geheimnis ELTERN_PASSWORT, signiertes Cookie), das Gerät mit Schlüssel
// (Geheimnis GERAET_SCHLUESSEL, Kopfzeile „Authorization: Bearer …“). Ohne beide Geheimnisse antwortet der Worker nicht.
//
// R2-Objekte:
//   cfg.json                Einstellungen (Sperre, Laut, Schlaf, Quellen, Kontakte, Wecker, Nachtlicht, Apps, aktives Album)
//   fragen.json             Rätsel-Fragen (fehlt: eingebauter Katalog quiz/kinder-2klasse.json)
//   alben.json              Sammelalben {id: thema}, Bilder unter alben/<id>/<karte>.jpg
//   aufnahmen/<id>.pcm      Radio-Aufnahmen (16 kHz mono, wandelt der Browser um)
//   nachrichten/index.json  Sprachnachrichten, Ton unter nachrichten/<id>.pcm
//   antwort.txt             vorgebaute Antwort für das Gerät (ohne Uhrzeit), wird bei jeder Änderung neu geschrieben
//   geraet.json             zuletzt gesehen und gemeldeter Zustand (höchstens einmal pro Minute geschrieben)
import html from "../../portal/index.html";
import start from "../../../quiz/kinder-2klasse.json";

const RATE = 16000;
const MAX_CARDS = 48, MAX_CONTACTS = 4;
const APPS = [[1, "Wie geht's dir?"], [2, "Grummel"], [3, "Atmen"], [4, "Kugel ins Loch"], [5, "Sammelalbum"], [6, "Rätsel"], [7, "Radio"], [8, "Post"]];
const DEFAULT = {
  sperre: { modus: 0, von: 19 * 60 + 30, bis: 7 * 60 }, laut: 70, schlaf: 30, zufall: true, quellen: [], apps_aus: 0, album: "pferde",
  kontakte: [], wecker: { an: false, min: 7 * 60, tage: 0x1f, ton: "melodie" }, nachtlicht: false,
};

// ---------------- Hilfen ----------------

const enc = new TextEncoder();
const CRC = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; }
  return t;
})();
function crc32(str, seed = 0) {
  let c = ~seed >>> 0;
  for (const b of enc.encode(str)) c = CRC[(c ^ b) & 0xff] ^ (c >>> 8);
  return ~c >>> 0;
}
const hex8 = (n) => (n >>> 0).toString(16).padStart(8, "0");
const field = (s, n) => String(s ?? "").replace(/[\r\n|]+/g, " ").trim().slice(0, n);
const cleanName = (s, n = 46) => field(s, n) || "Ohne Namen";
const text = (body, status = 200, extra = {}) => new Response(body, { status, headers: { "Content-Type": "text/plain; charset=utf-8", "Cache-Control": "no-store", ...extra } });
const json = (obj, status = 200) => new Response(JSON.stringify(obj), { status, headers: { "Content-Type": "application/json; charset=utf-8", "Cache-Control": "no-store" } });
const fail = (msg, status = 400) => json({ ok: false, fehler: msg }, status);

async function getJson(env, key, def) {
  const o = await env.DATEN.get(key);
  if (!o) return structuredClone(def);
  try { return await o.json(); } catch { return structuredClone(def); }
}
const putJson = (env, key, obj) => env.DATEN.put(key, JSON.stringify(obj), { httpMetadata: { contentType: "application/json" } });

async function loadCfg(env) {
  const c = await getJson(env, "cfg.json", DEFAULT);
  for (const [k, v] of Object.entries(DEFAULT)) if (!(k in c)) c[k] = structuredClone(v);
  return c;
}

// Ortszeit als „Unix-Sekunden“ (wie das Gerät sie erwartet)
function localEpoch(tz) {
  const p = Object.fromEntries(new Intl.DateTimeFormat("en-GB", { timeZone: tz, hourCycle: "h23", year: "numeric", month: "2-digit", day: "2-digit", hour: "2-digit", minute: "2-digit", second: "2-digit" })
    .formatToParts(new Date()).map((x) => [x.type, x.value]));
  return Math.floor(Date.UTC(+p.year, +p.month - 1, +p.day, +p.hour, +p.minute, +p.second) / 1000);
}

function wavHeader(n) {
  const b = new ArrayBuffer(44), v = new DataView(b);
  const s = (o, t) => { for (let i = 0; i < t.length; i++) v.setUint8(o + i, t.charCodeAt(i)); };
  s(0, "RIFF"); v.setUint32(4, 36 + n, true); s(8, "WAVEfmt "); v.setUint32(16, 16, true); v.setUint16(20, 1, true); v.setUint16(22, 1, true);
  v.setUint32(24, RATE, true); v.setUint32(28, RATE * 2, true); v.setUint16(32, 2, true); v.setUint16(34, 16, true); s(36, "data"); v.setUint32(40, n, true);
  return new Uint8Array(b);
}

async function wav(env, key) {
  const o = await env.DATEN.get(key);
  if (!o) return text("nicht gefunden\n", 404);
  const pcm = new Uint8Array(await o.arrayBuffer());
  const out = new Uint8Array(44 + pcm.length);
  out.set(wavHeader(pcm.length)); out.set(pcm, 44);
  return new Response(out, { headers: { "Content-Type": "audio/wav", "Cache-Control": "no-store" } });
}

async function blob(env, key, type) {
  const o = await env.DATEN.get(key);
  if (!o) return text("nicht gefunden\n", 404);
  return new Response(o.body, { headers: { "Content-Type": type, "Content-Length": String(o.size), "Cache-Control": "no-store" } });
}

// ---------------- Rätsel-Fragen (Regeln wie tools/quiz.py) ----------------

const DIFF = { leicht: 0, mittel: 1, fies: 2 };
const CHARSET = new Set([...Array.from({ length: 95 }, (_, i) => String.fromCharCode(32 + i)), ..."ÄÖÜäöüß×·–„“”…°€éèáàâñçÉ"]);
const LIMITS = { frage: 110, antwort: 30, erklaerung: 170, kategorie: 24 };

function quizProblems(q) {
  const out = [];
  for (const k of ["kategorie", "schwierigkeit", "frage", "antworten", "richtig", "erklaerung"]) if (!(k in q) || q[k] === "") out.push(`Feld „${k}“ fehlt`);
  if (out.length) return out;
  if (!(q.schwierigkeit in DIFF)) out.push("Schwierigkeit muss leicht, mittel oder fies sein");
  if (q.antworten.length !== 3 || q.antworten.some((a) => !a)) out.push("genau drei Antworten nötig");
  if (new Set(q.antworten.map((a) => a.trim().toLowerCase())).size !== q.antworten.length) out.push("Antworten doppelt");
  const texts = [["frage", q.frage], ["erklaerung", q.erklaerung], ["kategorie", q.kategorie], ...q.antworten.map((a) => ["antwort", a])];
  for (const [kind, t] of texts) {
    if (t.length > LIMITS[kind]) out.push(`${kind} zu lang (${t.length} > ${LIMITS[kind]})`);
    const bad = [...new Set([...t].filter((c) => !CHARSET.has(c)))];
    if (bad.length) out.push("Zeichen fehlen in der Schrift: " + bad.join(""));
  }
  if (q.bild) {
    const m = /^zaehl:(\d+):(apfel|stern|herz|ball)$/.exec(q.bild);
    if (!m || +m[1] < 1 || +m[1] > 12) out.push("Bild muss zaehl:<1-12>:<apfel|stern|herz|ball> sein");
    else if (String(+m[1]) !== q.antworten[q.richtig]) out.push(`Bild zeigt ${m[1]} Dinge, richtige Antwort ist ${q.antworten[q.richtig]}`);
  }
  return out;
}

const loadFragen = (env) => getJson(env, "fragen.json", start.fragen);
function quizText(fragen) {
  return "#pack portal\n" + fragen.map((q) => [q.kategorie, DIFF[q.schwierigkeit], q.frage, ...q.antworten, q.richtig, q.erklaerung, ...(q.bild ? [q.bild] : [])].join("|")).join("\n") + "\n";
}

// ---------------- Sammelalben ----------------

function slug(s) {
  return (s || "").toLowerCase().replace(/ä/g, "ae").replace(/ö/g, "oe").replace(/ü/g, "ue").replace(/ß/g, "ss").replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, "").slice(0, 20) || "album";
}
function albumText(a) {
  const lines = [`#album ${a.id}`, `#titel ${a.titel}`, `#symbol ${a.symbol}`, `#zaehlwort ${a.zaehlwort}`, `#frage ${a.frage}`, "#format 2",
    "#bilder " + a.karten.filter((k) => k.bild).map((k) => k.id).join(",")];
  for (const k of a.karten)
    lines.push([k.id, k.name, k.untertitel || "", k.zeile1 || "", k.zeile2 || "", a.ueberschrift3 || "", k.text3 || "", a.ueberschrift4 || "", k.text4 || "", k.wusstest || "", k.foto || ""].join("|"));
  return lines.join("\n") + "\n";
}
const albumVersion = (a) => crc32(albumText(a) + a.karten.map((k) => k.bild || 0).join(",")) || 1;

// ---------------- Antwort fürs Gerät ----------------

async function rebuild(env) {
  const [cfg, alben, fragen, post] = await Promise.all([loadCfg(env), getJson(env, "alben.json", {}), loadFragen(env), getJson(env, "nachrichten/index.json", [])]);
  const L = [`sperre ${cfg.sperre.modus} ${cfg.sperre.von} ${cfg.sperre.bis}`, `laut ${cfg.laut}`, `schlaf ${cfg.schlaf}`];
  for (const s of cfg.quellen) if (s.art === "aufnahme") L.push(`quelle ${s.id} aufnahme ${s.name}`);
  L.push(`apps ${cfg.apps_aus | 0}`);
  L.push(`fragen ${hex8(crc32(quizText(fragen)) || 1)}`);
  const w = cfg.wecker;
  L.push(`wecker ${w.an ? 1 : 0} ${w.min} ${w.tage} ${w.ton || "melodie"}`);
  L.push(`nacht ${cfg.nachtlicht ? 1 : 0}`);
  for (const s of cfg.quellen) if (s.art === "aufnahme" && s.bytes) L.push(`datei ${s.id} ${s.bytes}`);
  for (const k of cfg.kontakte) L.push(`kontakt ${k.id} ${k.name}`);
  const names = Object.fromEntries(cfg.kontakte.map((k) => [k.id, k.name]));
  for (const m of post.filter((m) => m.richtung === "an_kind").slice(0, 6)) L.push(`nachricht ${m.id} ${m.gehoert ? 1 : 0} ${names[m.kontakt] || "Eltern"}`);
  const a = alben[cfg.album];
  L.push(a ? `album ${a.id} ${hex8(albumVersion(a))}` : "album pferde 0");
  const body = L.join("\n") + "\n";
  await env.DATEN.put("antwort.txt", body);
  return body;
}

let lastDeviceWrite = 0, lastDeviceParams = "";
async function deviceSeen(env, params, ctx) {
  const p = JSON.stringify(params), now = Date.now();
  if (p === lastDeviceParams && now - lastDeviceWrite < 60000) return;   // höchstens einmal pro Minute schreiben
  lastDeviceParams = p; lastDeviceWrite = now;
  ctx.waitUntil(putJson(env, "geraet.json", { seen: Math.floor(now / 1000), params }));
}

// ---------------- Anmeldung ----------------

async function digest(s) { return new Uint8Array(await crypto.subtle.digest("SHA-256", enc.encode(s))); }
async function same(a, b) { return crypto.subtle.timingSafeEqual(await digest(a), await digest(b)); }
async function sign(env, msg) {
  const key = await crypto.subtle.importKey("raw", enc.encode("grummel:" + env.ELTERN_PASSWORT), { name: "HMAC", hash: "SHA-256" }, false, ["sign"]);
  return [...new Uint8Array(await crypto.subtle.sign("HMAC", key, enc.encode(msg)))].map((b) => b.toString(16).padStart(2, "0")).join("");
}
async function parentOk(env, req) {
  const m = /(?:^|;\s*)s=(\d+)\.([0-9a-f]{64})/.exec(req.headers.get("Cookie") || "");
  if (!m || +m[1] < Date.now() / 1000) return false;
  return same(await sign(env, m[1]), m[2]);
}
async function deviceOk(env, req) {
  const m = /^Bearer (.+)$/.exec(req.headers.get("Authorization") || "");
  return !!m && same(m[1], env.GERAET_SCHLUESSEL);
}

const LOGIN = (msg = "") => `<!doctype html><html lang=de><head><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1"><title>Grummel – Anmelden</title>
<style>body{font:17px -apple-system,sans-serif;background:#f6f4fb;color:#221d33;margin:0}main{max-width:360px;margin:12vh auto;padding:24px}
input,button{width:100%;box-sizing:border-box;font:inherit;padding:12px;border-radius:12px;border:1px solid #d8d2ea;margin-top:10px}button{background:#6a52d9;color:#fff;border:0}
p{color:#c0582a}@media (prefers-color-scheme:dark){body{background:#14121c;color:#ece8f6}input{background:#1e1b2a;color:#ece8f6;border-color:#2e2a40}}</style></head>
<body><main><h1>Grummel – Eltern-Portal</h1><form method=post action=/anmelden><input type=password name=passwort placeholder=Passwort autofocus autocomplete=current-password><button>Anmelden</button></form><p>${msg}</p></main></body></html>`;

// ---------------- Anfragen ----------------

const DEVICE_PATHS = [/^\/api\/geraet$/, /^\/api\/radio$/, /^\/api\/sperre$/, /^\/api\/apps$/, /^\/api\/nachricht\/gehoert$/, /^\/radio\/[A-Za-z0-9_-]+\.pcm$/,
  /^\/nachricht\/n\d+\.pcm$/, /^\/album\//, /^\/fragen\.txt$/];

export default {
  async fetch(req, env, ctx) {
    if (!env.ELTERN_PASSWORT || !env.GERAET_SCHLUESSEL) return text("Portal noch nicht eingerichtet (Geheimnisse fehlen)\n", 503);
    const url = new URL(req.url), path = url.pathname, q = Object.fromEntries(url.searchParams);
    try {
      // Gerät
      if (DEVICE_PATHS.some((r) => r.test(path)) || (path === "/api/nachricht" && req.method === "POST")) {
        if (!(await deviceOk(env, req))) return text("Schlüssel fehlt\n", 401);
        return await device(env, req, path, q, ctx);
      }
      // Anmeldung
      if (path === "/anmelden" && req.method === "POST") {
        const form = await req.formData();
        await new Promise((r) => setTimeout(r, 400));   // Raten bremsen
        if (!(await same(String(form.get("passwort") || ""), env.ELTERN_PASSWORT)))
          return new Response(LOGIN("Das Passwort stimmt nicht."), { status: 401, headers: { "Content-Type": "text/html; charset=utf-8" } });
        const exp = String(Math.floor(Date.now() / 1000) + 60 * 86400);
        return new Response(null, { status: 302, headers: { Location: "/", "Set-Cookie": `s=${exp}.${await sign(env, exp)}; Path=/; Max-Age=${60 * 86400}; HttpOnly; Secure; SameSite=Strict` } });
      }
      if (path === "/abmelden") return new Response(null, { status: 302, headers: { Location: "/anmelden", "Set-Cookie": "s=; Path=/; Max-Age=0; HttpOnly; Secure; SameSite=Strict" } });
      if (!(await parentOk(env, req))) {
        if (path === "/" || path === "/anmelden") return new Response(LOGIN(), { headers: { "Content-Type": "text/html; charset=utf-8" } });
        return text("Bitte anmelden\n", 401);
      }
      return await parent(env, req, path, q);
    } catch (e) {
      return req.method === "POST" ? fail(String(e.message || e)) : text("Fehler: " + (e.message || e) + "\n", 500);
    }
  },
};

async function device(env, req, path, q, ctx) {
  if (path === "/api/geraet") {
    deviceSeen(env, q, ctx);
    const o = await env.DATEN.get("antwort.txt");
    const body = o ? await o.text() : await rebuild(env);
    return text(`zeit ${localEpoch(env.ZEITZONE)}\n` + body);
  }
  if (path === "/api/radio") {
    const hint = q.aktion === "play" && String(q.quelle || "").startsWith("sp-") ? "Spotify gibt es nur über das Mac-Portal" : "";
    return text(hint ? "hinweis " + hint + "\n" : "ok\n");
  }
  if (path === "/api/sperre") {
    const cfg = await loadCfg(env);
    cfg.sperre.modus = Math.max(0, Math.min(2, +q.modus || 0));
    await putJson(env, "cfg.json", cfg);
    await rebuild(env);
    return text("ok\n");
  }
  if (path === "/api/apps") {
    const cfg = await loadCfg(env);
    cfg.apps_aus = (+q.aus || 0) & 0x1fe;
    await putJson(env, "cfg.json", cfg);
    await rebuild(env);
    return text("ok\n");
  }
  if (path === "/api/nachricht/gehoert") {
    const idx = await getJson(env, "nachrichten/index.json", []);
    for (const m of idx) if (m.id === q.id && m.richtung === "an_kind") m.gehoert = true;
    await putJson(env, "nachrichten/index.json", idx);
    await rebuild(env);
    return text("ok\n");
  }
  if (path === "/api/nachricht") {   // Aufnahme des Kindes (16 kHz mono PCM)
    const pcm = new Uint8Array(await req.arrayBuffer());
    await addMessage(env, "vom_kind", String(q.an || "").replace(/[^a-z0-9]/g, "").slice(0, 8), pcm);
    return text("ok\n");
  }
  let m;
  if ((m = /^\/radio\/([A-Za-z0-9_-]+)\.pcm$/.exec(path))) return blob(env, `aufnahmen/${m[1]}.pcm`, "application/octet-stream");
  if ((m = /^\/nachricht\/(n\d+)\.pcm$/.exec(path))) return blob(env, `nachrichten/${m[1]}.pcm`, "application/octet-stream");
  if (path === "/fragen.txt") return text(quizText(await loadFragen(env)));
  if (path === "/album/karten.txt") {
    const [cfg, alben] = await Promise.all([loadCfg(env), getJson(env, "alben.json", {})]);
    const a = alben[cfg.album];
    return a ? text(albumText(a)) : text("kein Thema\n", 404);
  }
  if ((m = /^\/album\/bild\/(\d+)\.jpg$/.exec(path))) {
    const cfg = await loadCfg(env);
    return blob(env, `alben/${cfg.album}/${m[1]}.jpg`, "image/jpeg");
  }
  return text("nicht gefunden\n", 404);
}

async function addMessage(env, richtung, kontakt, pcm) {
  const idx = await getJson(env, "nachrichten/index.json", []);
  const n = Math.max(0, ...idx.map((m) => +m.id.slice(1) || 0)) + 1, id = `n${n}`;
  await env.DATEN.put(`nachrichten/${id}.pcm`, pcm);
  idx.unshift({ id, richtung, kontakt, zeit: Math.floor(Date.now() / 1000), gehoert: false, sekunden: Math.round(pcm.length / 2 / RATE * 10) / 10 });
  for (const old of idx.slice(60)) await env.DATEN.delete(`nachrichten/${old.id}.pcm`);   // die letzten 60 behalten
  await putJson(env, "nachrichten/index.json", idx.slice(0, 60));
  await rebuild(env);
  return id;
}

async function parent(env, req, path, q) {
  let m;
  if (req.method === "GET") {
    if (path === "/") return new Response(html, { headers: { "Content-Type": "text/html; charset=utf-8", "Cache-Control": "no-store" } });
    if (path === "/api/status") return json(await status(env));
    if (path === "/api/alben") {
      const [cfg, alben] = await Promise.all([loadCfg(env), getJson(env, "alben.json", {})]);
      return json({ aktiv: cfg.album, alben: Object.values(alben).map((a) => ({ ...a, karten: a.karten.map((k) => ({ ...k, bild: !!k.bild })) })), max: MAX_CARDS });
    }
    if (path === "/api/fragen") {
      const fragen = await loadFragen(env);
      const probleme = fragen.flatMap((f, i) => quizProblems(f).map((p) => `Frage ${i + 1}: ${p}`));
      return json({ fragen, kategorien: [...new Set(fragen.map((f) => f.kategorie))].sort(), limits: LIMITS, probleme });
    }
    if (path === "/api/geraet-schluessel") return json({ schluessel: env.GERAET_SCHLUESSEL, adresse: new URL(req.url).origin });
    if ((m = /^\/alben\/([a-z0-9-]+)\/(\d+)\.jpg$/.exec(path))) return blob(env, `alben/${m[1]}/${m[2]}.jpg`, "image/jpeg");
    if ((m = /^\/nachricht\/(n\d+)\.wav$/.exec(path))) return wav(env, `nachrichten/${m[1]}.pcm`);
    if ((m = /^\/vorschau\/([A-Za-z0-9_-]+)\.wav$/.exec(path))) return wav(env, `aufnahmen/${m[1]}.pcm`);
    if (path.startsWith("/api/spotify") || path.startsWith("/spotify/")) return text("Spotify gibt es nur über das Mac-Portal\n", 400);
    return text("nicht gefunden\n", 404);
  }

  // Uploads: der Browser hat schon umgewandelt (16 kHz mono PCM bzw. fertiges Karten-JPEG)
  if (path === "/api/aufnahme") {
    if (q.typ !== "pcm16") return fail("Bitte die Seite neu laden (Umwandlung im Browser fehlt)");
    const pcm = new Uint8Array(await req.arrayBuffer());
    const cfg = await loadCfg(env);
    const used = new Set(cfg.quellen.map((s) => s.id));
    let i = 1;
    while (used.has(`a${i}`)) i++;
    const id = `a${i}`;
    await env.DATEN.put(`aufnahmen/${id}.pcm`, pcm);
    cfg.quellen.push({ id, art: "aufnahme", name: cleanName(q.name), sekunden: Math.round(pcm.length / 2 / RATE), bytes: pcm.length });
    await putJson(env, "cfg.json", cfg);
    await rebuild(env);
    return json({ ok: true, id });
  }
  if (path === "/api/nachricht/senden") {
    if (q.typ !== "pcm16") return fail("Bitte die Seite neu laden (Umwandlung im Browser fehlt)");
    const cfg = await loadCfg(env);
    if (!cfg.kontakte.some((k) => k.id === q.von)) return fail("Bitte auswählen, von wem die Nachricht ist");
    const id = await addMessage(env, "an_kind", q.von, new Uint8Array(await req.arrayBuffer()));
    return json({ ok: true, id });
  }
  if (path === "/api/album/bild") {
    const alben = await getJson(env, "alben.json", {});
    const a = alben[q.album], kid = +q.karte;
    const k = a && a.karten.find((x) => x.id === kid);
    if (!k) return fail("Karte nicht gefunden");
    if (q.typ !== "karte") return fail("Bitte die Seite neu laden (Bildumwandlung im Browser fehlt)");
    await env.DATEN.put(`alben/${a.id}/${kid}.jpg`, await req.arrayBuffer(), { httpMetadata: { contentType: "image/jpeg" } });
    k.bild = Date.now();
    await putJson(env, "alben.json", alben);
    await rebuild(env);
    return json({ ok: true });
  }

  const data = await req.json().catch(() => ({}));
  const cfg = await loadCfg(env);
  let reply = { ok: true };
  switch (path) {
    case "/api/einstellungen":
      if (data.sperre) cfg.sperre = { modus: Math.max(0, Math.min(2, +data.sperre.modus)), von: (+data.sperre.von) % 1440, bis: (+data.sperre.bis) % 1440 };
      if ("laut" in data) cfg.laut = Math.max(10, Math.min(100, +data.laut));
      if ("schlaf" in data) cfg.schlaf = Math.max(0, Math.min(180, +data.schlaf));
      if ("zufall" in data) cfg.zufall = !!data.zufall;
      if ("apps_aus" in data) cfg.apps_aus = (+data.apps_aus) & 0x1fe;
      if ("nachtlicht" in data) cfg.nachtlicht = !!data.nachtlicht;
      if (data.wecker) {
        const w = data.wecker;
        const ton = w.ton && w.ton !== "melodie" && cfg.quellen.some((s) => s.id === w.ton) ? w.ton : "melodie";
        cfg.wecker = { an: !!w.an, min: (+w.min || 420) % 1440, tage: (+w.tage) & 0x7f, ton };
      }
      break;
    case "/api/quelle/loeschen":
      cfg.quellen = cfg.quellen.filter((s) => s.id !== data.id);
      await env.DATEN.delete(`aufnahmen/${String(data.id).replace(/[^A-Za-z0-9_-]/g, "")}.pcm`);
      break;
    case "/api/quelle/umbenennen": {
      const s = cfg.quellen.find((x) => x.id === data.id);
      if (s) s.name = cleanName(data.name);
      break;
    }
    case "/api/quelle/reihenfolge": {
      const order = Object.fromEntries((data.ids || []).map((id, i) => [id, i]));
      cfg.quellen.sort((a, b) => (order[a.id] ?? 999) - (order[b.id] ?? 999));
      break;
    }
    case "/api/kontakt": {
      if (cfg.kontakte.length >= MAX_CONTACTS) return fail(`Höchstens ${MAX_CONTACTS} Kontakte`);
      const used = new Set(cfg.kontakte.map((k) => k.id));
      let i = 1;
      while (used.has(`k${i}`)) i++;
      cfg.kontakte.push({ id: `k${i}`, name: cleanName(data.name, 16) });
      break;
    }
    case "/api/kontakt/loeschen":
      cfg.kontakte = cfg.kontakte.filter((k) => k.id !== data.id);
      break;
    case "/api/nachricht/loeschen":
    case "/api/nachricht/gelesen": {
      let idx = await getJson(env, "nachrichten/index.json", []);
      if (path.endsWith("loeschen")) { idx = idx.filter((x) => x.id !== data.id); await env.DATEN.delete(`nachrichten/${String(data.id).replace(/[^a-z0-9]/g, "")}.pcm`); }
      else for (const x of idx) if (x.id === data.id) x.gehoert = true;
      await putJson(env, "nachrichten/index.json", idx);
      break;
    }
    case "/api/album/speichern":
    case "/api/album/aktiv":
    case "/api/album/loeschen":
    case "/api/album/karte":
    case "/api/album/karte/loeschen":
      reply = await albumPost(env, cfg, path, data);
      if (!reply.ok) return json(reply, 400);
      break;
    case "/api/frage": {
      const fragen = await loadFragen(env);
      const i = data.index;
      const fq = { kategorie: field(data.kategorie, 24), schwierigkeit: data.schwierigkeit || "leicht", frage: field(data.frage, 110),
        antworten: (data.antworten || []).slice(0, 3).map((a) => field(a, 30)), richtig: 0, erklaerung: field(data.erklaerung, 170) };
      if (data.bild) fq.bild = field(data.bild, 24);
      const probs = quizProblems(fq);
      if (fragen.some((f, k) => k !== i && f.frage.trim().toLowerCase() === fq.frage.trim().toLowerCase())) probs.push("Diese Frage gibt es schon");
      if (probs.length) return fail(probs.join(" · "));
      if (i === null || i === undefined) fragen.push(fq);
      else fragen[+i] = fq;
      await putJson(env, "fragen.json", fragen);
      break;
    }
    case "/api/frage/loeschen": {
      const fragen = await loadFragen(env);
      fragen.splice(+data.index, 1);
      await putJson(env, "fragen.json", fragen);
      break;
    }
    case "/api/fragen/zuruecksetzen":
      await env.DATEN.delete("fragen.json");
      break;
    default:
      if (path.startsWith("/api/spotify")) return fail("Spotify gibt es nur über das Mac-Portal");
      return fail("nicht gefunden", 404);
  }
  await putJson(env, "cfg.json", cfg);
  await rebuild(env);
  return json(reply);
}

async function albumPost(env, cfg, path, data) {
  const alben = await getJson(env, "alben.json", {});
  let reply = { ok: true };
  if (path === "/api/album/speichern") {
    let id = data.id || slug(data.symbol || data.titel);
    let a = data.id ? alben[data.id] : null;
    if (!a) {
      if (id === "pferde" || alben[id]) id = id.slice(0, 16) + "-" + crypto.getRandomValues(new Uint16Array(1))[0].toString(16);
      a = alben[id] = { id, karten: [] };
    }
    a.titel = field(data.titel, 28) || "Sammelalbum";
    a.symbol = field(data.symbol, 12) || "Album";
    a.zaehlwort = field(data.zaehlwort, 14) || "Karten";
    a.frage = field(data.frage, 40) || "Wer ist das?";
    a.ueberschrift3 = field(data.ueberschrift3, 20);
    a.ueberschrift4 = field(data.ueberschrift4, 20);
    reply = { ok: true, id };
  } else if (path === "/api/album/aktiv") {
    if (data.id !== "pferde" && !alben[data.id]) return { ok: false, fehler: "Thema nicht gefunden" };
    cfg.album = data.id;
  } else if (path === "/api/album/loeschen") {
    const a = alben[data.id];
    if (a) for (const k of a.karten) await env.DATEN.delete(`alben/${a.id}/${k.id}.jpg`);
    delete alben[data.id];
    if (cfg.album === data.id) cfg.album = "pferde";
  } else if (path === "/api/album/karte") {
    const a = alben[data.album];
    if (!a) return { ok: false, fehler: "Thema nicht gefunden" };
    let k = a.karten.find((x) => x.id === +data.id);
    if (!k) {
      if (a.karten.length >= MAX_CARDS) return { ok: false, fehler: `Höchstens ${MAX_CARDS} Karten pro Thema` };
      k = { id: Math.max(0, ...a.karten.map((x) => x.id)) + 1 };
      a.karten.push(k);
    }
    k.name = field(data.name, 30) || "Ohne Namen";
    for (const [key, n] of [["untertitel", 30], ["zeile1", 60], ["zeile2", 40], ["text3", 90], ["text4", 90], ["wusstest", 160], ["foto", 90]]) k[key] = field(data[key], n);
    reply = { ok: true, id: k.id };
  } else if (path === "/api/album/karte/loeschen") {
    const a = alben[data.album];
    if (a) { a.karten = a.karten.filter((x) => x.id !== +data.id); await env.DATEN.delete(`alben/${a.id}/${+data.id}.jpg`); }
  }
  await putJson(env, "alben.json", alben);
  return reply;
}

async function status(env) {
  const [cfg, dev, post] = await Promise.all([loadCfg(env), getJson(env, "geraet.json", { seen: 0, params: {} }), getJson(env, "nachrichten/index.json", [])]);
  return {
    cloud: true,
    geraet: { online: Date.now() / 1000 - dev.seen < 90, zuletzt: dev.seen, werte: dev.params },
    sperre: cfg.sperre, laut: cfg.laut, schlaf: cfg.schlaf, zufall: cfg.zufall, quellen: cfg.quellen,
    apps: APPS.map(([i, n]) => ({ index: i, name: n, an: !((cfg.apps_aus >> i) & 1) })),
    kontakte: cfg.kontakte, nachrichten: post.slice(0, 30), wecker: cfg.wecker, nachtlicht: cfg.nachtlicht,
    spotify: { client_id: "", verbunden: false, librespot: "in der Cloud nicht verfügbar", hinweis: "" },
    uhr: "",
  };
}
