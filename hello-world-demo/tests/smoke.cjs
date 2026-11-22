// TDD contract for C++ ↔ SQLite. Run without an Electron UI or npm install.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'ecu-tdd-'));
const db = path.join(temp, 'test.sqlite');
const bin = process.env.ECU_ENGINE ||
  path.resolve(__dirname, '..', 'build', process.platform === 'win32' ? 'ecu_engine.exe' : 'ecu_engine');

function run(...args) {
  return spawnSync(bin, [db, ...args], {encoding: 'utf8'});
}
function success(...args) {
  const result = run(...args);
  assert.equal(result.status, 0, 'Expected success: ' + result.stderr);
  return JSON.parse(result.stdout);
}
function failure(...args) {
  const result = run(...args);
  assert.notEqual(result.status, 0, 'Expected failure: ' + args.join(' '));
}

try {
  // RED: fail if schema initialization is not deterministic.
  assert.deepEqual(success('list').map(row => row.name), ['ADAS','BCM','CGW','VCU']);
  assert.equal(success('list').filter(row => row.enabled).length, 3);
  assert.equal(success('check').integrity, 'ok');
  assert.deepEqual(success('history'), []);

  // GREEN: one transition = one committed audit event.
  assert.equal(success('set','VCU','1').find(row => row.name === 'VCU').enabled, true);
  assert.equal(success('list').find(row => row.name === 'VCU').enabled, true);
  assert.equal(success('history').length, 1);
  assert.deepEqual(
    (({ecu,before,after}) => ({ecu,before,after}))(success('history')[0]),
    {ecu:'VCU',before:false,after:true}
  );

  // Idempotent state updates must not duplicate history entries.
  success('set','VCU','1');
  assert.equal(success('history').length, 1);

  // Rejection must not mutate persistent state.
  failure('set','NOT_AN_ECU','1');
  failure('set','ADAS','yes');
  failure('bogus');
  assert.equal(success('history').length, 1);
  assert.equal(success('list').find(row=>row.name==='ADAS').enabled, true);

  success('set','ADAS','0');
  assert.equal(success('history').length, 2);
  assert.equal(success('check').integrity, 'ok');

  console.log('PASS 8 contracts: seed, persistence, transaction, history, idempotence, invalid inputs, integrity, JSON');
} finally {
  fs.rmSync(temp, {recursive:true,force:true});
}
