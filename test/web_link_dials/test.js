// Pulls the Link card's per-dial block out of web/index.html (between the
// linkDials markers) and checks it against the state frame and command that
// main/web_ws.cpp emits and accepts. Both files are tracked sources read at
// test time; the block runs through vm, the same way test/web_state does.
const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const ROOT = path.join(__dirname, '..', '..');
const html = fs.readFileSync(path.join(ROOT, 'web', 'index.html'), 'utf8');
const ws = fs.readFileSync(path.join(ROOT, 'main', 'web_ws.cpp'), 'utf8');
const bond = fs.readFileSync(path.join(ROOT, 'main', 'sl2_bond.h'), 'utf8');

function block(name) {
  const start = `// ── ${name}:start`, end = `// ── ${name}:end`;
  const a = html.indexOf(start), b = html.indexOf(end);
  if (a < 0 || b < a) throw new Error(`${name} markers missing from web/index.html`);
  return html.slice(a + start.length, b);
}
const esc = /function escHtml\(t\)\{[^\n]*\}/.exec(html);
if (!esc) throw new Error('escHtml missing from web/index.html');

const ui = vm.runInThisContext(
  '(function(){\n' +
  '  function fmtReadingTemp(c){return c.toFixed(1)}\n' +
  '  function fmtAgo(s){return s+"s ago"}\n' +
  esc[0] + '\n' + block('linkDials') + '\n' +
  '  return {linkDialList:linkDialList,linkDialRowHtml:linkDialRowHtml,forgetDialCmd:forgetDialCmd,\n' +
  '          linkPairLabel:linkPairLabel,LINK_MAX_DIALS:LINK_MAX_DIALS};\n' +
  '})')();

let checks = 0;
function ok(v, msg) { assert.ok(v, msg); checks++; }
function same(a, b, msg) { assert.deepStrictEqual(a, b, msg); checks++; }

const A = { mac: 'AA:BB:CC:DD:EE:01', live: true, seen: 2, t: 21.25 };
const B = { mac: 'AA:BB:CC:DD:EE:02', live: false, seen: 90, t: null };
const C = { mac: 'AA:BB:CC:DD:EE:03', live: false, seen: -1, t: null };

// Two or more bonded dials: one row each, each with its own Forget.
{
  const list = ui.linkDialList({ remoteBonded: true, remoteDials: [A, B, C] });
  same(list.length, 3, 'every bonded dial gets a row');
  list.forEach(function (d) {
    const row = ui.linkDialRowHtml(d);
    ok(row.indexOf(d.mac) >= 0, 'row shows its MAC');
    ok(row.indexOf('data-mac="' + d.mac + '"') >= 0, 'row Forget is bound to its own MAC');
    same((row.match(/<button/g) || []).length, 1, 'one Forget button per row');
  });
  ok(ui.linkDialRowHtml(A).indexOf('21.3') >= 0, 'a live dial shows its own reading');
  ok(/Seen 90s ago/.test(ui.linkDialRowHtml(B)), 'an offline dial shows when it was last heard');
  ok(/Not seen yet/.test(ui.linkDialRowHtml(C)), 'a never-heard dial says so');
}

// One dial: the card header already is that dial. A frame that rolled the
// section back (no remoteDials) or an unpaired unit renders no list.
same(ui.linkDialList({ remoteBonded: true, remoteDials: [A] }), [], 'one dial, no list');
same(ui.linkDialList({ remoteBonded: true }), [], 'missing field, no list');
same(ui.linkDialList({ remoteBonded: false, remoteDials: [A, B] }), [], 'unpaired, no list');

// Pair: offered before the first dial and, as "Pair another Link", while the
// bond table has room; hidden while a window is open or the table is full.
{
  const bondMax = /#define SL2_MAX_DIALS (\d+)/.exec(bond);
  ok(bondMax, 'SL2_MAX_DIALS defined in main/sl2_bond.h');
  same(ui.LINK_MAX_DIALS, Number(bondMax[1]), 'the UI table size is the firmware table size');
  const dials = (n) => [A, B, C, A].slice(0, n);
  same(ui.linkPairLabel({ remoteBonded: false, remoteDials: [] }), 'Pair', 'unpaired: Pair');
  same(ui.linkPairLabel({ remoteBonded: false, remotePairing: true }), null, 'listening: no Pair');
  for (let n = 1; n < ui.LINK_MAX_DIALS; n++)
    same(ui.linkPairLabel({ remoteBonded: true, remoteDials: dials(n) }), 'Pair another Link',
         n + ' paired: Pair another Link');
  same(ui.linkPairLabel({ remoteBonded: true, remoteDials: dials(ui.LINK_MAX_DIALS) }), null,
       'table full: no Pair');
  same(ui.linkPairLabel({ remoteBonded: true, remoteDials: dials(1), remotePairing: true }), null,
       'listening while paired: no Pair');
  same(ui.linkPairLabel({ remoteBonded: true }), null, 'dial count unknown: no Pair');
}

// Markup in a MAC cannot reach the DOM.
{
  const row = ui.linkDialRowHtml({ mac: '<img src=x onerror=1>', live: false, seen: -1, t: null });
  ok(row.indexOf('<img') < 0, 'MAC is escaped');
}

// The command the row sends, and the firmware's side of the same contract.
same(ui.forgetDialCmd(B.mac), { cmd: 'forgetDial', mac: B.mac }, 'forgetDial carries the MAC');
ok(/strcmp\(cmd, "forgetDial"\) == 0/.test(ws), 'web_ws.cpp handles forgetDial');
ok(/jsonGetString\(msg, "mac",/.test(ws), 'web_ws.cpp reads the mac field');
ok(ws.indexOf(',\\"remoteDials\\":[') >= 0, 'web_ws.cpp emits remoteDials');
ok(/\{\\"mac\\":\\"%02X(:%02X){5}\\",\\"live\\":%s,\\"seen\\":%ld,\\"t\\":%s\}/.test(ws),
   'each remoteDials entry carries mac, live, seen and t');

console.log(`web_link_dials: ${checks} checks passed`);
