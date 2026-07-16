import azure.cognitiveservices.speech as speechsdk
import serial
import config  # 你的 config.py 需有 KEY 變數

# Azure Speech Services 設定
speech_key = config.KEY
service_region = "eastus2"
speech_config = speechsdk.SpeechConfig(subscription=speech_key, region=service_region)
speech_config.speech_recognition_language = "zh-TW"
audio_config = speechsdk.AudioConfig(use_default_microphone=True)
speech_recognizer = speechsdk.SpeechRecognizer(speech_config=speech_config, audio_config=audio_config)

# 串列埠設定（請依實際修改）
SERIAL_PORT = "COM6"  # 你的 ESP32 串列埠
BAUDRATE = 115200

def send_serial_command(cmd):
    with serial.Serial(SERIAL_PORT, BAUDRATE, timeout=1) as ser:
        ser.write((cmd + '\n').encode())
        print(f"已送出：{cmd}")

def recognize_and_send():
    print("請說出馬達控制指令，例如：M1,F,150")
    result = speech_recognizer.recognize_once()
    if result.reason == speechsdk.ResultReason.RecognizedSpeech:
        print("語音辨識結果:", result.text)
        # 這裡可根據語音內容做進一步處理，例如關鍵字判斷
        # 例如：如果說「前進」就轉成 "M1,F,150"
        if "前進" in result.text:
            send_serial_command("M1,F,200")
        elif "停止" in result.text:
            send_serial_command("M1,F,0")
        else:
            print("無對應指令")
    else:
        print("語音辨識失敗或取消")

if __name__ == "__main__":
    try:
        print("請開始說話...")
        while True:
            recognize_and_send()
    except Exception as e:
        print("錯誤：", e)