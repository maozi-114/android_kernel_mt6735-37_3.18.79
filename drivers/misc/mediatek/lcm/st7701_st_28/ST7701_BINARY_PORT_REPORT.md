# ST7701 (st7701_st_28 / boe7701_28) 二进制还原与移植经验报告

| 项 | 内容 |
|----|------|
| 平台 | MT6735M / kernel-3.18 |
| 目标屏 | BOE ST7701 类 2.8"，480×640 |
| 二进制驱动名 | `st7701_st_28`（日志：`hui boe7701_28`） |
| 工程对外名 | `st7701`（兼容 `CONFIG_CUSTOM_KERNEL_LCM` / mtkfb force） |
| 工作目录 | `drivers/misc/mediatek/lcm/st7701/` |
| 报告日期 | 2026-07-26 |
| 刷入范围 | 仅 kernel（LK / DTB / system / vendor 不动） |

---

## 1. 背景与目标

设备已有完整可运行旧固件，本次只换新屏 + 自编译 kernel。早期问题是 LCM 名/驱动不匹配导致 `dpmgr_handle == NULL` panic。后续阶段目标变为：

> **完全按厂商二进制 LCM 驱动行为**，在本 tree 写出可编译的 `st7701.c`，而不是拼凑无关参考驱动。

### 1.1 证据优先级（本轮铁律）

| 优先级 | 来源 | 可否当真值 |
|--------|------|------------|
| P0 | 同一镜像反编译：`get_params` / `init` / `compare_id` / init 表 | **是** |
| P1 | 从二进制抽出的 init 命令表（`st7701.c_dump_org_lk_st7701`） | **是**（表本身） |
| P2 | dump 文件里的 host 时序、RST、suspend 模板 | **否**（常是移植填空） |
| P3 | `lcd_ST7701S_480x640.c`（AIT/RTNA 平台） | **否**（仅同类屏参考） |
| P4 | 目录内被改过的 `st7701.c` 工作副本 | **否**（曾偏离二进制） |

**教训：** 同分辨率、同 ST7701 家族 ≠ 同一套 lane/mode/porch/init 表。  
“参考文件”只能帮读寄存器语义，不能替代反编译常量。

---

## 2. 二进制驱动定位

### 2.1 角色与地址（用户反编译）

| 角色 | 符号/地址 | 说明 |
|------|-----------|------|
| 驱动名 | `"st7701_st_28"` @ `0x40C3C2E6` | 非工程 force 名 `st7701` |
| `set_util_funcs` | `sub_40413324` | 标准 |
| `get_params` | `sub_40413288` | **host DSI 真值** |
| `init` | `sub_404133F4` | RST + push_table |
| `suspend` | `sub_404133D4` | 短表 `unk_40E5ED10`（5 条） |
| `resume` | `sub_40413470` | 直接再调 init |
| `compare_id` | `sub_404131A0` | **不是** 读 DSI ID |
| init 表 | `unk_40E5EE60` | 41 条，步长 66 字节 |

### 2.2 易错点

- `sub_404131A0` **只是** `lcm_compare_id`，不是整驱。
- 日志里的 `hui boe7701_28` 是模组/BOM 线索，与 `.name = st7701_st_28` 并存。
- 本 tree 为避免改 mtkfb force / defconfig，**导出仍用** `st7701_lcm_drv` + `.name = "st7701"`，行为按 `st7701_st_28`。

---

## 3. 行为规格（已锁定）

### 3.1 Host DSI（`lcm_get_params`）

