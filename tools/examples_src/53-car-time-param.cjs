'use strict';
const d = require('./_dsl.cjs');
const forward = d.defProc('前進', [
  d.pwm(1, 'FORWARD', 50), d.pwm(2, 'FORWARD', 50), d.delay(d.getVar('毫秒')), d.stop(0)], ['毫秒']);
const left = d.defProc('左轉', [
  d.stop(1), d.pwm(2, 'FORWARD', 50), d.delay(d.getVar('毫秒')), d.stop(0)], ['毫秒']);
module.exports = {
  meta: {id: '53-car-time-param', name: '小車：一個參數',
    desc: '「前進(毫秒)」「左轉(毫秒)」把等待時間變成參數：呼叫時才填要走多久。',
    hw: '兩輪小車：左輪 M1、右輪 M2（無編碼器）', level: '基礎', series: 'G'},
  setup: [
    d.stop(0),
    d.note('新的：參數。括號裡的數字就是「呼叫時才填的空格」。前進(1000) = 走 1 秒，前進(500) = 走半秒。',
      d.callProc('前進', ['毫秒'], [1000]), {w: 290, h: 110}),
    d.callProc('左轉', ['毫秒'], [400]),
    d.callProc('前進', ['毫秒'], [1000])
  ],
  loop: [],
  extra: [
    d.note('等待與停車都包進副程式了，主程式不用再寫。參數是全域變數，名字別和主程式的變數重複。',
      forward, {w: 300, h: 130}),
    left
  ]
};
