# CLAUDE.md — AP7350 / MT6735M kernel-3.18 显示启动崩溃修复

## 工作约定（必须遵守）

1. **用户手动编译**：不要主动执行全量/内核编译（如 `./build.sh`、`make` 出镜像等）。只改源码与配置说明；编译、刷机由用户完成。验证以用户提供的 `putty.log` / 串口结果为准。
2. **禁止越界读树**：工作范围 **仅限本目录** 
   `/home/maozi/AP7350-master/kernel-3.18` 
   **不要**查看、搜索、对比上级目录或其它项目（含 `../` 下的另一套 kernel、旧固件源码树等）。需要对照旧行为时，只根据用户描述与本树内串口日志推断。
3. **项目目的与差分前提**：
   - 目标：给 **已有完整可运行固件** 的设备 **适配新屏幕**（本树补 LCM / display 相关 kernel 改动）。
   - 旧固件：**除不能显示外，其它均可正常启动**（含充电、USB、KPOC、进系统等）。
   - 因此：**当前新问题一定来自「自编译并替换的 kernel」相对旧 kernel 的差异**，而不是 LK/DTB/系统分区本身坏了。
   - 用户替换范围 **只有 kernel 镜像**；**未改动** LK、DTB/DTBO、ramdisk、system、vendor 等。分析与修改应优先怀疑本 tree 内 display/LCM/与之耦合的初始化路径，以及本树相对旧 kernel 的 config/代码差分；避免把根因推到未改动的 userspace/LK（除非日志只能由 kernel 侧解释，且仍只在本树内修）。

---

## 工程摘要

| 项 | 值 |
|----|-----|
| 平台 | MT6735M (DT) |
| Kernel | Linux 3.18.79 |
| 工作目录 | `/home/maozi/AP7350-master/kernel-3.18`（**唯一允许访问的工程树**） |
| 串口日志 | `putty.log` |
| 目标 LCM（kernel 强制） | `st7701` |
| LK 原传递 LCM 名 | `st7785m_boe_ips`（忽略） |
| 刷入范围 | **仅替换 kernel**；LK / DTB·DTBO / ramdisk / system / vendor **不动** |
| 编译 | **用户手动**；助手不代为编译 |

---

## 阶段问题（Problem Timeline）

### 阶段 0 - 21
**已修复** 
- 强制使用st7701屏幕驱动
- 已通过逆向lk获取正确gpio timing和电压等设置

### 阶段22

    - LCM 驱动强制为 `st7701` + `lcm_init()` 成功，但**DSI 时序 / 面板 timing / 电压 /
    色深配置**未重新加载，导致面板未收到有效信号，画面全绿。
    - Framebuffer 内存分配（LK FB ignore + kernel DMA alloc + m4u config）已完成，但未严格对齐面板读地址和初始化路径。
    - 内存生命周期管理部分完成，但未覆盖全部 DSI init 流程（`primary_display_init` / `dpmgr_path_config` /
    `disp_lcm_init`）。
    - 关键日志：`filled first page white`、`kernel va=0xffffff801130f000 mva=0x100000`、`LK framebuffer
    ignored`、`initial layer config ret=0`、`M4Uconfig_port:DISP_WDMA0`。

    **下一步建议**：在 `mtkfb.c` / `primary_display.c` 中，force LCM 后重新走完整 panel init + DSI timing 配置，确保
    `src_fmt`、`pitch`、`xres`、`yres` 与 `st7701_st_28` 定义完全一致。



## 已实施修改（Fix Report）

### 1. 强制 LCM 名：`st7701`

**文件：** `drivers/misc/mediatek/video/common/mtkfb.c`

- `mtkfb_find_lcm_driver()`  
  - 仍解析 videolfb（fb base / vram / fps 等）  
  - **覆盖** lcmname 为 `"st7701"`  
  - 打印：`[mtkfb] force LCM driver name: st7701`

