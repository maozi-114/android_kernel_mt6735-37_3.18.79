# ST7796S SPI diagnostic port (2026-11-15)

## Why the previous port left the LK logo on screen

PuTTY capture shows recovery PID196 running and USB/battery activity past 55s.
The screen remaining on the logo is not evidence of a kernel boot hang.
LK explicitly calls spi_show_lk_center. Original kernel init sends through
send_cmd/send_data, and resume calls SPI routines 0x6634c8/0x66353c.
The previous DSI V2 fallback did not reconstruct the panel's SPI transport.

## Current implementation

- Independent folder and LCM registration, no NV3049F_28 modifications.
- Commands/reset now use GPIO software SPI in the Linux build, not DSI V2.
- CS65, CLK66, DC67, MOSI68, RESET146 are evidenced by original pinctrl names.
- MSB-first, data setup then clock low/high follows original software writer
  0x662ca4. GPIO functions are the target MTK API, no original MMIO addresses.
- A delayed worker polls registered framebuffer 0, reads its actual bpp,
  RGB bitfields, stride and x/y offsets, converts to RGB565 big-endian,
  sets 2A/2B window 240x320 and writes 2C plus frame pixels.
- Worker is gated by LCM set_util_funcs selection, supports LK-init handover,
  serialized with init/suspend/resume, and reports first frame/checksum/time.
- Backlight GPIO80 is evidenced in original pinctrl names but not forcibly
  controlled: the current PWM/backlight path is retained.

## Scope and limitations

This is a SOFTWARE SPI diagnostic implementation, not the completed original
hardware SPI/DMA/WDMA display pipeline. It can be slow. It reads fb0 CPU memory;
this is suitable to diagnose recovery framebuffer output, but hardware overlay
composition may not appear in fb0 in a normal Android session. No claim of
hardware validation or exact original behavior is made.

No guessed controller binding or DT node is added. GPIO mode0/output is used
for the software transport. Actual pin mapping/voltage must match the board.
Current worker refreshes roughly every 250ms PLUS transfer time. Frames are
not skipped, so unchanged framebuffer addresses do not suppress pixel updates.

Expected serial logs (grep st7796s-spi):
- LCM selected
- GPIO transport CS65 CLK66 DC67 MOSI68 RST146
- panel init complete, refresh enabled
- first frame sent fb... bpp... stride... offset...
- frames=... checksum=... transfer_ms=...
Missing selection indicates config/name/probe issue; setup error indicates
GPIO configuration failure. Changing checksum proves changing source pixels,
not successful electrical transmission. If checksum changes but screen does
not, investigate pinmux, clock/DC/CS, addressing and transfer on hardware.

## Binary provenance

Input C:/Users/123/Desktop/yx/test/kernel
SHA256 d0f193f83eef10aa7e7827e3dcec6054b9606245de082eb4066c2467efae66f7
Addresses are file offsets (VA = offset + 0xffffffc000080000):
descriptor e7fc40; set_util 3f7478; params 3f73c8; init 3f6f58;
suspend 3f7354; resume 3f72f8; compare_id 3f6f48 returns1.
Command table is copied from instructions. DSI params remain the original
framework's video configuration, but panel commands/pixels are now SPI.
Original ABI writes 5c/2bc/364/368 and descriptor slot+a0 remain unconfirmed.

## Build

Independent Makefile builds st7796s_hvga_dsi_cmd.o and st7796s_spi.o.
Only ST7796S_HVGA_DSI_CMD controls registration. Ensure the configuration used
by your actual build includes st7796s_hvga_dsi_cmd. Compile success is not proof
that the running image contains/selects this implementation.
