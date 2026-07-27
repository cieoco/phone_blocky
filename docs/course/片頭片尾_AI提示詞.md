# 片頭片尾 AI 提示詞

> 給文生影片／文生圖工具的提示詞集，含使用方式與注意事項。
> 拍攝規格見 [拍攝規範.md](拍攝規範.md)。

---

## 使用前必讀：三個會讓你白花錢的坑

### 坑 1：不要讓 AI 生中文字

所有文生影片模型（Sora、Veo、Runway、Kling、Pika…）**渲染中文都是災難**——
筆畫會糊、會生出不存在的字。日文漢字模型看過比較多，中文繁體幾乎必錯。

**正確做法**：讓 AI 只生**背景動態素材**（光影、粒子、金屬質感、電路板景深），
中文字全部在剪輯軟體（剪映／DaVinci／Premiere／AE）用向量文字疊上去。
這樣還有一個好處：文字永遠銳利，改字不用重生成。

所有 prompt 結尾都要帶：`no text, no letters, no words, no logos, no watermark`

### 坑 2：片頭超過 3 秒，30 支片就是災難

學員會連續看 5–7 支片。5 秒片頭 × 7 次 = 35 秒的重複疲勞，
第三集他就會開始拖進度條，而**進度條一拖，成功訊號那段就會被跳過**。

- 片頭：**2–3 秒**（S0 第一集可以放 5 秒完整版，之後全用短版）
- 片頭**不要放在 0:00**。先給 10–15 秒的內容鉤子（cold open），再進片頭。
  E00 腳本已經寫了「不要先自我介紹，不要先講理念」，這是同一個道理。

### 坑 3：AI 生成的「電子零件」不會是你的板子

模型不知道 WeMos D1 R32 長什麼樣，生出來會是似是而非的 PCB。
所以 **AI 素材只用在抽象／局部／失焦的畫面**，凡是要辨識具體硬體的鏡頭，
一律用你自己拍的實拍素材。

---

## 一、片頭

### 方案 A（主推）：綠燈序列 —— 用課程自己的隱喻

課程最強的視覺資產是自檢頁那排**由灰轉綠**的燈。片頭直接用它，
學員看到片頭就會想起「逐項通過」的感覺，這比任何科技感光效都有意義。

**畫面設計（3 秒）**：

```
0.0–1.2s  深藍近黑背景，四個灰色圓點橫向排列（AI 生成的背景質感）
1.2–2.0s  圓點由左至右依序亮綠（剪輯軟體做，不是 AI）
2.0–3.0s  綠光擴散，「phone_blocky」字樣淡入
```

AI 只負責背景。Prompt：

```
Abstract dark technology background, deep navy-black (#0B1220) gradient,
soft volumetric light slowly drifting from the left, subtle floating dust
particles catching the light, very shallow depth of field, out-of-focus
bokeh circles in the far background, slow gentle camera push-in,
cinematic, moody, minimal, clean negative space in the center,
16:9, 5 seconds, no text, no letters, no logos, no watermark, no people
```

生 5 秒剪成 3 秒（頭尾各切掉不穩定的部分，這是所有 AI 影片的通則）。

---

### 方案 B：微距質感 —— 抽象但有工程感

如果你想要更有「硬體」味道又不暴露具體型號，用極淺景深的微距抽象：

```
Extreme macro shot of a dark circuit board surface, almost entirely
out of focus, tiny green and amber indicator lights glowing softly in
the blurred background, very shallow depth of field, slow lateral
camera drift from left to right, single soft key light raking across
the surface, deep teal and navy color palette with warm amber accents,
cinematic anamorphic look, film grain, 16:9, 5 seconds,
no text, no letters, no logos, no watermark, no hands
```

**變體：加入運動感**（適合 S1「動」季的片頭）

```
Extreme macro of a small brass gear slowly starting to rotate in the
dark, catching a single rim light on its teeth, everything else falls
into deep shadow and bokeh, dust motes floating, shallow depth of field,
slow motion, deep navy background with green accent light,
cinematic, minimal, 16:9, 5 seconds,
no text, no letters, no logos, no watermark
```

---

### 方案 C：不用 AI —— 0.8 秒音效 sting

