ls -la ./out/arch/arm64/boot/Image.gz
cp ./out/arch/arm64/boot/Image.gz /mnt/c/Users/Administrator/Desktop/maozi/tool/aik/aik-pa03_boot/magiskboot/kernel
cd /mnt/c/Users/Administrator/Desktop/maozi/tool/aik/aik-pa03_boot/magiskboot && magiskboot repack -n boot.img st7701.img
ls -la st7701.img
