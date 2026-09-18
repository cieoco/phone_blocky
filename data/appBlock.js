/********************************************
 * (1) 定義積木 (block definitions)
 ********************************************/

// Arduino 設定區塊
Blockly.Blocks['arduino_setup'] = {
  init: function () {
    this.appendDummyInput().appendField("setup");
    this.appendStatementInput("SETUP_BODY").setCheck(null);
    this.setNextStatement(true, null);
    this.setColour(20);
    this.setTooltip("Arduino setup function");
    this.setHelpUrl("");
  }
};

// 自訂 Arduino loop 積木定義
Blockly.Blocks['arduino_loop'] = {
  init: function () {
    this.appendDummyInput().appendField("loop");
    this.appendStatementInput("LOOP_BODY").setCheck(null);
    this.setPreviousStatement(true, null);
    this.setColour(20);
    this.setTooltip("Arduino loop function");
    this.setHelpUrl("");
  }
};

Blockly.Blocks['arduino_pinMode'] = {
  init: function () {
    this.appendDummyInput()
      .appendField("設定腳位")
      .appendField(new Blockly.FieldTextInput("15"), "PIN")
      .appendField("模式")
      .appendField(new Blockly.FieldDropdown([
        ["INPUT", "INPUT"],
        ["OUTPUT", "OUTPUT"]
      ]), "MODE");
    this.setPreviousStatement(true, null);
    this.setNextStatement(true, null);
    this.setColour(120);
    this.setTooltip("設定指定腳位為 INPUT 或 OUTPUT");
    this.setHelpUrl("");
  }
};

Blockly.Blocks['arduino_digitalWrite'] = {
  init: function () {
    this.appendDummyInput()
      .appendField("數位寫入腳位")
      .appendField(new Blockly.FieldTextInput("2"), "PIN")
      .appendField("值")
      .appendField(new Blockly.FieldDropdown([
        ["HIGH", "HIGH"],
        ["LOW", "LOW"]
      ]), "VALUE");
    this.setPreviousStatement(true, null);
    this.setNextStatement(true, null);
    this.setColour(120);
    this.setTooltip("將指定腳位寫入 HIGH 或 LOW");
    this.setHelpUrl("");
  }
};

Blockly.Blocks['arduino_digitalRead'] = {
  init: function () {
    this.appendDummyInput()
      .appendField("數位讀取腳位")
      .appendField(new Blockly.FieldTextInput("15"), "PIN");
    this.setOutput(true, null);
    this.setColour(120);
    this.setTooltip("讀取指定腳位的數位值；預設 GPIO15，避開 M3/M4 編碼器腳位");
    this.setHelpUrl("");
  }
};

Blockly.Blocks['arduino_analogWrite'] = {
  init: function () {
    this.appendDummyInput()
      .appendField("類比寫入腳位")
      .appendField(new Blockly.FieldTextInput("26"), "PIN")
      .appendField("值")
      .appendField(new Blockly.FieldTextInput("128"), "VALUE");
    this.setPreviousStatement(true, null);
    this.setNextStatement(true, null);
    this.setColour(65);
    this.setTooltip("對指定腳位輸出類比值 (0-255)");
    this.setHelpUrl("");
  }
};

Blockly.Blocks['arduino_analogRead'] = {
  init: function () {
    this.appendDummyInput()
      .appendField("類比讀取腳位")
      .appendField(new Blockly.FieldTextInput("32"), "PIN");
    this.setOutput(true, null);
    this.setColour(65);
    this.setTooltip("讀取指定腳位的類比值；預設 GPIO32，避開 M3/M4 編碼器腳位");
    this.setHelpUrl("");
  }
};
Blockly.Blocks['lego_button'] = {
  init: function () {
    this.appendDummyInput()
      .appendField("Lego按鈕")
      .appendField(new Blockly.FieldTextInput("4"), "PIN")
      .appendField("腳位");
    this.setOutput(true, null);
    this.setColour(120);
    this.setTooltip("按鈕輸入");
    this.setHelpUrl("");
  }
};

