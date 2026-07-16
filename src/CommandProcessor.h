#ifndef COMMAND_PROCESSOR_H
#define COMMAND_PROCESSOR_H

#include <Arduino.h>
#include <freertos/semphr.h>
#include <vector>
#include <memory>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "PWMManager.h"
#include "MotorController.h"
#include "ServoController.h"
#include "EncoderHandler.h"
#include "HardwareConfig.h"
#include "CommandTranslator.h"
#include "CommChannel.h"
#include "config.h"

// --- 效能優化：定義指令的枚舉和結構體 ---
enum CommandType {
  CMD_NONE,
  CMD_DIGITAL_WRITE,
  CMD_ANALOG_WRITE,
  CMD_DELAY,
  CMD_MOTOR,
  CMD_MOTOR_POSITION,
  CMD_MOTOR_ZERO,
  CMD_MOTOR_SPEED,
  CMD_SERVO,
  CMD_PRINT,
  CMD_PLOT,
  CMD_VARIABLE_DECLARE,
  CMD_VARIABLE_SET,
  CMD_MATH_CHANGE,
  CMD_IF,
  CMD_WHILE,
};

// Blockly value 積木的執行期表示；可遞迴組成算術與邏輯運算式。
struct BlocklyValueExpression {
  String kind;
  String op;
  String variableName;
  int value = 0;
  int pin = -1;
  int auxPin = -1;
  std::shared_ptr<BlocklyValueExpression> left;
  std::shared_ptr<BlocklyValueExpression> right;
};

struct BlocklyCommand {
  CommandType type = CMD_NONE;
  int pin;
  int value;
  int motor_id;
  char direction;
  int speed;   // signed when direction=='P' (PWM ±100)
  int servo_id;
  int angle;
  int delay_ms;
  String message;
  String plotSeries;
  String plotUnit;
  String variableName;
  std::shared_ptr<BlocklyValueExpression> valueExpr;

  std::vector<BlocklyCommand> nested_commands_then;
  std::vector<BlocklyCommand> nested_commands_else;
};
// --- 效能優化結束 ---


class CommandProcessor
{
private:
  enum MotorRunMode {
    MOTOR_MODE_IDLE,
    MOTOR_MODE_PWM,
    MOTOR_MODE_SPEED,
    MOTOR_MODE_POSITION,
  };

  enum PositionPhase {
    POSITION_PHASE_DRIVE,
    POSITION_PHASE_BRAKE,
    POSITION_PHASE_SETTLE,
    POSITION_PHASE_HOLD,   // 到位後持續輕力修正，維持角度
  };

  struct MotorRuntimeState {
    MotorRunMode mode = MOTOR_MODE_IDLE;
    PositionPhase positionPhase = POSITION_PHASE_DRIVE;
    bool positionHold = false; // 到位後是否進入角度保持（joy 角度模式 + move_to/move_by）
    float rpmCmd = 0.0f;
    float rampedRpmCmd = 0.0f; // 斜坡後的速度參考（避免階躍指令讓前饋瞬間全開衝過頭）
    float rpmMeas = 0.0f;
    long targetTicks = 0;
    int duty = 0; // signed PWM percent, -100..100
    long lastTicks = 0;
    long positionWatchTicks = 0;
    long positionDriveStartTicks = 0;
    unsigned long lastSampleMs = 0;
    unsigned long positionStartMs = 0;
    unsigned long positionLastMoveMs = 0;
    unsigned long positionPhaseStartMs = 0;
    int positionBrakeDuty = 0;
    float speedIntegral = 0.0f;
    float speedLastError = 0.0f;
    float positionIntegral = 0.0f;
    float positionLastError = 0.0f;
  };

  MotorController &motorCtrl;
  ServoController &servoCtrl;
  EncoderHandler *encoder;
  PWMManager &pwmMgr;

  int x1Value, y1Value, x2Value, y2Value;
  int S1_value, S2_value;
  int Button1_value, Button2_value, Action3_value, Action4_value;
  int M3_value, M4_value, M5_value, M6_value;
  int M3A_value, M4A_value;
  int joyM3TargetCentiDeg = 0;
  int joyM4TargetCentiDeg = 0;
  bool joyServoFieldsPresent = true;

  bool waitingForResponse = false;
  unsigned long lastMsgPrintTime = 0;
  unsigned long lastPlotPrintTime = 0;
  unsigned long lastPIDTime = 0;
  unsigned long lastTelemetryTime = 0;
  unsigned long lastPositionLogMs[NUM_MOTORS + 1] = {0};
  uint8_t telemetryChannels = 0;   // 哪些通道訂閱了 telemetry（bitmask，精準路由）
  uint8_t replyChannel = Comm::CH_WS; // 當前指令的回應通道（processCommands 進入時設定）
  Preferences configPrefs;         // 參數持久化（NVS "motor" 命名空間）

  // 速度環：目標斜坡率與積分抗飽和上限（對齊 motorControl 的 RAMP / INTEGRAL_LIMIT 設計）
  static constexpr float SPEED_RAMP_RPM_S = 500.0f;        // 速度參考每秒最大變化量
  static constexpr float SPEED_INTEGRAL_DUTY_LIMIT = 60.0f; // 限制 |ki*積分| 的 duty 貢獻上限

  static constexpr unsigned long POSITION_TIMEOUT_MS = 3000;
  static constexpr unsigned long POSITION_STALL_MS = 500;
  static constexpr unsigned long POSITION_BRAKE_MS = 120;
  static constexpr unsigned long POSITION_SETTLE_MS = 80;
  static constexpr long POSITION_STALL_TICKS = 2;
  static constexpr long POSITION_BRAKE_MIN_TRAVEL_TICKS = 24;
  static constexpr float POSITION_TOLERANCE_DEG = 5.0f;
  static constexpr float POSITION_DONE_DEG = 1.5f;
  static constexpr float POSITION_BRAKE_WINDOW_DEG = 14.0f;
  static constexpr int POSITION_BRAKE_DUTY = 35;

  // 角度保持（hold）：主動式（伺服）P + 積分創爬。
  // 小誤差時 P 出力不足以破靜摩擦會卡住，靠積分慢慢加力把軸推進死區，進死區即放鬆並歸零積分。
  // 以下三項可由 set.html 即時調整 / 存 NVS；其餘為程式內固定值。
  float holdSettleDeg = DEFAULT_HOLD_SETTLE_DEG; // 進入此誤差內 → 放鬆、歸零積分（死區）
  float holdKi = DEFAULT_HOLD_KI;                // I：卡住時的創爬速率（每 deg·s 增加的 duty）
  int holdMaxDuty = DEFAULT_HOLD_MAX_DUTY;       // 夾持力上限 PWM%
  static constexpr float POSITION_HOLD_REENGAGE_MARGIN = 1.0f; // 重啟門檻 = settle + 此邊際（遲滯）
  static constexpr int POSITION_HOLD_MIN_DUTY = 18;            // 出力時的最小力道
  static constexpr float POSITION_HOLD_GAIN = 3.0f;            // P：每度誤差增加的 duty

  float kp = DEFAULT_PID_KP;
  float ki = DEFAULT_PID_KI;
  float kd = DEFAULT_PID_KD;
  int minSpeed = DEFAULT_MIN_SPEED;
  int maxSpeed = DEFAULT_MAX_SPEED;
  bool speedFFEnabled = DEFAULT_SPEED_FF_ENABLED;
  float speedFFkS = DEFAULT_SPEED_FF_KS;
  float speedFFkV = DEFAULT_SPEED_FF_KV;
  float speedFFkA = DEFAULT_SPEED_FF_KA;
  float gearRatio = GEAR_RATIO;
  float encoderPPR = ENCODER_PPR;
  int encoderPos = DEFAULT_ENCODER_POS;
  int posCtrlMode = DEFAULT_POS_CTRL_MODE;
  float posKp = DEFAULT_POS_KP;
  float posKi = DEFAULT_POS_KI;
  float posKd = DEFAULT_POS_KD;
  int posMaxDuty = DEFAULT_POS_MAX_DUTY;
  float posToleranceDeg = DEFAULT_POS_TOLERANCE_DEG;

  float m3_integral = 0;
  float m3_lastError = 0;
  float m4_integral = 0;
  float m4_lastError = 0;

  bool sysIdActive = false;
  int sysIdMotor = 0;
  int sysIdVoltage = 0;
  unsigned long sysIdStartTime = 0;
  unsigned long lastSysIdLogTime = 0;

  struct SysIdPoint {
    unsigned long time;
    long ticks;
  };
  static const int MAX_SYSID_POINTS = 50;
  SysIdPoint sysIdBuffer[MAX_SYSID_POINTS];
  int sysIdPointCount = 0;

  // 高速擷取緩存（autotune 用）：10Hz telemetry 量不到馬達上升段（τ 被低估成地板值），
  // 改用韌體端在控制迴圈（~100Hz）裡以固定間隔（預設 20ms / 50Hz）擷取，停掃後一次回傳。
  // 與 motorControl/motor_control_v4 的 chart_buffer_* 協議對齊。
  struct ChartSample {
    uint32_t t_ms;
    float rpm_meas;
    float rpm_cmd;
    long deg_centi;
    int duty;
  };
  static constexpr size_t kMaxChartSamples = 400;
  ChartSample chartBuffer[kMaxChartSamples];
  size_t chartBufferCount = 0;
  bool chartBufferActive = false;        // 取樣中（由 motor task 擁有）
  bool chartBufferOverflow = false;
  bool chartBufferDumping = false;       // 回傳中（分批送，避免塞爆 async WS 佇列）
  size_t chartBufferDumpIdx = 0;
  uint32_t chartBufferStartMs = 0;
  uint32_t chartBufferLastMs = 0;
  uint32_t chartBufferIntervalMs = 20;
  int chartBufferMotor = 3;
  uint8_t chartBufferChannel = Comm::CH_WS;
  // 跨任務請求旗標：start/stop 在 AsyncTCP（WS）情境設定，實際緩存存取一律由 motor task 消化，
  // 確保 chartBuffer / 計數器只被單一任務讀寫，無資料競爭。
  volatile bool chartBufferStartReq = false;
  volatile bool chartBufferStopReq = false;
  volatile uint32_t chartBufferReqInterval = 20;
  volatile int chartBufferReqMotor = 3;
  volatile uint8_t chartBufferReqChannel = Comm::CH_WS;

  std::vector<BlocklyCommand> setupCommands;
  std::vector<BlocklyCommand> loopCommands;
  size_t loopIndex = 0;

  // PROG 程式跨任務交接：WS / 序列任務把解析結果放進 staging，jsonTask 在迴圈安全點
  // swap 套用。如此「執行中（含長 delay）的 active 向量」永遠只被 jsonTask 單一任務動到，
  // 杜絕「另一核心 clear/realloc 向量 → 執行端持有的參考懸空（use-after-free）」。
  std::vector<BlocklyCommand> pendingSetupCommands;
  std::vector<BlocklyCommand> pendingLoopCommands;
  volatile bool pendingProgram = false;
  SemaphoreHandle_t progMutex = nullptr;
  MotorRuntimeState motorState[NUM_MOTORS + 1];
  int servoDeg[NUM_SERVOS + 1] = {0, 90, 90};
  struct RuntimeVariable { String name; int value; };
  std::vector<RuntimeVariable> runtimeVariables;

  void (*sendJsonResponseCallback)(const DynamicJsonDocument &doc,
                                   uint8_t channels) = nullptr;

  float effectivePPR() const
  {
    return (encoderPos == ENCODER_POS_BEFORE_REDUCER)
        ? encoderPPR * gearRatio
        : encoderPPR;
  }

  float ticksPerDegree() const
  {
    return (effectivePPR() * ENCODER_MODE) / 360.0f;
  }

  float ticksPerRevolution() const
  {
    return effectivePPR() * ENCODER_MODE;
  }

  long centiDegToTicks(long centiDeg) const
  {
    return (long)((centiDeg / 100.0f) * ticksPerDegree());
  }

  long ticksToCentiDeg(long ticks) const
  {
    float tpd = ticksPerDegree();
    if (tpd <= 0.0f) return 0;
    return (long)((ticks * 100.0f) / tpd);
  }

  long getMotorTicks(int motor) const
  {
    if (!encoder) return 0;
    if (motor == 3) return encoder->getM3Ticks();
    if (motor == 4) return encoder->getM4Ticks();
    return 0;
  }

  const char *motorModeName(MotorRunMode mode) const
  {
    switch (mode) {
      case MOTOR_MODE_PWM:
        return "pwm";
      case MOTOR_MODE_SPEED:
        return "speed";
      case MOTOR_MODE_POSITION:
        return "position";
      case MOTOR_MODE_IDLE:
      default:
        return "idle";
    }
  }