| 字段 | 值 | 备注 |
|------|-----|------|
| type | `LCM_TYPE_DSI` | |
| width × height | 480 × 640 | |
| mode | `SYNC_PULSE_VDO_MODE` (=1) | **不是** EVENT / BURST / CMD |
| LANE_NUM | `LCM_TWO_LANE` (=2) | **不是** 1-lane |
| PS | `0x100` → `LCM_PACKED_PS_24BIT_RGB888` | |
| color | RGB（`LCM_COLOR_ORDER_RGB`） | 与 `0x36=0x00` 一致 |
| VSA / VBP / VFP | 8 / 20 / 20 | VTotal = 688 |
| HSA / HBP / HFP | 10 / 80 / 80 | HTotal = 650 |
| PLL_CLOCK | 150 MHz | |
| cont_clock | 1 | 连续 clock |
| ssc | 相关位置 1 | 实现为 `ssc_disable = 1` |

**帧时序一句话：**

```text
2-lane | SYNC_PULSE video | RGB888 | PLL≈150
H: 10 + 80 + 480 + 80 = 650
V:  8 + 20 + 640 + 20 = 688
```

### 3.2 颜色顺序

| 层级 | 设定 | 结论 |
|------|------|------|
| Host | 24bit packed RGB888，未强制 BGR | RGB |
| 面板 `0x36` | `0x00`（BGR 位=0，无 MX/MY/MV） | RGB，正向 |
| `0x3A` COLMOD | 表中无 | 依赖复位默认 + host 打包 |

**不要** 使用 `lcd_ST7701S` 的 `DSI_TOP_BGR_ORDER`。

### 3.3 硬件 Reset（`lcm_init`）

```text
RESET = 1;  delay 20 ms
RESET = 0;  delay 50 ms
RESET = 1;  delay 100 ms
push_table(init_table, 41 entries)
```

与 `compare_id` 的 RST **不同**（见下）。init 与 compare 各自独立，移植时不要混成一套。

### 3.4 初始化命令表（面板）

- 基址概念：`unk_40E5EE60`，**41** 项，结构 `{cmd, count, para[64]}` 步长 66。
- 发送：`cmd==delay标记 → MDELAY`；`end → 停`；其它 → `dsi_set_cmdq_V2(..., force=1)`。
- 本 tree 用 `REGFLAG_DELAY=0xFFFC` / `REGFLAG_END_OF_TABLE=0xFFFD`（与 MTK 惯例一致；二进制标记为 `0xFC`/`0xFD` 语义相同）。

**流程：**

```text
Page 0x13:  FF ... 13 ; EF=08
Page 0x10:  C0=4F00, C1=0D12, C2=000A, CC=10, Gamma B0/B1
Page 0x11:  电源 B0..D0 → delay 100 → E0..ED (GIP)
Page 0x00:  11 → delay 120 → 35 00 → 36 00 → 29 → delay 50 → END
```

**关键寄存器：**

| 命令 | 作用 |
|------|------|
| `FF 77 01 00 00 xx` | 切 page 10/11/13/00 |
| `C0/C1/C2` | 显示行/porch/帧相关（**面板内部**，≠ host H/V porch） |
| `B0/B1` page10 | 正负 Gamma |
| `B0/B1` page11 | Vop / VCOM |
| `E0~ED` | Power + GIP |
| `11` / `29` | Sleep Out / Display On |
| `35 00` | TE off |
| `36 00` | MADCTL：RGB |

完整表已写入 [`st7701.c`](st7701.c) 的 `init_setting_vdo[]`，与 dump 二进制提取表一致。

### 3.5 `lcm_compare_id`（`sub_404131A0`）

```text
RESET: 1 → 10ms → 0 → 10ms → 1
delay 120 + 120 ms
AUXADC channel 12，采 10 次
adc_vol = avg(100 * data[0] + data[1])
log: "hui boe7701_28 lcm adc_vol is %d"
return (adc_vol <= 89)
```

- **不是** `read_reg_v2(0x04)`。
- 多 LCM 共存时必须保留真实 ADC 逻辑；当前工程若 force 名匹配，compare 可能被弱化，但仍应按二进制实现。
- Kernel 侧使用 `IMM_GetOneChannelValue()`（`mt_auxadc`）。

### 3.6 Suspend / Resume

