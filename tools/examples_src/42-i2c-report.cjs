'use strict';
const d = require('./_dsl.cjs');
module.exports = {
  meta: {id: '42-i2c-report', name: '回報距離給主機',
    desc: '本板把超音波距離寫進對外功能，主機讀得到。需燒 esp32dev_i2c 並接上 i2cESP32 主機。',
    hw: '超音波 TRIG 2 / ECHO 33 + i2cESP32 主機（SDA 21 / SCL 22，0x36）', level: '進階', series: 'F'},
  setup: [
    d.note('功能 0：類比、勾「可回讀」。沒勾的話主機讀不到本板寫的值。',
      d.funcDeclare(0, 'ANALOG', true), {w: 270, h: 100})
  ],
  loop: [
    d.note('把距離（cm，整數）寫給主機。數值範圍 −32768～32767；沒回波是 −1。',
      d.funcSet(0, d.ultrasonic(2, 33)), {w: 270, h: 100}),
    d.delay(100)
  ]
};