Blockly.Blocks['arduino_ultrasonic'] = {
  init: function () {
    this.appendDummyInput()
      .appendField("超音波感測器")
      .appendField(new Blockly.FieldTextInput("2"), "TRIG")
      .appendField("Trig")
      .appendField(new Blockly.FieldTextInput("33"), "ECHO")
      .appendField("Echo");
    this.setOutput(true, null);
    this.setColour(65);
    this.setTooltip("超音波感測器；預設 Trig=GPIO2、Echo=GPIO33，避開 M3/M4 編碼器腳位");
    this.setHelpUrl("");
  }
};

Blockly.Blocks['arduino_delay'] = {
  init: function () {
    this.appendDummyInput()
      .appendField("延遲")
      .appendField(new Blockly.FieldTextInput("1000"), "DELAY_TIME")
      .appendField("毫秒");
    this.setPreviousStatement(true, null);
    this.setNextStatement(true, null);
    this.setColour(290);
    this.setTooltip("延遲 0–10000 毫秒；較長等待請拆成多個積木。");
    this.setHelpUrl("");
  }
};

Blockly.Blocks['arduino_serial_println'] = {
  init: function () {
    this.appendValueInput("CONTENT")
      .setCheck(null)
      .appendField("serialPrintln");
    this.setPreviousStatement(true, null);
    this.setNextStatement(true, null);
    this.setColour(160);
    this.setTooltip("Prints to serial monitor");
    this.setHelpUrl("");
  }
};


Blockly.Blocks['message_print'] = {
  init: function () {
    this.appendValueInput("TEXT")
      .setCheck(null)
      .appendField("顯示訊息");
    this.setPreviousStatement(true, null);
    this.setNextStatement(true, null);
    this.setColour(160);
    this.setTooltip("顯示訊息");
    this.setHelpUrl("");
  }
};

Blockly.Blocks['plot_print'] = {
  init: function () {
    this.appendValueInput("VALUE").setCheck(null)
      .appendField("繪圖 名稱")
      .appendField(new Blockly.FieldTextInput("距離"), "SERIES")
      .appendField("數值");
    this.appendDummyInput()
      .appendField("單位")
      .appendField(new Blockly.FieldTextInput("cm"), "UNIT");
    this.setPreviousStatement(true, null);
    this.setNextStatement(true, null);
    this.setColour(20);
    this.setTooltip("將指定數值送到 Plotter；建議同一張圖使用相同單位");
  }
};

// 控制流程積木：if
Blockly.Blocks['controls_if'] = {
  init: function () {
    this.appendValueInput("IF0").setCheck(["Boolean", "Number"]).appendField("if");
    this.appendStatementInput("DO0").setCheck(null).appendField("do");
    this.appendStatementInput("ELSE").setCheck(null).appendField("else");
    this.setPreviousStatement(true, null);
    this.setNextStatement(true, null);
    this.setColour(210);
    this.setTooltip("如果條件為 true 則執行 do 區塊");
  }
};

// 流程次數積木：repeat
Blockly.Blocks['controls_repeat_ext'] = {
  init: function () {
    this.appendDummyInput().appendField("repeat");
    this.appendValueInput("TIMES").setCheck("Number");
    this.appendStatementInput("DO").setCheck(null);
    this.setPreviousStatement(true, null);
    this.setNextStatement(true, null);
    this.setColour(210);
    this.setTooltip("舊版積木：目前不支援 repeat，執行前請移除，使用頂層 loop。");
    this.setHelpUrl("");
  }
};

Blockly.Blocks['controls_whileUntil'] = {
  init: function () {
    this.appendDummyInput().appendField(new Blockly.FieldDropdown([
      ["while", "WHILE"],
      ["until", "UNTIL"]
    ]), "MODE");
    this.appendValueInput("BOOL").setCheck("Boolean");
    this.appendStatementInput("DO").setCheck(null);
    this.setPreviousStatement(true, null);
    this.setNextStatement(true, null);
    this.setColour(210);
    this.setTooltip("舊版積木：目前不支援 while/until，執行前請移除，使用頂層 loop + if。");
  }
};

Blockly.Blocks['arduino_millis'] = {
  init: function () {
    this.appendDummyInput().appendField("millis");
    this.setOutput(true, "Number");
    this.setColour(290);
    this.setTooltip("取得系統運行時間");
  }
};