| 函数 | 行为 |
|------|------|
| suspend | 只推短表（二进制 5 条 @ `unk_40E5ED10`），**不做**完整 re-init |
| resume | 直接 `lcm_init()` |

**未完全锁定：** 短表 5 条的原始字节尚未从二进制拆出。  
当前实现采用常见序列（可替换）：

```text
0x28 → delay 50 → 0x10 → delay 120 → END
```

且 suspend **不再** 额外 `SET_RESET_PIN(0)`（与“只推短表”描述一致）。若实机休眠花屏/起不来，优先补二进制 suspend 表。

---

## 4. 参考文件对比（为何不能直接抄）

### 4.1 `st7701.c_dump_org_lk_st7701`

| 模块 | 评价 |
|------|------|
| init 命令表 | **可信**（用户自二进制提取） |
| `get_params` 1-lane / EVENT / 小 porch / PLL260 | **不可信**（与 `sub_40413288` 矛盾） |
| init RST 50/50/120 | **不可信**（二进制为 20/50/100） |
| compare 读 0x04 + return 1 | **不可信** |

**教训：** dump 文件 =「表真 + 壳假」。只许抄表，不许抄 host 时序。

### 4.2 `lcd_ST7701S_480x640.c`

| 项 | 该文件 | 二进制 st7701_st_28 |
|----|--------|---------------------|
| 平台 | AIT/RTNA `Hal_DSI_*` | MTK `lcm_drv` |
| Lane | 2 | 2（碰巧相同） |
| Mode | SYNC_PULSE | SYNC_PULSE（碰巧相同） |
| 颜色 host | BGR | RGB |
| Porch | 大 VFP 等另一套 | H10/80/80 V8/20/20 |
| init 顺序 | 先 `0x11` 再配 page | 先配 page 再 `0x11` |
| 寄存器值 | 另一套 gamma/GIP | boe7701_28 专用 |

**教训：** 同 IC 家族、同分辨率仍可能是**完全不同模组调校**。2-lane+PULSE 一致不代表可以换表。

### 4.3 改写前的工作副本 `st7701.c`

曾出现：

- 1-lane + `SYNC_EVENT`
- PLL 180/260
- 被改过的 gamma / 多出的 page13 `E8` 步进
- compare 强制成功

这些都会造成「能 probe 不 panic，但时序/寄存器不对 → 黑屏/花屏」。

---

## 5. 已落地修改

**文件：** [`st7701.c`](st7701.c)

| 模块 | 实现要点 |
|------|----------|
| `lcm_get_params` | 2-lane、SYNC_PULSE、porch、PLL150、RGB888、cont_clock、ssc_disable |
| `lcm_init` | RST 20/50/100 + 二进制 init 表 |
| `lcm_suspend` | 短表 only |
| `lcm_resume` | `lcm_init()` |
| `lcm_compare_id` | ADC ch12，≤89，日志 boe7701_28 |
| 导出 | `st7701_lcm_drv`，`.name = "st7701"` |

未改（本轮不需要）：

- `mtkfb` force 名逻辑
- `mt65xx_lcm_list` / defconfig（已是 st7701 路径）

---

## 6. 方法论文：以后怎么反编译 LCM

### 6.1 必拆函数（按顺序）

1. **`LCM_DRIVER` 结构体** — 看挂了哪些回调、name 字符串  
2. **`get_params`** — lane / mode / porch / PLL / PS / color（**没有这步不要猜 host**）  
3. **`init`** — RST 波形 + 是否只 push_table  
4. **init 表** — 基址、条数、步长、delay/end 标记  
5. **`compare_id`** — ADC 还是 DSI read  
6. **`suspend`/`resume`** — 短表内容、是否拉 RST  

### 6.2 搜索线索

- 字符串：`st7701`、`boe7701`、`lcm adc_vol`、`hui`
- 常量：`480`、`640`、porch 小数、`PLL` 100–200 段
- 表特征：连续 `0xFF,5,{0x77,0x01,0x00,0x00,0x1x}` bank 切换