**老實說，30 支片的課程，這個方案最實際。**

不做影片片頭，只做一個 0.8 秒的**聲音標記** + 一個靜態 logo 閃現：

```
0.0–0.3s  黑底，「phone_blocky」白字瞬間出現（無動畫）
0.3–0.8s  下方綠色線條由左至右畫過
0.8s      切入正片
```

配一個乾淨的音效（見下方音效 prompt）。
成本幾乎為零、永遠不會過時、學員不會想跳過。

如果你不確定要投多少心力在片頭上，**先用方案 C 錄完 S0，之後再決定要不要升級**。
片頭是最容易事後統一替換的東西。

---

## 二、片尾

片尾的任務不是致謝，是**推下一集**。腳本每集都寫了「下一集的鉤子」，
片尾就是那句話的視覺容器。

**畫面設計（6 秒）**：

```
0.0–1.0s  正片最後一個畫面凍結，加深色遮罩
1.0–3.5s  中央出現該集的鉤子文字（腳本裡那句）
3.5–6.0s  左下「下一集：E06 第一次指揮它」+ 右下 YouTube 訂閱/播放清單按鈕位
```

背景素材 prompt：

```
Slow abstract dark background loop, deep navy-black gradient with a
faint green light glow drifting slowly across the lower third,
very subtle floating particles, extremely minimal, low contrast,
nothing in the center of the frame, seamless slow motion,
16:9, 8 seconds, no text, no letters, no logos, no watermark
```

> **注意「nothing in the center of the frame」**——片尾中央要放字，
> 背景必須留白。這句話不加，AI 一定會把主體放在正中央。

**YouTube 端片尾畫面（End Screen）** 要留最後 5–20 秒，且元素框不能被字擋住：
右下角預留 400×225 px 的空白區給「下一部影片」卡片。

---

## 三、五季轉場字卡（動 → 準 → 穩 → 判 → 交）

這五個字是課程骨架，值得做成一套統一的季首字卡。E00 腳本已經要求
「五個字一個一個出現」。

每季一張，共用同一個構圖，只換背景動態與主色。中文字一律後製疊加。

| 季 | 字 | 主色 | 背景 prompt |
|---|---|---|---|
| S1 | 動 | `#22C55E` 綠 | 見下 §1 |
| S2 | 準 | `#38BDF8` 藍 | 見下 §2 |
| S3 | 穩 | `#A78BFA` 紫 | 見下 §3 |
| S4 | 判 | `#F59E0B` 橘 | 見下 §4 |
| S5 | 交 | `#F8FAFC` 白 | 見下 §5 |

**§1 動**
```
Abstract dark background, a soft green streak of light sweeping fast
from left to right across the lower half, motion blur trails,
deep navy-black background, floating dust particles pushed by the motion,
empty space in the upper center, cinematic, minimal,
16:9, 5 seconds, no text, no letters, no logos, no watermark
```

**§2 準**
```
Abstract dark background, a thin cyan-blue light line moving inward and
coming to a precise, sharp stop in the frame, subtle ripple on stopping,
deep navy-black background, crisp and clean, empty space in the center,
cinematic, minimal, 16:9, 5 seconds,
no text, no letters, no logos, no watermark
```

**§3 穩**
```
Abstract dark background, a soft violet light oscillating side to side
with decreasing amplitude until it settles perfectly still,
damped harmonic motion, deep navy-black background, faint reflection below,
empty space in the center, cinematic, minimal,
16:9, 6 seconds, no text, no letters, no logos, no watermark
```

**§4 判**
```
Abstract dark background, a single amber light pulse traveling forward,
splitting into two diverging paths, one path fades out and the other
brightens, deep navy-black background, decision fork feeling,
empty space in the center, cinematic, minimal,
16:9, 5 seconds, no text, no letters, no logos, no watermark
```

**§5 交**
```
Abstract dark background, a soft white glowing light moving smoothly
from the left side of the frame to the right and passing out of frame,
like something being handed over, gentle trailing glow,
deep navy-black background, warm and calm, empty space in the center,
cinematic, minimal, 16:9, 5 seconds,
no text, no letters, no logos, no watermark
```

