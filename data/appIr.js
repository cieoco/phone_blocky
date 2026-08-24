/********************************************
 * (2) 定義 IR Node 類別 (中間表示)
 ********************************************/

// 所有 IR Node 的基底類別
class BaseNode {
    toJson() {
        throw new Error("BaseNode: toJson() must be implemented by subclass");
    }
}

class arduino_setupNode extends BaseNode {
    constructor(body = []) {
        super();
        this.body = body;
    }
    toJson() {
        return {
            command: "arduino_setup",
            body: this.body.map(cmd => cmd.toJson())
        };
    }
}

class arduino_loopNode extends BaseNode {
    constructor(body = []) {
        super();
        this.body = body;
    }
    toJson() {
        return {
            command: "arduino_loop",
            body: this.body.map(cmd => cmd.toJson())
        };
    }
}


class PinModeNode extends BaseNode {
    constructor(pin, mode) {
        super();
        this.pin = pin;
        this.mode = mode;
    }
    toJson() {
        return {
            command: "pinMode",
            pin: this.pin,
            mode: this.mode
        };
    }
}

// DigitalWriteNode: 代表 「digitalWrite」 的 IR
class DigitalWriteNode extends BaseNode {
    constructor(pin, state) {
        super();
        this.pin = pin;
        this.state = state; // "HIGH" or "LOW"
    }
    toJson() {
        return {
            command: "digitalWrite",
            pin: this.pin,
            state: this.state
        };
    }
}

// AnalogWriteNode: 代表 「analogWrite」 的 IR
class AnalogWriteNode extends BaseNode {
    constructor(pin, value) {
        super();
        this.pin = pin;
        this.value = value;
    }
    toJson() {
        return {
            command: "analogWrite",
            pin: this.pin,
            value: this.value
        };
    }
}

class DigitalReadNode extends BaseNode {
    constructor(pin) {
        super();
        this.pin = pin;
    }
    toJson() {
        return {
            command: "digitalRead",
            pin: this.pin
        };
    }
}

class LegoButtonNode extends BaseNode {
    constructor(pin) {
        super();
        this.pin = pin;
    }
    toJson() {
        return {
            command: "legoButton",
            pin: this.pin
        };
    }
}



class AnalogReadNode extends BaseNode {
    constructor(pin) {
        super();
        this.pin = pin;
    }
    toJson() {
        return {
            command: "analogRead",
            pin: this.pin
        };
    }
}

class ArduinoUltrasonicNode extends BaseNode {
    constructor(trig, echo) {
        super();
        this.trig = trig;
        this.echo = echo;
    }
    toJson() {
        return {
            command: "arduinoUltrasonic",
            trigPin: this.trig,
            echoPin: this.echo
        };
    }
}

class DelayNode extends BaseNode {
    constructor(delayTime) {
        super();
        this.delayTime = delayTime;
    }
    toJson() {
        return {
            command: "delay",
            delayTime: this.delayTime
        };
    }
}

class SerialPrintlnNode extends BaseNode {
    constructor(content) {
        super();
        this.content = content;
    }
    toJson() {
        return {
            command: "serial_println",
            content: (this.content && typeof this.content.toJson === "function") ?
                this.content.toJson() :
                this.content
        };
    }
}


class MessageNode extends BaseNode {
    constructor(content) {
        super();
        this.content = content;
    }
    toJson() {
        return {
            command: "message_print",
            content: (this.content && typeof this.content.toJson === "function") ?
            this.content.toJson() :
            this.content
        };
    }
}

class PlotNode extends BaseNode {
    constructor(series, unit, value) { super(); this.series = series; this.unit = unit; this.value = value; }
    toJson() {
        return { command: "plot_print", series: this.series, unit: this.unit,
            value: (this.value && typeof this.value.toJson === "function") ? this.value.toJson() : this.value };
    }
}

