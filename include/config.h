#ifndef CONFIG_H
#define CONFIG_H

// Default WiFi settings (leave empty "" to use saved Preferences only).
// NOTE: keep real credentials out of version control. Fill these in locally,
// or leave empty and configure Wi-Fi via set.html (stored in Preferences/NVS).
static const char *CONFIG_STA_SSID = "";
static const char *CONFIG_STA_PASSWORD = "";

// AP settings (leave name empty to use default ESP32-xxxx).
static const char *CONFIG_AP_NAME = "";
static const char *CONFIG_AP_PASSWORD = "12345678";

// 管理密碼
static const char *CONFIG_ADMIN_PASSWORD = "123456";

// 硬體腳位定義
static const int SERVO1_PIN = 5;
static const int SERVO2_PIN = 13;

// 編碼器腳位，對齊 motorControl 的 M3/M4 配置
static const int ENCODER_M3_PIN_A = 35;
static const int ENCODER_M3_PIN_B = 34;
static const int ENCODER_M4_PIN_A = 36;
static const int ENCODER_M4_PIN_B = 39;

// Blockly / 硬體測試預設輸入腳位，避開 34/35/36/39 編碼器腳位
static const int USER_LEGO_BUTTON_PIN = 4;
static const int USER_ULTRASONIC_TRIG_PIN = 2;
static const int USER_ULTRASONIC_ECHO_PIN = 33;
static const int USER_DIGITAL_INPUT_PIN = 15;
static const int USER_ANALOG_INPUT_PIN = 32;

// 馬達硬體參數
static const float GEAR_RATIO = 48.0; // 減速比 48:1 (僅供參考)
static const float ENCODER_PPR =
    360.0;                         // 編碼器每圈脈衝數 (輸出軸，已包含減速比)
static const int ENCODER_MODE = 2; // 修正為 2X 解碼模式 (以符合實際轉動表現)

// 編碼器位置設定
static const int ENCODER_POS_BEFORE_REDUCER = 0; // 減速器前 (馬達端)
static const int ENCODER_POS_AFTER_REDUCER = 1;  // 減速器後 (輸出軸端)
static const int DEFAULT_ENCODER_POS = ENCODER_POS_AFTER_REDUCER;

// 每度對應的 Tick 數計算
// (360 * 2) / 360 = 720 / 360 = 2.0
static const float TICKS_PER_DEGREE = ((ENCODER_PPR * ENCODER_MODE) / 360.0);

// PID 預設參數
static const float DEFAULT_PID_KP = 2.0;
static const float DEFAULT_PID_KI = 0.0;
static const float DEFAULT_PID_KD = 0.1;
static const int DEFAULT_MIN_SPEED = 60;
static const int DEFAULT_MAX_SPEED = 250;
static const bool DEFAULT_SPEED_FF_ENABLED = false;
static const float DEFAULT_SPEED_FF_KS = 0.0f; // duty fraction: 0.0~1.0
static const float DEFAULT_SPEED_FF_KV = 0.0f; // duty fraction per RPM
static const float DEFAULT_SPEED_FF_KA = 0.0f; // reserved

// SysId 測試參數
static const int DEFAULT_SYSID_VOLTAGE = 100;
static const int DEFAULT_SYSID_DURATION_MS = 3000;

// 位置控制模式
static const int POS_CTRL_LEGO    = 0;  // 樂高模式：角度控制用查找表
static const int POS_CTRL_GENERAL = 1;  // 一般馬達模式：角度控制用 PID
static const int DEFAULT_POS_CTRL_MODE = POS_CTRL_LEGO;

// 一般馬達：位置 PID 預設參數
static const float DEFAULT_POS_KP            = 1.0f;
static const float DEFAULT_POS_KI            = 0.0f;
static const float DEFAULT_POS_KD            = 0.05f;
static const int   DEFAULT_POS_MAX_DUTY      = 60;
static const float DEFAULT_POS_TOLERANCE_DEG = 3.0f;

// 角度保持（hold）：可由 set.html 調整的三個參數
static const float DEFAULT_HOLD_SETTLE_DEG = 1.5f; // 進此誤差內放鬆（死區）；重啟門檻 = 此值 + 1°
static const float DEFAULT_HOLD_KI         = 8.0f; // 積分創爬速率（卡住時加力快慢）
static const int   DEFAULT_HOLD_MAX_DUTY   = 60;   // 夾持力上限 PWM%

#endif
