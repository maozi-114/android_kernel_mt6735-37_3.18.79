# Integration status

Normal-touch reconstruction, default OFF. No defconfig or DTS modifications.
Built with target ARM64 GCC4.9, existing out configuration, command-line
CONFIG_TOUCHSCREEN_FT6X36_READBACK=m for compilation checking only.
Production Kconfig is bool because MTK singleton callback lifetime is built-in.

Implemented: MTK registration, explicit I2C match, 2.8V regulator, reset,
A3 communication check, IRQ thread, 64-byte packet read, type-A input,
remove/error unwind, conservative reset-held suspend and reset resume.

Adaptations, NOT original binary equivalence:
- PIO instead of MTK DMA; board-provided address unchanged.
- Threaded IRQ instead of original dedicated kthread.
- Suspend holds reset rather than partial A5/D0 gesture sequences.
- No factory keys, firmware updater, debug/proc/sysfs hooks.
- Does not reproduce original X-limit exception (X=400).
- New reconstructed,ft6x36-readback compatible is explicit opt-in, not evidence
  that the original device used that compatible.

No physical IC identity, successful ACK on this board, runtime PM behavior,
or working touch is established by compilation. Do not enable simultaneously
with another touchscreen driver. Existing display changes are untouched.