  int speedLimitToDuty(int speed255) const
  {
    return constrain((int)(speed255 * 100L / 255L), 0, 100);
  }

  void resetMotorControlState(int motor)
  {
    if (motor < 1 || motor > NUM_MOTORS) return;
    motorState[motor].rpmCmd = 0.0f;
    motorState[motor].rampedRpmCmd = 0.0f;
    motorState[motor].targetTicks = getMotorTicks(motor);
    motorState[motor].positionWatchTicks = motorState[motor].targetTicks;
    motorState[motor].positionDriveStartTicks = motorState[motor].targetTicks;
    motorState[motor].speedIntegral = 0.0f;
    motorState[motor].speedLastError = 0.0f;
    motorState[motor].positionIntegral = 0.0f;
    motorState[motor].positionLastError = 0.0f;
    motorState[motor].positionStartMs = 0;
    motorState[motor].positionLastMoveMs = 0;
    motorState[motor].positionPhaseStartMs = 0;
    motorState[motor].positionBrakeDuty = 0;
    motorState[motor].positionPhase = POSITION_PHASE_DRIVE;
    motorState[motor].positionHold = false;
    lastPositionLogMs[motor] = 0;
  }

  void clearManualControlState()
  {
    x1Value = y1Value = x2Value = y2Value = 0;
    Button1_value = Button2_value = 0;
    Action3_value = Action4_value = 0;
    M3_value = M4_value = M5_value = M6_value = 0;
    M3A_value = M4A_value = 0;
    joyM3TargetCentiDeg = joyM4TargetCentiDeg = 0;
    m3_integral = m4_integral = 0.0f;
    m3_lastError = m4_lastError = 0.0f;
  }

  void setMotorDuty(int motor, int duty, MotorRunMode mode)
  {
    if (motor < 1 || motor > NUM_MOTORS) return;
    duty = constrain(duty, -100, 100);
    // 閉迴路（位置/速度）即使瞬間輸出 0 也要保留其控制模式，否則 updateAngleControl
    // 的 mode==POSITION 閘門會在輸出 0 的當下變成 idle，使控制迴路從此不再被呼叫，
    // 造成保持/驅動被永久鎖死（log 實證：mode=idle、duty=0、誤差一路放大）。
    if (duty == 0 && mode != MOTOR_MODE_POSITION && mode != MOTOR_MODE_SPEED) {
      motorState[motor].mode = MOTOR_MODE_IDLE;
    } else {
      motorState[motor].mode = mode;
    }
    motorState[motor].duty = duty;
    motorCtrl.pwmSigned(motor, duty);
  }

  void stopMotorRuntime(int motor)
  {
    if (motor < 1 || motor > NUM_MOTORS) return;
    motorCtrl.stopMotor(motor);
    motorState[motor].mode = MOTOR_MODE_IDLE;
    motorState[motor].duty = 0;
    resetMotorControlState(motor);
  }

  void releaseMotorOutput(int motor)
  {
    if (motor < 1 || motor > NUM_MOTORS) return;
    motorCtrl.stopMotor(motor);
    motorState[motor].duty = 0;
  }

  void stopAllRuntime()
  {
    motorCtrl.stopAll();
    for (int motor = 1; motor <= NUM_MOTORS; motor++) {
      motorState[motor].mode = MOTOR_MODE_IDLE;
      motorState[motor].duty = 0;
      resetMotorControlState(motor);
    }
  }

  bool zeroMotorPosition(int motor)
  {
    if (motor < 1 || motor > NUM_MOTORS || !MOTOR_CAPS[motor - 1].has_encoder || !encoder) {
      return false;
    }

    motorCtrl.stopMotor(motor);
    if (motor == 3) {
      encoder->resetM3();
      m3_integral = 0.0f;
      m3_lastError = 0.0f;
    } else if (motor == 4) {
      encoder->resetM4();
      m4_integral = 0.0f;
      m4_lastError = 0.0f;
    }

    motorState[motor].mode = MOTOR_MODE_IDLE;
    motorState[motor].duty = 0;
    resetMotorControlState(motor);
    Serial.printf("[CTRL] M%d zeroed current position as 0 deg\n", motor);
    return true;
  }

  void startMotorPositionMove(int motor, long targetTicks, const char *source,
                              bool hold = false)
  {
    if (motor < 1 || motor > NUM_MOTORS || !MOTOR_CAPS[motor - 1].has_encoder || !encoder) {
      return;
    }

    unsigned long now = millis();
    long currentTicks = getMotorTicks(motor);
    MotorRuntimeState &state = motorState[motor];
    state.mode = MOTOR_MODE_POSITION;
    state.targetTicks = targetTicks;
    state.positionIntegral = 0.0f;
    state.positionLastError = 0.0f;
    state.positionStartMs = now;
    state.positionLastMoveMs = now;
    state.positionPhaseStartMs = now;
    state.positionWatchTicks = currentTicks;
    state.positionDriveStartTicks = currentTicks;
    state.positionBrakeDuty = 0;
    state.positionPhase = POSITION_PHASE_DRIVE;
    state.positionHold = hold;
    lastPositionLogMs[motor] = 0;
    Serial.printf("[%s] move_to M%d current=%ld target=%ld hold=%d\n",
                  source, motor, currentTicks, targetTicks, hold ? 1 : 0);
  }

  // 到位處理：hold=true（joy 角度模式 / move_to / move_by）進入角度保持，其餘維持原本停止行為。
  void finishOrHoldPosition(int motor, long currentTicks)
  {
    MotorRuntimeState &state = motorState[motor];
    if (!state.positionHold) {
      stopMotorRuntime(motor);
      return;
    }
    releaseMotorOutput(motor);             // 放鬆輸出（duty=0），但維持 POSITION 模式
    state.positionPhase = POSITION_PHASE_HOLD;
    state.positionStartMs = 0;             // hold 期間關閉到位逾時
    state.positionLastMoveMs = 0;          // hold 期間關閉失速保護
    state.positionWatchTicks = currentTicks;
    state.positionIntegral = 0.0f;
    state.positionLastError = 0.0f;
    Serial.printf("[HOLD] M%d entering hold at ticks=%ld\n", motor, currentTicks);
  }

  // 角度保持（主動/伺服式）：持續通電。誤差在死區內不出力（避免齒隙抖動），
  // 超出死區即依比例施力把軸夾回目標。靠的是持續閉迴路修正，不需馬驅板硬體保持功能。
  void updateMotorPositionHold(int motor, float dt)
  {
    MotorRuntimeState &state = motorState[motor];
    float tpd = ticksPerDegree();
    if (tpd <= 0.0f) return;
    if (dt <= 0.0f) dt = 0.01f;

    long currentTicks = getMotorTicks(motor);
    float errorDeg = (float)(state.targetTicks - currentTicks) / tpd;
    float absErrorDeg = fabs(errorDeg);
    bool engaged = state.duty != 0;

    // 遲滯：出力中 → 進 settle 內才放鬆；放鬆中 → 偏離超過 reengage 才出力。
    float settleBand = holdSettleDeg;
    float reengageBand = holdSettleDeg + POSITION_HOLD_REENGAGE_MARGIN;
    float exitBand = engaged ? settleBand : reengageBand;
    if (absErrorDeg <= exitBand) {
      if (engaged) releaseMotorOutput(motor);
      state.positionIntegral = 0.0f;   // anti-windup：到位即清積分
      return;
    }

    // anti-windup：誤差跨越目標（換邊）時清積分，避免反向殘留把它推過頭
    if (errorDeg * state.positionIntegral < 0.0f) {
      state.positionIntegral = 0.0f;
    }
    state.positionIntegral += errorDeg * dt;
    // 積分項限幅，使其最多能把 duty 推到上限（anti-windup）
    float ki = holdKi > 0.001f ? holdKi : 0.001f;
    float iLimit = (float)holdMaxDuty / ki;
    state.positionIntegral = constrain(state.positionIntegral, -iLimit, iLimit);

    // P 比例 + I 創爬：卡住不動時 I 持續累加，慢慢把力道頂到能破靜摩擦
    float mag = POSITION_HOLD_MIN_DUTY + POSITION_HOLD_GAIN * absErrorDeg +
                holdKi * fabs(state.positionIntegral);
    int duty = constrain((int)mag, POSITION_HOLD_MIN_DUTY, holdMaxDuty);
    int signedDuty = errorDeg > 0 ? duty : -duty;
    setMotorDuty(motor, signedDuty, MOTOR_MODE_POSITION);
  }

  void updateJoyPositionTarget(int motor, int button, int action,
                               int targetDeg, int prevButton, int prevAction,
                               int &lastTargetCentiDeg)
  {
    if (!encoder || action != 2 || button != 1) return;

    int targetCentiDeg = targetDeg * 100;
    bool justStarted = prevAction != 2 || prevButton != 1;
    bool targetChanged = targetCentiDeg != lastTargetCentiDeg;
    if (!justStarted && !targetChanged) return;

    if (justStarted) {
      zeroMotorPosition(motor);
    }
    lastTargetCentiDeg = targetCentiDeg;
    startMotorPositionMove(motor, centiDegToTicks(targetCentiDeg), "JOY", true);
  }

  void updateMeasuredRpm(int motor, unsigned long now)
  {
    if (motor < 1 || motor > NUM_MOTORS || !MOTOR_CAPS[motor - 1].has_encoder) {
      return;
    }

    long ticks = getMotorTicks(motor);
    MotorRuntimeState &state = motorState[motor];
    if (state.lastSampleMs == 0) {
      state.lastSampleMs = now;
      state.lastTicks = ticks;
      state.rpmMeas = 0.0f;
      return;
    }

    float dt = (now - state.lastSampleMs) / 1000.0f;
    if (dt < 0.02f) return;

    float tpr = ticksPerRevolution();
    if (tpr <= 0.0f) return;

    long delta = ticks - state.lastTicks;
    state.rpmMeas = (delta / tpr) * (60.0f / dt);
    state.lastTicks = ticks;
    state.lastSampleMs = now;
  }

  void updateMotorAngle(int motor, long currentTicks, long targetTicks,
                        float &integral, float &lastError, float dt)
  {
    float tpd = ticksPerDegree();
    if (tpd <= 0.0f) return;

    float errorTicks = (float)(targetTicks - currentTicks);
    float errorDeg = errorTicks / tpd;
    float absErrorDeg = fabs(errorDeg);

    // Position control needs a gentle ramp. The shared PID defaults are tuned
    // too aggressively for angle targets and can overshoot around the setpoint.
    integral = 0.0f;
    lastError = errorDeg;

    if (absErrorDeg <= 2.0f) {
      if (lastPositionLogMs[motor] != 0) {
        Serial.printf("[CTRL] M%d reached current=%ld target=%ld error=%.1f deg\n",
                      motor, currentTicks, targetTicks, errorDeg);
      }
      stopMotorRuntime(motor);
      return;
    }

    int duty = constrain((int)(10.0f + absErrorDeg * 1.4f), 10, 65);
    unsigned long now = millis();
    if (now - lastPositionLogMs[motor] >= 250) {
      lastPositionLogMs[motor] = now;
      Serial.printf("[CTRL] M%d current=%ld target=%ld error=%.1f deg duty=%d\n",
                    motor, currentTicks, targetTicks, errorDeg,
                    errorDeg > 0 ? duty : -duty);
    }
    setMotorDuty(motor, errorDeg > 0 ? duty : -duty, MOTOR_MODE_POSITION);
  }

  int positionDutyForError(float absErrorDeg) const
  {
    if (absErrorDeg > 180.0f) return 55;
    if (absErrorDeg > 90.0f) return 45;
    if (absErrorDeg > 45.0f) return 35;
    if (absErrorDeg > 18.0f) return 26;
    if (absErrorDeg > 8.0f) return 22;
    return 18;
  }

  void beginPositionBrake(int motor, unsigned long now, long currentTicks,
                          float errorDeg)
  {
    MotorRuntimeState &state = motorState[motor];
    int travelDir = state.duty > 0 ? 1 : (state.duty < 0 ? -1 : (errorDeg > 0 ? 1 : -1));
    state.positionPhase = POSITION_PHASE_BRAKE;
    state.positionPhaseStartMs = now;
    state.positionBrakeDuty = -travelDir * POSITION_BRAKE_DUTY;
    Serial.printf("[CTRL] M%d counter-brake current=%ld target=%ld error=%.1f deg duty=%d\n",
                  motor, currentTicks, state.targetTicks, errorDeg, state.positionBrakeDuty);
    setMotorDuty(motor, state.positionBrakeDuty, MOTOR_MODE_POSITION);
  }

