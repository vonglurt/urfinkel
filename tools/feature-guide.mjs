// tools/feature-guide.mjs - the feature guide's pictures, taken from the play page.
//
//   node tools/feature-guide.mjs            -> docs/features/NN-name.png
//   node tools/feature-guide.mjs --port 8200 --keep --minutes 40
//
// WHAT IT DOES.  Serves docs/index.html the way tools/dist.sh builds its
// offline copy - the game from build/urfinkel.prg, the emulator from
// vendor/emulatorjs/, nothing from the network - opens it in headless
// Chromium, presses Play, and plays through the game a person would: the
// cabinet, the rules screen, a match against URBOT to the trophy, a manual
// match left with x x x, and the demo.  Every key goes in through the page's
// own on-screen keys (the same buttons a phone gets), and names are typed as
// keystrokes aimed at the emulator's canvas, as the page's SEND button does.
//
// HOW IT KNOWS WHERE THE GAME IS.  It reads the Plus/4's own screen.  The
// emulator's RAM is a block of the WebAssembly heap; the program's code is
// found in it by searching for a stretch of build/urfinkel.prg, which fixes
// where $0000 is, and screen memory at $0C00 is then decoded to text.  So
// "wait for the piece prompt" is a wait for the words the game prints, not a
// guess at how long a turn takes.
//
// THE PICTURES.  The Plus/4's frame is taken from the emulator at its native
// 384x288 (EmulatorJS's own screenshot, before any scaling), shown three
// times the size with image-rendering:pixelated, ringed and numbered, and
// captured - so every pixel is a 3x3 block and nothing is smoothed.  The
// pages around it are ordinary page screenshots.
//
// A match is random: a re-run plays a different game and the pictures differ.
// Captures, a piece borne off and the win are caught when they happen; if the
// match does not reach one in --minutes, that picture is skipped and said so.
//
// Needs node 22+ and chromium.  Zero dependencies.  Ports: --port (http) and
// --port + 1 (DevTools).
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import http from 'node:http';

const ROOT = path.resolve(path.dirname(new URL(import.meta.url).pathname), '..');
const arg = (k, d = null) => { const i = process.argv.indexOf(`--${k}`); return i < 0 ? d : (process.argv[i + 1] && !process.argv[i + 1].startsWith('--') ? process.argv[i + 1] : true); };
const PORT = Number(arg('port', 8200)), CDP = PORT + 1, BASE = `http://127.0.0.1:${PORT}`;
const MINUTES = Number(arg('minutes', 40));
const SHOTS = path.resolve(arg('out', path.join(ROOT, 'docs', 'features')));
const SCALE = 3, PAD = 24, SIDE = 360;           // native frame shown at 3x, captions beside it
const W = 1600, H = 960;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const say = (...a) => console.log(...a);
const t00 = Date.now();
const clock = () => `${((Date.now() - t00) / 1000).toFixed(0).padStart(4)}s`;

