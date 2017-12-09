#
# if you want the ram-disk device, define this to be the
# size in blocks.
#
RAMDISK = #-DRAMDISK=512

# as86 和 ld86 是由 Bruce Evans 编写的 Intel 8086 汇编编译程序和连接程序。
#它完全是一个 8086 的汇编编译器，但却可以为 386 处理器编制 32 位的代码。
#如未完装, 则 yum install dev86*
AS86	=as86 -0 -a
LD86	=ld86 -0

#确认 Gnu assembler 是否已经安装
# which as
#gas 安装 yum install binutils* 或 下载binutils-2.20.tar.gz 手动安装 下载地址http://ftp.gnu.org/gnu/binutils/

#gas gld 是很古老的名字，现在不用了.
#AS	=gas
#LD	=gld

AS	=as --32
LD	=ld  
#LD 参数不要加 -s  保留符号

LDFLAGS =-m elf_i386 -M --print-map -Ttext 0 -e startup_32
#LDFLAGS =-m elf_i386 -M --print-map -Ttext 0 
#LDFLAGS	=-s -x -M

CC	=gcc $(RAMDISK)

#CFLAGS	=-m32 -Wall -O -g -fleading-underscore -fstrength-reduce -fomit-frame-pointer 
#CFLAGS	=-m32 -Wall -O -fstrength-reduce -fomit-frame-pointer 
#CFLAGS	=-m32 -Wall -O -g
CFLAGS	=-m32 -Wall -O -g -fno-builtin

#-nostdinc 不搜索默认路径头文件

#-fcombine-regs -mstring-insns #不再使用?
#-fleading-underscore 导出字符要在前面加下横线 如  main() 导出是 _main() 而不是 main(), 古老的做法

#CPP	=cpp -nostdinc -Iinclude
CPP	=cpp -Iinclude

#
# ROOT_DEV specifies the default root-device when making the image.
# This can be either FLOPPY, /dev/xxxx or empty, in which case the
# default of /dev/hd6 is used by 'build'.
#
#ROOT_DEV=/dev/hd6
#ROOT_DEV=/dev/sda5
ROOT_DEV="FLOPPY"

ARCHIVES=kernel/kernel.o mm/mm.o fs/fs.o
DRIVERS =kernel/blk_drv/blk_drv.a kernel/chr_drv/chr_drv.a
MATH	=kernel/math/math.a
LIBS	=lib/lib.a



#-nostdinc  不在标准系统目录中搜索头文件，只在-I指定的目录中搜索
#-nostdinc++ 不在C++标准系统目录中搜索头文件，但在其他标准目录仍然搜索
#这个规则表示所有的 .s文件都是依赖与相应的.c文件的。
.c.s:
	$(CC) $(CFLAGS) \
	-Iinclude -S -o $*.s $<
	#-nostdinc -Iinclude -S -o $*.s $<
#这个规则表示所有的 .o文件都是依赖与相应的.s文件的。
.s.o:
	#$(AS) -c -o $*.o $< #  -c 不使用了
	$(AS)  -o $*.o $<
#这个规则表示所有的 .o文件都是依赖与相应的.c文件的。
.c.o:
	$(CC) $(CFLAGS) \
	-Iinclude -c -o $*.o $<
	#-nostdinc -Iinclude -c -o $*.o $<

all:	Image

#Image: boot/bootsect boot/setup tools/system tools/build
#	tools/build boot/bootsect boot/setup tools/system $(ROOT_DEV) > Image
#	sync

Image: boot/bootsect boot/setup tools/system tools/build
	objcopy -O binary -R .note -R .comment tools/system 
	tools/build boot/bootsect boot/setup tools/system $(ROOT_DEV) 
	rm tools/kernel -f
	 sync
#objcopy命令能复制和转化目标文件 
#objcopy -O binary -R .note -R .comment tools/system tools/kernel
#-O binary tools/system tools/kernel将 tools/system 生成二进制文件 tools/kernel
#-R .note -R .comment 删除 .note段 和 .comment 段


disk: Image
	dd bs=8192 if=Image of=/dev/PS0

tools/build: tools/build.c
	$(CC) $(CFLAGS) \
	-o tools/build tools/build.c

boot/head.o: boot/head.s

tools/system:	boot/head.o init/main.o \
		$(ARCHIVES) $(DRIVERS) $(MATH) $(LIBS)
	$(LD) $(LDFLAGS) boot/head.o init/main.o \
	$(ARCHIVES) \
	$(DRIVERS) \
	$(MATH) \
	$(LIBS) \
	-o tools/system > System.map

kernel/math/math.a:
	(cd kernel/math; make)

kernel/blk_drv/blk_drv.a:
	(cd kernel/blk_drv; make)

kernel/chr_drv/chr_drv.a:
	(cd kernel/chr_drv; make)

kernel/kernel.o:
	(cd kernel; make)

mm/mm.o:
	(cd mm; make)

fs/fs.o:
	(cd fs; make)

lib/lib.a:
	(cd lib; make)

boot/setup: boot/setup.s
	$(AS86) -o boot/setup.o boot/setup.s
	$(LD86) -s -o boot/setup boot/setup.o

boot/bootsect:	boot/bootsect.s
	$(AS86) -o boot/bootsect.o boot/bootsect.s
	$(LD86) -s -o boot/bootsect boot/bootsect.o

tmp.s:	boot/bootsect.s tools/system
	(echo -n "SYSSIZE = (";ls -l tools/system | grep system \
		| cut -c25-31 | tr '\012' ' '; echo "+ 15 ) / 16") > tmp.s
	cat boot/bootsect.s >> tmp.s

clean:
	rm -f Image.img System.map tmp_make core boot/bootsect boot/setup
	rm -f init/*.o tools/system tools/build boot/*.o
	(cd mm;make clean)
	(cd fs;make clean)
	(cd kernel;make clean)
	(cd lib;make clean)

backup: clean
	(cd .. ; tar cf - linux | compress - > backup.Z)
	sync

dep:
	sed '/\#\#\# Dependencies/q' < Makefile > tmp_make
	(for i in init/*.c;do echo -n "init/";$(CPP) -M $$i;done) >> tmp_make
	cp tmp_make Makefile
	(cd fs; make dep)
	(cd kernel; make dep)
	(cd mm; make dep)

### Dependencies:
init/main.o : init/main.c include/unistd.h include/sys/stat.h \
  include/sys/types.h include/sys/times.h include/sys/utsname.h \
  include/utime.h include/time.h include/linux/tty.h include/termios.h \
  include/linux/sched.h include/linux/head.h include/linux/fs.h \
  include/linux/mm.h include/signal.h include/asm/system.h include/asm/io.h \
  include/stddef.h include/stdarg.h include/fcntl.h 
