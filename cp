ls -la ./out/arch/arm64/boot/Image.gz
cp ./out/arch/arm64/boot/Image.gz /mnt/c/Users/123/Desktop/yx/port/kernel
cd /mnt/c/Users/123/Desktop/yx/port && magiskboot repack -n twrp_st7701s.img yx.img
ls -la yx.img
