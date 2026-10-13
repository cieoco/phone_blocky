'use strict';
const d = require('./_dsl.cjs');
const forward = d.defProc('前進', [
  d.pwm(1, 'FORWARD', 50), d.pwm(2, 'FORWARD', 50), d.delay(d.getVar('毫秒')), d.stop(0)], ['毫秒']);
const right = d.defProc('右轉', [
  d.pwm(1, 'FORWARD', 50), d.stop(2), d.delay(d.getVar('毫秒')), d.stop(0)], ['毫秒']);
const square = d.defProc('走正方形', [
  d.repeat(4, [d.callProc('前進', ['毫秒'], [800]), d.callProc('右轉', ['毫秒'], [400])])]);
module.exports = {
  meta: {id: '55-car-square', name: '小車：走正方形',
    desc: '「走正方形」呼叫「前進」「右轉」各四次。副程式可以呼叫別的副程式，轉彎時間要實車調。',
    hw: '兩輪小車：左輪 M1、右輪 M2（無編碼器）', level: '基礎', series: 'G'},
  setup: [
    d.stop(0),
    d.note('新的：副程式呼叫副程式。主程式只剩一行。',
      d.callProc('走正方形'), {w: 250, h: 80})
  ],
  loop: [],
  extra: [
    d.note('重複 4 次：前進、右轉。400 毫秒剛好轉 90° 是舉例，地板、電量不同就不同，要在真車上調。不能呼叫自己。',
      square, {w: 300, h: 140}),
    forward, right
  ]
};
