'use strict';
const d = require('./_dsl.cjs');
module.exports = {
  meta: {id: '05-angle-hold', name: '轉到 90° 並保持',
    desc: 'M3 歸零後轉到 90°，到位後閉迴路持續保持；用手扳軸會被推回來，下停止才放開。',
    hw: 'M3（含編碼器）', level: '基礎', series: 'B'},
  setup: [
    d.stop(3),
    d.note('先放開保持，再把「現在的位置」設成 0°（設零點）。之後的角度都以這裡為準。',
      d.zero(3), {w: 260, h: 90}),
    d.note('轉到 90° 後不會停：韌體會持續修正，用手扳軸會被推回 90°。要放開就對 M3 下「停止」。角度單位是度；放在 setup 只送一次。',
      d.moveTo(3, 90), {w: 280, h: 130})
  ],
  loop: []
};
