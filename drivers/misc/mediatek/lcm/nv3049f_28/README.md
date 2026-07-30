# nv3049f_28 LCM driver reconstruction

This directory contains a standalone MediaTek LCM driver reconstructed from **only** the currently analyzed kernel Image:

```text
C:/Users/Administrator/Desktop/maozi/tool/aik/aik-tmp/split_img/_boot.img-kernel.extracted/0
```

No initialization data, timing values, or callback logic was imported from another panel driver.

## Confirmed binary provenance

| Item | Image VA | Result |
|---|---:|---|
| `LCM_DRIVER` descriptor | `0x40E62A70` | name and six callbacks |
| `get_params` | `0x4041356C` | params writes decoded from instructions |
| `init` | `0x404136DC` | reset sequence and 183-entry command table |
| init table | `0x40E5FB40` | copied byte-for-byte into C table |
| `suspend` | `0x404136BC` | 5-entry command table |
| suspend table | `0x40E5F9F0` | copied byte-for-byte into C table |
| `resume` | `0x40413758` | invokes init |

## Implemented behavior

- DSI, 480x640, 2 data lanes.
- RGB888 and packed 24-bit DSI pixel stream.
- Sync-pulse video mode (`mode == 1`).
- DSI clock: 150 MHz.
- Timing: VSA/VBP/VFP = `14/16/0`; HSA/HBP/HFP = `4/44/46`.
- Reset sequence exactly follows the binary: high 20 ms, low 50 ms, high 100 ms.
- All 183 initialization records and all 5 suspend records are copied exactly.
- Resume re-runs init, exactly as the binary does.

## Target ABI requirements

`nv3049f_28.c` is intended for the MTK 3.18-era `lcm_drv.h` ABI. It uses these standard identifiers:

```c
LCM_TYPE_DSI
SYNC_PULSE_VDO_MODE
LCM_TWO_LANE
LCM_DSI_FORMAT_RGB888
LCM_PACKED_PS_24BIT_RGB888
```

The binary’s `set_util_funcs` copies `0xD8` bytes of `LCM_UTIL_FUNCS`. Confirm that `sizeof(LCM_UTIL_FUNCS)` is `0xD8` in the kernel tree that will compile this source. If it differs, adapt the surrounding ABI/tree rather than blindly changing panel data.

## Integration steps

1. Create your panel folder, typically:

   ```text
   drivers/misc/mediatek/lcm/nv3049f_28/
   ```

2. Copy `nv3049f_28.c` there.
3. Add the folder/object to the LCM Makefile/Kconfig used by your target tree.
4. Register `&nv3049f_28_lcm_drv` in that tree’s LCM driver list.
5. Build, boot, then validate DSI clocks/timing and command traffic on actual hardware.

## Deliberately not fabricated

The original descriptor has only `set_util_funcs`, `get_params`, `init`, `suspend`, and `resume` populated. It has no binary callbacks for compare-id, backlight, ESD, ATA, power, update, or command-mode switching. Those callbacks remain absent here rather than being copied or guessed from a different driver.

The sole adjacent function at `0x40413484` is an ADC-based selector associated with the distinct `hui boe7701_28` string. It is not wired into the `nv3049f_28` descriptor and is intentionally excluded.
