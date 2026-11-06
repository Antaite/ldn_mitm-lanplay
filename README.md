# ldn_mitm-lanplay：不用電腦的 Switch 區網／網路連線對戰

這是 [ldn_mitm](https://github.com/spacemeowx2/ldn_mitm) 的分支版，把原本要在電腦上跑的
[switch-lan-play](https://github.com/spacemeowx2/switch-lan-play) 用戶端直接做進 Switch 的系統模組裡，
再用 **Tesla 覆蓋層** 切換、設定。

| 模式 | 用途 | 需要電腦？ |
| --- | --- | --- |
| 關閉 | 遊戲用原本的本機無線通訊（ldn_mitm 不介入） | — |
| 同區網 | 同一個 Wi-Fi 底下的 Switch 互連（原版 ldn_mitm 功能） | 不用 |
| 網際網路 | 連到 lan-play 中繼伺服器，跟外地朋友同房 | **不用** |

網際網路模式與官方 lan-play 協定相容：朋友用電腦跑 lan-play、或也用這個模組，只要連**同一台伺服器**就能一起玩。

## 支援版本

| 下載檔 | 大氣層 | 系統版本 |
| --- | --- | --- |
| `ldn_mitm-lanplay_v1.3.0_AMS1.12.0_FW23.0.0` | 1.12.0 | 23.0.0 |
| `ldn_mitm-lanplay_v1.3.0_AMS1.12.1_FW23.0.1` | 1.12.1 | 23.0.1 |

- 真系統（sysMMC）和虛擬系統（emuMMC）都能用，從哪個系統開進大氣層就在哪個生效。
- 請下載**跟主機大氣層版本相同**的那一包。版本號在「設定 → 主機 → 系統更新」，例如 `23.0.1｜AMS 1.12.1｜E`。
- 裝錯版本時：大版本不同（例如 1.11 對 1.12）會自動停用網際網路模式；小版本不同會在覆蓋層顯示黃色提醒。
- 覆蓋層需要 [nx-ovlloader](https://github.com/WerWolv/nx-ovlloader) ＋ [Tesla Menu](https://github.com/WerWolv/Tesla-Menu)（或 Ultrahand）。

## 安裝

1. 下載對應版本的 zip，解壓後把裡面的 `atmosphere`、`switch`、`config` 資料夾**整個複製到 SD 卡根目錄**，覆蓋原版 ldn_mitm。
2. 重新開機進入大氣層。
3. 按 Tesla 叫出鍵（預設 `L + ↓ + R3`）→ 選「LAN Play 區網對戰」。

## 使用方式

1. **對戰模式**：按 A 依序切換「關閉 → 同區網 → 網際網路」。改了會立刻存檔並套用。
2. **網際網路伺服器** → 「＋ 新增伺服器」：輸入名稱、位址（網域或 IP）、連接埠（預設 11451），有帳號密碼的伺服器再填帳密 → 「儲存並使用」。
   - 清單裡按 **A** 切換使用中的伺服器、按 **Y** 編輯或刪除。
3. 上方「狀態」會顯示：連線狀態、延遲、伺服器在線人數、自己的虛擬 IP（10.13.x.y）。
4. 進遊戲，在遊戲裡選「本地通訊／區域連線」那類選項（**不是**任天堂線上），房主開房、其他人搜尋即可。
   - 切換模式或伺服器後，要在遊戲裡**重新進入**本地通訊畫面才會生效。

### 伺服器清單

- **我的伺服器**：你存過的伺服器，Ⓐ 使用、Ⓨ 編輯或刪除、Ⓧ 重新測試。打開時會測每台的延遲
- **＋ 新增伺服器**：自己輸入位址（例如自架在 NAS 上的）
- **＋ 從公開伺服器挑選…**：29 台社群／私人架設的公開伺服器（台灣、香港、日本、中國優先），進去會先測延遲，
  有回應的排前面。按 Ⓐ 就加進「我的伺服器」並開始使用；已加過的標「已加入」。不要的回「我的伺服器」按 Ⓨ 刪除

公開伺服器來源：[GreatWizard/lan-play-status](https://github.com/GreatWizard/lan-play-status)（2026-07）、
[NS WIKI LanPlay](https://nswiki.cn/switch1/tools/lanplay/)（2026-06）、CarlZeng 的 lan-play 教學（2024-07）。
都是別人架的，隨時可能下線，請以測試結果為準。

### 房間列表

主畫面「房間列表」會在目前的網路上搜尋 1.5 秒，列出**正在等人加入**的房間：

- 遊戲名稱（主機有裝的遊戲會顯示名稱，沒裝的顯示 ID）、人數／上限
- 房主和房內成員的名字、房主的虛擬 IP
- 網際網路模式看的是**目前連著的伺服器**；同區網模式看的是同一個 Wi-Fi
- 已經開打的房間、只連著伺服器沒開房的人不會出現；按 Y 重新搜尋

### 狀態說明

| 顯示 | 意思／怎麼辦 |
| --- | --- |
| 已連線 | 正常，可以開房 |
| 連線中… | 剛開始連線，等幾秒 |
| 找不到伺服器 | 位址打錯，或 Switch 沒連上網路 |
| 伺服器沒有回應 | 伺服器關了、連接埠不對，或路由器沒轉發 UDP |
| 這台伺服器需要帳號密碼／帳號或密碼錯誤 | 在伺服器設定裡填正確帳密 |
| 大氣層版本不符 | 換成對應大氣層版本的那一包 |

## 自己架伺服器（NAS／電腦）

`server/docker-compose.yml` 是官方 lan-play 伺服器的設定：

```sh
cd server
docker compose up -d
```

路由器要把 **UDP 11451 和 TCP 11451** 轉發到那台機器（TCP 只用來顯示在線人數）。
朋友們在覆蓋層新增伺服器時填你的 DDNS 網域即可。想加帳密：看檔案內的註解。

## 設定檔

`sdmc:/config/ldn_mitm/lanplay.ini`，覆蓋層會自動改寫；格式見 `config/ldn_mitm/lanplay.ini.example`。
密碼以明文存在 SD 卡上（與電腦版 lan-play 相同），請不要用重要帳號的密碼。

## 注意事項

- 網際網路模式開著時，模組會一直跟伺服器保持連線（每 3 秒 ping、每 10 秒保活），流量很小；不玩時可以切回「同區網」或「關閉」。
- 每台主機會自動產生一個虛擬 IP（10.13.x.y）。如果同一台伺服器上有人跟你用到同一個 IP，系統模組會自動偵測並在背景換一個新的、存進設定檔，不用手動處理。正在房間裡對戰時會先等 5 秒，讓剛連上的那一方先換。
- 延遲取決於伺服器距離，台灣玩家盡量選近的伺服器或自己架。
- 只支援 IPv4 伺服器位址。

## 編譯

推到 GitHub 後 Actions 會自動用 devkitPro 容器，各用大氣層 1.12.0 和 1.12.1 的函式庫編一次；
在 Actions 頁面下載 artifact，或推 `v*` 標籤自動建立 Release（兩個 zip）。

本機編譯（需要 Docker）：

```sh
AMS=1.12.1 docker compose run --rm devkit
# 產物在 out/sd/
```

要支援新的大氣層版本：在 `.github/workflows/build.yml` 的 `matrix` 加一行即可。

### 協定測試（電腦上）

`lanplay/tests` 可以在 Linux 上把隧道與 lwIP 編成兩個模擬主機，透過真的 lan-play 伺服器互相廣播、建立 TCP、傳 200 KB 驗證：

```sh
make -C lanplay/tests
./lanplay/tests/lpclient host <伺服器> 10.13.1.1 &
./lanplay/tests/lpclient join <伺服器> 10.13.2.2
```

## 架構

```
遊戲 ──ldn:u──▶ ldn_mitm（攔截本地通訊）
                  │  socket 呼叫經 net_backend 分流
                  ├─ 同區網：系統 socket（原版行為）
                  └─ 網際網路：lwIP 虛擬網卡 10.13.x.y
                                 │ 原始 IPv4 封包
                                 ▼
                        lan-play 隧道（UDP 11451）──▶ lan-play 伺服器 ──▶ 其他玩家
Tesla 覆蓋層 ──IPC 65100~65103──▶ 讀狀態／重新載入設定
```

- `lanplay/src/lp_tunnel.c`：lan-play 用戶端（保活、ping、分段重組、帳密驗證、在線人數、DNS 備援）
- `lanplay/src/vnet.c`：lwIP 網卡與 socket 包裝（虛擬 fd ≥ 0x300）
- `lanplay/port/switch/`：lwIP 的 ams::os 移植層
- `ldn_mitm/source/net_backend.cpp`、`lanplay_service.cpp`：分流與設定／狀態
- `overlay/source/main.cpp`：Tesla 覆蓋層（內建鍵盤）

## 授權

- 本專案沿用 ldn_mitm 的 **GPLv2**；散布編好的檔案時請一併公開原始碼。
- lwIP：BSD 授權（`third_party/lwip/COPYING`）；libtesla：GPLv2。
- lan-play 協定依 switch-lan-play 的伺服器行為重新實作，未複製其用戶端程式碼。
- 原版說明：`README.upstream.md`。