class IfNode extends BaseNode {
    constructor(condition, thenBranch = [], elseBranch = []) {
        super();
        this.condition = condition;
        this.thenBranch = thenBranch;
        this.elseBranch = elseBranch;
    }
    toJson() {
        return {
            command: "if",
            condition: (this.condition && typeof this.condition.toJson === "function") ?
                this.condition.toJson() : this.condition,
            then: this.thenBranch.map(node => node.toJson()),
            else: this.elseBranch.map(node => node.toJson())
        };
    }
}



class RepeatNode extends BaseNode {
    constructor(times) {
        super();
        this.times = times;
    }
    toJson() {
        return {
            command: "repeat",
            times: this.times
        };
    }
}

class WhileNode extends BaseNode {
    constructor(condition, doCommands, mode) {
        super();
        this.condition = condition;
        this.doCommands = doCommands || [];
        this.mode = mode; // "WHILE" 或 "UNTIL"
    }
    toJson() {
        return {
            command: this.mode === "WHILE" ? "while" : "until",
            condition: (this.condition && typeof this.condition.toJson === "function") ?
                this.condition.toJson() : this.condition,
            do: this.doCommands.map(cmd => (cmd.toJson ? cmd.toJson() : cmd))
        };
    }
}

class ArduinoMillisNode extends BaseNode {
    constructor() {
        super();
    }
    toJson() {
        return {
            command: "arduino_millis"
        };
    }
}

class MathNumberNode extends BaseNode {
    constructor(number) {
        super();
        this.number = number;
    }
    toJson() {
        return {
            command: "math_number",
            number: this.number
        };
    }
}

class MathArithmeticNode extends BaseNode {
    constructor(operator, leftOperand, rightOperand) {
        super();
        this.operator = operator;
        this.leftOperand = leftOperand;
        this.rightOperand = rightOperand;
    }
    toJson() {
        return {
            command: "math_arithmetic",
            operator: this.operator,
            left: (this.leftOperand && typeof this.leftOperand.toJson === "function") ? this.leftOperand.toJson() : this.leftOperand,
            right: (this.rightOperand && typeof this.rightOperand.toJson === "function") ? this.rightOperand.toJson() : this.rightOperand
        };
    }
}

class LogicBooleanNode extends BaseNode {
    constructor(boolStr) {
        super();
        // 將字串 "TRUE" 轉換為 true，其餘視為 false
        this.value = (boolStr === "TRUE");
    }
    toJson() {
        return {
            command: "logic_boolean",
            value: this.value
        };
    }
}


class LogicCompareNode extends BaseNode {
    constructor(operator, leftOperand, rightOperand) {
        super();
        this.operator = operator;
        this.leftOperand = leftOperand;
        this.rightOperand = rightOperand;
    }
    toJson() {
        return {
            command: "logic_compare",
            operator: this.operator,
            left: (this.leftOperand && typeof this.leftOperand.toJson === "function") ? this.leftOperand.toJson() : this.leftOperand,
            right: (this.rightOperand && typeof this.rightOperand.toJson === "function") ? this.rightOperand.toJson() : this.rightOperand
        };
    }
}

//
class LogicNegateNode extends BaseNode {
    constructor(content) {
        super();
        this.content = content;
    }
    toJson() {
        return {
            command: "logic_negate",
            content: (this.content && typeof this.content.toJson === "function") ?
            this.content.toJson() : this.content
        };
    }
}

class VariableDeclareNode extends BaseNode {
    constructor(variableName) {
        super();
        this.variableName = variableName;
    }
    toJson() {
        return {
            command: "variable_declare",
            variableName: this.variableName
        };
    }
}

class VariableSetNode extends BaseNode {
    constructor(variableName, value) {
      super();
      this.variableName = variableName;
      this.value = value;  // 儲存變數的設定值（可能是其他積木的 IR 節點）
    }
    toJson() {
      return {
        command: "variable_set",
        variableName: this.variableName,
        value: (this.value && typeof this.value.toJson === "function")
                 ? this.value.toJson()
                 : this.value
      };
    }
  }
  

  class MathChangeNode extends BaseNode {
    constructor(variableName, value) {
        super();
        this.variableName = variableName;
        this.value = value; // 儲存變數的設定值（可能是其他積木的 IR 節點）
    }
    toJson() {
        return {
            command: "math_change",
            variableName: this.variableName,  // 加上逗號
            value: (this.value && typeof this.value.toJson === "function")
                 ? this.value.toJson()
                 : this.value
        };
    }
}