// -- the stage: the play page made local, as dist.sh makes index-offline.html
const BOX = fs.mkdtempSync(path.join(os.tmpdir(), 'urfinkel-features-'));
function buildStage() {
  const skip = (s) => !path.basename(s).startsWith('._');
  fs.cpSync(path.join(ROOT, 'vendor/emulatorjs'), path.join(BOX, 'emulatorjs'), { recursive: true, filter: skip });
  fs.copyFileSync(path.join(ROOT, 'build/urfinkel.prg'), path.join(BOX, 'urfinkel.prg'));
  let html = fs.readFileSync(path.join(ROOT, 'docs/index.html'), 'utf8');
  const sub = (re, to, what) => { const n = (html.match(new RegExp(re.source, re.flags.includes("g") ? re.flags : re.flags + "g")) || []).length; if (n !== 1) throw new Error(`stage: expected one ${what}, found ${n}`); html = html.replace(re, to); };
  sub(/(EJS_gameUrl\s*=\s*)"[^"]*";/, '$1"urfinkel.prg";', 'EJS_gameUrl');
  sub(/(EJS_pathtodata\s*=\s*)"[^"]*";/, '$1"emulatorjs/";', 'EJS_pathtodata');
  sub(/(s\.src\s*=\s*)"https:\/\/cdn\.emulatorjs\.org\/[^"]*";/, '$1"emulatorjs/loader.js";', 'CDN loader URL');
  // The video embed would reach for youtube.com; dist.sh removes it too.
  sub(/<div class="tube">.*?<\/div>/s, '<p class="sub">(video not embedded in this copy)</p>', 'video embed');
  // EmulatorJS's update check is a hard-coded fetch of its CDN; answer it here.
  const ver = JSON.parse(fs.readFileSync(path.join(ROOT, 'vendor/emulatorjs/version.json'), 'utf8')).version;
  sub(/  function loadEmulator\(/, `  (function(){var real=window.fetch;window.fetch=function(i){var u=(typeof i==="string")?i:(i&&i.url)||"";if(u.indexOf("cdn.emulatorjs.org")!==-1){return Promise.resolve(new Response(JSON.stringify({version:${JSON.stringify(ver)}}),{status:200,headers:{"Content-Type":"application/json"}}));}return real.apply(this,arguments);};})();\n  function loadEmulator(`, 'loadEmulator');
  fs.writeFileSync(path.join(BOX, 'index.html'), html);
}

const MIME = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript', '.css': 'text/css', '.json': 'application/json', '.png': 'image/png', '.wasm': 'application/wasm' };
let server;
function serve() {
  server = http.createServer((req, res) => {
    const p = path.join(BOX, decodeURIComponent(new URL(req.url, BASE).pathname));
    if (!p.startsWith(BOX) || !fs.existsSync(p) || fs.statSync(p).isDirectory()) { res.writeHead(404); return res.end(); }
    res.writeHead(200, { 'content-type': MIME[path.extname(p)] || 'application/octet-stream', 'cache-control': 'no-store' });
    fs.createReadStream(p).pipe(res);
  });
  return new Promise((r) => server.listen(PORT, '127.0.0.1', r));
}

// -- the browser ----------------------------------------------------------
// WebGL is required: EmulatorJS has no 2D renderer.  Headless Chromium on a
// Linux box without a GPU and without SwiftShader (Alpine ships none) gets
// it from Mesa's lavapipe through ANGLE's Vulkan backend.  Elsewhere the
// default is left alone.
let chrome;
function launch() {
  const lvp = ['/usr/share/vulkan/icd.d'].flatMap((d) => (fs.existsSync(d) ? fs.readdirSync(d).filter((f) => f.startsWith('lvp_icd')).map((f) => path.join(d, f)) : []))[0];
  const gl = arg('gl', lvp ? 'vulkan' : 'default');
  const flags = ['--headless=new', '--no-sandbox', `--remote-debugging-port=${CDP}`, `--user-data-dir=${path.join(BOX, 'chrome')}`,
    `--window-size=${W},${H}`, '--hide-scrollbars', '--no-first-run', '--disable-extensions', '--mute-audio',
    '--autoplay-policy=no-user-gesture-required', '--ignore-gpu-blocklist',
    // A tab the browser thinks is in the background gets no animation frames,
    // and the emulator runs on animation frames.
    '--disable-background-timer-throttling', '--disable-renderer-backgrounding', '--disable-backgrounding-occluded-windows',
    ...(gl === 'vulkan' ? ['--use-angle=vulkan', '--enable-features=Vulkan', '--enable-unsafe-swiftshader'] : []), 'about:blank'];
  const env = { ...process.env, ...(gl === 'vulkan' && lvp ? { VK_ICD_FILENAMES: lvp } : {}) };
  chrome = spawn(process.env.CHROMIUM || 'chromium', flags, { env, stdio: 'ignore', detached: true });
  say(`chromium: WebGL via ${gl}`);
}
function stop() {
  try { process.kill(-chrome.pid, 'SIGKILL'); } catch { try { chrome.kill('SIGKILL'); } catch {} }
  try { server.close(); } catch {}
  if (!arg('keep') && !process.exitCode) fs.rmSync(BOX, { recursive: true, force: true }); else say(`kept ${BOX}`);
}

let ws, seq = 0; const pending = new Map(), pageErrors = [];
const send = (method, params = {}) => new Promise((res) => { const i = ++seq; pending.set(i, res); ws.send(JSON.stringify({ id: i, method, params })); });
const ev = async (expr) => {
  const r = await send('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: true });
  if (r.result?.exceptionDetails) throw new Error('page: ' + (r.result.exceptionDetails.exception?.description || r.result.exceptionDetails.text).slice(0, 300) + '\n  in: ' + expr.slice(0, 160));
  return r.result?.result?.value;
};
async function connect() {
  let target;
  for (let i = 0; i < 100 && !target; i++) {
    await sleep(200);
    try { target = await (await fetch(`http://127.0.0.1:${CDP}/json/new?about:blank`, { method: 'PUT' })).json(); } catch {}
  }
  if (!target) throw new Error('chromium did not answer on the DevTools port');
  await fetch(`http://127.0.0.1:${CDP}/json/activate/${target.id}`);     // the foreground tab
  ws = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((r, j) => { ws.addEventListener('open', r); ws.addEventListener('error', j); });
  ws.addEventListener('message', (m) => {
    const d = JSON.parse(m.data);
    if (d.id && pending.has(d.id)) { pending.get(d.id)(d); pending.delete(d.id); }
    if (d.method === 'Runtime.exceptionThrown') pageErrors.push((d.params.exceptionDetails.exception?.description || d.params.exceptionDetails.text || '').split('\n')[0].slice(0, 160));
  });
  await send('Runtime.enable'); await send('Page.enable');
  await send('Emulation.setDeviceMetricsOverride', { width: W, height: H, deviceScaleFactor: 1, mobile: false });
}

// -- the page's helpers, injected once it has loaded ------------------------
const PRG = fs.readFileSync(path.join(ROOT, 'build/urfinkel.prg'));
const LOAD = PRG[0] | (PRG[1] << 8);
const SIG_OFF = 0x300;                         // a stretch of code, well past the start-up stub
const SIG = [...PRG.subarray(2 + SIG_OFF, 2 + SIG_OFF + 32)];
const HELPERS = `
window.__sig=${JSON.stringify(SIG)}; window.__sigAddr=${LOAD + SIG_OFF}; window.__ramBase=-1;
window.__findRam=()=>{
  const H=EJS_emulator.Module.HEAPU8, s=__sig, n=H.length-s.length; const found=[];
  for(let i=0;i<n;i++){ if(H[i]!==s[0])continue; let k=1; while(k<s.length&&H[i+k]===s[k])k++; if(k===s.length)found.push(i-__sigAddr); }
  return found;
};
window.__decode=(c)=>{c&=0x7f; if(c===0)return '@'; if(c<=26)return String.fromCharCode(96+c); if(c===32)return ' '; if(c>32&&c<64)return String.fromCharCode(c); return '#';};
window.__screen=(base)=>{ const H=EJS_emulator.Module.HEAPU8, b=(base===undefined?__ramBase:base)+0x0C00, rows=[];
  for(let r=0;r<25;r++){let s='';for(let c=0;c<40;c++)s+=__decode(H[b+r*40+c]);rows.push(s);} return rows; };
window.__raw=(r0,r1)=>{ const H=EJS_emulator.Module.HEAPU8, b=__ramBase+0x0C00; return Array.from(H.subarray(b+r0*40,b+(r1+1)*40)); };
window.__frame=()=>EJS_emulator.gameManager.screenshot().then(u=>{let s='';for(let i=0;i<u.length;i+=8192)s+=String.fromCharCode.apply(null,u.subarray(i,i+8192));return btoa(s);});
window.__frameNo=()=>{try{return EJS_emulator.gameManager.functions.getFrameNum()}catch(e){return -1}};
window.__tap=(k)=>{const el=[...document.querySelectorAll('#pad .key[data-key]')].find(e=>e.getAttribute('data-key')===k); if(!el)return false;
  el.dispatchEvent(new PointerEvent('pointerdown',{bubbles:true,cancelable:true,pointerType:'touch'})); return true;};
window.__key=(type,ch)=>{ // the page's own sendKey, for letters it has no button for
  const l=ch.toLowerCase(), code=(l>='a'&&l<='z')?'Key'+l.toUpperCase():(l>='0'&&l<='9')?'Digit'+l:l===' '?'Space':ch, which=(l>='a'&&l<='z')?l.toUpperCase().charCodeAt(0):(ch==='Enter'?13:l.charCodeAt(0));
  const ev=new KeyboardEvent(type,{key:ch==='Enter'?'Enter':l,code:ch==='Enter'?'Enter':code,keyCode:which,which:which,bubbles:true,cancelable:true,composed:true,view:window});
  if(ev.keyCode!==which){Object.defineProperty(ev,'keyCode',{get:()=>which});Object.defineProperty(ev,'which',{get:()=>which});}
  (document.querySelector('#game canvas')||document.getElementById('game')).dispatchEvent(ev);};
window.__clear=()=>document.querySelectorAll('.__g').forEach(e=>e.remove());
// Rings and numbered captions over an element or a box; captions are placed
// where they cover the fewest rings and other captions, inside 'bounds'.
window.__ring=(items,bounds,side)=>{
  const B=bounds||{x:0,y:0,w:innerWidth,h:innerHeight}, rings=[], caps=[], found=[];
  const over=(a,b)=>Math.max(0,Math.min(a.x+a.w,b.x+b.w)-Math.max(a.x,b.x))*Math.max(0,Math.min(a.y+a.h,b.y+b.h)-Math.max(a.y,b.y));
  items.forEach(([t])=>{
    let r=null; if(t&&t.getBoundingClientRect){const q=t.getBoundingClientRect(); r={x:q.left,y:q.top,w:q.width,h:q.height};} else if(t&&'x' in t) r=t;
    if(!r){found.push(false);rings.push(null);return;} found.push(true);
    const x=Math.max(B.x+3,r.x-4), y=Math.max(B.y+3,r.y-4), w=Math.min(B.x+B.w-3-x,r.w+8), h=Math.min(B.y+B.h-3-y,r.h+8);
    const ring=document.createElement('div'); ring.className='__g';
    ring.style.cssText='position:fixed;z-index:2147483646;pointer-events:none;border:3px solid #c2185b;border-radius:4px;box-shadow:0 0 0 2px #fff;left:'+x+'px;top:'+y+'px;width:'+w+'px;height:'+h+'px;box-sizing:border-box';
    document.body.appendChild(ring); rings.push({x,y,w,h});
  });
  items.forEach(([t,text],i)=>{
    const g=rings[i]; if(!g)return;
    const cap=document.createElement('div'); cap.className='__g';
    const n=document.createElement('b'); n.textContent=String(i+1);
    n.style.cssText='display:inline-block;background:#fff;color:#c2185b;border-radius:50%;width:20px;height:20px;line-height:20px;text-align:center;margin-right:7px';
    cap.appendChild(n); cap.appendChild(document.createTextNode(text));
    cap.style.cssText='position:fixed;z-index:2147483647;pointer-events:none;max-width:330px;background:#c2185b;color:#fff;font:600 14px/1.35 system-ui,sans-serif;padding:6px 10px;border-radius:4px;box-shadow:0 2px 8px rgba(0,0,0,.5);left:0;top:0';
    document.body.appendChild(cap);
    if(side){ // a column beside the picture, each caption level with its ring, stacked
      const badge=document.createElement('b'); badge.className='__g'; badge.textContent=String(i+1);
      badge.style.cssText='position:fixed;z-index:2147483647;background:#c2185b;color:#fff;border:2px solid #fff;border-radius:50%;width:22px;height:22px;line-height:22px;text-align:center;font:700 13px system-ui,sans-serif;left:'+(g.x-9)+'px;top:'+(g.y-9)+'px';
      document.body.appendChild(badge);
      cap.style.maxWidth=side.w+'px'; const ch=cap.getBoundingClientRect().height;
      const prev=caps.length?caps[caps.length-1]:null; let y=Math.max(g.y, prev?prev.y+prev.h+10:B.y+4); y=Math.min(y,B.y+B.h-ch-4);
      cap.style.left=side.x+'px'; cap.style.top=y+'px'; caps.push({x:side.x,y,w:side.w,h:ch}); return; }
    const c=cap.getBoundingClientRect(), cw=c.width, chh=c.height, m=8;
    const spots=[[g.x,g.y+g.h+m],[g.x+g.w-cw,g.y+g.h+m],[g.x,g.y-chh-m],[g.x+g.w-cw,g.y-chh-m],[g.x+g.w+m,g.y],[g.x-cw-m,g.y],
      [g.x+g.w-cw-m,g.y+g.h-chh-m],[g.x+g.w-cw-m,g.y+m],[g.x+m,g.y+g.h-chh-m],[g.x+m,g.y+m]];
    let best=null;
    spots.forEach(([sx,sy],k)=>{
      const fits=sx>=B.x+4&&sy>=B.y+4&&sx+cw<=B.x+B.w-4&&sy+chh<=B.y+B.h-4;
      const x=Math.min(Math.max(B.x+4,sx),B.x+B.w-cw-4), y=Math.min(Math.max(B.y+4,sy),B.y+B.h-chh-4), box={x,y,w:cw,h:chh};
      const cost=rings.reduce((a,r,j)=>a+(r?over(box,r)*(j===i?(k>=6?0.05:1):3):0),0)+caps.reduce((a,r)=>a+over(box,r)*10,0)+(fits?0:500)+k;
      if(!best||cost<best.cost)best={x,y,cost};
    });
    cap.style.left=best.x+'px'; cap.style.top=best.y+'px'; caps.push({x:best.x,y:best.y,w:cw,h:chh});
  });
  return found;
};
// The light table: a native frame at ${SCALE}x, pixel for pixel, over the page.
window.__plate=(b64)=>new Promise((res)=>{
  __clear(); const d=document.createElement('div'); d.className='__g';
  d.style.cssText='position:fixed;left:0;top:0;z-index:2147483640;background:#16120a;padding:${PAD}px ${PAD + SIDE}px ${PAD}px ${PAD}px;';
  const im=new Image(); im.onload=()=>{ im.style.cssText='display:block;image-rendering:pixelated;width:'+(im.naturalWidth*${SCALE})+'px;height:'+(im.naturalHeight*${SCALE})+'px';
    d.appendChild(im); document.body.appendChild(d); const r=d.getBoundingClientRect(); res({x:r.left,y:r.top,w:r.width,h:r.height}); };
  im.src='data:image/png;base64,'+b64; });
`;

// -- reading the machine ---------------------------------------------------
async function screen() { return (await ev('__screen()')) || []; }
const has = (rows, s) => rows.some((r) => r.includes(s));
async function waitText(s, ms = 30000, what = s) {
  const t0 = Date.now();
  while (Date.now() - t0 < ms) { const rows = await screen(); if (typeof s === 'function' ? s(rows) : has(rows, s)) return rows; await sleep(150); }
  throw new Error(`timed out waiting for: ${what}`);
}
async function tap(k, gap = 350) { if (!(await ev(`__tap(${JSON.stringify(k)})`))) throw new Error(`no on-screen key ${k}`); await sleep(gap); }
async function hold(k, ms) { await ev(`__key('keydown',${JSON.stringify(k)})`); await sleep(ms); await ev(`__key('keyup',${JSON.stringify(k)})`); await sleep(300); }
// x x x, checked: three in a row leaves a match, and a press that falls
// between two scans of the keyboard has to be made again.
async function leave() {
  for (let i = 0; i < 9; i++) {
    const rows = await screen();
    if (has(rows, 'leaving the game') || has(rows, 'royal game of ur') && !has(rows, 'wins the')) return;
    await hold('x', 250); await sleep(500);
  }
}
async function type(str) { for (const ch of str) { await ev(`__key('keydown',${JSON.stringify(ch)})`); await sleep(70); await ev(`__key('keyup',${JSON.stringify(ch)})`); await sleep(90); } }

// -- the pictures ----------------------------------------------------------
fs.mkdirSync(SHOTS, { recursive: true });
let shotN = 0; const made = [];
const cell = (c0, r0, c1 = c0, r1 = r0) => ({ c0, r0, c1, r1 });     // character cells, inclusive
// A frame from the emulator, ringed by character cell.  Text area: 320x200 at (32,40) in the 384x288 frame.
async function grab() { return ev('__frame()'); }
async function plate(name, b64, marks = []) {
  const box = await ev(`__plate(${JSON.stringify(b64)})`);
  const items = marks.map(([c, t]) => `[{x:${box.x + PAD + (32 + 8 * c.c0) * SCALE},y:${box.y + PAD + (40 + 8 * c.r0) * SCALE},w:${(c.c1 - c.c0 + 1) * 8 * SCALE},h:${(c.r1 - c.r0 + 1) * 8 * SCALE}},${JSON.stringify(t)}]`);
  await ev(`__ring([${items.join(',')}],${JSON.stringify(box)},${JSON.stringify({ x: box.x + box.w - SIDE - 4, w: SIDE - 20 })})`);
  await sleep(120);
  const file = `${String(++shotN).padStart(2, '0')}-${name}.png`;
  const r = await send('Page.captureScreenshot', { format: 'png', clip: { x: box.x, y: box.y, width: box.w, height: box.h, scale: 1 } });
  fs.writeFileSync(path.join(SHOTS, file), Buffer.from(r.result.data, 'base64'));
  await ev('__clear()');
  made.push(file); say(`${clock()}  ${file}`);
  return file;
}
async function machineShot(name, marks = []) { return plate(name, await grab(), marks); }
async function pageShot(name, marks = [], clip = null) {
  await ev(`__ring([${marks.map(([e, t]) => `[${e},${JSON.stringify(t)}]`).join(',')}])`);
  await sleep(200);
  const file = `${String(++shotN).padStart(2, '0')}-${name}.png`;
  const r = await send('Page.captureScreenshot', { format: 'png', ...(clip ? { clip: { ...clip, scale: 1 } } : {}) });
  fs.writeFileSync(path.join(SHOTS, file), Buffer.from(r.result.data, 'base64'));
  await ev('__clear()');
  made.push(file); say(`${clock()}  ${file}`);
  return file;
}
const q = (sel) => `document.querySelector(${JSON.stringify(sel)})`;
const log = []; const note = (s) => { log.push(s); say(`${clock()}  ${s}`); };
// The chronicle: rows 21-24, columns 2-38 (column 0 carries a marker).
const chron = (rows) => rows.slice(21, 25).map((r) => r.slice(2, 39).trim()).filter(Boolean);

// The board's squares, for reading where a piece would land.  Destinations in
// the legal list are path squares; 4, 8 and 14 are rosettes.
const ROSETTES = new Set([4, 8, 14]);
function choose(list) {
  // "1>4 2>h 3>9" - prefer bearing off, then a rosette, then the furthest
  // destination.  A player's habit, not URBOT's: it is only here to finish.
  const moves = [...list.matchAll(/([1-7])>(h|\d+)/g)].map((m) => ({ p: m[1], to: m[2] === 'h' ? 15 : Number(m[2]) }));
  if (!moves.length) return null;
  moves.sort((a, b) => (b.to === 15) - (a.to === 15) || ROSETTES.has(b.to) - ROSETTES.has(a.to) || b.to - a.to);
  return moves[0].p;
}

// -- the tour --------------------------------------------------------------
async function tour() {
  await send('Page.navigate', { url: `${BASE}/index.html` });
  await sleep(2500);
  await ev(HELPERS);

  // 1. The page as it arrives: nothing fetched yet.
  await ev('scrollTo(0,0)');
  await pageShot('play-page', [
    [q('#play'), 'Nothing is fetched until this is pressed: then the emulator loads and a Plus/4 boots the game.'],
    [q('#rescue'), 'If it crashes on a phone: silent, simple video, keep nothing. Remembered on this device.'],
    [q('nav.deck'), 'Downloads, how to play, the video, the project.'],
  ]);

  // Press Play.  "Keep nothing" is switched on first, as a visitor could: it
  // keeps the emulator out of IndexedDB, which a throwaway profile has no use for.
  await ev(`localStorage.setItem("urfinkel:lite", JSON.stringify({fresh:true})); 1`);
  await send('Page.reload'); await sleep(2500); await ev(HELPERS);
  await ev(`document.getElementById("play").click()`);
  const t0 = Date.now();
  while (!(await ev('!!(window.EJS_emulator&&EJS_emulator.started&&__frameNo()>20)'))) {
    if (Date.now() - t0 > 180000) throw new Error('the emulator did not start in three minutes');
    await sleep(500);
  }
  note(`emulator running after ${((Date.now() - t0) / 1000).toFixed(1)} s`);

  // Find the machine's RAM in the WebAssembly heap.
  let base = -1;
  for (let i = 0; i < 90 && base < 0; i++) {
    const cands = await ev('__findRam()');
    // The .prg is in the heap more than once (the loaded file is too), so the
    // copy that is the machine is the one whose screen shows the cabinet.
    for (const c of cands || []) { const rows = await ev(`__screen(${c})`); if (rows && rows.join('').includes('royal game of ur')) { base = c; break; } }
    if (base < 0) await sleep(1000);
  }
  if (base < 0) throw new Error('could not find the Plus/4 RAM in the heap');
  await ev(`__ramBase=${base}`);
  note(`Plus/4 RAM found in the heap at ${base}`);

  // 2. The way in: the laser cutting the title, if it is still on screen.
  await waitText('royal game of ur', 60000, 'the cabinet');
  await waitText('how we play', 30000, 'the menu to scroll in');
  await sleep(1500);
  await machineShot('cabinet', [
    [cell(5, 2, 34, 2), 'The marquee, between rails of chasing lamps.'],
    [cell(6, 12, 33, 16), 'Five modes: against URBOT, two players, two players with real lots, the demo, and the rules.'],
    [cell(6, 19, 33, 20), 'm cycles the music, s turns the effects off and on.'],
    [cell(6, 21, 33, 22), 'How to leave a match, and the build stamp.'],
  ]);

  // 3. The same moment as the page shows it, with the on-screen keys.
  await pageShot('page-running', [
    [q('#game'), 'A Plus/4, emulated: VICE in WebAssembly.'],
    [q('#pad .padrow'), 'The keys the game asks for, for a phone with none.'],
    [q('#typebox'), 'Names are typed here and played in one keystroke at a time.'],
    [q('#power'), 'Unload: a reload, the only thing that ends an emulator cleanly.'],
  ], { x: 0, y: 0, width: W, height: H });

  // 4. The rules screen.
  await tap('5', 200);
  await waitText('space = back', 20000, 'the rules screen');
  await sleep(800);
  await machineShot('rules', [
    [cell(0, 0, 39, 11), 'The path, numbered in the squares: 1-4 in your own lane, 5-12 the shared corridor, 13-14 back home.'],
    [cell(1, 13, 38, 22), 'The Finkel ruleset in ten lines.'],
    [cell(1, 24, 38, 24), 'Bearing off is exact.'],
  ]);
  await tap(' ');
  await waitText('how we play', 30000, 'back at the cabinet');
  await sleep(1500);

  // 5. A match against URBOT.
  await tap('1', 0);
  await waitText('you against the machine', 10000, 'the mode read back');
  await sleep(500);
  await machineShot('mode-read-back', [[cell(0, 13, 39, 17), 'The choice read back: a chime, the name cut by the laser, a line saying what it is.']]);
  await waitText('your name:', 20000, 'the name prompt');
  await sleep(400);
  await type('paul');
  await sleep(300);
  await machineShot('name', [[cell(2, 2, 30, 4), 'Up to eight letters, typed on the Plus/4. Return alone takes the name the game offers.']]);
  await tap('Enter');
  await waitText('colours for paul', 10000, 'the colour picker');
  await tap('1'); await tap('4'); await tap('Enter');      // 14: lt blue
  await waitText((r) => r[14].includes('shade'), 5000, 'the shade prompt');
  await tap('6'); await tap('Enter');
  await waitText('keep it?', 5000, 'the preview');
  await sleep(500);
  await machineShot('colour', [
    [cell(2, 2, 38, 9), 'Sixteen hues, each with a swatch.'],
    [cell(2, 11, 38, 14), 'Shades 3 to 6 only: the range the board itself is lit in.'],
    [cell(4, 16, 32, 20), 'The preview, drawn by the real renderers: the piece on a plaque beside a rosette.'],
  ]);
  await tap('y');

  // The opening throw.
  await waitText('higher hand opens', 30000, 'the opening throw');
  await waitText((r) => chron(r).some((l) => l.includes('higher and opens')), 120000, 'the opener');
  await sleep(300);
  await machineShot('opening', [
    [cell(0, 0, 39, 13), 'The board: URBOT holds the far side, you the near one, the shared corridor between.'],
    [cell(2, 21, 38, 24), 'The chronicle: four lines, scrolling. Each side throws; the higher hand opens.'],
  ]);

  // Play it out.
  const want = { yourTurn: false, urbot: false, capture: false, home: false, rosette: false };
  let lastPrompt = '', moves = 0;
  const deadline = Date.now() + MINUTES * 60000;
  let won = null;
  while (Date.now() < deadline) {
    const rows = await screen();
    const c = chron(rows);
    // The win.
    if (rows[13] && rows[13].includes('wins the royal game of ur')) { won = rows[13].trim(); break; }
    // A capture: the fire takes rows 14-24; the chronicle comes back after.
    const raw = await ev('__raw(14,24)');
    const graphic = raw.filter((b) => { const k = b & 0x7f; return b !== 32 && (b >= 128 || k >= 64 || k === 0); }).length;
    if (!want.capture && graphic > 40 && !has(rows, 'bear them off') && !has(rows, 'pips to go')) {
      let best = null;
      for (let i = 0; i < 9; i++) {
        const raw2 = await ev('__raw(14,24)'); const g2 = raw2.filter((b) => b >= 128 || (b & 0x7f) >= 64).length;
        const fr = await grab(); if (!best || g2 > best.g) best = { g: g2, fr };
        await sleep(60);
      }
      // The chronicle repaints its old lines first; the capture is said after.
      const after = await waitText((r) => chron(r).some((l) => l.includes('captures a foe') || l.includes(' is home')), 8000).catch(() => []);
      if (has(after, 'captures a foe')) {
        want.capture = true;
        note(chron(after).find((l) => l.includes('captures')));
        await plate('capture', best.fr, [[cell(0, 14, 39, 24), 'CAPTURED, burnt in block letters over a bed of flame, then gone: about 1.2 seconds.']]);
      }
      continue;
    }
    if (!want.home && has(rows, 'bear them off the board')) {
      await waitText('pips to go', 15000, 'the race count').catch(() => {});
      await sleep(400);
      want.home = true;
      await machineShot('home', [
        [cell(0, 14, 39, 15), 'The laser writes the rule.'],
        [cell(0, 16, 39, 20), 'The count, in block letters: how many of this side are home.'],
        [cell(0, 22, 39, 23), 'The race: pieces home out of seven, and the pips each side still has to travel.'],
      ]);
      note('a piece borne off');
      continue;
    }
    // URBOT's reasoning, once it has said what it will do.
    if (!want.urbot && c.some((l) => l.startsWith('so: piece')) && c.some((l) => l.startsWith('mind:'))) {
      want.urbot = true; note('URBOT: ' + c.join(' / '));
      await machineShot('urbot', [
        [cell(2, 21, 38, 24), 'URBOT thinks aloud: its doctrine, its census, the branch that fired, and the move.'],
        [cell(0, 0, 39, 3), 'Its side of the board, lit; yours recedes while it moves.'],
      ]);
      continue;
    }
    if (!want.rosette && c.some((l) => l.startsWith('a rosette - ') && l.endsWith('throws again'))) {
      want.rosette = true; note(c.find((l) => l.startsWith('a rosette')));
      await machineShot('rosette', [
        [cell(2, 21, 38, 24), 'Landing on a rosette: another throw, and no turn change.'],
      ]);
      continue;
    }
    // Our prompt: the last two lines are "paul - pick a piece, h = off" and the legal list.
    const pi = c.findIndex((l) => l.startsWith('paul - pick a piece'));
    if (pi >= 0 && pi === c.length - 2) {
      const list = c[pi + 1];
      const key = c.join('|');
      if (key !== lastPrompt) {
        lastPrompt = key;
        if (!want.yourTurn && moves >= 1) {
          want.yourTurn = true;
          await machineShot('your-turn', [
            [cell(0, 10, 39, 13), 'Your side, lit; URBOT\'s has receded three luminance steps.'],
            [cell(2, 16, 38, 19), 'The four lots on the casting floor: the tips showing are the throw.'],
            [cell(2, 23, 38, 24), `The legal moves, piece>square ("h" is home). Here: ${list}`],
          ]);
        }
        const p = choose(list);
        if (p) { moves++; await tap(p, 600); }
      }
    }
    await sleep(200);
  }

  if (won) {
    note(won);
    const winner = won.split(' wins')[0];
    // Each stage is caught when the apron stops changing: the name is cut,
    // then held; the cup is poured, then the fireworks start over it.
    const settle = async (ms = 900, cap = 20000) => {
      let last = '', since = Date.now(); const t0 = Date.now();
      while (Date.now() - t0 < cap) { const cur = JSON.stringify(await ev('__raw(15,24)')); if (cur !== last) { last = cur; since = Date.now(); } else if (Date.now() - since >= ms) return; await sleep(100); }
    };
    await settle(700);
    await machineShot('win-name', [
      [cell(0, 13, 39, 14), 'The result, and how many the other side got home.'],
      [cell(0, 15, 39, 21), 'The winner\'s name, cut by the laser at the size of the screen.'],
    ]);
    await waitText((r) => r.slice(15, 25).some((l) => l.includes(winner)), 20000, 'the name in the cup').catch(() => {});
    await sleep(400);
    await machineShot('trophy', [[cell(0, 15, 39, 24), 'The gold cup, poured a row at a time, the winner\'s name engraved in its bowl.']]);
    // Fireworks: a few frames, keeping the busiest (the largest PNG).
    await sleep(2500);
    let best = '';
    for (let i = 0; i < 8; i++) { const f = await grab(); if (f.length > best.length) best = f; await sleep(350); }
    await plate('fireworks', best, [[cell(0, 13, 39, 24), 'Fireworks, three shells in the air at once, the border flashing each burst\'s hue: five minutes, or until a key.']]);
    // Any key ends the fireworks - but the loop drawing them scans the keys
    // only between shells, so a 120 ms tap from the on-screen button can fall
    // between two scans and never be seen (observed here).  A key held as a
    // finger holds it gets through.
    await hold(' ', 500);
    await waitText('press space', 15000).catch(() => {});
    await hold(' ', 300);
    await waitText('play again?', 15000).catch(() => {});
    await machineShot('play-again', [[cell(0, 24, 39, 24), 'y keeps the same two players and their colours on a fresh board; n goes back to the cabinet.']]);
    await tap('n', 500);
  } else {
    note(`the match did not finish in ${MINUTES} minutes; leaving it with x x x`);
    await leave();
  }
  const missing = Object.entries(want).filter(([, v]) => !v).map(([k]) => k);
  if (missing.length) note(`not seen in this match: ${missing.join(', ')}`);

  // 6. Real lots, and leaving with x x x.
  await waitText('how we play', 60000, 'back at the cabinet');
  await sleep(1500);
  await tap('3', 0);
  await waitText('player one, your name:', 20000);
  await tap('Enter', 500);
  await waitText('player two, your name:', 10000);
  await tap('Enter', 500);
  for (const who of ['ena', 'dumuzi']) {
    await waitText(`colours for ${who}`, 10000);
    await tap(who === 'ena' ? '9' : '6'); await tap('Enter');
    await waitText((r) => r[14].includes('shade'), 5000);
    await tap('5'); await tap('Enter');
    await waitText('keep it?', 5000); await tap('y');
  }
  await waitText('higher hand opens', 30000);
  await waitText('type the tip count', 60000, 'the manual throw prompt');
  await sleep(400);
  await tap('x', 600);
  await waitText('again to leave', 5000).catch(() => {});
  await machineShot('real-lots', [
    [cell(2, 21, 38, 24), 'Real lots: throw your own four lots at the table and type the count, 0-4. And x: one of three to leave.'],
  ]);
  await leave();

  // 7. The demo, which the cabinet also starts by itself after two idle minutes.
  await waitText('how we play', 60000, 'back at the cabinet');
  await sleep(1500);
  await tap('4', 0);
  await waitText('demo - press space', 60000, 'the demo');
  await waitText((r) => chron(r).some((l) => l.startsWith('so: piece')), 60000, 'URBOT A or B choosing').catch(() => {});
  await machineShot('demo', [
    [cell(0, 0, 39, 13), 'URBOT A against URBOT B, each with a doctrine drawn at random.'],
    [cell(2, 21, 38, 24), 'Space, or x x x, ends it.'],
  ]);
  await tap(' ', 1000);
}

// -- run -------------------------------------------------------------------
buildStage();
await serve();
launch();
for (const sig of ['SIGINT', 'SIGTERM']) process.on(sig, () => { process.exitCode = 130; stop(); process.exit(130); });
try {
  await connect();
  await tour();
  fs.writeFileSync(path.join(BOX, 'log.txt'), log.join('\n'));
  say(`\n${made.length} pictures in ${path.relative(ROOT, SHOTS) || SHOTS}/`);
  say(log.map((l) => '  ' + l).join('\n'));
  if (pageErrors.length) say(`\npage errors seen:\n  ${[...new Set(pageErrors)].join('\n  ')}`);
} catch (err) {
  console.error(`\nSTOPPED: ${err.message}`);
  try { const r = await send('Page.captureScreenshot', { format: 'png' }); fs.writeFileSync(path.join(BOX, 'stopped-here.png'), Buffer.from(r.result.data, 'base64')); console.error(`   the page at that moment: ${path.join(BOX, 'stopped-here.png')}`); } catch {}
  try { const rows = await ev('__screen()'); if (rows) console.error('   the Plus/4 screen:\n' + rows.map((r) => '   |' + r + '|').join('\n')); } catch {}
  process.exitCode = 1;
} finally { stop(); }