  void updateMotorPositionMovePID(int motor, unsigned long now, float dt)
  {
    MotorRuntimeState &state = motorState[motor];
    float tpd = ticksPerDegree();
    if (tpd <= 0.0f) return;

    long currentTicks = getMotorTicks(motor);
    float errorTicks = (float)(state.targetTicks - currentTicks);
    float errorDeg = errorTicks / tpd;
    float absErrorDeg = fabs(errorDeg);

    if (state.positionStartMs != 0 && now - state.positionStartMs >= POSITION_TIMEOUT_MS) {
      Serial.printf("[PID] M%d timeout error=%.1f deg\n", motor, errorDeg);
      finishOrHoldPosition(motor, currentTicks);
      return;
    }

    if (labs(currentTicks - state.positionWatchTicks) >= POSITION_STALL_TICKS) {
      state.positionWatchTicks = currentTicks;
      state.positionLastMoveMs = now;
    } else if (state.positionLastMoveMs != 0 &&
               now - state.positionLastMoveMs >= POSITION_STALL_MS) {
      Serial.printf("[PID] M%d stalled error=%.1f deg\n", motor, errorDeg);
      finishOrHoldPosition(motor, currentTicks);
      return;
    }

    if (absErrorDeg <= posToleranceDeg) {
      Serial.printf("[PID] M%d reached error=%.1f deg\n", motor, errorDeg);
      finishOrHoldPosition(motor, currentTicks);
      return;
    }

    if (dt <= 0.0f) dt = 0.01f;
    float derivative = 0.0f;
    if (state.positionLastError != 0.0f) {
      derivative = (errorDeg - state.positionLastError) / dt;
    }
    state.positionLastError = errorDeg;
    state.positionIntegral += errorDeg * dt;

    float output = posKp * errorDeg + posKi * state.positionIntegral + posKd * derivative;
    int duty = constrain((int)fabs(output), 0, posMaxDuty);
    setMotorDuty(motor, output >= 0.0f ? duty : -duty, MOTOR_MODE_POSITION);

    if (now - lastPositionLogMs[motor] >= 250) {
      lastPositionLogMs[motor] = now;
      Serial.printf("[PID] M%d error=%.1f deg out=%.1f duty=%d\n",
                    motor, errorDeg, output, output >= 0.0f ? duty : -duty);
    }
  }

  void updateMotorPositionMove(int motor, unsigned long now, float dt)
  {
    // 角度保持與控制模式無關，兩種馬達（樂高 / 一般 PID）共用
    if (motorState[motor].positionPhase == POSITION_PHASE_HOLD) {
      updateMotorPositionHold(motor, dt);
      return;
    }
    if (posCtrlMode == POS_CTRL_LEGO) {
      updateMotorPositionMoveLego(motor, now);
    } else {
      updateMotorPositionMovePID(motor, now, dt);
    }
  }

  void updateMotorPositionMoveLego(int motor, unsigned long now)
  {
    MotorRuntimeState &state = motorState[motor];
    float tpd = ticksPerDegree();
    if (tpd <= 0.0f) return;

    long currentTicks = getMotorTicks(motor);
    float errorTicks = (float)(state.targetTicks - currentTicks);
    float errorDeg = errorTicks / tpd;
    float absErrorDeg = fabs(errorDeg);

    if (state.positionPhase == POSITION_PHASE_BRAKE) {
      if (now - state.positionPhaseStartMs < POSITION_BRAKE_MS) {
        setMotorDuty(motor, state.positionBrakeDuty, MOTOR_MODE_POSITION);
        return;
      }

      releaseMotorOutput(motor);
      state.positionPhase = POSITION_PHASE_SETTLE;
      state.positionPhaseStartMs = now;
      Serial.printf("[CTRL] M%d settle current=%ld target=%ld error=%.1f deg\n",
                    motor, currentTicks, state.targetTicks, errorDeg);
      return;
    }

    if (state.positionPhase == POSITION_PHASE_SETTLE) {
      if (now - state.positionPhaseStartMs < POSITION_SETTLE_MS) return;

      if (absErrorDeg <= POSITION_TOLERANCE_DEG) {
        Serial.printf("[CTRL] M%d reached current=%ld target=%ld error=%.1f deg\n",
                      motor, currentTicks, state.targetTicks, errorDeg);
        finishOrHoldPosition(motor, currentTicks);
        return;
      }

      state.positionPhase = POSITION_PHASE_DRIVE;
      state.positionPhaseStartMs = now;
      state.positionLastMoveMs = now;
      state.positionWatchTicks = currentTicks;
      state.positionDriveStartTicks = currentTicks;
      lastPositionLogMs[motor] = 0;
      state.positionLastError = errorDeg;
      Serial.printf("[CTRL] M%d correct current=%ld target=%ld error=%.1f deg\n",
                    motor, currentTicks, state.targetTicks, errorDeg);
    }

    if (absErrorDeg <= POSITION_DONE_DEG) {
      if (state.duty != 0) {
        beginPositionBrake(motor, now, currentTicks, errorDeg);
        return;
      }
      Serial.printf("[CTRL] M%d reached current=%ld target=%ld error=%.1f deg\n",
                    motor, currentTicks, state.targetTicks, errorDeg);
      finishOrHoldPosition(motor, currentTicks);
      return;
    }

    if (absErrorDeg <= POSITION_TOLERANCE_DEG) {
      Serial.printf("[CTRL] M%d reached current=%ld target=%ld error=%.1f deg\n",
                    motor, currentTicks, state.targetTicks, errorDeg);
      finishOrHoldPosition(motor, currentTicks);
      return;
    }

    if (state.positionStartMs != 0 && now - state.positionStartMs >= POSITION_TIMEOUT_MS) {
      Serial.printf("[CTRL] M%d timeout current=%ld target=%ld error=%.1f deg\n",
                    motor, currentTicks, state.targetTicks, errorDeg);
      finishOrHoldPosition(motor, currentTicks);
      return;
    }

    if (labs(currentTicks - state.positionWatchTicks) >= POSITION_STALL_TICKS) {
      state.positionWatchTicks = currentTicks;
      state.positionLastMoveMs = now;
    } else if (state.positionLastMoveMs != 0 &&
               now - state.positionLastMoveMs >= POSITION_STALL_MS) {
      Serial.printf("[CTRL] M%d stalled current=%ld target=%ld error=%.1f deg duty=%d\n",
                    motor, currentTicks, state.targetTicks, errorDeg, state.duty);
      finishOrHoldPosition(motor, currentTicks);
      return;
    }

    bool crossedTarget = state.duty != 0 &&
                         state.positionLastError != 0.0f &&
                         ((state.positionLastError > 0.0f && errorDeg < 0.0f) ||
                          (state.positionLastError < 0.0f && errorDeg > 0.0f));
    bool movingTowardTarget = state.duty != 0 &&
                              ((state.duty > 0 && errorDeg > 0.0f) ||
                               (state.duty < 0 && errorDeg < 0.0f));
    bool hasDrivenEnough = labs(currentTicks - state.positionDriveStartTicks) >=
                           POSITION_BRAKE_MIN_TRAVEL_TICKS;
    if (crossedTarget ||
        (movingTowardTarget && hasDrivenEnough && absErrorDeg <= POSITION_BRAKE_WINDOW_DEG)) {
      beginPositionBrake(motor, now, currentTicks, errorDeg);
      state.positionLastError = errorDeg;
      return;
    }

    int duty = positionDutyForError(absErrorDeg);
    int signedDuty = errorDeg > 0 ? duty : -duty;
    if (now - lastPositionLogMs[motor] >= 250) {
      lastPositionLogMs[motor] = now;
      Serial.printf("[CTRL] M%d current=%ld target=%ld error=%.1f deg duty=%d\n",
                    motor, currentTicks, state.targetTicks, errorDeg, signedDuty);
    }
    state.positionLastError = errorDeg;
    setMotorDuty(motor, signedDuty, MOTOR_MODE_POSITION);
  }

  void updateMotorSpeed(int motor, float dt)
  {
    MotorRuntimeState &state = motorState[motor];
    if (fabs(state.rpmCmd) < 0.01f) {
      stopMotorRuntime(motor);
      return;
    }

    // 目標斜坡：把階躍指令平滑成有限加速度的參考，前饋與 PID 都用斜坡值，
    // 避免前饋瞬間全開把馬達衝過頭（settle 拉長、超調）。
    float maxStep = SPEED_RAMP_RPM_S * dt;
    float refDelta = state.rpmCmd - state.rampedRpmCmd;
    if (refDelta > maxStep) state.rampedRpmCmd += maxStep;
    else if (refDelta < -maxStep) state.rampedRpmCmd -= maxStep;
    else state.rampedRpmCmd = state.rpmCmd;
    float ref = state.rampedRpmCmd;

    float error = ref - state.rpmMeas;
    state.speedIntegral += error * dt;
    // 抗積分飽和：限制積分對 duty 的貢獻，階躍後不會長時間爬不回來。
    if (ki > 1e-6f) {
      float intLimit = SPEED_INTEGRAL_DUTY_LIMIT / ki;
      state.speedIntegral = constrain(state.speedIntegral, -intLimit, intLimit);
    }
    float derivative = (error - state.speedLastError) / dt;
    state.speedLastError = error;

    float output = (kp * error) + (ki * state.speedIntegral) + (kd * derivative);
    if (speedFFEnabled) {
      float ff = speedFFkS + (speedFFkV * fabs(ref));
      if (ff < 0.0f) ff = 0.0f; // 低參考時負截距會讓前饋反向；夾到 0 避免起步反踢
      output += (ref >= 0.0f ? 1.0f : -1.0f) * (ff * 100.0f);
    }
    int minDuty = speedLimitToDuty(minSpeed);
    int maxDuty = speedLimitToDuty(maxSpeed);
    int duty = constrain((int)fabs(output), minDuty, maxDuty);
    setMotorDuty(motor, output >= 0 ? duty : -duty, MOTOR_MODE_SPEED);
  }

  void updateClosedLoopMotor(int motor, unsigned long now, float dt)
  {
    if (motor < 1 || motor > NUM_MOTORS || !MOTOR_CAPS[motor - 1].has_encoder) {
      return;
    }

    updateMeasuredRpm(motor, now);
    MotorRuntimeState &state = motorState[motor];
    if (state.mode == MOTOR_MODE_SPEED) {
      updateMotorSpeed(motor, dt);
    } else if (state.mode == MOTOR_MODE_POSITION) {
      updateMotorPositionMove(motor, now, dt);
    }
  }

  void sendTelemetryIfDue(unsigned long now)
  {
    if (!telemetryChannels || !sendJsonResponseCallback) return;
    if (now - lastTelemetryTime < 100) return;
    lastTelemetryTime = now;

    DynamicJsonDocument resp(768);
    resp["type"] = "telemetry";
    resp["t"] = (uint32_t)now;

    JsonArray motors = resp.createNestedArray("motors");
    for (int motor = 1; motor <= NUM_MOTORS; motor++) {
      JsonObject m = motors.createNestedObject();
      m["i"] = motor;
      m["mode"] = motorModeName(motorState[motor].mode);
      m["duty"] = motorState[motor].duty;
      if (MOTOR_CAPS[motor - 1].has_encoder) {
        m["rpm_meas"] = motorState[motor].rpmMeas;
        if (motorState[motor].mode == MOTOR_MODE_SPEED) {
          m["rpm_cmd"] = motorState[motor].rpmCmd;
        } else {
          m["rpm_cmd"] = nullptr;
        }
        m["deg"] = ticksToCentiDeg(getMotorTicks(motor));
      } else {
        m["rpm_meas"] = nullptr;
        m["rpm_cmd"] = nullptr;
        m["deg"] = nullptr;
      }
    }

    JsonArray servos = resp.createNestedArray("servos");
    for (int servo = 1; servo <= NUM_SERVOS; servo++) {
      JsonObject s = servos.createNestedObject();
      s["i"] = servo;
      s["deg"] = servoDeg[servo];
    }

    sendJsonResponseCallback(resp, telemetryChannels);
  }

  // ---- 高速擷取緩存 ----
  // 命令處理（AsyncTCP/序列情境）：只設定請求旗標，不直接動緩存。
  void handleChartBufferStart(const JsonDocument &doc) {
    uint32_t interval = doc["interval_ms"] | 20;
    if (interval < 5) interval = 5;        // 控制迴圈 ~100Hz，低於 5ms 無意義
    int m = 3;
    if (doc.containsKey("motors")) {
      JsonArrayConst arr = doc["motors"].as<JsonArrayConst>();
      if (arr.size() > 0) m = arr[0].as<int>();
    } else if (doc.containsKey("motor")) {
      m = doc["motor"] | 3;
    }
    chartBufferReqInterval = interval;
    chartBufferReqMotor = m;
    chartBufferReqChannel = replyChannel;
    chartBufferStartReq = true;
    sendOk();
  }

  void handleChartBufferStop() {
    chartBufferStopReq = true;
    sendOk();
  }

