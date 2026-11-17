# Dynasty Wars 32xM（天地を喰らう 32X＋Mega-CD 移植）

把 CPS1 街機《天地を喰らう》（Dynasty Wars，日版 dynwarj）移植到 **SEGA Mega Drive + 32X + Mega-CD**，目標是儘量無損：
68000 程式靜態轉譯成 C 在 SH-2 上執行、32X 畫人物與前景、MD 畫背景、Mega-CD 播放 CD 音樂，並附中文化。

**本 repo 不含任何 Capcom 的資料。** 圖形、音樂、程式碼、文字都在建置時從你自己的 MAME ROM 檔重新產生。

## 一鍵建置

需要：Docker（Windows / macOS 用 Docker Desktop）、MAME 的 `dynwar.zip`（parent set，內含 `dynwarj/`、`dynwara/` 子資料夾，0.2xx 版以後的標準格式）。

```sh
# Linux / macOS
./build.sh /path/to/dynwar.zip

# Windows（PowerShell）
.\build.ps1 C:\path\to\dynwar.zip
```

第一次會建 Docker 映像（編譯 SH-2 與 68000 交叉編譯器，約 20–40 分鐘），之後每次建置約 5 分鐘。輸出在 `out/`：

| 檔案 | 用途 |
|---|---|
| `DynastyWars_32xM.32x` | 32X 卡帶 |
| `DynastyWars_32xM.cue` / `.iso` / `DW_MSU_02–29.wav` | Mega-CD 光碟（資料軌＋28 條 CD 音軌） |

模擬器：Genesis Plus EX / PicoDrive 等支援 32X＋Mega-CD 同時使用者（載入 .32x 卡帶，光碟放入 .cue）。
建置結束會印出 SHA-1；與發佈版相同代表建置完全一致（見下方「可重現性」）。

## Windows 逐步說明

