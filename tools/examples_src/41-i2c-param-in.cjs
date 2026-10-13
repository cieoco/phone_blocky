'use strict';
const d = require('./_dsl.cjs');
module.exports = {
  meta: {id: '41-i2c-param-in', name: '主機設定舵機角度',
    desc: '主機透過 I2C 送一個數字，舵機就轉到那個角度。需燒 esp32dev_i2c 並接上 i2cESP32 主機。',
    hw: 'S1 + i2cESP32 主機（SDA 21 / SCL 22，位址 0x36）', level: '進階', series: 'F'},
  setup: [
    d.note('向主機宣告對外功能 0：類比（數值）、不可回讀（只有主機單向下令）。主機儀表板會依宣告長出元件。',
      d.funcDeclare(0, 'ANALOG', false), {w: 280, h: 110}),
    d.servo(1, 90)
  ],
  loop: [
    d.note('讀主機下的值（主機還沒送過時是 0）。限制在 0–180 再給舵機，主機亂送也不會出事。',
      d.servo(1, d.constrain(d.funcGet(0), 0, 180)), {w: 280, h: 110}),
    d.delay(50)
  ]
};