- `mtkfb_probe()`  
  ```c
  mtkfb_find_lcm_driver();
  is_lcm_inited = 0;   /* 强制kernel重新初始化lcm */
  primary_display_init(mtkfb_lcm_name, lcd_fps, is_lcm_inited);
  ```

## 预期串口日志（修复后）

应出现：

```
[mtkfb] force LCM driver name: st7701
[LCM ST7701] util funcs initialized: ...
[LCM ST7701] lcm_init() called
dpmgr create path SUCCESS(...)
```

---

## 验证步骤

1. 用户手动确认 `.config` 与重编 kernel：
   ```bash
   grep CONFIG_CUSTOM_KERNEL_LCM out/.config
   # 用户自行执行项目既有编译命令（助手不代为编译）
   ```
2. 刷入 **仅 kernel**，抓串口，确认 force 名 + path SUCCESS
3. 若仍黑屏但不 panic：再查 GPIO reset/power、分辨率 640x480 与实屏是否一致、DSI timing
4. 若再次 panic：确认 `st7701.o` 是否链进内核、`ST7701` 宏是否生效

---

### 建议下一阶段

1. 查看putty.log日志，分析m4u的是否能正确读取kernel分配的新framebuffer


---

## 关键路径速查

```
drivers/misc/mediatek/video/common/mtkfb.c          # force LCM name
drivers/misc/mediatek/video/mt6735/primary_display.c # path create / config input
drivers/misc/mediatek/video/mt6735/ddp_manager.c     # ASSERT(dp_handle)
drivers/misc/mediatek/lcm/st7701/st7701.c            # st7701 驱动
putty.log                                            # 串口日志
```

---

## 一句话结论

现在已经通过强制写驱动名字st7701与强制加载st7701驱动成功 但是后续的画面内存分配与显示初始化生命周期未严格对齐 导致图像缓冲区读出来大部分是绿色画面 下一步建议着重修复 内存分配和生命周期管理

---

## 阶段 23：实体屏全黑、投屏绿色 - 结论修正（2026-07-30）

### 新的实机现象（优先级最高）
- **物理 ST7701 屏幕从开机到系统启动全程黑屏。**
- Android 投屏/截图画面大部分呈绿色。

这两个现象不能等同：投屏通常取得 SurfaceFlinger/HWC 的合成输出，未必经过实体 DSI 面板。因此，投屏绿色**不能**证明 OVL -> RDMA -> DSI -> ST7701 的实体输出正常；此前“绿色画面”不应再用作实体面板显示成功的证据。

### 本轮已确认的 kernel 侧事实
1. `st7701` 已被 force 选择，且 `lcm_init()` 已执行：
   ```text
   call lcm_drv->init name=st7701 force=1
   st7701_st_28 lcm_init: RST 20/50/100 then DCS table
   disp_lcm_init ... ret=0
   ```
2. 新 framebuffer 的 coherent DMA 分配、三缓冲布局与 M4U 映射自洽：
   ```text
   dma=0x9b100000, mva=0x100000, alloc=0x384000
   pages: 0x100000 / 0x22c000 / 0x358000
   ```
   单页 `480 * 640 * 4 = 0x12c000`，总计 `0x384000`。
3. framebuffer 由 `M4U_PORT_DISP_OVL0` 映射，日志中 layer0 也实际指向 `0x100000`；未发现 M4U translation fault 或 map failure。
4. kernel 显示 path 曾启动并收到首帧完成事件：
   ```text
   bootstrap path start/trigger done, busy=1 video=1
   initial frame started ... frame_wait=19
   ```
5. 当前 `st7701.c` 的 host 参数来自同镜像二进制还原，不应改回 dump 壳中的 1-lane/PLL260：
   ```text
   480x640, 2-lane, SYNC_PULSE_VDO_MODE, RGB888,
   packed 24-bit PS, PLL=150, H=10/80/80, V=8/20/20
   ```

