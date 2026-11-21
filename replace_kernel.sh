#!/usr/bin/env python3

import argparse
import os
import struct
import sys
import zlib
import hashlib
import shutil

KDIR = os.path.dirname(os.path.abspath(__file__))
IMG_DIR = os.path.join(KDIR, "Images")
OUT_DIR = os.path.join(KDIR, "out", "arch", "arm64", "boot")

BOOT_IMG = os.path.join(IMG_DIR, "boot.img")
REC_IMG = os.path.join(IMG_DIR, "recovery.img")

NEW_IMAGE_GZ = os.path.join(OUT_DIR, "Image.gz")
NEW_IMAGE_GZ_DTB = os.path.join(OUT_DIR, "Image.gz-dtb")

MAGIC = b"ANDROID!"
HDR_FIELDS = ("kernel_size", "kernel_addr", "ramdisk_size", "ramdisk_addr",
              "second_size", "second_addr", "tags_addr", "page_size",
              "dt_size", "unused")


def c(txt, color=None):
    if not sys.stdout.isatty():
        return txt
    colors = {"r": "31", "g": "32", "y": "33", "b": "36", "d": "90"}
    return "\033[%sm%s\033[0m" % (colors.get(color, "0"), txt)


def align(n, page):
    return (n + page - 1) // page * page


class BootImg:
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as f:
            self.raw = f.read()
        self._parse()

    def _parse(self):
        d = self.raw
        if d[:8] != MAGIC:
            raise ValueError("%s 不是 Android bootimg (magic=%r)" % (self.path, d[:8]))
        vals = struct.unpack("<10I", d[8:48])
        for k, v in zip(HDR_FIELDS, vals):
            setattr(self, k, v)
        self.name = d[48:64]
        self.cmdline = d[64:576]
        self.id = d[576:596]
        self.extra_cmdline = d[596:612]

        pg = self.page_size
        off = pg
        self.kernel = d[off:off + self.kernel_size]
        off += align(self.kernel_size, pg)
        self.ramdisk = d[off:off + self.ramdisk_size]
        off += align(self.ramdisk_size, pg)
        self.second = d[off:off + self.second_size]
        off += align(self.second_size, pg)
        self.dtb = d[off:off + self.dt_size]
        off += align(self.dt_size, pg)

        self.sections_end = off
        self.tail = d[off:]          # boot: DER 签名块; recovery: 补零到分区大小
        self.total_size = len(d)

    def split_kernel(self):
        return split_gz(self.kernel)

    def build(self, kernel=None, ramdisk=None, second=None, dtb=None,
              tail=None):
        kernel = self.kernel if kernel is None else kernel
        ramdisk = self.ramdisk if ramdisk is None else ramdisk
        second = self.second if second is None else second
        dtb = self.dtb if dtb is None else dtb
        pg = self.page_size
        if tail is None:
            tail = b"" if self.tail.strip(b"\x00") == b"" else self.tail

        hdr = bytearray()
        hdr += MAGIC
        hdr += struct.pack("<10I",
                           len(kernel), self.kernel_addr,
                           len(ramdisk), self.ramdisk_addr,
                           len(second), self.second_addr,
                           self.tags_addr, pg,
                           len(dtb), 0)
        hdr += self.name
        hdr += self.cmdline
        hdr += self.id
        hdr += self.extra_cmdline
        assert len(hdr) <= pg, len(hdr)
        hdr += b"\x00" * (pg - len(hdr))

        def pad(b):
            return b + b"\x00" * (align(len(b), pg) - len(b))

        out = bytearray(hdr)
        out += pad(kernel)
        out += pad(ramdisk)
        out += pad(second)
        out += pad(dtb)
        out += tail

        return bytes(out)


def split_gz(blob):
    if blob[:2] != b"\x1f\x8b":
        return blob, b""
    d = zlib.decompressobj(16 + zlib.MAX_WBITS)
    try:
        d.decompress(blob)
    except zlib.error:
        return blob, b""
    if not d.eof:
        return blob, b""
    used = len(blob) - len(d.unused_data)
    return blob[:used], blob[used:]


