// Offline timing model. Uses the same transition table and timeout as the I2C slave.
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const header = fs.readFileSync(path.join(__dirname, '../src/DashboardLease.h'), 'utf8');
const names = ['Standalone','Stopped','Ready','Running'];
const table = [...header.matchAll(/^    \{(\w+, \w+, \w+, \w+)\},$/gm)]
  .map(match => match[1].split(', ').map(name => names.indexOf(name)));
assert.equal(table.length,4);
const timeout = Number(header.match(/timeoutMs = (\d+);/)[1]);
function moduleModel() {
  let requested=0, applied=0, lastBeat=0, duty=80;
  function expire(now) {
    if ([2,3].includes(requested) && ((now-lastBeat)>>>0) >= timeout) requested=1;
  }
  return {
    op(op,now) {
      expire(now);
      if (op<2 || requested===applied) requested=table[requested][op];
      lastBeat=now;
    },
    tick(now) {
      expire(now);
      applied=requested;
      if (applied===1 || applied===2) duty=0;
      return applied;
    },
    programWrite(value) { if (applied===0 || applied===3) duty=value; },
    output:()=>duty,
  };
}
for (const origin of [0,0xffffff00]) {
  const board=moduleModel();
  board.op(1,origin); assert.equal(board.tick(origin),3);
  board.op(0,origin+1);board.op(1,origin+2);board.op(2,origin+3);
  assert.equal(board.tick(origin+4),1);
  board.programWrite(80);assert.equal(board.output(),0);
  board.op(2,origin+5);board.op(3,origin+6);assert.equal(board.tick(origin+7),2);
  board.programWrite(80);assert.equal(board.output(),0);
  board.op(3,origin+8);assert.equal(board.tick(origin+8),3);
  board.programWrite(75);assert.equal(board.output(),75);
  assert.equal(board.tick(origin+8+timeout-1),3);
  assert.equal(board.tick(origin+8+timeout),1);
  board.op(1,origin+10+timeout);assert.equal(board.tick(origin+10+timeout),1);
  assert.equal(board.output(),0);
}
console.log('PASS: simulated stop priority, explicit release/start, timeout boundary, late heartbeat and uint32 rollover');