class VariableGetNode extends BaseNode {
    constructor(variableName) {
        super();
        this.variableName = variableName;
    }
    toJson() {
        return {
            command: "variable_get",
            variableName: this.variableName
        };
    }
}

// ============================================================
// 新格式 IR Nodes — cmd 風格，與 motorControl 對齊
// ============================================================

class MotorPwmNode extends BaseNode {
    constructor(motor, direction, pwm) {
        super();
        this.motor = motor;
        this.direction = direction;
        this.pwm = pwm;
    }
    toJson() {
        const duty = parseInt(this.pwm);
        if (this.direction === 'STOP' || duty === 0) {
            return { cmd: 'stop', motor: parseInt(this.motor) };
        }
        return {
            cmd: 'pwm',
            motor: parseInt(this.motor),
            duty: this.direction === 'BACKWARD' ? -duty : duty
        };
    }
}

class MotorStopNode extends BaseNode {
    constructor(motor) {
        super();
        this.motor = motor;
    }
    toJson() {
        const m = parseInt(this.motor);
        const obj = { cmd: "stop" };
        if (m > 0) obj.motor = m;
        return obj;
    }
}

class MotorSpeedNode extends BaseNode {
    constructor(motor, rpm) {
        super();
        this.motor = motor;
        this.rpm = rpm;
    }
    toJson() {
        return {
            cmd: "speed",
            motor: parseInt(this.motor),
            rpm: parseInt(this.rpm)
        };
    }
}

class MotorMoveByNode extends BaseNode {
    constructor(motor, deg) {
        super();
        this.motor = motor;
        this.deg = deg;
    }
    toJson() {
        return {
            cmd: "move_by",
            motor: parseInt(this.motor),
            deg: Math.round(parseFloat(this.deg) * 100)
        };
    }
}

class MotorPositionNode extends BaseNode {
    constructor(motor, deg) {
        super();
        this.motor = motor;
        this.deg = deg;
    }
    toJson() {
        return {
            cmd: "move_to",
            motor: parseInt(this.motor),
            deg: Math.round(parseFloat(this.deg) * 100)
        };
    }
}

class MotorZeroNode extends BaseNode {
    constructor(motor) {
        super();
        this.motor = motor;
    }
    toJson() {
        return {
            cmd: "zero",
            motor: parseInt(this.motor)
        };
    }
}

class ServoSetNode extends BaseNode {
    constructor(ch, deg) {
        super();
        this.ch = ch;
        this.deg = deg;
    }
    toJson() {
        return {
            cmd: "servo",
            ch: parseInt(this.ch),
            deg: parseInt(this.deg)
        };
    }
}

// ============================================================
// 舊格式 IR Nodes (deprecated shim)
// ============================================================

class MotorNode extends BaseNode {
    constructor(motor, direction, power) {
        super();
        this.motor = motor;
        this.direction = direction;
        this.power = power;
    }
    toJson() {
        const power = parseInt(this.power);
        if (this.direction === "STOP" || power === 0) {
            return { cmd: "stop", motor: parseInt(this.motor) };
        }
        return {
            cmd: "pwm",
            motor: parseInt(this.motor),
            duty: this.direction === "BACKWARD" ? -power : power
        };
    }
}

class ServoNode extends BaseNode {
    constructor(servo, angle) {
        super();
        this.servo = servo;
        this.angle = angle;
    }
    toJson() {
        return {
            command: "servo_control",
            servo: this.servo,
            angle: this.angle
        };
    }
}



/********************************************
 * (3) 定義「翻譯器」：每個積木如何轉成 IR Node
 ********************************************/

/**共用函數，解析積木鏈 
 * 取得積木指定輸入的值，如果有連接則用該連接的翻譯器處理，
 * 否則直接從欄位讀取。
 * @param {Object} block - 當前積木
 * @param {string} inputName - 輸入連接或欄位名稱
 * @returns 轉換後的值或欄位值
 */