DT_MAGIC = b"\xd0\x0d\xfe\xed"


def dtb_summary(dtb):
    info = {}
    try:
        txt = dtb
        info["msdc_okay"] = txt.count(b"okay")
        import re
        m = re.findall(rb"mediatek,lcm_ops-([a-zA-Z0-9_]+)", dtb)
        info["lcm_ops"] = sorted(set(x.decode() for x in m))
    except Exception:
        pass
    return info


def dtb_has_emmc(dtb):
    return dtb.count(b"okay") >= dtb.count(b"disabled")


def main():
    parser = argparse.ArgumentParser(description="重打包 Android boot 和 recovery 镜像")
    parser.add_argument("--dry-run", action="store_true", help="只检查，不写文件")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--recovery-dtb", dest="replace_recovery_dtb", action="store_true",
                      help="替换 Recovery 追加 DTB（默认）")
    mode.add_argument("--keep-recovery-dtb", dest="replace_recovery_dtb", action="store_false",
                      help="保留 Recovery 原追加 DTB")
    parser.set_defaults(replace_recovery_dtb=True)
    parser.add_argument("--recovery-partition-size", type=lambda value: int(value, 0),
                        metavar="BYTES", help="按实测分区容量（字节）检查，不补零")
    parser.add_argument("--out", metavar="DIR", help="输出目录；省略则替换 Images 中的镜像")
    args = parser.parse_args()
    capacity = args.recovery_partition_size
    if capacity is not None and capacity <= 0:
        parser.error("Recovery 分区容量必须大于 0")
    out_dir = args.out

    missing = []
    for p in (BOOT_IMG, REC_IMG, NEW_IMAGE_GZ, NEW_IMAGE_GZ_DTB):
        if not os.path.exists(p):
            missing.append(p)
    if missing:
        print(c("错误: 缺少文件:", "r"))
        for p in missing:
            print("  " + p)
        if NEW_IMAGE_GZ in missing or NEW_IMAGE_GZ_DTB in missing:
            print(c("\n请先编译:  ./build.sh yuxin", "y"))
        if BOOT_IMG in missing or REC_IMG in missing:
            print(c("\n请确认 Images/ 目录里有原厂 boot.img / recovery.img", "y"))
        return 1

    with open(NEW_IMAGE_GZ, "rb") as f:
        new_gz = f.read()
    with open(NEW_IMAGE_GZ_DTB, "rb") as f:
        new_gz_dtb = f.read()

    gz_part, dtb_part = split_gz(new_gz_dtb)
    if gz_part != new_gz:
        print(c("警告: Image.gz-dtb 的 gzip 部分与 Image.gz 不一致，以 Image.gz-dtb 为准", "y"))

    print("=" * 66)
    print(" 新内核: %s" % os.path.relpath(NEW_IMAGE_GZ_DTB, KDIR))
    print("   gzip 部分 : %d 字节  sha1=%s" % (len(gz_part), hashlib.sha1(gz_part).hexdigest()[:16]))
    print("   DTB  部分 : %d 字节  sha1=%s" % (len(dtb_part), hashlib.sha1(dtb_part).hexdigest()[:16]))
    if dtb_part[:4] != DT_MAGIC:
        parser.error("Image.gz-dtb 缺少可识别的追加 DTB (magic=%s)" % dtb_part[:4].hex())
    print("=" * 66)

    boot = BootImg(BOOT_IMG)
    b_gz, b_dtb = boot.split_kernel()
    print("\n[boot.img]")
    print("  当前: kernel=%d (gz %d + dtb %d)  ramdisk=%d  tail=%d  总计=%d"
          % (boot.kernel_size, len(b_gz), len(b_dtb), boot.ramdisk_size,
             len(boot.tail), boot.total_size))
    print("  新DTB 与 旧DTB 是否相同: %s"
          % ("是" if b_dtb == dtb_part else "否"))
    print("  操作: 内核和 DTB 一起替换 (放 LK 加载 dtbo 分区补齐板级配置)")

    new_boot = boot.build(kernel=new_gz_dtb)
    chk = BootImg.__new__(BootImg)
    chk.raw = new_boot
    chk.path = "<new boot>"
    chk._parse()
    print("  新镜像: kernel=%d (gz %d + dtb %d)  ramdisk=%d  总计=%d"
          % (chk.kernel_size, len(split_gz(chk.kernel)[0]),
             len(split_gz(chk.kernel)[1]), chk.ramdisk_size, len(new_boot)))
    assert chk.ramdisk == boot.ramdisk, "boot ramdisk 变化了！"
    assert split_gz(chk.kernel)[1] == dtb_part, "boot DTB 写入校验失败"

    rec = BootImg(REC_IMG)
    r_gz, r_dtb = rec.split_kernel()
    print("\n[recovery.img]")
    print("  当前: kernel=%d (gz %d + dtb %d)  ramdisk=%d  tail=%d  总计=%d"
          % (rec.kernel_size, len(r_gz), len(r_dtb), rec.ramdisk_size,
             len(rec.tail), rec.total_size))
    ri = dtb_summary(r_dtb)
    print("  原DTB: lcm_ops=%s" % (ri.get("lcm_ops") or "无"))

    if args.replace_recovery_dtb:
        print("  操作: 内核和追加 DTB 一起替换")
        expected_dtb = dtb_part
    else:
        print("  操作: --keep-recovery-dtb，保留原追加 DTB")
        expected_dtb = r_dtb

    new_rec = rec.build(kernel=gz_part + expected_dtb)
    if capacity is None:
        print("  未指定 Recovery 分区容量，刷写前请确认实际分区容量")
    else:
        print("  Recovery 分区容量: %d 字节" % capacity)
        if len(new_rec) > capacity:
            parser.error("新 Recovery 镜像 %d 字节超过分区容量 %d 字节"
                         % (len(new_rec), capacity))
    chk2 = BootImg.__new__(BootImg)
    chk2.raw = new_rec
    chk2.path = "<new recovery>"
    chk2._parse()
    n_gz, n_dtb = split_gz(chk2.kernel)
    print("  新镜像: kernel=%d (gz %d + dtb %d)  ramdisk=%d  总计=%d"
          % (chk2.kernel_size, len(n_gz), len(n_dtb), chk2.ramdisk_size,
             len(new_rec)))
    assert chk2.ramdisk == rec.ramdisk, "recovery ramdisk 变化了！"
    assert n_gz == gz_part, "recovery gzip 内核写入校验失败"
    assert n_dtb == expected_dtb, "recovery DTB 写入校验失败"

    if args.dry_run:
        print(c("\n--dry-run: 未写入任何文件", "y"))
        return 0

    if out_dir:
        dst_boot = os.path.join(out_dir, "boot.img")
        dst_rec = os.path.join(out_dir, "recovery.img")
        os.makedirs(out_dir, exist_ok=True)
    else:
        dst_boot, dst_rec = BOOT_IMG, REC_IMG
        for src in (BOOT_IMG, REC_IMG):
            bak = src + ".orig"
            if not os.path.exists(bak):
                shutil.copy2(src, bak)
                print("  已备份 -> %s" % os.path.relpath(bak, KDIR))

    with open(dst_boot, "wb") as f:
        f.write(new_boot)
    with open(dst_rec, "wb") as f:
        f.write(new_rec)

    print("\n" + c("完成:", "g"))
    print("  %s  %d 字节" % (os.path.relpath(dst_boot, KDIR), len(new_boot)))
    print("  %s  %d 字节" % (os.path.relpath(dst_rec, KDIR), len(new_rec)))
    print("\n刷入方式 (fastboot):")
    print("  fastboot flash boot    %s" % os.path.relpath(dst_boot, KDIR))
    print("  fastboot flash recovery %s" % os.path.relpath(dst_rec, KDIR))
    print("  (或直接用 MTK SP Flash Tool 刷 boot / recovery 分区)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