1. 安裝 [Docker Desktop for Windows](https://www.docker.com/products/docker-desktop/)（預設的 WSL 2 模式），開啟後等它顯示 Engine running。
2. 安裝 [Git for Windows](https://git-scm.com/download/win)。
3. 準備自己的 MAME `dynwar.zip`（parent 集，內含 `dynwarj/`、`dynwara/`），例如放在 `C:\ROM\dynwar.zip`。
4. 在 PowerShell 下載原始碼並建置：

   ```powershell
   cd C:\
   git clone https://github.com/ihamana/DynastyWars_32X.git
   cd DynastyWars_32X
   .\build.ps1 C:\ROM\dynwar.zip
   ```

   若出現「無法執行指令碼」，先執行 `Set-ExecutionPolicy -Scope Process Bypass` 再重跑 `.\build.ps1`。
5. 成品在 `out\`：用 Genesis Plus EX 等支援 32X＋Mega-CD 的模擬器載入 `DynastyWars_32xM.32x`，光碟放入 `DynastyWars_32xM.cue`（模擬器需要 Mega-CD BIOS）。

注意：硬碟需約 10 GB 空間（Docker 映像與渲染的音樂）。腳本的換行由 `.gitattributes` 固定為 LF；若把 `.sh` 檔改成 CRLF，容器內會無法執行。也可以用 GitHub 的「Download ZIP」取代 `git clone`。

## 建置流程（`scripts/build_all.sh`）

1. `scripts/rom.py`：從 zip 組出程式 ROM、圖形 ROM、Z80、OKI（檢查 SHA-1）。
2. 編譯主機工具：Musashi 反組譯器（`d32/tools/libdasm.so`）、聲音渲染器（`dw/ref/sndref`：Z80＋ymfm YM2151＋OKI）。
3. 68000 → C：`meta/code_addr.json`（只有指令位址）＋程式 ROM → 反組譯 → `x32/tools/x68.py` 轉譯成 `x32/gen/split/*.c`。
4. 圖形：sprite／scroll1 轉 4bpp（`mkgfx.py`）、預縮圖片（`mkpic.py` ＋ `meta/pic.json`：圖塊編號與調色盤）、MD 顯示的圖片（`mdpic.py` ＋ `meta/snap`：稀疏的模擬器快照，只有地圖格與調色盤）。
5. 關卡背景：`scripts/pixrec.py` 依 `meta/bg/*.dwpx` 的「配方」從街機圖形重建 MD 圖塊（每個 8x8 圖塊＝某個街機圖塊的一塊經顏色對照表轉換；repo 只存參照與對照表），地圖、調色盤等索引資料直接在 `meta/bg`。
6. 中文：`meta/zh/*.xlsx`（翻譯表，只有位址與中文）＋字型 → `mkzh.py`。
7. 聲音：YM2612 音效（`fmsfx.py`，用街機的 Z80 音效程式錄下暫存器寫入再轉換）、CD 音樂（`sndref` 渲染街機音樂後 `mkmus.py` 切出循環點）。
8. CD 資料軌（`mksnd.py`）。
9. 卡帶（`x32/p1/build.sh`：SH-2、68000、Mega-CD 副 CPU 程式，`mkrom.py` 組合）。

不用 Docker 也可以：把 repo 放在 `/dw`，準備好 `/opt/tc/{sh,m68k}`（`docker/toolchain.sh`）、`/opt/sgdk/libgcc.a`、`/opt/fonts/SourceHanSerifTC-VF.ttf` 與 Python 套件（見 `Dockerfile`），執行 `scripts/build_all.sh dynwar.zip`。

## 目錄

| 路徑 | 內容 |
|---|---|
| `x32/p1/sh2` | 32X SH-2 程式（主／副 CPU、繪圖 render.c、選單 ui.c） |
| `x32/p1/md`、`x32/p1/cd` | MD 68000 程式（背景、音效）、Mega-CD 副 CPU 程式（PCM、CD 讀取） |
| `x32/p1/tools` | 資料轉換工具 |
| `x32/tools/x68.py`、`x32/gen/rt.h`、`x32/gen/nat.c` | 68000 → C 靜態轉譯器、執行環境、手寫的原生版常用函式 |
| `meta/` | 建置用的中繼資料（位址、圖塊參照、翻譯）：**沒有像素、音訊、程式碼或原文** |
| `fonts/` | KH-Dot-Dougenzaka-16、Fusion Pixel 8/12px（SIL OFL 1.1）；思源宋體建置時下載 |
| `docs/PLAN.md` | 開發紀錄 |

## 可重現性

在同一個 Docker 映像（x86-64）內建置會得到與發佈版逐位元相同的檔案。少數步驟用到浮點運算（調色盤 k-means、
圖片縮放、音樂循環點），在不同 CPU（例如 ARM 主機上模擬 x86）或不同版本的 numpy／OpenBLAS 下可能產生
極小差異；遊戲仍可正常執行，只是 SHA-1 不同。

## 授權與第三方

- 本專案（移植程式、工具、腳本、中繼資料）採用 **CC BY-NC-SA 4.0**（姓名標示－非商業性－相同方式分享），作者 Mana，見 `LICENSE`。
- 遊戲本體《天地を喰らう》© CAPCOM。本專案與 Capcom 無關，請使用你合法擁有的 ROM。
- `dw/ref/m68k*`：Musashi（Karl Stenerud，MIT）。`dw/ref/z80.c`：superzazu/z80（MIT）。`third_party/ymfm`：Aaron Giles（BSD-3-Clause）。
- `x32/p1/md/icd_block.inc`：32X 開機必需的 MARS 初始化／安全碼（32X 自製軟體的標準內容）。
- 建置時下載：SGDK 2.11 的 `libgcc.a`、思源宋體 TC 2.003（SIL OFL 1.1）。
- 字型（`fonts/`）：SIL Open Font License 1.1（見各字型內的授權資訊）。

---

**English summary.** A port of Capcom's CPS1 arcade game *Dynasty Wars* (Japanese set `dynwarj`) to Sega Mega Drive + 32X + Mega-CD.
This repository contains **no Capcom data**: code, graphics, music and text are regenerated at build time from your own MAME
`dynwar.zip`. Build with Docker: `./build.sh /path/to/dynwar.zip` (Windows: install Docker Desktop and Git, `git clone` this repository, then `.\build.ps1 C:\path\to\dynwar.zip` in PowerShell); output in `out/`.
License: CC BY-NC-SA 4.0 for this project's own work (see `LICENSE`); third-party parts keep their own licenses.