function getBlockValue(block, inputName) {
    const connectedBlock = block.getInputTargetBlock(inputName);
    if (connectedBlock) {
        const translator = getTranslator(connectedBlock.type);
        if (translator) {
            return translator(connectedBlock);
        }
    }
    return block.getFieldValue(inputName);
}



function translateArduinoSetup(block) {
    // 取得 SETUP_BODY 輸入的積木鏈
    const bodyBlock = block.getInputTargetBlock("SETUP_BODY");
    let body = bodyBlock ? parseBlockChain(bodyBlock) : [];
    return new arduino_setupNode(body);
}

function translateArduinoLoop(block) {
    // 取得 LOOP_BODY 輸入的積木鏈
    const bodyBlock = block.getInputTargetBlock("LOOP_BODY");
    let body = bodyBlock ? parseBlockChain(bodyBlock) : [];
    return new arduino_loopNode(body);
}


function translatePinMode(block) {
    const pin = block.getFieldValue("PIN");
    const mode = block.getFieldValue("MODE");
    return new PinModeNode(pin, mode);
}

function translateDigitalWrite(block) {
    const pin = block.getFieldValue("PIN");
    const state = block.getFieldValue("VALUE");
    return new DigitalWriteNode(pin, state);
}
function translateLegoButton(block) {
    const pin = block.getFieldValue("PIN");
    return new LegoButtonNode(pin);
}

function translateAnalogWrite(block) {
    const pin = block.getFieldValue("PIN");
    const val = parseInt(block.getFieldValue("VALUE"), 10);
    return new AnalogWriteNode(pin, val);
}

function translateDigitalRead(block) {
    const pin = block.getFieldValue("PIN");
    return new DigitalReadNode(pin);
}

function translateAnalogRead(block) {
    const pin = block.getFieldValue("PIN");
    return new AnalogReadNode(pin);
}

function translateArduinoUltrasonic(block) {
    const trig = block.getFieldValue("TRIG");
    const echo = block.getFieldValue("ECHO");
    return new ArduinoUltrasonicNode(trig, echo);
}

function translateDelay(block) {
    const delayTime = block.getFieldValue("DELAY_TIME");
    return new DelayNode(delayTime);
}

function translateSerialPrintln(block) {
    // 使用共用輔助函式取得 "CONTENT" 的值
    let content = getBlockValue(block, "CONTENT");
    return new SerialPrintlnNode(content);
}


function translateMessage(block) {
    let content = getBlockValue(block, "TEXT");
    return new MessageNode(content);
}

function translatePlot(block) {
    return new PlotNode(block.getFieldValue("SERIES"), block.getFieldValue("UNIT"), getBlockValue(block, "VALUE"));
}

function translateIf(block) {
    // 取得 IF 條件區塊 (這裡只示範單一 if 分支)
    const conditionBlock = block.getInputTargetBlock("IF0");
    let condition = conditionBlock ? getTranslator(conditionBlock.type)(conditionBlock) : null;

    // 解析 then 區塊，僅解析線性連結
    const doBlock = block.getInputTargetBlock("DO0");
    let thenBranch = doBlock ? parseBlockChain(doBlock) : [];

    // 解析 else 區塊
    const elseBlock = block.getInputTargetBlock("ELSE");
    let elseBranch = elseBlock ? parseBlockChain(elseBlock) : [];

    return new IfNode(condition, thenBranch, elseBranch);
}


function translateRepeat(block) {
    const times = block.getFieldValue("TIMES");
    return new RepeatNode(times);
}

function translateWhile(block) {
    const mode = block.getFieldValue("MODE");
    const conditionBlock = block.getInputTargetBlock("BOOL");
    let condition = conditionBlock ? getTranslator(conditionBlock.type)(conditionBlock) : null;

    // 使用 parseBlockChain 來解析 while 內部所有命令
    const doBlock = block.getInputTargetBlock("DO");
    let doCommands = doBlock ? parseBlockChain(doBlock) : [];

    return new WhileNode(condition, doCommands, mode);
}