  // 以下三個方法只在 motor task（updateAngleControl）情境呼叫，獨佔緩存存取。
  void serviceChartBuffer(unsigned long now) {
    if (chartBufferStartReq) {
      chartBufferStartReq = false;
      chartBufferActive = true;
      chartBufferDumping = false;
      chartBufferOverflow = false;
      chartBufferCount = 0;
      chartBufferDumpIdx = 0;
      chartBufferIntervalMs = chartBufferReqInterval;
      chartBufferMotor = chartBufferReqMotor;
      chartBufferChannel = chartBufferReqChannel;
      chartBufferStartMs = now;
      chartBufferLastMs = now;
    }
    if (chartBufferStopReq) {
      chartBufferStopReq = false;
      if (chartBufferActive) {
        chartBufferActive = false;
        chartBufferDumping = true;
        chartBufferDumpIdx = 0;
      }
    }
    if (chartBufferDumping) {
      serviceChartBufferDump();
    }
  }

  void sampleChartBufferIfDue(unsigned long now) {
    if (!chartBufferActive) return;
    if (now - chartBufferLastMs < chartBufferIntervalMs) return;
    chartBufferLastMs = now;
    if (chartBufferCount >= kMaxChartSamples) {
      chartBufferOverflow = true;
      return;
    }
    int m = chartBufferMotor;
    ChartSample &s = chartBuffer[chartBufferCount++];
    s.t_ms = now - chartBufferStartMs;
    if (m >= 1 && m <= NUM_MOTORS && MOTOR_CAPS[m - 1].has_encoder) {
      s.rpm_meas = motorState[m].rpmMeas;
      s.rpm_cmd = (motorState[m].mode == MOTOR_MODE_SPEED) ? motorState[m].rpmCmd : 0.0f;
      s.deg_centi = ticksToCentiDeg(getMotorTicks(m));
      s.duty = motorState[m].duty;
    } else {
      s.rpm_meas = 0.0f;
      s.rpm_cmd = 0.0f;
      s.deg_centi = 0;
      s.duty = 0;
    }
  }

  void serviceChartBufferDump() {
    if (!sendJsonResponseCallback) {       // 無回傳通道 → 直接結束，別卡在 dumping
      chartBufferDumping = false;
      return;
    }
    const size_t BATCH = 8;                // 每個 ~10ms tick 最多送 8 筆，分攤 WS 佇列壓力
    size_t sent = 0;
    while (chartBufferDumpIdx < chartBufferCount && sent < BATCH) {
      ChartSample &s = chartBuffer[chartBufferDumpIdx];
      DynamicJsonDocument doc(192);
      doc["cmd"] = "chart_buffer_sample";
      doc["t_ms"] = s.t_ms;
      JsonArray motors = doc.createNestedArray("motors");
      JsonObject mo = motors.createNestedObject();
      mo["i"] = chartBufferMotor;
      mo["rpm_meas"] = s.rpm_meas;
      mo["rpm_cmd"] = s.rpm_cmd;
      mo["deg"] = s.deg_centi;
      mo["duty"] = s.duty;
      sendJsonResponseCallback(doc, chartBufferChannel);
      chartBufferDumpIdx++;
      sent++;
    }
    if (chartBufferDumpIdx >= chartBufferCount) {
      DynamicJsonDocument doc(96);
      doc["cmd"] = "chart_buffer_done";
      doc["count"] = (uint32_t)chartBufferCount;
      doc["overflow"] = chartBufferOverflow;
      sendJsonResponseCallback(doc, chartBufferChannel);
      chartBufferDumping = false;
    }
  }

  void sendOk() {
    if (!sendJsonResponseCallback) return;
    DynamicJsonDocument resp(64);
    resp["ok"] = true;
    sendJsonResponseCallback(resp, replyChannel);
  }

  void sendError(const char *err) {
    if (!sendJsonResponseCallback) return;
    DynamicJsonDocument resp(128);
    resp["ok"] = false;
    resp["err"] = err;
    sendJsonResponseCallback(resp, replyChannel);
  }

  void sendCapabilities() {
    if (!sendJsonResponseCallback) return;
    DynamicJsonDocument resp(512);
    resp["type"] = "capabilities";
    resp["schema"] = "phone_blocky/v1";
    JsonArray motors = resp.createNestedArray("motors");
    for (int i = 0; i < NUM_MOTORS; i++) {
      JsonObject m = motors.createNestedObject();
      m["id"] = i + 1;
      m["has_encoder"] = MOTOR_CAPS[i].has_encoder;
      m["label"] = String("M") + String(i + 1);
      if (MOTOR_CAPS[i].has_encoder) {
        m["ppr"] = encoderPPR;
        m["encoder_mode"] = ENCODER_MODE;
        m["ticks_per_degree"] = ticksPerDegree();
        m["gear_ratio"] = gearRatio;
      }
    }
    resp["num_servos"] = NUM_SERVOS;
    sendJsonResponseCallback(resp, replyChannel);
  }

  void sendMotorRead(int motor) {
    if (!sendJsonResponseCallback) return;
    if (motor < 1 || motor > NUM_MOTORS) {
      sendError("invalid_motor");
      return;
    }

    DynamicJsonDocument resp(256);
    resp["type"] = "motor_read";
    resp["motor"] = motor;
    resp["mode"] = motorModeName(motorState[motor].mode);
    resp["duty"] = motorState[motor].duty;
    if (motor >= 1 && motor <= NUM_MOTORS && MOTOR_CAPS[motor - 1].has_encoder) {
      resp["has_encoder"] = true;
      int pinA = motor == 3 ? ENCODER_M3_PIN_A : ENCODER_M4_PIN_A;
      int pinB = motor == 3 ? ENCODER_M3_PIN_B : ENCODER_M4_PIN_B;
      long ticks = getMotorTicks(motor);
      resp["pin_a"] = pinA;
      resp["pin_b"] = pinB;
      resp["a"] = digitalRead(pinA);
      resp["b"] = digitalRead(pinB);
      resp["ticks"] = ticks;
      resp["rpm_meas"] = motorState[motor].rpmMeas;
      resp["rpm_cmd"] = motorState[motor].mode == MOTOR_MODE_SPEED ? motorState[motor].rpmCmd : 0;
      resp["deg"] = (int)(ticks / ticksPerDegree());
      resp["deg_centi"] = ticksToCentiDeg(ticks);
    } else {
      resp["has_encoder"] = false;
      resp["rpm_meas"] = nullptr;
      resp["rpm_cmd"] = nullptr;
      resp["deg"] = nullptr;
      resp["deg_centi"] = nullptr;
    }
    sendJsonResponseCallback(resp, replyChannel);
  }

  bool ensureEncoderMotor(int motor)
  {
    if (motor < 1 || motor > NUM_MOTORS) {
      sendError("invalid_motor");
      return false;
    }
    if (!MOTOR_CAPS[motor - 1].has_encoder) {
      sendError("no_encoder");
      return false;
    }
    if (!encoder) {
      sendError("encoder_unavailable");
      return false;
    }
    return true;
  }

  // Direct command dispatch for new "cmd" verb format
  void executeDirectCommand(const JsonDocument &doc) {
    const char *cmd = doc["cmd"] | "";

    if (strcmp(cmd, "pwm") == 0) {
      int motor = doc["motor"] | 0;
      int duty = doc["duty"] | 0;
      if (motor >= 1 && motor <= NUM_MOTORS) {
        setMotorDuty(motor, duty, MOTOR_MODE_PWM);
        resetMotorControlState(motor);
        sendOk();
      } else {
        sendError("invalid_motor");
      }

    } else if (strcmp(cmd, "stop") == 0) {
      if (doc.containsKey("motor")) {
        int motor = doc["motor"] | 0;
        if (motor >= 1 && motor <= NUM_MOTORS) {
          stopMotorRuntime(motor);
        } else {
          stopAllRuntime();
        }
      } else {
        stopAllRuntime();
      }
      sendOk();

    } else if (strcmp(cmd, "servo") == 0) {
      int ch = doc["ch"] | 0;
      int deg = doc["deg"] | 90;
      if (ch >= 1 && ch <= NUM_SERVOS) {
        servoDeg[ch] = constrain(deg, 0, 180);
        servoCtrl.controlServo(ch, servoDeg[ch]);
        sendOk();
      } else {
        sendError("invalid_servo");
      }

    } else if (strcmp(cmd, "ping") == 0) {
      if (!sendJsonResponseCallback) return;
      DynamicJsonDocument resp(64);
      resp["type"] = "pong";
      resp["t"] = (uint32_t)millis();
      sendJsonResponseCallback(resp, replyChannel);

    } else if (strcmp(cmd, "capabilities") == 0) {
      sendCapabilities();

    } else if (strcmp(cmd, "read") == 0) {
      sendMotorRead(doc["motor"] | 0);

    } else if (strcmp(cmd, "speed") == 0) {
      int motor = doc["motor"] | 0;
      if (!ensureEncoderMotor(motor)) return;
      float rpm = doc["rpm"] | 0.0f;
      if (fabs(rpm) < 0.01f) {
        stopMotorRuntime(motor);
      } else {
        MotorRuntimeState &state = motorState[motor];
        state.mode = MOTOR_MODE_SPEED;
        state.rpmCmd = rpm;
        state.rampedRpmCmd = state.rpmMeas; // 從目前實際轉速起斜坡，換速更平順
        state.speedIntegral = 0.0f;
        state.speedLastError = 0.0f;
        state.lastSampleMs = 0;
      }
      sendOk();

    } else if (strcmp(cmd, "move_to") == 0 || strcmp(cmd, "move_by") == 0) {
      int motor = doc["motor"] | 0;
      if (!ensureEncoderMotor(motor)) return;
      long centiDeg = doc["deg"] | 0;
      long currentCentiDeg = ticksToCentiDeg(getMotorTicks(motor));
      long targetCentiDeg = strcmp(cmd, "move_by") == 0
                                ? currentCentiDeg + centiDeg
                                : centiDeg;
      startMotorPositionMove(motor, centiDegToTicks(targetCentiDeg), "CMD", true);
      updateAngleControl();
      sendOk();

    } else if (strcmp(cmd, "zero") == 0) {
      int motor = doc["motor"] | 0;
      if (!ensureEncoderMotor(motor)) return;
      zeroMotorPosition(motor);
      sendOk();

    } else if (strcmp(cmd, "pid") == 0 || strcmp(cmd, "apply_config") == 0) {
      // 套用參數到 RAM（不寫 NVS）。"pid" 為相容別名。
      applyConfigFromDoc(doc);
      sendOk();

    } else if (strcmp(cmd, "read_config") == 0) {
      sendConfig();

    } else if (strcmp(cmd, "write_config") == 0) {
      // 套用到 RAM 後寫入 NVS；NVS 失敗必須回報（規範 #5）
      applyConfigFromDoc(doc);
      if (saveConfigToNVS()) {
        sendOk();
      } else {
        sendError("nvs_write_failed");
      }

    } else if (strcmp(cmd, "reset_config") == 0) {
      // 還原 config.h 預設到 RAM（不動 NVS），回送新值讓介面回填
      resetConfigToDefaults();
      sendConfig();

    } else if (strcmp(cmd, "set_wifi") == 0) {
      // 寫入 WiFi 設定（NVS "wifi" 命名空間）後重啟。管理密碼驗證 + 規範 #5 檢查。
      if (strcmp(doc["adminPassword"] | "", CONFIG_ADMIN_PASSWORD) != 0) {
        sendError("bad_admin");
        return;
      }
      const char *ssid = doc["ssid"] | "";
      const char *password = doc["password"] | "";
      const char *apName = doc["apName"] | "";
      if (strlen(ssid) == 0) {
        sendError("ssid_required");
        return;
      }
      Preferences wifiPrefs;
      if (!wifiPrefs.begin("wifi", false)) {
        sendError("nvs_open_failed");
        return;
      }
      bool ok = wifiPrefs.putString("ssid", ssid) != 0;
      size_t wp = wifiPrefs.putString("password", password);
      ok &= (wp != 0) || (strlen(password) == 0); // 空密碼（開放網路）合法
      if (strlen(apName) > 0) ok &= wifiPrefs.putString("apName", apName) != 0;
      wifiPrefs.end();
      if (ok) {
        sendOk();
        delay(500);
        ESP.restart();
      } else {
        sendError("nvs_write_failed");
      }

    } else if (strcmp(cmd, "set_name") == 0) {
      // 只寫控制板（AP）名稱後重啟
      if (strcmp(doc["adminPassword"] | "", CONFIG_ADMIN_PASSWORD) != 0) {
        sendError("bad_admin");
        return;
      }
      const char *apName = doc["apName"] | "";
      if (strlen(apName) == 0) {
        sendError("name_required");
        return;
      }
      Preferences wifiPrefs;
      if (!wifiPrefs.begin("wifi", false)) {
        sendError("nvs_open_failed");
        return;
      }
      bool ok = wifiPrefs.putString("apName", apName) != 0;
      wifiPrefs.end();
      if (ok) {
        sendOk();
        delay(500);
        ESP.restart();
      } else {
        sendError("nvs_write_failed");
      }

    } else if (strcmp(cmd, "subscribe") == 0) {
      bool on = doc["on"] | true;
      if (on) {
        telemetryChannels |= replyChannel;   // 只對訂閱來源的通道送遙測
      } else {
        telemetryChannels &= ~replyChannel;
      }
      lastTelemetryTime = 0;
      sendOk();

    } else if (strcmp(cmd, "chart_buffer_start") == 0) {
      handleChartBufferStart(doc);

    } else if (strcmp(cmd, "chart_buffer_stop") == 0) {
      handleChartBufferStop();

    } else {
      sendError("unknown_cmd");
    }
  }

public:
  CommandProcessor(MotorController &mc, ServoController &sc, PWMManager &pm)
      : motorCtrl(mc), servoCtrl(sc), encoder(nullptr), pwmMgr(pm)
  {
    x1Value = y1Value = x2Value = y2Value = 0;
    S1_value = S2_value = 90;
    Button1_value = Button2_value = 0;
    Action3_value = Action4_value = 0;
    M3_value = M4_value = M5_value = M6_value = 0;
    M3A_value = M4A_value = 0;
    if (!progMutex) progMutex = xSemaphoreCreateMutex();
  }

