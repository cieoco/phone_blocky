'use strict';
const d = require('./_dsl.cjs');
module.exports = {
  meta: {id:'35-oled-distance', name:'OLED 顯示距離',
    desc:'0.91 吋 SSD1306 128×32，免初始化，顯示文字與超音波距離；與對上 I2C 互斥。',
    hw:'OLED SDA21/SCL22（0x3C）＋超音波 2/33', level:'入門', series:'C'},
  setup: [
    d.note('使用前移除上層主控接線。OLED：VCC 接 3.3V、GND 共地、SDA21、SCL22。位址不符時把所有 OLED 積木改成 0x3D。',
      d.block('oled_clear', {ADDRESS:'60'})),
    d.block('oled_text', {ADDRESS:'60', ROW:'0', TEXT:'Distance (cm):'})
  ],
  loop: [d.block('oled_value', {ADDRESS:'60', ROW:'1'}, {VALUE:d.ultrasonic(2,33)}), d.delay(200)]
};
