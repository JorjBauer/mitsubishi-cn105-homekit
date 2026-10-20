const assert = require('assert');
const { readVersions, menuModel } = require('./extract.js');

const URL = 'https://h/x/firmware/homekit/versions.json';
const sha = c => c.repeat(64);
const build = (board, ver, c) => ({
  board, path: 'archive/' + ver + '/' + board + '/firmware.bin', sha256: sha(c)
});
const rel = (ver, builds, date) => ({ version: ver, date: date || '2026-01-01', builds });

// Scrambled on purpose: readVersions owns the ordering.
const INDEX = {
  name: 'Mitsubishi HomeKit Controller',
  versions: [
    rel('0.2.7', [build('nanoc6', '0.2.7', 'a')]),
    rel('0.2.9-beta.1', [build('nanoc6', '0.2.9-beta.1', 'b')]),
    rel('0.2', [build('nanoc6', '0.2', 'c')]),
    rel('0.2.8', [build('nanoc6', '0.2.8', 'd'), build('m5atoms3-lite', '0.2.8', 'e')]),
    rel('0.2.6-beta.4', [build('nanoc6', '0.2.6-beta.4', 'f')]),
    rel('0.2.5', [build('m5atoms3-lite', '0.2.5', '1')]),            // no nanoc6 build
    rel('0.2.4', [{ board: 'nanoc6', path: 'archive/0.2.4/nanoc6/firmware.bin', sha256: 'zz' }])
  ]
};

const list = readVersions(INDEX, URL, 'nanoc6');
const v = rows => rows.map(r => r.entry.version);
assert.deepStrictEqual(list.map(e => e.version), ['0.2.9-beta.1', '0.2.8', '0.2.7', '0.2.6-beta.4', '0.2']);
assert.strictEqual(list[1].binUrl, 'https://h/x/firmware/homekit/archive/0.2.8/nanoc6/firmware.bin');
[null, {}, {versions: 'x'}].forEach(bad => assert.deepStrictEqual(readVersions(bad, URL, 'nanoc6'), []));

// An Entry is exactly {version, date, binUrl, sha256}: no notes, no release_url.
assert.deepStrictEqual(list[1], {
  version: '0.2.8',
  date: '2026-01-01',
  binUrl: 'https://h/x/firmware/homekit/archive/0.2.8/nanoc6/firmware.bin',
  sha256: sha('d')
});
assert.strictEqual(readVersions(INDEX, URL, 'm5atoms3-lite')[1].version, '0.2.5');
assert.deepStrictEqual(readVersions(INDEX, URL, 'esp32'), []);

// Defensive skips: bad version, missing path, junk entries, non-array builds.
assert.deepStrictEqual(readVersions({versions: [
  rel('latest', [build('nanoc6', 'latest', 'a')]),
  rel('1.0.0', [{ board: 'nanoc6', sha256: sha('a') }]),
  rel('1.0.1', 'nope'),
  null, 7, {},
  rel('1.0.2', [null, build('nanoc6', '1.0.2', 'a')])
]}, URL, 'nanoc6').map(e => e.version), ['1.0.2']);

let m = menuModel(list, '0.2.6-beta.4', false);
assert.strictEqual(m.offer.version, '0.2.8');
assert.deepStrictEqual(v(m.pinned), ['0.2.8', '0.2.6-beta.4']);
assert.deepStrictEqual(m.pinned.map(r => r.tags), [['latest'], ['beta', 'installed']]);
assert.deepStrictEqual(v(m.others), ['0.2.9-beta.1', '0.2.7', '0.2']);

m = menuModel(list, 'v0.2.8', false);
assert.strictEqual(m.offer, null);
assert.deepStrictEqual(m.pinned.map(r => r.tags), [['latest', 'installed']]);

m = menuModel(list, '0.2.8', true);
assert.strictEqual(m.offer.version, '0.2.9-beta.1');
assert.deepStrictEqual(v(m.pinned), ['0.2.9-beta.1', '0.2.8']);

m = menuModel(list, '0.2.9-beta.1', false);                 // leaving a beta: stable is pinned, nothing offered
assert.strictEqual(m.offer, null);
assert.deepStrictEqual(v(m.pinned), ['0.2.8', '0.2.9-beta.1']);

m = menuModel(list, '0.2.8-3-gabc1234', false);              // dev build: no orange at equal, no installed tag
assert.strictEqual(m.offer, null);
assert.ok(m.pinned.concat(m.others).every(r => !r.installed));
assert.strictEqual(menuModel(list, '0.2.7-5-gabc1234-dirty', false).offer.version, '0.2.8');

m = menuModel(list, '0.3.0', false);                         // running a version the index doesn't know
assert.strictEqual(m.offer, null);
assert.deepStrictEqual(v(m.pinned), ['0.2.8']);
assert.deepStrictEqual(menuModel([], '0.2.8', true), {pinned: [], others: [], offer: null});

console.log('PASS: version menu model');