function translateMillis(block) {
    return new ArduinoMillisNode();
}

function translateMathNumber(block) {
    const number = block.getFieldValue("NUM");
    return new MathNumberNode(number);
}

function translateMathArithmetic(block) {
    const operator = block.getFieldValue("OP");
    const leftOperand = getBlockValue(block, "A");
    const rightOperand = getBlockValue(block, "B");
    return new MathArithmeticNode(operator, leftOperand, rightOperand);
  }


function translateLogicBoolean(block) {
    const boolStr = block.getFieldValue("BOOL");
    return new LogicBooleanNode(boolStr);
}

function translateLogicCompare(block) {
    const operator = block.getFieldValue("OP");
    const leftOperand = getBlockValue(block, "A");
    const rightOperand = getBlockValue(block, "B");
    return new LogicCompareNode(operator, leftOperand, rightOperand);
}

function translateLogicNegate(block) {
    const boolBlock = block.getInputTargetBlock("BOOL");
    const content = boolBlock ? getTranslator(boolBlock.type)(boolBlock) : null;
    return new LogicNegateNode(content);
}

function translateVariableDeclare(block) {
    const variableName = block.getFieldValue("VAR");
    return new VariableDeclareNode(variableName);
}

function translateVariableSet(block) {
    const variableName = block.getFieldValue("VAR");
    const valueBlock = block.getInputTargetBlock("VALUE");
    let value = valueBlock ? getTranslator(valueBlock.type)(valueBlock) : null;
    return new VariableSetNode(variableName, value);
  }

function translateMathChange(block) {
    const variableName = block.getFieldValue("VAR");
    const valueBlock = block.getInputTargetBlock("DELTA");
    let value = valueBlock ? getTranslator(valueBlock.type)(valueBlock) : null;
    return new MathChangeNode(variableName, value);
}

function translateVariableGet(block) {
    const variableName = block.getFieldValue("VAR");
    return new VariableGetNode(variableName);
}

// New format translators
function translateMotorPwm(block) {
    const motor = block.getFieldValue("MOTOR");
    const direction = block.getFieldValue("DIRECTION");
    const pwm = block.getFieldValue("PWM");
    return new MotorPwmNode(motor, direction, pwm);
}

function translateMotorStop(block) {
    const motor = block.getFieldValue("MOTOR");
    return new MotorStopNode(motor);
}

function translateMotorSpeed(block) {
    const motor = block.getFieldValue("MOTOR");
    const rpm = block.getFieldValue("RPM");
    return new MotorSpeedNode(motor, rpm);
}

function translateMotorMoveBy(block) {
    const motor = block.getFieldValue("MOTOR");
    const deg = block.getFieldValue("DEG");
    return new MotorMoveByNode(motor, deg);
}

function translateMotorPosition(block) {
    const motor = block.getFieldValue("MOTOR");
    const deg = block.getFieldValue("DEG");
    return new MotorPositionNode(motor, deg);
}

function translateMotorZero(block) {
    const motor = block.getFieldValue("MOTOR");
    return new MotorZeroNode(motor);
}

function translateServoSet(block) {
    const ch = block.getFieldValue("CH");
    const deg = block.getFieldValue("DEG");
    return new ServoSetNode(ch, deg);
}

// Legacy translators (deprecated shim)
function translateMotor(block) {
    const motor = block.getFieldValue("MOTOR");
    const direction = block.getFieldValue("DIRECTION");
    const speed = block.getFieldValue("SPEED");
    return new MotorNode(motor, direction, speed);
}

function translateServo(block) {
    const servo = block.getFieldValue("SERVO");
    const angle = block.getFieldValue("ANGLE");
    return new ServoNode(servo, angle);
}

/********************************************
 * 對外功能 IR（Blockly 模組功能契約 D1）
 ********************************************/

