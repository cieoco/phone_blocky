'use strict';
const d = require('./_dsl.cjs');
// Single-channel examples use function 0; the combined example uses 0..5.
module.exports = function outputs(id, channels) {
  const names = channels.map(c => c.name).join('、');
  const mapping = channels.map((c, i) => `${i}=${c.name}`).join('，');
  const setup = [d.note('僅控制 Blockly 板的輸出，不控制另一塊底盤。執行時先停止本板全部馬達。', d.stop(0))];
  const loop = [];
  channels.forEach((c, i) => {
    setup.push(d.note(`功能 ${i} 對應 ${c.name}；回讀的是命令值，不是實測回授。`, d.funcDeclare(i, 'ANALOG', true)));
    setup.push(d.funcSet(i, c.servo ? 90 : 0));
    if (c.servo) {
      setup.push(d.note('執行範例會將此舵機移到 90 度。', d.servo(c.port, 90)));
      loop.push(d.note(`功能 ${i}：角度 0～180 度。`, d.servo(c.port, d.constrain(d.funcGet(i), 0, 180))));
    } else {
      // PWM blocks accept magnitude and explicit direction, not signed duty.
      loop.push(d.note(`功能 ${i}：-100～100%；正值正轉、負值反轉、0 停止。失聯不會自動清除此命令。`,
        d.ifElse(d.lt(d.funcGet(i), 0),
          [d.pwm(c.port, 'BACKWARD', d.constrain(d.abs(d.funcGet(i)), 0, 100))],
          [d.pwm(c.port, 'FORWARD', d.constrain(d.funcGet(i), 0, 100))])));
    }
  });
  loop.push(d.delay(50));
  return {
    meta: {id, name: `I²C 控制 ${names}${channels.length === 6 ? '（六路整合）' : ''}`,
      desc: `功能對應：${mapping}。馬達 -100～100%，舵機 0～180 度；執行時馬達命令歸零、舵機置中 90 度。主機須支援對外功能寫入；僅偵測到板子不會自動控制。`,
      hw: `Blockly 板 ${names}；esp32dev_i2c，SDA21/SCL22，0x36`, level: '進階', series: 'F'},
    setup, loop
  };
};
