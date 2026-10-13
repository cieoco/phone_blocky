'use strict';
const d = require('./_dsl.cjs');
const P = ['左輪', '右輪'];
const drive = d.defProc('開車', [
  d.pwm(1, 'FORWARD', d.getVar('左輪')), d.pwm(2, 'FORWARD', d.getVar('右輪'))], P);
const go = (l, r) => d.callProc('開車', P, [l, r]);
module.exports = {
  meta: {id: '54-car-drive-lr', name: '小車：兩個參數',
    desc: '「開車(左輪, 右輪)」左右輪各一個動力（−100～100）。兩輪速度不同，車子就轉彎。',
    hw: '兩輪小車：左輪 M1、右輪 M2（無編碼器）', level: '基礎', series: 'G'},
  setup: [
    d.stop(0),
    d.note('新的：兩個參數，順序是（左輪, 右輪）。填反了車子就往反方向轉。(50, 50) 兩輪一樣快 = 直走。',
      go(50, 50), {w: 290, h: 110}),
    d.delay(1000),
    d.note('(20, 60) 左輪慢右輪快 = 往左彎。', go(20, 60), {w: 230, h: 70}),
    d.delay(1000),
    d.note('(60, 20) 左快右慢 = 往右彎。', go(60, 20), {w: 230, h: 70}),
    d.delay(1000),
    d.note('(50, −50) 一正一負 = 原地右轉。轉彎就是左右輪的速度差。', go(50, -50), {w: 270, h: 90}),
    d.delay(500),
    go(0, 0)
  ],
  loop: [],
  extra: [
    d.note('動力放變數時可以是負數，負的就倒退（寫成固定數字的積木只能 0–100，倒退要改方向）。範圍 −100～100。',
      drive, {w: 300, h: 130})
  ]
};