Blockly.Blocks['math_number'] = {
  init: function () {
    this.appendDummyInput().appendField(new Blockly.FieldNumber(0), "NUM");
    this.setOutput(true, "Number");
    this.setColour(230);
    this.setTooltip("A number.");
  }
};

Blockly.Blocks['math_arithmetic'] = {
  init: function () {
    this.appendValueInput("A").setCheck("Number");
    this.appendDummyInput().appendField(new Blockly.FieldDropdown([
      ["+", "ADD"],
      ["-", "MINUS"],
      ["*", "MULTIPLY"],
      ["/", "DIVIDE"]
    ]), "OP");
    this.appendValueInput("B").setCheck("Number");
    this.setInputsInline(true);
    this.setOutput(true, "Number");
    this.setColour(230);
    this.setTooltip("進行兩數的運算");
  }
};

Blockly.Blocks['logic_boolean'] = {
  init: function () {
    this.appendDummyInput().appendField(new Blockly.FieldDropdown([
      ["true", "TRUE"],
      ["false", "FALSE"]
    ]), "BOOL");
    this.setOutput(true, "Boolean");
    this.setColour(270);
    this.setTooltip("邏輯布林值");
  }
};


Blockly.Blocks['logic_compare'] = {
  init: function () {
    this.appendValueInput("A").setCheck(null);
    this.appendDummyInput().appendField(new Blockly.FieldDropdown([
      ["==", "EQ"],
      ["!=", "NEQ"],
      ["<", "LT"],
      ["<=", "LTE"],
      [">", "GT"],
      [">=", "GTE"]
    ]), "OP");
    this.appendValueInput("B").setCheck(null);
    this.setOutput(true, "Boolean");
    this.setColour(270);
    this.setTooltip("比較兩個值");
  }
};

Blockly.Blocks['logic_negate'] = {
  init: function () {
    this.appendValueInput("BOOL").setCheck("Boolean").appendField("not");
    this.setOutput(true, "Boolean");
    this.setColour(270);
    this.setTooltip("邏輯否定");
  }
};

// 移除重複定義的標準積木 (variables_get, variables_set, math_change)
// 這些積木在 blockly.min.js 中已有定義，重新定義會導致 v12 的序列化錯誤。

// ============================================================
// 新格式積木 — 與 motorControl 語意對齊 (Phase A)
// ============================================================

Blockly.Blocks['motor_pwm'] = {
  init: function () {
    this.appendDummyInput()
      .appendField("馬達")
      .appendField(new Blockly.FieldDropdown([
        ["1", "1"], ["2", "2"], ["3", "3"], ["4", "4"]
      ]), "MOTOR")
      .appendField("方向")
      .appendField(new Blockly.FieldDropdown([
        ["Forward", "FORWARD"],
        ["Backward", "BACKWARD"],
        ["Stop", "STOP"]
      ]), "DIRECTION")
      .appendField("動力")
      .appendField(new Blockly.FieldNumber(50, 0, 100), "PWM")
      .appendField("%");
    this.setPreviousStatement(true, null);
    this.setNextStatement(true, null);
    this.setColour(180);
    this.setTooltip("控制馬達動力 (0–100%)，方向由下拉選單決定");
    this.setHelpUrl("");
  }
};

Blockly.Blocks['motor_stop'] = {
  init: function () {
    this.appendDummyInput()
      .appendField("停止馬達")
      .appendField(new Blockly.FieldDropdown([
        ["全部", "0"], ["1", "1"], ["2", "2"], ["3", "3"], ["4", "4"]
      ]), "MOTOR");
    this.setPreviousStatement(true, null);
    this.setNextStatement(true, null);
    this.setColour(180);
    this.setTooltip("停止指定馬達或全部馬達");
    this.setHelpUrl("");
  }
};

Blockly.Blocks['motor_position'] = {
  init: function () {
    this.appendDummyInput()
      .appendField("馬達")
      .appendField(new Blockly.FieldDropdown([
        ["3", "3"], ["4", "4"]
      ]), "MOTOR")
      .appendField("移動到角度")
      .appendField(new Blockly.FieldNumber(0, -36000, 36000), "DEG")
      .appendField("°");
    this.setPreviousStatement(true, null);
    this.setNextStatement(true, null);
    this.setColour(180);
    this.setTooltip("控制有編碼器的馬達移動到指定角度 (M3/M4)");
    this.setHelpUrl("");
  }
};

