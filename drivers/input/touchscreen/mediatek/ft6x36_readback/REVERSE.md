# Readback FT6x36 reconstruction (work in progress)

Source: Desktop/yx/test/kernel, SHA256 d0f193f83eef10aa7e7827e3dcec6054b9606245de082eb4066c2467efae66f7.
All offsets below are raw file offsets (IDA imagebase 0), not runtime virtual addresses.

## Recovered framework

init 0xdceaac -> platform setup 0x6c7dd0 -> registration 0x6c89e8(data 0xeaa6f8).
The data table contains absolute pointers with mapping VA = offset + 0xffffffc000080000:

- 0xeaa6f8: name pointer -> 0xd28520 (fts6x36)
- 0xeaa700: local init -> 0x6cbb44
- 0xeaa708: PM callback -> 0x6cccdc (enters sleep/gesture mode; earlier resume label withdrawn)
- 0xeaa710: PM callback -> 0x6cc9cc (reset/exit-gesture path; earlier suspend label withdrawn)
- 0xeaa760: I2C probe candidate -> 0x6cd354
- 0xeaa768: remove candidate -> 0x6cb48c

Callback roles remain inferred until target struct layout and callers are fully checked.
0x6cbb44 allocates 250 bytes DMA memory, obtains supply at string 0xd27fa0,
passes 2800000 twice to function 0x2db814, and registers table at 0xeaa750.
Do not interpret DMA flag 0x2000 as a slave address.

## Data plane

- I2C read helper 0x6cbd7c; write helper 0x6cc940.
- Both preserve low byte of client address field (+2), temporarily OR 0x2000.
- Primary reader/parser 0x6cc0d0: command 0x00, 64-byte read at 0x6cc174.
- count = data[2]&15; records at 3+6*i; 10 unrolled records.
- raw X=((p[0]&15)<<8)|p[1], raw Y=((p[2]&15)<<8)|p[3].
- event=p[0]>>6, id=p[2]>>4. Axis mapping/report policy not recovered yet.
- The 0xd3 function 0x6cbeb0 is separate, not the normal report parser.

ft6x36_protocol.c implements only this verified pure parsing layer, with added
bounds rejection. No inferred hardware address, GPIO, firmware writes or binding.

## Remaining before integration

1. Resolve I2C ID/OF match table and address sourcing (not from unrelated source).
2. Verify reset wrapper 0x6c8098 and pinctrl acquisition.
3. Verify IRQ registration target 0x8ada4, handler 0x6cbaf0 and thread 0x6cc434.
4. Recover input event and axis conversion policy.
5. Complete suspend/resume control flow and failure handling.
6. Adapt recovered behavior to destination MTK APIs, independent directory,
   default-off Kconfig; compile using destination kernel toolchain.
7. Select runtime config only after review; no firmware auto-upgrade initially.

Host check (not a kernel compilation or hardware test):

    cc -std=c89 -Wall -Wextra -Werror -fsanitize=address,undefined ft6x36_protocol.c test_protocol.c -o /tmp/ft-rb-test
    /tmp/ft-rb-test

Destination intended: /home/maozi/android_kernel_mt6735-37_3.18.79/drivers/input/touchscreen/mediatek/ft6x36_readback/
Staged outside kernel tree until platform integration is ready. Existing drivers and display changes untouched.

## Additional recovered platform logic

0x6c8180 obtains pinctrl states by literal name. Slots relative to 0x1118d08:
+0x138 state_eint_as_int; +0x148 state_eint_output0;
+0x140 state_eint_output1; +0x158 state_rst_output0;
+0x150 state_rst_output1. 0x6c8098 selects reset for selector !=1,
interrupt output for selector==1, with argument 2 choosing low/high.
Thus probe calls (0,0), waits 100ms, calls (0,1), waits 200ms.
No GPIO number is encoded in this wrapper; pinctrl resolves it externally.

IRQ handler 0x6cbaf0 sets flag at 0x11195a4 and wakes waitqueue 0xeaa738
via 0x58c80. There is no I2C read in this hard-IRQ routine.

PM callback 0x6cccdc sets paused flag and invokes IRQ routine 0x8a230;
normal branch writes command 0xa5 through 0x6db698. Gesture branch writes
{0xd0,1} and checks readback with bounded retries. Consequently the earlier
resume candidate label was wrong; roles require behavioral tracing.
0x6cc9cc resets low/high with 5ms delays, then invokes 0x8a2e8,
releases BTN_TOUCH and SYN_REPORT, and handles command 0xc1 branches.
PM code is NOT yet implemented; do not blindly transplant partial sequences.

## Target toolchain check

Added ft6x36_transport.[ch]: bounded non-DMA send+receive frame reader,
serialized by mutex, rejecting short/error transfers. PIO is a deliberate
adaptation, not a claim of byte-for-byte original implementation.
Built using destination Linux 3.18 tree, existing out config, ARM64 GCC4.9:

    make O=out ARCH=arm64 CROSS_COMPILE=/home/maozi/android_kernel_mt6735-37_3.18.79/toolchains/gcc4.9-aarch64/bin/aarch64-linux-android- M=/tmp/ft-rb-build modules -j2

build.log confirms both CC objects, LD composite, MODPOST, LD .ko.
This is a compilation harness only: no device registration/module entrypoint,
not a loadable functional touch driver and not hardware-tested.

