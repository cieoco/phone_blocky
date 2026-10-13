'use strict';
// Tiny DSL for authoring Blockly example programs. Block field/input names mirror data/appBlock.js.
// A "block" is {type, fields, values, statements, comment}. Arrays of blocks are chained with <next>.
// A bare JS number used as a value input becomes <shadow type="math_number"> for inputs that the
// toolbox also pre-fills with a shadow (delay, pwm, servo ...) and a real <block> elsewhere
// (comparisons, plot ...), matching the hand-written examples. Use num(n) / shadow(n) to force one.

const SHADOW_INPUTS = {
  arduino_delay: ['DELAY_TIME'], controls_repeat_ext: ['TIMES'],
  math_arithmetic: ['A', 'B'], math_abs: ['VALUE'], math_random_int: ['FROM', 'TO'],
  math_constrain: ['VALUE', 'LOW', 'HIGH'],
  math_map: ['VALUE', 'FROM_LOW', 'FROM_HIGH', 'TO_LOW', 'TO_HIGH'],
  motor_pwm: ['PWM'], motor_speed: ['RPM'], motor_position: ['DEG'], motor_move_by: ['DEG'],
  servo_set: ['DEG']
};

const block = (type, fields = {}, values = {}, statements = {}) => ({type, fields, values, statements});
const num = n => ({type: 'math_number', fields: {NUM: n}, values: {}, statements: {}, shadow: false, forced: true});
const shadow = n => ({...num(n), shadow: true});

// Attach a teaching comment to a block: note('text', blk[, {w, h}]). Returns the block.
const note = (text, blk, opts = {}) => {
  const lines = Math.max(1, Math.ceil([...text].length / 16));
  blk.comment = {text, w: opts.w || 260, h: opts.h || Math.min(200, 60 + lines * 16)};
  return blk;
};

// --- motors / servo ---
const pwm = (motor, dir, duty) => block('motor_pwm', {MOTOR: motor, DIRECTION: dir}, {PWM: duty});
const stop = (motor = 0) => block('motor_stop', {MOTOR: motor});
const speed = (motor, rpm) => block('motor_speed', {MOTOR: motor}, {RPM: rpm});
const moveTo = (motor, deg) => block('motor_position', {MOTOR: motor}, {DEG: deg});
const moveBy = (motor, deg) => block('motor_move_by', {MOTOR: motor}, {DEG: deg});
const zero = motor => block('motor_zero', {MOTOR: motor});
const servo = (ch, deg) => block('servo_set', {CH: ch}, {DEG: deg});

// --- timing / output ---
const delay = ms => block('arduino_delay', {}, {DELAY_TIME: ms});
const millis = () => block('arduino_millis');
const plot = (series, unit, value) => block('plot_print', {SERIES: series, UNIT: unit}, {VALUE: value});
const print = text => block('message_print', {}, {TEXT: text});
const serialPrintln = content => block('arduino_serial_println', {}, {CONTENT: content});

// --- sensors / io ---
const ultrasonic = (trig, echo) => block('arduino_ultrasonic', {TRIG: trig, ECHO: echo});
const encoder = (motor, read) => block('encoder_read', {MOTOR: motor, READ: read}); // read: deg|ticks|rpm
const irDigital = pin => block('ir_digital', {PIN: pin});
const irAnalog = pin => block('ir_analog', {PIN: pin});
const legoButton = pin => block('lego_button', {PIN: pin});
const pinMode = (pin, mode) => block('arduino_pinMode', {PIN: pin, MODE: mode});
const digitalWrite = (pin, value) => block('arduino_digitalWrite', {PIN: pin, VALUE: value});
const digitalRead = pin => block('arduino_digitalRead', {PIN: pin});
const analogWrite = (pin, value) => block('arduino_analogWrite', {PIN: pin, VALUE: value});
const analogRead = pin => block('arduino_analogRead', {PIN: pin});