### 6.3 禁止事项

- 禁止用另一平台（AIT）的 BGR/porch 直接覆盖 MTK `LCM_PARAMS`
- 禁止把面板 `C1/C2` 当成 host `VBP/VFP`
- 禁止 init 表与 host 时序来自两个不同驱动却假装是一套
- 禁止在未确认 suspend 表时乱加 RST/电源动作掩盖问题

---

## 7. 验证清单（用户手动编译）

助手不代为全量编译。建议：

```bash
# 确认 LCM 配置
grep CONFIG_CUSTOM_KERNEL_LCM out/.config
# 应含 st7701
```

刷 **仅 kernel** 后串口期望：

```text
st7701_st_28/boe7701_28: 480x640 RGB888 2-lane SYNC_PULSE PLL=150
st7701_st_28 lcm_init: RST 20/50/100 then DCS table
hui boe7701_28 lcm adc_vol is <number>    # 若走到 compare
dpmgr create path SUCCESS(...)
```

不应再出现：

```text
disp_lcm_probe returns null
PC is at dpmgr_path_get_last_config
```

### 现象分流

| 现象 | 优先怀疑 |
|------|----------|
| 仍 panic @ dpmgr | 驱动未链入 / 名未匹配 / primary_display_init 失败 |
| 不 panic 全黑 | lane 硬件是否 2-lane、RST GPIO、供电、init 表是否被改 |
| 花屏/带状 | porch/PLL、GIP(E*)、lane 极性/交换 |
| R/B 反色 | 先只改 host `color_order` **或** 只改 `0x36`，不要同时改 |
| 休眠异常 | 补齐二进制 suspend 5 条表 |

---

## 8. 已知缺口 / 后续

1. **suspend 表 `unk_40E5ED10` 五条原始字节** — 待反编译替换当前占位。  
2. **`E2` 参数长度** — 叙述有 12B，dump 为 13B（末尾 `0x00`）；现按 dump。若二进制确认 12B 再删。  
3. **对外名** — 若取消 mtkfb force、改走真实名匹配，需同步 `.name` / list / defconfig 为 `st7701_st_28` 或保持 force。  
4. **`mtkfb_probe` 对 `primary_display_init` 返回值防护** — 与本屏参数无关，但是防二次 panic 的工程债。  
5. **同镜像另一套 `sub_4041356C`** — 更紧 porch 的另一 480×640 面板；勿与 boe7701_28 混淆。

---

## 9. 一句话结论

**厂商二进制 `st7701_st_28`（日志 boe7701_28）是 480×640、2-lane、SYNC_PULSE、RGB888、PLL≈150、porch H10/80/80 V8/20/20 的 ST7701 模组驱动；认屏靠 AUXADC ch12≤89，init 为 RST 20/50/100 + 固定 page10/11/13 命令表。移植时必须以反编译的 `get_params`/`init`/`compare_id`/命令表为唯一真源；`lcd_ST7701S` 与 dump 壳参数不得覆盖 host 时序。本 tree 已按该规格重写 `st7701.c`，对外仍注册为 `st7701` 以兼容现有 force 与配置。**

---

## 10. 相关文件

```text
drivers/misc/mediatek/lcm/st7701/st7701.c                 # 现行驱动（本轮重写）
drivers/misc/mediatek/lcm/st7701/st7701.c_dump_org_lk_st7701  # 二进制 init 表来源
drivers/misc/mediatek/lcm/st7701/lcd_ST7701S_480x640.c    # 仅参考，非真源
drivers/misc/mediatek/lcm/st7701/st7701.c_bak             # 历史备份
drivers/misc/mediatek/video/common/mtkfb.c                # force LCM name=st7701
drivers/misc/mediatek/lcm/mt65xx_lcm_list.c               # 驱动列表
putty.log                                                 # 串口验证
CLAUDE.md                                                 # 工程总约定与阶段史
```