  CommandProcessor(MotorController &mc, ServoController &sc, EncoderHandler &eh, PWMManager &pm)
      : motorCtrl(mc), servoCtrl(sc), encoder(&eh), pwmMgr(pm)
  {
    x1Value = y1Value = x2Value = y2Value = 0;
    S1_value = S2_value = 90;
    Button1_value = Button2_value = 0;
    Action3_value = Action4_value = 0;
    M3_value = M4_value = M5_value = M6_value = 0;
    M3A_value = M4A_value = 0;
    if (!progMutex) progMutex = xSemaphoreCreateMutex();
  }

  void setSendJsonResponseCallback(void (*callback)(const DynamicJsonDocument &doc,
                                                    uint8_t channels))
  {
    sendJsonResponseCallback = callback;
  }

  // jsonTask 專用：在迴圈安全點套用 staging 程式。只有此任務會動 active 向量與 loopIndex，
  // 因此 swap 後的執行（含長時間 delay）完全無跨任務競爭。
  void applyPendingProgramIfAny()
  {
    if (!pendingProgram) return;

    std::vector<BlocklyCommand> incomingSetup, incomingLoop;
    if (progMutex) xSemaphoreTake(progMutex, portMAX_DELAY);
    if (!pendingProgram) {            // 上鎖後再確認一次（雙重檢查）
      if (progMutex) xSemaphoreGive(progMutex);
      return;
    }
    incomingSetup.swap(pendingSetupCommands);   // 快速搬出，鎖只持有極短時間
    incomingLoop.swap(pendingLoopCommands);
    pendingProgram = false;
    if (progMutex) xSemaphoreGive(progMutex);

    setupCommands.swap(incomingSetup);          // 舊程式落到 incoming*，離開作用域即釋放
    loopCommands.swap(incomingLoop);
    loopIndex = 0;
    runtimeVariables.clear();
    clearManualControlState();
    if (setupCommands.empty() && loopCommands.empty()) {
      resetActuators();                         // 空程式 = 停止程式：停所有致動器
    } else {
      executeSetupCommands();
    }
  }

  void resetWaitingForResponse()
  {
    waitingForResponse = false;
  }

  void setPIDGains(float p, float i, float d)
  {
    kp = p;
    ki = i;
    kd = d;
    m3_integral = m4_integral = 0;
    for (int motor = 1; motor <= NUM_MOTORS; motor++) {
      motorState[motor].speedIntegral = 0.0f;
      motorState[motor].speedLastError = 0.0f;
      motorState[motor].positionIntegral = 0.0f;
      motorState[motor].positionLastError = 0.0f;
    }
  }

  void setSpeedLimits(int minS, int maxS)
  {
    minSpeed = minS;
    maxSpeed = maxS;
  }

  void getPIDGains(float &p, float &i, float &d, int &minS, int &maxS)
  {
    p = kp;
    i = ki;
    d = kd;
    minS = minSpeed;
    maxS = maxSpeed;
  }

  void setSpeedFeedForward(bool enabled, float kS, float kV, float kA)
  {
    speedFFEnabled = enabled;
    speedFFkS = constrain(kS, 0.0f, 1.0f);
    speedFFkV = constrain(kV, 0.0f, 1.0f);
    speedFFkA = constrain(kA, 0.0f, 1.0f);
    resetControlIntegrals();
  }

  void getSpeedFeedForward(bool &enabled, float &kS, float &kV, float &kA)
  {
    enabled = speedFFEnabled;
    kS = speedFFkS;
    kV = speedFFkV;
    kA = speedFFkA;
  }

  void setMotorHardwareParams(float ratio, float ppr, int encPos)
  {
    gearRatio = ratio;
    encoderPPR = ppr;
    encoderPos = encPos;
  }

  void getMotorHardwareParams(float &ratio, float &ppr, int &encPos)
  {
    ratio = gearRatio;
    ppr = encoderPPR;
    encPos = encoderPos;
  }

  void setPosCtrlMode(int mode) { posCtrlMode = mode; }
  int getPosCtrlMode() const { return posCtrlMode; }

  void setPositionPIDParams(float pkp, float pki, float pkd, int maxDuty, float tolerance)
  {
    posKp = pkp; posKi = pki; posKd = pkd;
    posMaxDuty = maxDuty; posToleranceDeg = tolerance;
    for (int m = 1; m <= NUM_MOTORS; m++) {
      motorState[m].positionIntegral = 0.0f;
      motorState[m].positionLastError = 0.0f;
    }
  }

  void getPositionPIDParams(float &pkp, float &pki, float &pkd, int &maxDuty, float &tolerance)
  {
    pkp = posKp; pki = posKi; pkd = posKd;
    maxDuty = posMaxDuty; tolerance = posToleranceDeg;
  }

  // 角度保持參數（settle 死區 / 創爬 KI / 夾持上限）
  void setHoldParams(float settleDeg, float ki, int maxDuty)
  {
    holdSettleDeg = constrain(settleDeg, 0.2f, 10.0f);
    holdKi = constrain(ki, 0.0f, 50.0f);
    holdMaxDuty = constrain(maxDuty, 10, 100);
    for (int m = 1; m <= NUM_MOTORS; m++) {
      motorState[m].positionIntegral = 0.0f;
    }
  }

  void getHoldParams(float &settleDeg, float &ki, int &maxDuty)
  {
    settleDeg = holdSettleDeg; ki = holdKi; maxDuty = holdMaxDuty;
  }

  void startSysId(int motor, int voltage)
  {
    if (motor < 3 || motor > 4 || !encoder) return;
    sysIdActive = true;
    sysIdMotor = motor;
    sysIdVoltage = constrain(voltage, -255, 255);
    sysIdStartTime = millis();
    lastSysIdLogTime = 0;
    sysIdPointCount = 0;
    encoder->reset();
  }

  void stopSysId()
  {
    if (sysIdMotor >= 1 && sysIdMotor <= NUM_MOTORS) {
      stopMotorRuntime(sysIdMotor);
    }
    sysIdActive = false;
  }

  String getSysIdResultsJson()
  {
    DynamicJsonDocument doc(2048);
    JsonArray results = doc.createNestedArray("results");
    for (int i = 0; i < sysIdPointCount; i++) {
      JsonObject point = results.createNestedObject();
      point["t"] = sysIdBuffer[i].time;
      point["v"] = sysIdBuffer[i].ticks;
    }
    String output;
    serializeJson(doc, output);
    return output;
  }

  void handleHardwareTest(const JsonDocument &doc)
  {
    const char *type = doc["type"] | "";
    const char *test = doc["test"] | "";

    if (strcmp(test, "motorControl") == 0 || strcmp(type, "motorControl") == 0)
    {
      const char *motor = doc["motor"] | "";
      int speed = doc["speed"] | 0;
      if (strcmp(motor, "M1") == 0) {
        motorCtrl.controlMotor(1, speed > 0 ? 'F' : (speed < 0 ? 'B' : 'R'), abs(speed));
      } else if (strcmp(motor, "M2") == 0) {
        motorCtrl.controlMotor(2, speed > 0 ? 'F' : (speed < 0 ? 'B' : 'R'), abs(speed));
      } else if (strcmp(motor, "M3") == 0) {
        motorCtrl.controlMotor(3, speed > 0 ? 'F' : (speed < 0 ? 'B' : 'R'), abs(speed));
      } else if (strcmp(motor, "M4") == 0) {
        motorCtrl.controlMotor(4, speed > 0 ? 'F' : (speed < 0 ? 'B' : 'R'), abs(speed));
      }
    } else if (strcmp(test, "servoTest") == 0 || strcmp(type, "servoTest") == 0) {
      int ch = doc["servo"] | (doc["ch"] | 1);
      int angle = doc["angle"] | (doc["deg"] | 90);
      angle = constrain(angle, 0, 180);
      if (ch == 1 || ch == 2) {
        servoDeg[ch] = angle;
        servoCtrl.controlServo(ch, angle);
      }
      if (sendJsonResponseCallback) {
        DynamicJsonDocument responseDoc(256);
        responseDoc["type"] = "servoAck";
        responseDoc["servo"] = ch;
        responseDoc["angle"] = angle;
        responseDoc["timestamp"] = millis();
        sendJsonResponseCallback(responseDoc, replyChannel);
      }
    } else if (strcmp(test, "encoderReading") == 0 || strcmp(type, "encoderReading") == 0) {
      const char *motor = doc["motor"] | "M3";
      bool doReset = doc["reset"] | false;
      long ticks = 0;
      if (strcmp(motor, "M4") == 0) {
        if (doReset && encoder) encoder->resetM4();
        ticks = encoder ? encoder->getM4Ticks() : 0;
      } else {
        if (doReset && encoder) encoder->resetM3();
        ticks = encoder ? encoder->getM3Ticks() : 0;
      }
      if (sendJsonResponseCallback) {
        DynamicJsonDocument responseDoc(256);
        responseDoc["type"] = "encoderValue";
        responseDoc["motor"] = motor;
        responseDoc["ticks"] = ticks;
        responseDoc["deg"] = ticks / TICKS_PER_DEGREE;
        responseDoc["timestamp"] = millis();
        sendJsonResponseCallback(responseDoc, replyChannel);
      }
    } else if (strcmp(test, "buttonReading") == 0 || strcmp(type, "buttonReading") == 0) {
      int gpio = doc["gpio"] | USER_LEGO_BUTTON_PIN;
      pinMode(gpio, INPUT_PULLUP);
      int value = digitalRead(gpio);
      if (sendJsonResponseCallback) {
        DynamicJsonDocument responseDoc(256);
        responseDoc["type"] = "buttonData";
        responseDoc["gpio"] = gpio;
        responseDoc["value"] = value;
        responseDoc["timestamp"] = millis();
        sendJsonResponseCallback(responseDoc, replyChannel);
      }
    } else if (strcmp(test, "digitalReading") == 0 || strcmp(type, "digitalReading") == 0) {
      int gpio = doc["gpio"] | USER_DIGITAL_INPUT_PIN;
      pinMode(gpio, INPUT);
      int value = digitalRead(gpio);
      if (sendJsonResponseCallback) {
        DynamicJsonDocument responseDoc(256);
        responseDoc["type"] = "digitalValue";
        responseDoc["gpio"] = gpio;
        responseDoc["value"] = value;
        responseDoc["timestamp"] = millis();
        sendJsonResponseCallback(responseDoc, replyChannel);
      }
    } else if (strcmp(test, "analogReading") == 0 || strcmp(type, "analogReading") == 0) {
      int gpio = doc["gpio"] | USER_ANALOG_INPUT_PIN;
      int value = analogRead(gpio);
      if (sendJsonResponseCallback) {
        DynamicJsonDocument responseDoc(256);
        responseDoc["type"] = "analogValue";
        responseDoc["gpio"] = gpio;
        responseDoc["value"] = value;
        responseDoc["timestamp"] = millis();
        sendJsonResponseCallback(responseDoc, replyChannel);
      }
    } else if (strcmp(test, "ultrasonicReading") == 0 || strcmp(type, "ultrasonicReading") == 0) {
      int trigPin = doc["trig"] | USER_ULTRASONIC_TRIG_PIN;
      int echoPin = doc["echo"] | USER_ULTRASONIC_ECHO_PIN;
      pinMode(trigPin, OUTPUT);
      pinMode(echoPin, INPUT);
      digitalWrite(trigPin, LOW);
      delayMicroseconds(2);
      digitalWrite(trigPin, HIGH);
      delayMicroseconds(10);
      digitalWrite(trigPin, LOW);
      long duration = pulseIn(echoPin, HIGH, 30000);
      float distance = duration == 0 ? -1.0f : (duration * 0.034f) / 2.0f;
      if (sendJsonResponseCallback) {
        DynamicJsonDocument responseDoc(256);
        responseDoc["type"] = "ultrasonicDistance";
        responseDoc["trig"] = trigPin;
        responseDoc["echo"] = echoPin;
        responseDoc["duration"] = duration;
        responseDoc["distance"] = distance;
        responseDoc["timestamp"] = millis();
        sendJsonResponseCallback(responseDoc, replyChannel);
      }
    }
  }

