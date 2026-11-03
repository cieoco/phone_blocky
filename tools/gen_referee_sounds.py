#!/usr/bin/env python3
"""產生裁判頁的四個比賽音效（本專案原創合成音，非任何錄音、非 FRC 原版）。

只用 Python 標準函式庫；輸出 22.05 kHz、單聲道、16-bit WAV：
  start.wav   開場：銅管風格的短號角
  resume.wav  間隔後進手動：三聲鐘響
  end.wav     階段／比賽結束：蜂鳴器
  abort.wav   中止／故障：低沉霧笛

用法：python tools/gen_referee_sounds.py [輸出資料夾] [--force]
預設輸出到 .pio/generated-referee-audio，不覆蓋目前使用的裁判音效。
指定的目的地已有同名檔案時，必須明確加 --force 才會覆蓋。
同一 Python 執行環境下可重複產生相同檔案；不讀取或下載原版錄音。
"""
import argparse
import math
import os
import struct
import wave

RATE = 22050


def silence(seconds):
    return [0.0] * int(RATE * seconds)


def envelope(n, attack, release, i):
    """線性起音／收尾，避免爆音（單位：樣本數）"""
    if i < attack:
        return i / attack
    if i > n - release:
        return max(0.0, (n - i) / release)
    return 1.0


def brass(freq, seconds, vibrato=True):
    """銅管風格：奇偶諧波都有、高頻漸弱，加一點延遲顫音"""
    n = int(RATE * seconds)
    out = []
    harmonics = [(1, 1.0), (2, 0.55), (3, 0.42), (4, 0.25), (5, 0.16), (6, 0.09), (7, 0.05)]
    phase = 0.0
    for i in range(n):
        t = i / RATE
        vib = 1.0 + (0.006 * math.sin(2 * math.pi * 5.5 * t) * min(1.0, t / 0.25) if vibrato else 0.0)
        phase += 2 * math.pi * freq * vib / RATE
        # 起音時高頻較少，模擬吹嘴建立音色
        bright = min(1.0, 0.35 + t / 0.06)
        s = sum(a * (bright if h > 2 else 1.0) * math.sin(h * phase) for h, a in harmonics)
        out.append(s * envelope(n, int(RATE * 0.02), int(RATE * 0.05), i))
    return out


def bell(freq, seconds):
    """鐘聲：不協和泛音 + 指數衰減"""
    n = int(RATE * seconds)
    partials = [(1.0, 1.0, 1.6), (2.0, 0.6, 2.4), (2.76, 0.45, 3.2), (5.4, 0.25, 5.0), (8.93, 0.12, 7.0)]
    out = []
    for i in range(n):
        t = i / RATE
        s = sum(a * math.exp(-d * t) * math.sin(2 * math.pi * freq * r * t) for r, a, d in partials)
        out.append(s * envelope(n, int(RATE * 0.002), int(RATE * 0.03), i))
    return out


def buzzer(seconds):
    """蜂鳴器：兩個接近的方波產生拍頻，帶點粗糙感"""
    n = int(RATE * seconds)
    out = []
    for i in range(n):
        t = i / RATE
        a = 1.0 if math.sin(2 * math.pi * 196 * t) >= 0 else -1.0
        b = 1.0 if math.sin(2 * math.pi * 207 * t) >= 0 else -1.0
        out.append(0.5 * (a + b) * envelope(n, int(RATE * 0.01), int(RATE * 0.04), i))
    return lowpass(out, 3500)


def foghorn(seconds):
    """霧笛：低頻鋸齒波 + 慢起音、音高微微上滑"""
    n = int(RATE * seconds)
    out = []
    phase = 0.0
    for i in range(n):
        t = i / RATE
        freq = 98 + 6 * min(1.0, t / 0.4)
        phase += freq / RATE
        saw = 2.0 * (phase - math.floor(phase + 0.5))
        swell = min(1.0, t / 0.35)
        out.append(saw * swell * envelope(n, 1, int(RATE * 0.25), i))
    return lowpass(lowpass(out, 900), 900)


def lowpass(samples, cutoff):
    """一階低通，讓方波／鋸齒波不刺耳"""
    rc = 1.0 / (2 * math.pi * cutoff)
    alpha = (1.0 / RATE) / (rc + 1.0 / RATE)
    out, y = [], 0.0
    for x in samples:
        y += alpha * (x - y)
        out.append(y)
    return out


def mix_at(base, part, start_seconds):
    start = int(RATE * start_seconds)
    need = start + len(part)
    if len(base) < need:
        base.extend([0.0] * (need - len(base)))
    for i, v in enumerate(part):
        base[start + i] += v
    return base


def normalize(samples, peak_db=-1.0):
    peak = max(abs(s) for s in samples) or 1.0
    gain = (10 ** (peak_db / 20)) / peak
    return [s * gain for s in samples]


def write_wav(path, samples):
    data = b"".join(struct.pack("<h", int(max(-1.0, min(1.0, s)) * 32767)) for s in normalize(samples))
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(data)


def make_start():
    # 衝鋒號：G4 C5 E5 G5（三短一長），E5 G5（短、長）
    G4, C5, E5, G5 = 392.00, 523.25, 659.26, 783.99
    notes = [(G4, 0.13), (C5, 0.13), (E5, 0.13), (G5, 0.36), (0, 0.06), (E5, 0.16), (G5, 0.75)]
    out = []
    for f, d in notes:
        out += silence(d) if f == 0 else brass(f, d)
    return out + silence(0.15)


def make_resume():
    out = []
    for k in range(3):
        mix_at(out, bell(1046.5, 1.2), 0.38 * k)
    return out + silence(0.1)


def make_end():
    return buzzer(1.6) + silence(0.1)


def make_abort():
    return foghorn(2.4) + silence(0.1)


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", nargs="?", default=os.path.join(root, ".pio", "generated-referee-audio"))
    parser.add_argument("--force", action="store_true", help="覆蓋目的地已有的同名 WAV")
    args = parser.parse_args()
    out_dir = args.output
    sounds = [("start", make_start), ("resume", make_resume), ("end", make_end), ("abort", make_abort)]
    existing = [name + ".wav" for name, _ in sounds if os.path.lexists(os.path.join(out_dir, name + ".wav"))]
    if existing and not args.force:
        parser.error("目的地已有音效，未寫入任何檔案：" + ", ".join(existing) + "；如確定覆蓋，請加 --force")
    os.makedirs(out_dir, exist_ok=True)
    for name, fn in sounds:
        path = os.path.join(out_dir, name + ".wav")
        write_wav(path, fn())
        print(f"{path}  {os.path.getsize(path)} bytes")


if __name__ == "__main__":
    main()