// --- external (I2C) functions: idx 0-15, kind 'DIGITAL'|'ANALOG' ---
const funcDeclare = (idx, kind, readable) =>
  block('blockly_func_declare', {IDX: String(idx), KIND: kind, READABLE: readable ? 'TRUE' : 'FALSE'});
const funcGet = idx => block('blockly_func_get', {IDX: String(idx)});
const funcSet = (idx, value) => block('blockly_func_set', {IDX: String(idx)}, {VALUE: value});

// --- control flow ---
const ifThen = (cond, then) => block('controls_if', {}, {IF0: cond}, {DO0: then});
const ifElse = (cond, then, els) => block('controls_if', {}, {IF0: cond}, {DO0: then, ELSE: els});
const repeat = (times, body) => block('controls_repeat_ext', {}, {TIMES: times}, {DO: body});
const whileDo = (cond, body) => block('controls_whileUntil', {MODE: 'WHILE'}, {BOOL: cond}, {DO: body});
const untilDo = (cond, body) => block('controls_whileUntil', {MODE: 'UNTIL'}, {BOOL: cond}, {DO: body});

// --- logic ---
const bool = v => block('logic_boolean', {BOOL: v ? 'TRUE' : 'FALSE'});
const cmp = (op, a, b) => block('logic_compare', {OP: op}, {A: a, B: b});
const eq = (a, b) => cmp('EQ', a, b);
const neq = (a, b) => cmp('NEQ', a, b);
const lt = (a, b) => cmp('LT', a, b);
const lte = (a, b) => cmp('LTE', a, b);
const gt = (a, b) => cmp('GT', a, b);
const gte = (a, b) => cmp('GTE', a, b);
const and = (a, b) => block('logic_operation', {OP: 'AND'}, {A: a, B: b});
const or = (a, b) => block('logic_operation', {OP: 'OR'}, {A: a, B: b});
const not = a => block('logic_negate', {}, {BOOL: a});

// --- math ---
const arith = (op, a, b) => block('math_arithmetic', {OP: op}, {A: a, B: b});
const add = (a, b) => arith('ADD', a, b);
const sub = (a, b) => arith('MINUS', a, b);
const mul = (a, b) => arith('MULTIPLY', a, b);
const div = (a, b) => arith('DIVIDE', a, b);
const mod = (a, b) => arith('MODULO', a, b);
const abs = a => block('math_abs', {}, {VALUE: a});
const randomInt = (from, to) => block('math_random_int', {}, {FROM: from, TO: to});
const constrain = (v, low, high) => block('math_constrain', {}, {VALUE: v, LOW: low, HIGH: high});
const map = (v, fl, fh, tl, th) =>
  block('math_map', {}, {VALUE: v, FROM_LOW: fl, FROM_HIGH: fh, TO_LOW: tl, TO_HIGH: th});

// --- variables / procedures (Blockly core blocks; variable referenced by name) ---
const getVar = name => block('variables_get', {VAR: name});
const setVar = (name, value) => block('variables_set', {VAR: name}, {VALUE: value});
const changeVar = (name, delta) => block('math_change', {VAR: name}, {DELTA: delta});
const defProc = (name, body) => block('procedures_defnoreturn', {NAME: name}, {}, {STACK: body});
// A call block carries its target in <mutation name=...>, not in a field (real Blockly ignores a NAME field).
const callProc = name => ({...block('procedures_callnoreturn'), mutation: {name}});

module.exports = {
  block, num, shadow, note, SHADOW_INPUTS,
  pwm, stop, speed, moveTo, moveBy, zero, servo, delay, millis, plot, print, serialPrintln,
  ultrasonic, encoder, irDigital, irAnalog, legoButton, pinMode, digitalWrite, digitalRead,
  analogWrite, analogRead, ifThen, ifElse, repeat, whileDo, untilDo, bool, cmp, eq, neq, lt, lte,
  gt, gte, and, or, not, arith, add, sub, mul, div, mod, abs, randomInt, constrain, map,
  funcDeclare, funcGet, funcSet, getVar, setVar, changeVar, defProc, callProc
};