### 修正后的根因优先级
实体黑屏的首要方向不再是 framebuffer/M4U，而是：
1. **背光没有开启**：确认 LED/backlight enable、PWM、BL 电源及其 GPIO；DSI 即使工作，背光关闭时实体屏仍全黑。
2. **面板电源或 RESET 实际电平/时序不对**：`lcm_init()` 日志只证明调用发生，不证明 GPIO 接到正确管脚且实际翻转；核对 AVDD/IOVCC、reset、bias/PMIC 与旧 LK 行为。
3. **DSI PHY 或物理 lane 不工作/参数未真正生效**：核对 DSI0 的 lane number、PLL/HS clock、clock lane、lane mapping 与实体硬件；确认 bootstrap 的 `DSI_ForceConfig()` 后硬件寄存器确实是 ST7701 的参数，而非 LK 遗留值。
4. **面板初始化命令未被实体面板正确接收**：当前表和 reset 源自二进制，仍应以 DSI 命令/错误状态、示波器或逻辑分析验证实际总线活动。
5. **面板 COLMOD 状态**：二进制表没有 `0x3A`，默认依赖 host RGB888；仅在上述硬件项确认后，用 `{0x3A, 1, {0x77}}` 作为临时对照试验，不能先永久加入。

### framebuffer/投屏绿色的后续处理
- `src_fmt=d04` 已确认是 `BGRA8888`，不是非法格式。
- 当前白屏 bring-up 代码在首次 `mtkfb_set_par()`/首帧启动之后才只填第一页；且 KPOC 会切换第二、第三页。因此它不适合诊断实体 DSI 输出，也不能用全白检查色序。
- 如需验证 OVL/RDMA 输出，测试图案必须在 `primary_display_init()` 前写入**全部三页**，使用红/绿/蓝/白彩条；但实体屏全黑时，应先完成背光、电源、reset、DSI PHY/lane 的确认。

### 下一步最小验证清单（只改 kernel；用户手动编译）
1. 在 LCM init_power/init、reset 前后增加 GPIO/电源状态日志；确认实际使用的 GPIO 编号、方向和读回电平。
2. 检查并记录背光驱动的 enable/PWM/brightness 调用链；手动固定一个安全的亮度/enable 值作对照。
3. 在 `primary_display_init()` 打印最终生效的 LCM 参数：宽高、mode、lanes、RGB888/PS、PLL、H/V porch；必要时 dump DSI0/PHY 寄存器。
4. 将实体屏问题与投屏绿色问题分开记录：前者优先 DSI/面板硬件链路，后者再查 Android/HWC buffer 格式、KPOC 翻页和 framebuffer 生命周期。
5. 保持限制：只修改本 kernel tree；仅刷 kernel；不修改 LK、DTB/DTBO、ramdisk、system、vendor；编译和刷机由用户手动执行。
---

### 实施记录（2026-07-30）
- 用户确认：背光由 LK 初始化且工作正常；授权进行 kernel 源码修改。
- 已修改 `drivers/misc/mediatek/lcm/st7701/st7701.c`：在 `0x11` (Sleep Out) 后的 120 ms 延时之后、`0x35`/`0x36`/`0x29` 之前新增 `{0x3A, 1, {0x77}}`。
- 该命令为标准 MIPI DCS COLMOD，明确要求面板按 24-bit RGB888 接收像素流；原二进制表未显式设置，可能依赖不可靠的复位默认值或遗留状态。
- 未修改 480x640、2-lane、SYNC_PULSE、RGB888 host format、PLL=150、porch、M4U、framebuffer、LK/DTB/ramdisk。
- 该修改是受控的面板初始化对照修复；用户手动编译并仅刷 kernel 后，须采集串口和实体屏结果。若实体屏仍全黑，应保留该证据并转向 reset 实际 GPIO/DSI PHY/lane 寄存器检查。