// 宣告節點。注意：宣告**不會**進 setup 指令陣列——payload 組裝時會把它們
// 「拉高」到 PROG JSON 的頂層 `functions`（見 app.js 的 buildProgJson）。
// 這樣主機一套用新程式就能立刻讀到功能表，不必等 setup 跑完。
class BlocklyFuncDeclareNode extends BaseNode {
    constructor(idx, analog, readable) {
        super();
        this.idx = idx;
        this.analog = analog;
        this.readable = readable;
    }
    toJson() {
        return {
            command: "blockly_func_declare",
            idx: this.idx,
            analog: this.analog,
            readable: this.readable
        };
    }
}

class BlocklyFuncGetNode extends BaseNode {
    constructor(idx) {
        super();
        this.idx = idx;
    }
    toJson() {
        return { command: "blockly_func_get", idx: this.idx };
    }
}

class BlocklyFuncSetNode extends BaseNode {
    constructor(idx, value) {
        super();
        this.idx = idx;
        this.value = value;
    }
    toJson() {
        return {
            command: "blockly_func_set",
            idx: this.idx,
            value: (this.value && typeof this.value.toJson === "function")
                     ? this.value.toJson()
                     : this.value
        };
    }
}

function translateBlocklyFuncDeclare(block) {
    return new BlocklyFuncDeclareNode(
        Number(block.getFieldValue("IDX")),
        block.getFieldValue("KIND") === "ANALOG",
        block.getFieldValue("READABLE") === "TRUE"
    );
}

function translateBlocklyFuncGet(block) {
    return new BlocklyFuncGetNode(Number(block.getFieldValue("IDX")));
}

function translateBlocklyFuncSet(block) {
    const valueBlock = block.getInputTargetBlock("VALUE");
    const translator = valueBlock ? getTranslator(valueBlock.type) : null;
    const value = translator ? translator(valueBlock) : null;
    return new BlocklyFuncSetNode(Number(block.getFieldValue("IDX")), value);
}

// 用一個小小的 getTranslator() 來集中管理
function getTranslator(blockType) {
    switch (blockType) {
        case "arduino_setup":
            return translateArduinoSetup;
        case "arduino_loop":
            return translateArduinoLoop;
        case "arduino_pinMode":
            return translatePinMode;
        case "arduino_digitalWrite":
            return translateDigitalWrite;
        case "arduino_analogWrite":
            return translateAnalogWrite;
        case "arduino_digitalRead":
            return translateDigitalRead;
        case "arduino_analogRead":
            return translateAnalogRead;
        case "lego_button":
            return translateLegoButton;
        case "arduino_ultrasonic":
            return translateArduinoUltrasonic;
        case "arduino_delay":
            return translateDelay;
        case "arduino_serial_println":
            return translateSerialPrintln;
        case "message_print":
            return translateMessage;
        case "plot_print":
            return translatePlot;
        case "controls_if":
            return translateIf;
        case "controls_repeat_ext":
            return translateRepeat;
        case "controls_whileUntil":
            return translateWhile;
        case "arduino_millis":
            return translateMillis;
        case "math_number":
            return translateMathNumber;
        case "math_arithmetic":
            return translateMathArithmetic;
        case "logic_boolean":
            return translateLogicBoolean;
        case "logic_compare":
            return translateLogicCompare;
        case "logic_negate":
            return translateLogicNegate;
        case "variables_declare":
            return translateVariableDeclare;
        case "variables_set":
            return translateVariableSet;
        case "math_change":
            return translateMathChange;
        case "variables_get":
            return translateVariableGet;
        case "motor_speed":
            return translateMotorSpeed;
        case "motor_move_by":
            return translateMotorMoveBy;
        case "motor_pwm":
            return translateMotorPwm;
        case "motor_stop":
            return translateMotorStop;
        case "motor_position":
            return translateMotorPosition;
        case "motor_zero":
            return translateMotorZero;
        case "servo_set":
            return translateServoSet;
        case "blockly_func_declare":
            return translateBlocklyFuncDeclare;
        case "blockly_func_get":
            return translateBlocklyFuncGet;
        case "blockly_func_set":
            return translateBlocklyFuncSet;
        case "馬達":
            return translateMotor;
        case "舵機":
            return translateServo;
        default:
            return null;
    }
}
