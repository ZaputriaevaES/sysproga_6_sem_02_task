# sysproga_6_sem_02_task
## Задание: 
  1. Написать и продемонстрировать код отрисовки дерева таблиц валидных страниц для процесса, указанного в аргументе.

## Как работает программа:
- Парсит файлы /proc/[pid]/maps и /proc/[pid]/pagemap
- Строит дерево трансляции виртуальных адресов в физические в 5-уровневой системе (PGD → P4D → PUD → PMD → PTE) Sv57
- Печатает его в виде иерархии

## Пример вывода программы: 
```
  Building translation tree for PID: 1
  Page table hierarchy:
  PGD ── 0x0 ─┐
  PGD ── 0x0 ── 0x0 ─┐
  PGD ── 0x0 ── 0x0 ── 0x0 ─┐
  PGD ── 0x0 ── 0x0 ── 0x0 ── 0x0 ── PTE: PFN=0x80e0f
  PGD ── 0x0 ── 0xff ─┐
  PGD ── 0x0 ── 0xff ── 0x1fe ─┐
  PGD ── 0x0 ── 0xff ── 0x1fe ── 0xe7 ── PTE: PFN=0x81605
  PGD ── 0x0 ── 0xff ── 0x1ff ─┐
  PGD ── 0x0 ── 0xff ── 0x1ff ── 0x16a ── PTE: PFN=0x80e17
```

## Видео с запуском и работой программы:
![riscv_visualize_pt](https://github.com/user-attachments/assets/1a40f9cc-20ba-4b1b-8112-206d7ca78d55)

## Инструкция по установке зависимостей сборке и запуску:
  - компилятор для RISC-V, QEMU и др.:
```
	sudo apt update
	sudo apt install -y libncurses5-dev libncursesw5-dev build-essential \
        git bc bison flex libssl-dev libelf-dev qemu-system-misc gcc-riscv64-linux-gnu \
        gdb-multiarch binutils-riscv64-linux-gnu cpio busybox-static
```
  - Настройка и сборка ядра linux:
```
	[~/sysproga_6_sem_01_task/linux] make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- defconfig
	[~/sysproga_6_sem_01_task/linux] echo "CONFIG_BLK_DEV_INITRD=y" >> .config
	[~/sysproga_6_sem_01_task/linux] make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- -j$(nproc)
```
  - Настройка и сборка opensbi:
```
	[~/sysproga_6_sem_01_task/opensbi] make PLATFORM=generic CROSS_COMPILE=riscv64-linux-gnu- clean 
	[~/sysproga_6_sem_01_task/opensbi] make PLATFORM=generic CROSS_COMPILE=riscv64-linux-gnu- -j$(nproc)
```
  - Сборка BusyBox для initramfs:
```
	[~/sysproga_6_sem_01_task/busybox-1.36.1] make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- defconfig
	[~/sysproga_6_sem_01_task/busybox-1.36.1] make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- menuconfig
	[~/sysproga_6_sem_01_task/busybox-1.36.1] (Settings → Build static binary (no shared libs) → [X])
	[~/sysproga_6_sem_01_task/busybox-1.36.1] make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- -j$(nproc)
	[~/sysproga_6_sem_01_task/busybox-1.36.1] make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- install
```
  - Компиляция теста:
```
	[~/sysproga_6_sem_01_task] riscv64-linux-gnu-gcc -static riscv_visualize_pt.c -o riscv_visualize_pt  
	[~/sysproga_6_sem_01_task] sudo cp riscv_visualize_pt busybox-1.36.1/_install/riscv_visualize_pt
```
  - Создание initramfs:
```
	[~/sysproga_6_sem_01_task/busybox-1.36.1/_install] find . | cpio -o -H newc > ../rootfs.cpio
```
  - Запуск QEMU:
```
	[~/sysproga_6_sem_01_task]     
        qemu-system-riscv64 \
        -M virt \
        -cpu rv64 \
        -nographic \
        -bios opensbi/build/platform/generic/firmware/fw_dynamic.bin \
        -kernel linux/arch/riscv/boot/Image \
        -initrd busybox-1.36.1/rootfs.cpio \
        -append "root=/dev/ram rdinit=/init console=ttyS0"
```
  - Запуск теста внутри QEMU:
```
	./riscv_visualize_pt 1
```