  void TASKmoveCar()
  {
    motorCtrl.controlMotor(1, y1Value > 0 ? 'F' : (y1Value < 0 ? 'B' : 'R'), abs(y1Value) * 25 / 10);
    motorCtrl.controlMotor(2, y2Value > 0 ? 'F' : (y2Value < 0 ? 'B' : 'R'), abs(y2Value) * 25 / 10);
  }

  void TASKdoAction()
  {
    if (Button1_value) {
      if (Action3_value == 1) {
        setMotorDuty(3, M3_value, MOTOR_MODE_PWM);
      }
    }

    if (Button2_value) {
      if (Action4_value == 1) {
        setMotorDuty(4, M4_value, MOTOR_MODE_PWM);
      }
    }

    if (joyServoFieldsPresent) {
      servoDeg[1] = constrain(S1_value, 0, 180);
      servoDeg[2] = constrain(S2_value, 0, 180);
      servoCtrl.controlServo(1, servoDeg[1]);
      servoCtrl.controlServo(2, servoDeg[2]);
    }
  }

  void updateAngleControl()
  {
    unsigned long now = millis();

    // 高速擷取緩存的 start/stop/回傳一律在此情境（motor task）消化，與取樣同一任務無競爭。
    serviceChartBuffer(now);

    if (sysIdActive) {
      if (now - sysIdStartTime < DEFAULT_SYSID_DURATION_MS) {
        motorCtrl.pwmSigned(sysIdMotor, sysIdVoltage);
        if (sysIdMotor >= 1 && sysIdMotor <= NUM_MOTORS) {
          motorState[sysIdMotor].mode = MOTOR_MODE_PWM;
          motorState[sysIdMotor].duty = constrain(sysIdVoltage, -100, 100);
        }
        if (encoder && now - lastSysIdLogTime >= 100 && sysIdPointCount < MAX_SYSID_POINTS) {
          long ticks = sysIdMotor == 3 ? encoder->getM3Ticks() : encoder->getM4Ticks();
          sysIdBuffer[sysIdPointCount++] = {now - sysIdStartTime, ticks};
          lastSysIdLogTime = now;
        }
      } else {
        stopSysId();
      }
      sendTelemetryIfDue(now);
      return;
    }

    if (!encoder) {
      sendTelemetryIfDue(now);
      return;
    }

    float dt = (now - lastPIDTime) / 1000.0f;
    if (dt <= 0) dt = 0.01f;
    lastPIDTime = now;

    updateMeasuredRpm(3, now);
    updateMeasuredRpm(4, now);

    // RPM 已更新 → 擷取一筆（僅取樣中且到間隔時）
    sampleChartBufferIfDue(now);

    if (Action3_value == 2 && Button1_value == 1) {
      if (motorState[3].mode == MOTOR_MODE_POSITION) {
        updateClosedLoopMotor(3, now, dt);
      }
    } else if (motorState[3].mode == MOTOR_MODE_SPEED ||
               motorState[3].mode == MOTOR_MODE_POSITION) {
      updateClosedLoopMotor(3, now, dt);
    } else if (Action3_value == 2) {
      stopMotorRuntime(3);
      m3_integral = 0;
      m3_lastError = 0;
    }

    if (Action4_value == 2 && Button2_value == 1) {
      if (motorState[4].mode == MOTOR_MODE_POSITION) {
        updateClosedLoopMotor(4, now, dt);
      }
    } else if (motorState[4].mode == MOTOR_MODE_SPEED ||
               motorState[4].mode == MOTOR_MODE_POSITION) {
      updateClosedLoopMotor(4, now, dt);
    } else if (Action4_value == 2) {
      stopMotorRuntime(4);
      m4_integral = 0;
      m4_lastError = 0;
    }

    sendTelemetryIfDue(now);
  }

  void resetActuators()
  {
    stopAllRuntime();
    servoDeg[1] = 90;
    servoDeg[2] = 90;
    servoCtrl.controlServo(1, servoDeg[1]);
    servoCtrl.controlServo(2, servoDeg[2]);

    setupCommands.clear();
    loopCommands.clear();
    loopIndex = 0;
    Serial.println("Actuators reset and command queues cleared.");
  }

  // (4) 處理指令 (JSON) - 入口函式
  // --- 參數生命週期（read / apply-RAM / write-NVS / reset），cmd: 協定共用 ---

  // 從 doc 套用參數到 RAM（未給的鍵保留現值）。kp/ki/kd 一律讀（fallback 現值），
  // 其餘以 containsKey 判定，行為與舊 "pid" cmd 一致。
  void applyConfigFromDoc(const JsonDocument &doc) {
    kp = doc["kp"] | kp;
    ki = doc["ki"] | ki;
    kd = doc["kd"] | kd;
    if (doc.containsKey("minSpeed")) minSpeed = doc["minSpeed"] | minSpeed;
    if (doc.containsKey("maxSpeed")) maxSpeed = doc["maxSpeed"] | maxSpeed;
    if (doc.containsKey("speedFFEnabled")) speedFFEnabled = doc["speedFFEnabled"] | speedFFEnabled;
    if (doc.containsKey("speedFFkS")) speedFFkS = doc["speedFFkS"] | speedFFkS;
    if (doc.containsKey("speedFFkV")) speedFFkV = doc["speedFFkV"] | speedFFkV;
    if (doc.containsKey("speedFFkA")) speedFFkA = doc["speedFFkA"] | speedFFkA;
    if (doc.containsKey("encoderPos")) encoderPos = doc["encoderPos"] | encoderPos;
    if (doc.containsKey("gearRatio")) gearRatio = doc["gearRatio"] | gearRatio;
    if (doc.containsKey("encoderPPR")) encoderPPR = doc["encoderPPR"] | encoderPPR;
    if (doc.containsKey("posCtrlMode")) posCtrlMode = doc["posCtrlMode"] | posCtrlMode;
    if (doc.containsKey("posKp")) posKp = doc["posKp"] | posKp;
    if (doc.containsKey("posKi")) posKi = doc["posKi"] | posKi;
    if (doc.containsKey("posKd")) posKd = doc["posKd"] | posKd;
    if (doc.containsKey("posMaxDuty")) posMaxDuty = doc["posMaxDuty"] | posMaxDuty;
    if (doc.containsKey("posToleranceDeg")) posToleranceDeg = doc["posToleranceDeg"] | posToleranceDeg;
    if (doc.containsKey("holdSettleDeg")) holdSettleDeg = doc["holdSettleDeg"] | holdSettleDeg;
    if (doc.containsKey("holdKi")) holdKi = doc["holdKi"] | holdKi;
    if (doc.containsKey("holdMaxDuty")) holdMaxDuty = doc["holdMaxDuty"] | holdMaxDuty;
    resetControlIntegrals();
  }

  // 還原 config.h 預設值到 RAM（不動 NVS）
  void resetConfigToDefaults() {
    kp = DEFAULT_PID_KP;
    ki = DEFAULT_PID_KI;
    kd = DEFAULT_PID_KD;
    minSpeed = DEFAULT_MIN_SPEED;
    maxSpeed = DEFAULT_MAX_SPEED;
    speedFFEnabled = DEFAULT_SPEED_FF_ENABLED;
    speedFFkS = DEFAULT_SPEED_FF_KS;
    speedFFkV = DEFAULT_SPEED_FF_KV;
    speedFFkA = DEFAULT_SPEED_FF_KA;
    encoderPos = DEFAULT_ENCODER_POS;
    gearRatio = GEAR_RATIO;
    encoderPPR = ENCODER_PPR;
    posCtrlMode = DEFAULT_POS_CTRL_MODE;
    posKp = DEFAULT_POS_KP;
    posKi = DEFAULT_POS_KI;
    posKd = DEFAULT_POS_KD;
    posMaxDuty = DEFAULT_POS_MAX_DUTY;
    posToleranceDeg = DEFAULT_POS_TOLERANCE_DEG;
    holdSettleDeg = DEFAULT_HOLD_SETTLE_DEG;
    holdKi = DEFAULT_HOLD_KI;
    holdMaxDuty = DEFAULT_HOLD_MAX_DUTY;
    resetControlIntegrals();
  }

  void resetControlIntegrals() {
    for (int motor = 1; motor <= NUM_MOTORS; motor++) {
      motorState[motor].speedIntegral = 0.0f;
      motorState[motor].speedLastError = 0.0f;
      motorState[motor].positionIntegral = 0.0f;
      motorState[motor].positionLastError = 0.0f;
    }
  }

  // 回送目前所有參數（type:"config"）。鍵與 NVS / loadPIDSettings 一致。
  void sendConfig() {
    if (!sendJsonResponseCallback) return;
    DynamicJsonDocument resp(1024);
    resp["type"] = "config";
    resp["kp"] = kp;
    resp["ki"] = ki;
    resp["kd"] = kd;
    resp["minSpeed"] = minSpeed;
    resp["maxSpeed"] = maxSpeed;
    resp["speedFFEnabled"] = speedFFEnabled;
    resp["speedFFkS"] = speedFFkS;
    resp["speedFFkV"] = speedFFkV;
    resp["speedFFkA"] = speedFFkA;
    resp["encoderPos"] = encoderPos;
    resp["gearRatio"] = gearRatio;
    resp["encoderPPR"] = encoderPPR;
    resp["posCtrlMode"] = posCtrlMode;
    resp["posKp"] = posKp;
    resp["posKi"] = posKi;
    resp["posKd"] = posKd;
    resp["posMaxDuty"] = posMaxDuty;
    resp["posToleranceDeg"] = posToleranceDeg;
    resp["holdSettleDeg"] = holdSettleDeg;
    resp["holdKi"] = holdKi;
    resp["holdMaxDuty"] = holdMaxDuty;
    sendJsonResponseCallback(resp, replyChannel);
  }

  // 把目前 RAM 參數寫入 NVS。每筆都驗證回傳值（規範 #5），任一失敗回 false。
  // 鍵名與 WebServerHandler::loadPIDSettings 必須一致。
  bool saveConfigToNVS() {
    if (!configPrefs.begin("motor", false)) return false;
    bool ok = true;
    ok &= configPrefs.putFloat("kp", kp) != 0;
    ok &= configPrefs.putFloat("ki", ki) != 0;
    ok &= configPrefs.putFloat("kd", kd) != 0;
    ok &= configPrefs.putInt("minSpeed", minSpeed) != 0;
    ok &= configPrefs.putInt("maxSpeed", maxSpeed) != 0;
    ok &= configPrefs.putBool("speedFFEn", speedFFEnabled);
    ok &= configPrefs.putFloat("speedFFkS", speedFFkS) != 0;
    ok &= configPrefs.putFloat("speedFFkV", speedFFkV) != 0;
    ok &= configPrefs.putFloat("speedFFkA", speedFFkA) != 0;
    ok &= configPrefs.putInt("encoderPos", encoderPos) != 0;
    ok &= configPrefs.putFloat("gearRatio", gearRatio) != 0;
    ok &= configPrefs.putFloat("encoderPPR", encoderPPR) != 0;
    ok &= configPrefs.putInt("posCtrlMode", posCtrlMode) != 0;
    ok &= configPrefs.putFloat("posKp", posKp) != 0;
    ok &= configPrefs.putFloat("posKi", posKi) != 0;
    ok &= configPrefs.putFloat("posKd", posKd) != 0;
    ok &= configPrefs.putInt("posMaxDuty", posMaxDuty) != 0;
    ok &= configPrefs.putFloat("posTolDeg", posToleranceDeg) != 0;
    ok &= configPrefs.putFloat("holdSettle", holdSettleDeg) != 0;
    ok &= configPrefs.putFloat("holdKi", holdKi) != 0;
    ok &= configPrefs.putInt("holdMax", holdMaxDuty) != 0;
    configPrefs.end();
    return ok;
  }