> 這五段的動態**本身就在教東西**：綠色一路衝出去（開迴路沒人管）、
> 藍色精準停住（位置回授）、紫色振盪收斂（PID）。
> 學員不會意識到，但看第三季的時候會覺得「對，就是這個感覺」。

---

## 四、縮圖（文生圖）

縮圖用**文生圖**不是文生影片，而且**主體一律用實拍去背**，AI 只生背景。

背景 prompt（Midjourney / DALL·E / Nano Banana 等）：

```
Clean dark studio background for a video thumbnail, deep navy-black
gradient with a soft green light glow coming from the left edge,
subtle vignette, slightly textured, completely empty, no objects,
no text, no logos, high resolution, 16:9
```

**縮圖統一模板**：

```
┌────────────────────────────────────────┐
│  E05          │                        │
│  ────         │    該集關鍵畫面        │
│  一鍵自檢     │    （實拍去背）        │
│  逐項綠燈     │                        │
└────────────────────────────────────────┘
   左 40% 文字      右 60% 圖
```

- 集數 `E05` 用最大字級（縮圖在手機上只有拇指大，集數要能一眼認出）
- 主標題不超過 8 個字
- **不要放人臉**（這門課賣的是「東西會動」，不是講師個人品牌）

---

## 五、音樂

用 Suno / Udio 生，或直接用 YouTube 音樂庫（更省事、零版權風險）。

**片頭 sting（3 秒）**
```
Short 3-second technology logo sting, minimal, one clean synth swell
rising into a single bright bell-like hit, subtle low sub-bass,
no drums, no melody, clean and modern, ends with a short reverb tail,
instrumental
```

**課程背景音樂（用於操作段落，需低調到幾乎不存在）**
```
Minimal ambient background music for an instructional video,
soft warm synth pads, very slow tempo, no drums, no percussion,
no melody hooks, extremely low energy, unobtrusive, loopable,
calm and focused, instrumental, 2 minutes
```

> 背景音樂的鐵律：**旁白空檔時你不該注意到它，注意到就是太大聲。**
> 混音時壓到旁白之下 24 dB。E05 那 5 秒無旁白的編碼器段落，
> 音樂要**完全靜音**，只留馬達聲。

**片尾（6 秒）**
```
Short 6-second outro music, gentle resolving synth chord progression,
warm and satisfying ending, soft bell accent, fades out cleanly,
no drums, instrumental
```

---

## 六、音效

用 ElevenLabs Sound Effects 或素材庫。

| 用途 | Prompt |
|---|---|
| 綠燈亮起（成功訊號） | `Short soft UI confirmation chime, single clean bell tone, gentle, positive, 0.5 seconds, no reverb tail` |
| 字卡出現 | `Very short subtle whoosh, soft air movement, clean, 0.3 seconds` |
| 警示（卡住了） | `Short low soft thud, muted warning tone, not alarming, 0.4 seconds` |
| 圈選標註出現 | `Tiny soft click, minimal UI tick, 0.15 seconds` |

**成功訊號音效要全課程統一，而且只用在成功訊號。**
這是給學員的聽覺錨點——他在做別的事、只用聽的時候，
那個聲音代表「該抬頭看螢幕了」。

---

## 七、實務建議：一次生成的成本控制

AI 影片是**吃角子老虎**，同一個 prompt 生 4 次結果差很多。

1. 每個 prompt **生 3–4 次**，挑最穩的一次
2. **只挑中間的 2–3 秒用**，頭尾通常有畸變或運動突變
3. 挑到好的素材就**存起來重複用**——片頭素材全課程只需要 1 個
4. 不要追求「每一集不同的片頭」，那是浪費錢也浪費學員耐心

整套課程實際需要的 AI 素材大概是：

| 素材 | 數量 |
|---|---|
| 片頭背景 | 1 |
| 片尾背景 | 1 |
| 季轉場背景 | 5 |
| 縮圖背景 | 1 |
| 音樂 | 3 |
| 音效 | 4 |

**共 15 個。** 不到 20 次成功生成就能覆蓋 30 支影片。

---

## 相關文件

- [拍攝規範](拍攝規範.md)
- [課程對照表](README.md)
