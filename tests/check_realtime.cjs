// Execute page logic with in-memory UI/transport doubles only. No device access.
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '..');
const read = p => fs.readFileSync(path.join(root, p), 'utf8').replace(/\r\n/g, '\n');
const joy = read('data/joy.html');
const start = joy.indexOf('setInterval(function () {\n\t\tjoy1X.value');
assert.ok(start >= 0);
const tickSource = joy.slice(start, joy.indexOf('}, 200);', start) + '}, 200);'.length);
function state(y1, y2, button1 = 0, button2 = 0) {
    const elements = new Map();
    const c = vm.createContext({
        console: {log() {}},
        document: {getElementById(id) {
            if (!elements.has(id)) elements.set(id, {value: 0, style: {}, textContent: ''});
            return elements.get(id);
        }},
        setInterval(fn) { c.tick = fn; },
        Joy1: {GetY: () => y1, GetX: () => '0'},
        Joy2: {GetY: () => y2, GetX: () => '0'},
        vButton1: {GetValue: () => button1}, vButton2: {GetValue: () => button2},
        vButton1Value: 0, vButton2Value: 0, estopActive: true,
        actionType_ON_M1: 1, actionType_ON_M2: 1, vM3AngleValue: 90, vM4AngleValue: -45,
        joy1X: {}, joy1Y: {}, joy2X: {}, joy2Y: {},
        sendCommand(command) { c.commands.push(command); },
        commands: [], packets: [],
        websocket: {readyState: 1, send(text) { c.packets.push(JSON.parse(text)); }},
    });
    vm.runInContext(joy.slice(joy.indexOf('function clamp('), joy.indexOf('function emergencyStop(')), c);
    vm.runInContext(tickSource, c);
    return c;
}
for (const zero of ['0', '-0', 0]) {
    const c = state(zero, zero);
    c.tick();
    assert.equal(c.estopActive, false, 'neutral sticks must release the stop latch');
    assert.equal(c.packets[0].mode, 'joy');
    assert.equal(c.packets[0].M3A, 90, 'joy angles remain degrees');
    assert.equal(c.packets[0].M4A, -45);
}
for (const args of [['10', '0'], ['0', '-20'], ['0', '0', 1], ['0', '0', 0, 1]]) {
    const c = state(...args);
    c.tick();
    assert.equal(c.estopActive, true);
    const packet = c.packets[0];
    for (const field of ['y1', 'y2', 'Button1', 'Button2']) assert.equal(Number(packet[field]), 0);
    assert.ok(c.commands.every(x => x.cmd === 'stop'));
}
const c = state('0', '0');
vm.runInContext('setServoValue(2, "90", true); sendMotorPwm(3, "-60");', c);
assert.deepEqual(JSON.parse(JSON.stringify(c.commands)), [
    {cmd: 'servo', ch: 2, deg: 90}, {cmd: 'pwm', motor: 3, duty: -60},
]);
const hw = read('data/hardware_test.html');
const sendStart = hw.indexOf('    function sendCommand(command)');
const sent = [];
const h = vm.createContext({isConnected: true, WebSocket: {OPEN: 1},
    socket: {readyState: 1, send(text) { sent.push(JSON.parse(text)); }},
    alert() { throw new Error('Unexpected disconnected transport'); }});
vm.runInContext(hw.slice(sendStart, hw.indexOf('// 添加日誌', sendStart)), h);
vm.runInContext('sendCommand({test:"servoTest",servo:2,angle:90}); sendCommand({type:"motorControl",motor:"M3",speed:-255});', h);
assert.deepEqual(sent, [
    {test:'servoTest',servo:2,angle:90,mode:'hardwareTest'},
    {type:'motorControl',motor:'M3',speed:-255,mode:'hardwareTest'},
]);
console.log('Realtime stop latch, direct servo/PWM, joy units and hardwareTest envelope: PASS');
