# yuxin 改动记录

路径简称：`video/` = `drivers/misc/mediatek/video/`；`lcm/` = `drivers/misc/mediatek/lcm/st7796s_hvga_dsi_cmd/`；`ft/` = `drivers/input/touchscreen/mediatek/ft6x36_readback/`。DTS 指 `arch/arm64/boot/dts/k37mv1_64_yuxin.dts`，defconfig 指 `arch/arm64/configs/yuxin_defconfig`。

| 版本 | 改动文件 | 结果 |
|---|---|---|
| #17 | `video/mt6735/primary_display.c` | 补多遍合成的最后一帧；投屏时仍漏刷。 |
| #18 | `video/mt6735/primary_display.c`、`video/common/mtkfb_fence.c`、`video/common/mtkfb_fence.h`、`lcm/st7796s_spi.c`、`lcm/st7796s_spi.h` | 补投屏模式的 SPI 刷新，实机正常。 |
| #19 | DTS、`ft/ft6x36_driver.c` | 接入 FT6x36，补中断和坐标配置；`0x46` 无应答。 |
| #20 | defconfig、`ft/ft6x36_driver.c` | 保留早期日志，定位到 I²C 无应答。 |
| #21 | DTS | 改用 `0x38`，读到芯片 ID；64 字节报告仍读不出。 |
| #22 | `ft/ft6x36_driver.c`、`ft/ft6x36_transport.c`、`ft/ft6x36_transport.h` | 用 DMA 读触摸报告；触摸有反应，坐标偏小。 |
| #23 | DTS | 触摸范围改为 240×320，实机确认对齐。 |
| #24（撤回） | DTS | 改 CONSYS 时钟缓冲控制方式无效，已刷回 #23。 |
| #25 | `lcm/st7796s_spi.c`、`drivers/spi/mediatek/mt6735/spi.c` | 按变化行做 DMA，合成帧到达即调度，减少 SPI 日志。设置滚动中相邻刷帧间隔中位数 58.6 → 44.7 ms，未超频。 |
| #26 | `lcm/st7796s_spi.c` | 加分段计时；滚动时转换约 39 ms，SPI worker 等约 0.4 ms。 |
| #27 | `lcm/st7796s_spi.c` | 逐行复制后再转换；转换 38.9 → 3.5 ms，滚动帧间隔 44.3 → 21.8 ms。已刷入，未超频。 |
| #28 | DTS | 恢复原厂矩阵键表；`mtk-kpd` 注册新增键码。补原厂 modem 和 DSP 后，MD1 握手通过，但 SIM 初始化断言。 |
| #29 | DTS | 恢复 SIM1 低电平触发，移除多余 SIM2 热插拔节点。MD1 保持 ready，Android 读到基带版本。 |
| #30 | `ft/ft6x36_driver.c` | 加可关的触摸诊断，抓到屏外坐标 `(120,400)`。 |
| #31 | DTS、`ft/ft6x36_driver.c` | 将 `(120,400)` 映射为 `KEY_BACK`；重启后 `mtk-tpd` 已注册键码，待实按。 |
| #32 | `lcm/st7796s_spi.c` | BakaSU 花屏对照：默认改回整屏 DMA，保留像素转换加速；已刷入，实体屏待确认。 |
| #33 | `lcm/st7796s_spi.c` | 加只读 SPI 整帧抓取口；BakaSU 发送缓冲与 Android 截图约 99.8% 像素一致，待同一花屏现场确认。 |
| #34 | `lcm/st7796s_spi.c` | SPI 5/5，整屏约 118 ms；用户确认不花屏，但太慢。 |
| #35 | `lcm/st7796s_spi.c` | SPI 2/2，保留较长 CS 时序，恢复按变化行刷新；已写入 boot，未重启。 |

#28 在 `/vendor/firmware/` 新增从原厂 system 镜像提取的 `modem_1_lwg_n.img` 和 `dsp_1_lwg_n.bin`。未改原厂镜像或其他设备固件。

## 无线（沿用 #23）

- 模块源码：`common/common_detect/Makefile`、`common/common_main/mt6735/mtk_wcn_consys_hw.c`、`common/common_main/mt6735/wmt_plat_alps.c`。补编译宏、修正供电单位，加载时指定 `co_clock_override=0`。提前解除复位的实验无效，已撤回。
- 仓库文件：`device-config/yuxin-connectivity/` 下的 `module-adaptation.patch`、`build-modules.sh`、`init.yuxin-connectivity.rc`。重编模块和日志在 `out/yuxin-connectivity-reference/`。
- 设备文件：九个 `.ko` 放在 `/vendor/lib/modules/yuxin-connectivity/`，由 `/vendor/etc/init/init.yuxin-connectivity.rc` 在 `post-fs-data` 加载。旧模块和 Recovery 没动。回退时把该 rc 移出 `/vendor/etc/init/`，不要只改后缀。
- 实测：重启后 Wi-Fi 可扫描、蓝牙为 ON；未测联网或配对。GPS HAL 能启动，未验证定位；FM 应用能打开，未插耳机测收台。

## 设置文案

- `out/yuxin-label/DuraSpeed-unsigned.apk`：将资源里的 19 处「快霸」改为「机霸」，未签名，未替换设备原包。
- 已重启至 #35，Android 启动完成。

- 未签名部署：替换设备 `/system/priv-app/DuraSpeed/DuraSpeed.apk` 后重启，PackageManager 因缺少证书拒绝加载；已恢复原 APK，未改签名校验。

- 文案覆盖：新增 `device-config/yuxin-duraspeed-overlay/`（Manifest 和三份中文 strings）。未签名 RRO 实机因缺证书被拒，已移除；原 APK 未改。

## Recovery 重打包

- `replace_kernel.sh`：Recovery 默认换内核和追加 DTB，保留原 ramdisk；不再把旧镜像长度当分区容量。实机确认分区为 16 MiB，新包 11,739,136 字节，产物在 `out/yuxin-recovery-dtb/`。只打包，未刷入。

- #36：`lcm/st7796s_spi.c` 跳过变化区域之间干净的 32 行带，相邻带合并传输；SPI 2/2 和片选时序不变。只刷 boot，回读一致，已启动。设置页滚动 A/B 未证明提帧；实体屏观感待确认。产物和日志在 `out/yuxin-spi-bands/`。
