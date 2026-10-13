'use strict';
const d = require('./_dsl.cjs');
module.exports = {
  meta: {id: '51-car-no-proc', name: '小車：不用副程式',
    desc: '兩輪小車走「前進→左轉→前進→停」，全部用馬達積木硬寫。看看有多長、多重複。',
    hw: '兩輪小車：左輪 M1、右輪 M2（無編碼器）', level: '入門', series: 'G'},
  setup: [
    d.stop(0),
    d.note('左輪 M1、右輪 M2，兩輪都給 50% 就是前進。路線只跑一遍（放在 setup）。',
      d.pwm(1, 'FORWARD', 50), {w: 280, h: 100}),
    d.pwm(2, 'FORWARD', 50),
    d.delay(1000),
    d.note('左轉 = 左輪停、右輪轉。注意：「前進」那兩塊後面又要再寫一次。',
      d.stop(1), {w: 270, h: 100}),
    d.pwm(2, 'FORWARD', 50),
    d.delay(400),
    d.pwm(1, 'FORWARD', 50),
    d.pwm(2, 'FORWARD', 50),
    d.delay(1000),
    d.note('只有 4 個動作就用了 12 塊積木，而且很難一眼看出在做什麼。下一支用副程式來解決。',
      d.stop(0), {w: 280, h: 110})
  ],
  loop: []
};