Blockly.Blocks['motor_zero'] = {
  init: function () {
    this.appendDummyInput()
      .appendField("馬達")
      .appendField(new Blockly.FieldDropdown([
        ["3", "3"], ["4", "4"]
      ]), "MOTOR")
      .appendField("設目前位置為 0°");
    this.setPreviousStatement(true, null);
    this.setNextStatement(true, null);
    this.setColour(180);
    this.setTooltip("將有編碼器的馬達目前位置設為角度 0° (M3/M4)");
    this.setHelpUrl("");
  }
};

Blockly.Blocks['motor_speed'] = {
  init: function () {
    this.appendDummyInput()
      .appendField("馬達")
      .appendField(new Blockly.FieldDropdown([
        ["3", "3"], ["4", "4"]
      ]), "MOTOR")
      .appendField("轉速")
      .appendField(new Blockly.FieldNumber(120, 60, 250), "RPM")
      .appendField("RPM");
    this.setPreviousStatement(true, null);
    this.setNextStatement(true, null);
    this.setColour(180);
    this.setTooltip("以閉迴路 RPM 驅動 M3/M4（60–250 RPM，有編碼器）");
    this.setHelpUrl("");
  }
};

Blockly.Blocks['motor_move_by'] = {
  init: function () {
    this.appendDummyInput()
      .appendField("馬達")
      .appendField(new Blockly.FieldDropdown([
        ["3", "3"], ["4", "4"]
      ]), "MOTOR")
      .appendField("相對旋轉")
      .appendField(new Blockly.FieldNumber(90, -36000, 36000), "DEG")
      .appendField("°");
    this.setPreviousStatement(true, null);
    this.setNextStatement(true, null);
    this.setColour(180);
    this.setTooltip("M3/M4 從目前位置相對旋轉指定角度（有編碼器）");
    this.setHelpUrl("");
  }
};

Blockly.Blocks['servo_set'] = {
  init: function () {
    this.appendDummyInput()
      .appendField("舵機")
      .appendField(new Blockly.FieldDropdown([
        ["1", "1"], ["2", "2"]
      ]), "CH")
      .appendField("角度")
      .appendField(new Blockly.FieldNumber(90, 0, 180), "DEG");
    this.setPreviousStatement(true, null);
    this.setNextStatement(true, null);
    this.setColour(40);
    this.setTooltip("控制舵機角度 (0–180°)");
    this.setHelpUrl("");
  }
};

// ============================================================
// 舊格式積木 (deprecated shim — 保留相容)
// ============================================================

Blockly.Blocks['馬達'] = {
  init: function () {
    this.appendDummyInput()
      .appendField("馬達")
      .appendField(new Blockly.FieldDropdown([
        ["1", "1"],
        ["2", "2"],
        ["3", "3"],
        ["4", "4"]
      ]), "MOTOR")
      .appendField("方向")
      .appendField(new Blockly.FieldDropdown([
        ["Forward", "FORWARD"],
        ["Backward", "BACKWARD"],
        ["Stop", "STOP"]
      ]), "DIRECTION")
      .appendField("動力")
      .appendField(new Blockly.FieldNumber(0, 0, 100), "SPEED");
    this.setPreviousStatement(true, null);
    this.setNextStatement(true, null);
    this.setColour(180);
    this.setTooltip("控制馬達方向與動力百分比 (0-100)");
    this.setHelpUrl("");
  }
};

Blockly.Blocks['舵機'] = {
  init: function () {
    this.appendDummyInput()
      .appendField("舵機")
      .appendField(new Blockly.FieldDropdown([
        ["1", "1"],
        ["2", "2"]
      ]), "SERVO")
      .appendField("角度")
      .appendField(new Blockly.FieldNumber(90, 0, 180), "ANGLE");
    this.setPreviousStatement(true, null);
    this.setNextStatement(true, null);
    this.setColour(40);
    this.setTooltip("控制舵機角度");
    this.setHelpUrl("");
  }
};
