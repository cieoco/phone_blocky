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
    // 數值插槽（可接數字、變數或算式）；舊存檔的 DELAY_TIME 格子由 app.js upgradeLegacyXml 轉換
    this.appendValueInput("DELAY_TIME").setCheck("Number").appendField("延遲");
    this.appendDummyInput().appendField("毫秒");
    this.setInputsInline(true);
    this.setPreviousStatement(true, null);
    this.setNextStatement(true, null);
    this.setColour(290);
    this.setTooltip("延遲毫秒。直接填數字時限 0–10000；接變數／算式時負值視為 0。");
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

// 流程次數積木：重複 N 次（輸入名稱 TIMES / DO 不可改，舊存檔 XML 依賴）
Blockly.Blocks['controls_repeat_ext'] = {
  init: function () {
    this.appendValueInput("TIMES").setCheck("Number").appendField("重複");
    this.appendDummyInput().appendField("次");
    this.appendStatementInput("DO").setCheck(null).appendField("執行");
    this.setInputsInline(true);
    this.setPreviousStatement(true, null);
    this.setNextStatement(true, null);
    this.setColour(210);
    this.setTooltip("把裡面的積木重複指定次數。次數在開始時決定；裡面的指令會連續執行，需要間隔請自行加延遲。可巢狀，最多 8 層。");
    this.setHelpUrl("");
  }
};

// 條件迴圈：當條件成立時重複／重複直到條件成立（欄位 MODE / BOOL / DO 不可改）
Blockly.Blocks['controls_whileUntil'] = {
  init: function () {
    this.appendValueInput("BOOL").setCheck("Boolean").appendField(new Blockly.FieldDropdown([
      ["當條件成立時重複", "WHILE"],
      ["重複直到條件成立", "UNTIL"]
    ]), "MODE");
    this.appendStatementInput("DO").setCheck(null).appendField("執行");
    this.setPreviousStatement(true, null);
    this.setNextStatement(true, null);
    this.setColour(210);
    this.setTooltip("每圈重新判斷條件。條件一直不變時會一直重複，按「停止」可中止；迴圈後面的積木要等迴圈結束才會執行。可巢狀，最多 8 層。");
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
      ["−", "MINUS"],
      ["×", "MULTIPLY"],
      ["÷", "DIVIDE"],
      ["餘數", "MODULO"]
    ]), "OP");
    this.appendValueInput("B").setCheck("Number");
    this.setInputsInline(true);
    this.setOutput(true, "Number");
    this.setColour(230);
    this.setTooltip("兩數運算（整數）。÷ 會捨去小數；餘數的正負號跟左邊；除以 0 得 0。");
  }
};

// 絕對值
Blockly.Blocks['math_abs'] = {
  init: function () {
    this.appendValueInput("VALUE").setCheck("Number").appendField("絕對值");
    this.setOutput(true, "Number");
    this.setColour(230);
    this.setTooltip("去掉負號，例如 -5 → 5");
  }
};

// 隨機整數（含兩端）
Blockly.Blocks['math_random_int'] = {
  init: function () {
    this.appendValueInput("FROM").setCheck("Number").appendField("隨機整數 從");
    this.appendValueInput("TO").setCheck("Number").appendField("到");
    this.setInputsInline(true);
    this.setOutput(true, "Number");
    this.setColour(230);
    this.setTooltip("每次執行到時，產生一個介於兩數之間（含兩端）的整數");
  }
};

// 限制範圍（Arduino constrain）
Blockly.Blocks['math_constrain'] = {
  init: function () {
    this.appendValueInput("VALUE").setCheck("Number").appendField("限制");
    this.appendValueInput("LOW").setCheck("Number").appendField("在");
    this.appendValueInput("HIGH").setCheck("Number").appendField("到");
    this.appendDummyInput().appendField("之間");
    this.setInputsInline(true);
    this.setOutput(true, "Number");
    this.setColour(230);
    this.setTooltip("小於下限就用下限、大於上限就用上限，例如把動力限制在 0–100");
  }
};

// 對應換算（Arduino map）：直式排列，手機上比較好看
Blockly.Blocks['math_map'] = {
  init: function () {
    this.appendValueInput("VALUE").setCheck("Number").appendField("對應換算");
    this.appendValueInput("FROM_LOW").setCheck("Number").setAlign(Blockly.ALIGN_RIGHT).appendField("原範圍 從");
    this.appendValueInput("FROM_HIGH").setCheck("Number").setAlign(Blockly.ALIGN_RIGHT).appendField("到");
    this.appendValueInput("TO_LOW").setCheck("Number").setAlign(Blockly.ALIGN_RIGHT).appendField("新範圍 從");
    this.appendValueInput("TO_HIGH").setCheck("Number").setAlign(Blockly.ALIGN_RIGHT).appendField("到");
    this.setInputsInline(false);
    this.setOutput(true, "Number");
    this.setColour(230);
    this.setTooltip("按比例把數值從原範圍換到新範圍，例如距離 0–100 cm → 舵機 0–180°。超出原範圍時結果也會超出，需要時外面再包「限制」。");
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

// 且／或：兩個條件都成立／任一成立（韌體短路求值）
Blockly.Blocks['logic_operation'] = {
  init: function () {
    this.appendValueInput("A").setCheck("Boolean");
    this.appendValueInput("B").setCheck("Boolean").appendField(new Blockly.FieldDropdown([
      ["且", "AND"],
      ["或", "OR"]
    ]), "OP");
    this.setInputsInline(true);
    this.setOutput(true, "Boolean");
    this.setColour(270);
    this.setTooltip("且：兩邊條件都成立才成立；或：任一邊成立就成立");
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
      ]), "DIRECTION");
    this.appendValueInput("PWM").setCheck("Number").appendField("動力");
    this.appendDummyInput().appendField("%");
    this.setInputsInline(true);
    this.setPreviousStatement(true, null);
    this.setNextStatement(true, null);
    this.setColour(180);
    this.setTooltip("控制馬達動力 (0–100%)，方向由下拉選單決定。可接變數或算式，超出範圍時韌體會夾在 ±100。");
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
      .appendField("移動到角度");
    this.appendValueInput("DEG").setCheck("Number");
    this.appendDummyInput().appendField("°");
    this.setInputsInline(true);
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
      .appendField("轉速");
    this.appendValueInput("RPM").setCheck("Number");
    this.appendDummyInput().appendField("RPM");
    this.setInputsInline(true);
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
      .appendField("相對旋轉");
    this.appendValueInput("DEG").setCheck("Number");
    this.appendDummyInput().appendField("°");
    this.setInputsInline(true);
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
      .appendField("角度");
    this.appendValueInput("DEG").setCheck("Number");
    this.setInputsInline(true);
    this.setPreviousStatement(true, null);
    this.setNextStatement(true, null);
    this.setColour(40);
    this.setTooltip("控制舵機角度 (0–180°)。可接變數或算式，超出範圍時韌體會夾在 0–180。");
    this.setHelpUrl("");
  }
};