  void processCommands(const String &msg, uint8_t channel = Comm::CH_WS)
  {
    replyChannel = channel; // 本次指令的同步回應走回來源通道
    // 配在 heap(非堆疊):呼叫端任務堆疊僅 ~8KB,大文件放堆疊會爆;heap 充裕可容大程式。
    // 16KB 約可容 100+ 條含巢狀指令(每條簡單指令約 80~150 bytes,字串會複製進池)。
    DynamicJsonDocument doc(16384);
    DeserializationError error = deserializeJson(doc, msg);
    if (error)
    {
      Serial.print("JSON 解析失敗: ");
      Serial.println(error.f_str());
      sendError("json_parse_failed");   // 回報前端,避免「送出卻靜默失敗」
      return;
    }
    if (doc.overflowed())
    {
      // 池滿但未必回 error 的邊界情況:明確回報「程式太大」,別讓使用者以為成功
      Serial.println("JSON 文件溢位(程式過大)");
      sendError("json_too_large");
      return;
    }

    if (doc.containsKey("message_success")) {
      resetWaitingForResponse();
      return;
    }

    // Normalize legacy "command" key to new "cmd" key
    CommandTranslator::translate(doc);

    const char *mode = doc["mode"] | "";
    if (strcmp(mode, "joy") == 0)
    {
      auto toInt = [](JsonVariantConst v) {
        if (v.is<const char *>()) return atoi(v.as<const char *>());
        if (v.is<int>()) return v.as<int>();
        return 0;
      };

      x1Value = toInt(doc["x1"]);
      y1Value = toInt(doc["y1"]);
      x2Value = toInt(doc["x2"]);
      y2Value = toInt(doc["y2"]);
      joyServoFieldsPresent = doc.containsKey("S1") || doc.containsKey("S2");
      if (doc.containsKey("S1")) S1_value = toInt(doc["S1"]);
      if (doc.containsKey("S2")) S2_value = toInt(doc["S2"]);
      int prevButton1 = Button1_value;
      int prevButton2 = Button2_value;
      int prevAction3 = Action3_value;
      int prevAction4 = Action4_value;
      Button1_value = toInt(doc["Button1"]);
      Button2_value = toInt(doc["Button2"]);
      Action3_value = toInt(doc["Action3"]);
      Action4_value = toInt(doc["Action4"]);
      M3_value = toInt(doc["M3"]);
      M4_value = toInt(doc["M4"]);
      M3A_value = toInt(doc["M3A"]);
      M4A_value = toInt(doc["M4A"]);

      updateJoyPositionTarget(3, Button1_value, Action3_value, M3A_value,
                              prevButton1, prevAction3, joyM3TargetCentiDeg);
      updateJoyPositionTarget(4, Button2_value, Action4_value, M4A_value,
                              prevButton2, prevAction4, joyM4TargetCentiDeg);
      if ((prevAction3 == 2 && prevButton1 == 1) &&
          (Action3_value != 2 || Button1_value != 1)) {
        stopMotorRuntime(3);
      }
      if ((prevAction4 == 2 && prevButton2 == 1) &&
          (Action4_value != 2 || Button2_value != 1)) {
        stopMotorRuntime(4);
      }

      TASKmoveCar();
      delay(10);
      TASKdoAction();
      delay(10);
    }
    else if (strcmp(mode, "hardwareTest") == 0)
    {
      handleHardwareTest(doc);
    }
    else if (strcmp(mode, "PROG") == 0)
    {
      // 只解析到 staging;實際套用與 setup 執行由 jsonTask 在安全點進行(避免跨核心競爭)
      stageProgCommands(doc);
    }
    else if (doc.containsKey("cmd"))
    {
      executeDirectCommand(doc);
    }
  }

  // --- PROG mode parsing ---
  // WS / 序列任務呼叫:解析到本地 staging 向量(不碰 active 資料),再以極短上鎖把結果交給
  // jsonTask。解析本身不持鎖,避免阻塞控制迴圈;套用一律在 applyPendingProgramIfAny。
  void stageProgCommands(const JsonDocument &doc) {
    Serial.println("[PROG] Parsing commands...");

    std::vector<BlocklyCommand> newSetup;
    std::vector<BlocklyCommand> newLoop;
    if (doc.containsKey("setup") && doc["setup"].is<JsonArrayConst>()) {
      parseCommandArray(doc["setup"].as<JsonArrayConst>(), newSetup);
    }
    if (doc.containsKey("loop") && doc["loop"].is<JsonArrayConst>()) {
      parseCommandArray(doc["loop"].as<JsonArrayConst>(), newLoop);
    }

    if (progMutex) xSemaphoreTake(progMutex, portMAX_DELAY);
    pendingSetupCommands.swap(newSetup);
    pendingLoopCommands.swap(newLoop);
    pendingProgram = true;
    if (progMutex) xSemaphoreGive(progMutex);
    Serial.println("[PROG] Parsing complete.");
  }

  std::shared_ptr<BlocklyValueExpression> parseValueExpression(JsonVariantConst source) {
    auto expr = std::make_shared<BlocklyValueExpression>();
    if (!source.is<JsonObjectConst>()) {
      expr->kind = "constant";
      expr->value = parseIntOrString(source, 0);
      return expr;
    }

    JsonObjectConst obj = source.as<JsonObjectConst>();
    const char *kind = obj["command"] | "";
    if (strcmp(kind, "math_number") == 0) {
      expr->kind = "constant";
      expr->value = parseIntOrString(obj["number"], 0);
    } else if (strcmp(kind, "variable_get") == 0) {
      expr->kind = "variable_get";
      expr->variableName = obj["variableName"] | "";
    } else if (strcmp(kind, "arduinoUltrasonic") == 0) {
      expr->kind = "ultrasonic";
      expr->pin = parseIntOrString(obj["trigPin"], USER_ULTRASONIC_TRIG_PIN);
      expr->auxPin = parseIntOrString(obj["echoPin"], USER_ULTRASONIC_ECHO_PIN);
    } else if (strcmp(kind, "legoButton") == 0 || strcmp(kind, "digitalRead") == 0 ||
               strcmp(kind, "analogRead") == 0 || strcmp(kind, "arduino_millis") == 0) {
      expr->kind = kind;
      expr->pin = parseIntOrString(obj["pin"], -1);
    } else if (strcmp(kind, "math_arithmetic") == 0 || strcmp(kind, "logic_compare") == 0) {
      expr->kind = kind;
      expr->op = obj["operator"] | "";
      expr->left = parseValueExpression(obj["left"]);
      expr->right = parseValueExpression(obj["right"]);
      if (!expr->left || !expr->right) return nullptr;
    } else if (strcmp(kind, "logic_boolean") == 0) {
      expr->kind = "constant";
      expr->value = (obj["value"] | false) ? 1 : 0;
    } else if (strcmp(kind, "logic_negate") == 0) {
      expr->kind = "logic_negate";
      expr->left = parseValueExpression(obj["content"]);
      if (!expr->left) return nullptr;
    } else {
      return nullptr;
    }
    return expr;
  }

  // 將 Blockly value 節點附加到指令，讓設定變數、繪圖與列印共用同一個求值器。
  bool parseValueSource(JsonVariantConst source, BlocklyCommand &cmd) {
    cmd.valueExpr = parseValueExpression(source);
    if (!cmd.valueExpr) return false;
    const auto &expr = cmd.valueExpr;
    cmd.pin = expr->pin;
    cmd.value = expr->auxPin;
    return true;
  }

  int getRuntimeVariable(const String &name) const {
    for (const auto &item : runtimeVariables) if (item.name == name) return item.value;
    return 0;
  }

  void setRuntimeVariable(const String &name, int value) {
    for (auto &item : runtimeVariables) {
      if (item.name == name) { item.value = value; return; }
    }
    runtimeVariables.push_back({name, value});
  }

  void parseCommandArray(JsonArrayConst jsonArr, std::vector<BlocklyCommand> &commandList) {
    for (JsonVariantConst item : jsonArr) {
      if (!item.is<JsonObjectConst>()) continue;
      JsonObjectConst jsonObj = item.as<JsonObjectConst>();

      // Unwrap arduino_setup/arduino_loop body
      if (jsonObj.containsKey("body") && jsonObj["body"].is<JsonArrayConst>()) {
        parseCommandArray(jsonObj["body"].as<JsonArrayConst>(), commandList);
        continue;
      }

      // Support both legacy "command" key and new "cmd" key
      const char *commandStr = nullptr;
      bool isNewFormat = false;
      if (jsonObj.containsKey("cmd")) {
        commandStr = jsonObj["cmd"];
        isNewFormat = true;
      } else if (jsonObj.containsKey("command")) {
        commandStr = jsonObj["command"];
      }
      if (!commandStr) continue;

      BlocklyCommand cmd;

      // --- Legacy format ---
      if (!isNewFormat) {
        if (strcmp(commandStr, "digitalWrite") == 0) {
          cmd.type = CMD_DIGITAL_WRITE;
          cmd.pin = atoi(jsonObj["pin"]);
          cmd.value = (strcmp(jsonObj["state"], "HIGH") == 0) ? HIGH : LOW;
        } else if (strcmp(commandStr, "analogWrite") == 0) {
          cmd.type = CMD_ANALOG_WRITE;
          cmd.pin = atoi(jsonObj["pin"]);
          cmd.value = jsonObj["value"];
        } else if (strcmp(commandStr, "delay") == 0) {
          cmd.type = CMD_DELAY;
          cmd.delay_ms = parseIntOrString(jsonObj["delayTime"], 0);
        } else if (strcmp(commandStr, "motor_control") == 0) {
          cmd.type = CMD_MOTOR;
          cmd.motor_id = atoi(jsonObj["motor"]);
          cmd.direction = jsonObj["direction"].as<const char *>()[0];
          cmd.speed = parseIntOrString(jsonObj["speed"], 0);
        } else if (strcmp(commandStr, "servo_control") == 0) {
          cmd.type = CMD_SERVO;
          cmd.servo_id = atoi(jsonObj["servo"]);
          cmd.angle = parseIntOrString(jsonObj["angle"], 90);
        } else if (strcmp(commandStr, "if") == 0) {
          cmd.type = CMD_IF;
          if (!parseValueSource(jsonObj["condition"], cmd)) continue;
          // then / else 陣列遞迴解析
          if (jsonObj["then"].is<JsonArrayConst>()) {
            parseCommandArray(jsonObj["then"].as<JsonArrayConst>(),
                              cmd.nested_commands_then);
          }
          if (jsonObj["else"].is<JsonArrayConst>()) {
            parseCommandArray(jsonObj["else"].as<JsonArrayConst>(),
                              cmd.nested_commands_else);
          }
        } else if (strcmp(commandStr, "serial_println") == 0 ||
                   strcmp(commandStr, "message_print") == 0) {
          cmd.type = CMD_PRINT;
          cmd.direction = strcmp(commandStr, "message_print") == 0 ? 'M' : 'S';
          if (!parseValueSource(jsonObj["content"], cmd)) continue;
          if (jsonObj["content"].is<JsonObjectConst>()) {
            JsonObjectConst content = jsonObj["content"].as<JsonObjectConst>();
            const char *contentCommand = content["command"] | "";
            cmd.message = contentCommand;
            if (strcmp(contentCommand, "variable_get") == 0)
              cmd.message = cmd.valueExpr ? cmd.valueExpr->variableName : "變數";
            else if (strcmp(contentCommand, "math_arithmetic") == 0) cmd.message = "運算結果";
            else if (strcmp(contentCommand, "logic_compare") == 0 ||
                     strcmp(contentCommand, "logic_negate") == 0) cmd.message = "條件結果";
          }
        } else if (strcmp(commandStr, "plot_print") == 0) {
          cmd.type = CMD_PLOT;
          cmd.plotSeries = jsonObj["series"] | "value";
          cmd.plotUnit = jsonObj["unit"] | "";
          if (!parseValueSource(jsonObj["value"], cmd)) continue;
        } else if (strcmp(commandStr, "variable_declare") == 0) {
          cmd.type = CMD_VARIABLE_DECLARE;
          cmd.variableName = jsonObj["variableName"] | "";
        } else if (strcmp(commandStr, "variable_set") == 0) {
          cmd.type = CMD_VARIABLE_SET;
          cmd.variableName = jsonObj["variableName"] | "";
          if (!parseValueSource(jsonObj["value"], cmd)) continue;
        } else if (strcmp(commandStr, "math_change") == 0) {
          cmd.type = CMD_MATH_CHANGE;
          cmd.variableName = jsonObj["variableName"] | "";
          if (!parseValueSource(jsonObj["value"], cmd)) continue;
        } else {
          continue;
        }
      }
      // --- New format ---
      else {
        if (strcmp(commandStr, "pwm") == 0) {
          cmd.type = CMD_MOTOR;
          cmd.motor_id = parseIntOrString(jsonObj["motor"], 0);
          cmd.direction = 'P';             // marker: signed PWM duty
          cmd.speed = parseIntOrString(jsonObj["duty"], 0); // signed, -100..100
        } else if (strcmp(commandStr, "stop") == 0) {
          cmd.type = CMD_MOTOR;
          cmd.motor_id = parseIntOrString(jsonObj["motor"], 0); // 0 = all
          cmd.direction = 'X';                 // marker: stop
          cmd.speed = 0;
        } else if (strcmp(commandStr, "servo") == 0) {
          cmd.type = CMD_SERVO;
          cmd.servo_id = parseIntOrString(jsonObj["ch"], 1);
          cmd.angle = parseIntOrString(jsonObj["deg"], 90);
        } else if (strcmp(commandStr, "move_to") == 0) {
          cmd.type = CMD_MOTOR_POSITION;
          cmd.motor_id = parseIntOrString(jsonObj["motor"], 0);
          cmd.angle = parseIntOrString(jsonObj["deg"], 0); // centi-degrees
        } else if (strcmp(commandStr, "zero") == 0) {
          cmd.type = CMD_MOTOR_ZERO;
          cmd.motor_id = parseIntOrString(jsonObj["motor"], 0);
        } else if (strcmp(commandStr, "move_by") == 0) {
          cmd.type = CMD_MOTOR_POSITION;
          cmd.motor_id = parseIntOrString(jsonObj["motor"], 0);
          cmd.angle = parseIntOrString(jsonObj["deg"], 0); // centi-degrees
          cmd.direction = 'R'; // relative
        } else if (strcmp(commandStr, "speed") == 0) {
          cmd.type = CMD_MOTOR_SPEED;
          cmd.motor_id = parseIntOrString(jsonObj["motor"], 0);
          cmd.speed = parseIntOrString(jsonObj["rpm"], 0);
        } else if (strcmp(commandStr, "delay") == 0) {
          cmd.type = CMD_DELAY;
          cmd.delay_ms = jsonObj.containsKey("delayTime")
                             ? parseIntOrString(jsonObj["delayTime"], 0)
                             : parseIntOrString(jsonObj["ms"], 0);
        } else if (strcmp(commandStr, "digitalWrite") == 0) {
          cmd.type = CMD_DIGITAL_WRITE;
          cmd.pin = parseIntOrString(jsonObj["pin"], 0);
          const char *stateStr = jsonObj["state"] | "LOW";
          cmd.value = (strcmp(stateStr, "HIGH") == 0) ? HIGH : LOW;
        } else if (strcmp(commandStr, "analogWrite") == 0) {
          cmd.type = CMD_ANALOG_WRITE;
          cmd.pin = parseIntOrString(jsonObj["pin"], 0);
          cmd.value = parseIntOrString(jsonObj["value"], 0);
        } else {
          continue;
        }
      }

      commandList.push_back(cmd);
    }
  }

