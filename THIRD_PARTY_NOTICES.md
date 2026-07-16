# Third-Party Notices / 第三方元件聲明

本專案（phone_blocky）除自有程式碼（採 MIT，見 [LICENSE](LICENSE)）外，
散布時包含下列第三方元件。各元件之著作權與授權條款如下，散布本專案時必須一併保留。

This project bundles the following third-party components. Their copyright and
license terms are reproduced below and must be retained when redistributing.

---

## 1. JoyStick (bobboteck)

- **檔案 / Files**: [`data/joy.js`](data/joy.js), [`data/joy.css`](data/joy.css)
- **作者 / Author**: Roberto D'Amico (Bobboteck)
- **來源 / Source**: https://github.com/bobboteck/JoyStick
- **授權 / License**: MIT License
- **修改說明 / Modifications**: 觸控座標改用 `getBoundingClientRect()` 校正、
  搖桿限位改為距離判斷等本地調整；原始 MIT 授權標頭保留於 `data/joy.js` 檔頭。

```
The MIT License (MIT)

Copyright (c) 2015 Roberto D'Amico (Bobboteck).

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## 2. Blockly (Google)

- **檔案 / Files**: [`data/blockly.min.js.gz`](data/blockly.min.js.gz)
- **著作權 / Copyright**: Copyright Google LLC
- **來源 / Source**: https://github.com/google/blockly
- **授權 / License**: Apache License 2.0（全文見 [`LICENSE-APACHE-2.0.txt`](LICENSE-APACHE-2.0.txt)）
- **注意 / Note**: 隨附的 `blockly.min.js` 為壓縮後產物，建置工具已移除原始
  `@license` 標頭。依 Apache 2.0 §4，特於此保留授權與著作權聲明。

    Copyright Google LLC
    SPDX-License-Identifier: Apache-2.0

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