  // Blockly 的腳位常以字串送 ("2"),AI 端會用整數 (2)。兩種都收
  static int parseIntOrString(JsonVariantConst v, int def) {
    if (v.is<const char *>()) return atoi(v.as<const char *>());
    if (v.is<int>())          return v.as<int>();
    if (v.is<float>())        return (int)v.as<float>();
    return def;
  }

  // 給 CMD_IF / 未來迴圈用:把一串巢狀指令依序執行
  void executeCommandList(const std::vector<BlocklyCommand> &cmds) {
    for (const auto &c : cmds) executeSingleCommand(c);
  }

  // 讀感測器,回傳整數 (距離 cm / 數位值 / 類比值 ...)。供 CMD_IF 評估條件
  int readSensorValue(const BlocklyCommand &cmd) {
    if (cmd.message == "ultrasonic") {
      pinMode(cmd.pin, OUTPUT);
      pinMode(cmd.value, INPUT);
      digitalWrite(cmd.pin, LOW);
      delayMicroseconds(2);
      digitalWrite(cmd.pin, HIGH);
      delayMicroseconds(10);
      digitalWrite(cmd.pin, LOW);
      long duration = pulseIn(cmd.value, HIGH, 30000);
      return duration == 0 ? -1 : (int)((duration * 0.034f) / 2.0f);
    } else if (cmd.message == "legoButton") {
      pinMode(cmd.pin, INPUT_PULLUP);
      return digitalRead(cmd.pin);
    } else if (cmd.message == "digitalRead") {
      pinMode(cmd.pin, INPUT);
      return digitalRead(cmd.pin);
    } else if (cmd.message == "analogRead") {
      return analogRead(cmd.pin);
    }
    return 0;
  }

  int readBlocklyValue(const BlocklyCommand &cmd) {
    if (cmd.valueExpr) return evaluateValueExpression(cmd.valueExpr);
    if (cmd.message == "constant") return cmd.value;
    if (cmd.message == "variable_get") return getRuntimeVariable(cmd.variableName);
    if (cmd.message == "arduino_millis") return (int)millis();
    return readSensorValue(cmd);
  }

  int evaluateValueExpression(const std::shared_ptr<BlocklyValueExpression> &expr) {
    if (!expr) return 0;
    if (expr->kind == "constant") return expr->value;
    if (expr->kind == "variable_get") return getRuntimeVariable(expr->variableName);
    if (expr->kind == "arduino_millis") return (int)millis();
    if (expr->kind == "math_arithmetic") {
      int left = evaluateValueExpression(expr->left);
      int right = evaluateValueExpression(expr->right);
      if (expr->op == "ADD") return left + right;
      if (expr->op == "MINUS") return left - right;
      if (expr->op == "MULTIPLY") return left * right;
      return right == 0 ? 0 : left / right; // DIVIDE
    }
    if (expr->kind == "logic_compare") {
      int left = evaluateValueExpression(expr->left);
      int right = evaluateValueExpression(expr->right);
      if (expr->op == "LT") return left < right;
      if (expr->op == "LTE") return left <= right;
      if (expr->op == "GT") return left > right;
      if (expr->op == "GTE") return left >= right;
      if (expr->op == "NEQ") return left != right;
      return left == right; // EQ
    }
    if (expr->kind == "logic_negate") return !evaluateValueExpression(expr->left);

    BlocklyCommand sensorCmd;
    sensorCmd.message = expr->kind;
    sensorCmd.pin = expr->pin;
    sensorCmd.value = expr->auxPin;
    return readSensorValue(sensorCmd);
  }

  bool evaluateCondition(int left, char op, int right) {
    switch (op) {
      case '<': return left <  right;
      case '>': return left >  right;
      case 'L': return left <= right;
      case 'G': return left >= right;
      case '=': return left == right;
      case '!': return left != right;
      default:  return false;
    }
  }

  void executeSingleCommand(const BlocklyCommand &cmd) {
    switch (cmd.type) {
      case CMD_DIGITAL_WRITE:
        pinMode(cmd.pin, OUTPUT);
        digitalWrite(cmd.pin, cmd.value);
        break;
      case CMD_ANALOG_WRITE:
        pwmMgr.dynamicAnalogWrite(cmd.pin, cmd.value);
        break;
      case CMD_DELAY:
        {
          int remaining = cmd.delay_ms;
          while (remaining > 0) {
            if (pendingProgram) break;   // 有新程式待套用 → 中止等待,盡快切換(回應性)
            int step = remaining > 10 ? 10 : remaining;
            vTaskDelay(step / portTICK_PERIOD_MS);
            updateAngleControl();
            remaining -= step;
          }
        }
        break;
      case CMD_MOTOR:
        if (cmd.direction == 'P') {
          setMotorDuty(cmd.motor_id, cmd.speed, MOTOR_MODE_PWM);
          resetMotorControlState(cmd.motor_id);
        } else if (cmd.direction == 'X') {
          if (cmd.motor_id == 0) {
            stopAllRuntime();
          } else {
            stopMotorRuntime(cmd.motor_id);
          }
        } else {
          motorCtrl.controlMotor(cmd.motor_id, cmd.direction, cmd.speed);
          if (cmd.motor_id >= 1 && cmd.motor_id <= NUM_MOTORS) {
            int duty = constrain((int)(cmd.speed * 100L / 255L), 0, 100);
            if (cmd.direction == 'B' || cmd.direction == 'b') duty = -duty;
            if (cmd.direction == 'R' || cmd.direction == 'r') duty = 0;
            motorState[cmd.motor_id].mode = duty == 0 ? MOTOR_MODE_IDLE : MOTOR_MODE_PWM;
            motorState[cmd.motor_id].duty = duty;
          }
        }
        break;
      case CMD_MOTOR_POSITION:
        if (cmd.motor_id >= 1 && cmd.motor_id <= NUM_MOTORS &&
            MOTOR_CAPS[cmd.motor_id - 1].has_encoder && encoder) {
          long targetCentiDeg = cmd.direction == 'R'
              ? ticksToCentiDeg(getMotorTicks(cmd.motor_id)) + (long)cmd.angle
              : (long)cmd.angle;
          startMotorPositionMove(cmd.motor_id, centiDegToTicks(targetCentiDeg), "PROG", true);
          updateAngleControl();
        }
        break;
      case CMD_MOTOR_ZERO:
        zeroMotorPosition(cmd.motor_id);
        break;
      case CMD_MOTOR_SPEED:
        if (cmd.motor_id >= 1 && cmd.motor_id <= NUM_MOTORS &&
            MOTOR_CAPS[cmd.motor_id - 1].has_encoder && encoder) {
          MotorRuntimeState &state = motorState[cmd.motor_id];
          if (cmd.speed == 0) {
            stopMotorRuntime(cmd.motor_id);
          } else {
            state.mode = MOTOR_MODE_SPEED;
            state.rpmCmd = (float)cmd.speed;
            state.speedIntegral = 0.0f;
            state.speedLastError = 0.0f;
            state.lastSampleMs = 0;
          }
        }
        break;
      case CMD_SERVO:
        if (cmd.servo_id >= 1 && cmd.servo_id <= NUM_SERVOS) {
          servoDeg[cmd.servo_id] = constrain(cmd.angle, 0, 180);
          servoCtrl.controlServo(cmd.servo_id, servoDeg[cmd.servo_id]);
        }
        break;
      case CMD_IF: {
        bool branch = readBlocklyValue(cmd) != 0;
        executeCommandList(branch ? cmd.nested_commands_then
                                  : cmd.nested_commands_else);
        break;
      }
      case CMD_PRINT: {
        int result = readBlocklyValue(cmd);

        if (cmd.direction == 'S') {
          Serial.printf("[PROG] serial_print=%d\n", result);
          break;
        }

        if (millis() - lastMsgPrintTime < 50) break;
        lastMsgPrintTime = millis();

        if (sendJsonResponseCallback) {
          DynamicJsonDocument respDoc(256);
          if (cmd.pin >= 0) {
            respDoc["message"] = cmd.message + "(" + String(cmd.pin) + ")=" + String(result);
          } else {
            respDoc["message"] = cmd.message + "=" + String(result);
          }
          sendJsonResponseCallback(respDoc, replyChannel);
          waitingForResponse = true;
        }
        break;
      }
      case CMD_PLOT: {
        int result = readBlocklyValue(cmd);
        if (millis() - lastPlotPrintTime < 50) break;
        lastPlotPrintTime = millis();
        if (sendJsonResponseCallback) {
          DynamicJsonDocument respDoc(256);
          respDoc["type"] = "plot";
          respDoc["series"] = cmd.plotSeries;
          respDoc["value"] = result;
          if (cmd.plotUnit.length()) respDoc["unit"] = cmd.plotUnit;
          respDoc["t"] = (uint32_t)millis();
          sendJsonResponseCallback(respDoc, replyChannel);
        }
        break;
      }
      case CMD_VARIABLE_DECLARE:
        setRuntimeVariable(cmd.variableName, getRuntimeVariable(cmd.variableName));
        break;
      case CMD_VARIABLE_SET:
        setRuntimeVariable(cmd.variableName, readBlocklyValue(cmd));
        break;
      case CMD_MATH_CHANGE:
        setRuntimeVariable(cmd.variableName,
                           getRuntimeVariable(cmd.variableName) + readBlocklyValue(cmd));
        break;
      default:
        break;
    }
  }

  void executeSetupCommands() {
    Serial.println("[PROG] Executing setup commands...");
    for (const auto &cmd : setupCommands) {
      executeSingleCommand(cmd);
    }
    Serial.println("[PROG] Setup complete.");
  }

  void executeLoopCommands() {
    if (loopCommands.empty()) return;

    if (loopIndex >= loopCommands.size()) {
      loopIndex = 0;
    }
    executeSingleCommand(loopCommands[loopIndex]);
    loopIndex++;
  }
};

#endif // COMMAND_PROCESSOR_H
