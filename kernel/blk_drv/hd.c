/*
 *  linux/kernel/hd.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * This is the low-level hd interrupt support. It traverses the
 * request-list, using interrupts to jump between functions. As
 * all the functions are called within interrupts, we may not
 * sleep. Special care is recommended.
 * 
 *  modified by Drew Eckhardt to check nr of hd's from the CMOS.
 */

/*
结构 : //boot sector//super block//i-node map//sector map//inode_array//root 数据//
    superblock --- 通常也叫超级块，关于文件系统的Metadata我们统统记在这里。
    sector map --- 是一个位图，它用来映射扇区的使用情况，用 1 表示扇区已被使用，0表示未使用。
    i-node --- 是UNIX世界各种文件系统的核心数据结构之一，我们把它借用过来。每个 i-node对应一个文件，用于存放文件名、文件属性等内容，
    inode_array --- 就是把所有 i-node都放在这里，形成一个较大的数组。
    inode map --- 就是用来映射inode_array这个数组使用情况的一个位图，用法跟sector map类似。
    root --- 数据区类似于FAT12的根目录区，但本质上它也是个普通文件，由于它是所有文件的索引，所以我们把它单独看待。
    [[文件夹先不管]]

   
VFS文件系统介绍
        在Linux文件系统中，为了提高文件访问的效率，都使用了 Cache 机制来提高速度。所谓 Cache 机制就是指文件系统在读写磁盘上的数据之前，
   都要经过 Cache 系统的缓冲。如果数据在 Cache 里有的话，就直接读取，如果没有，才实际从驱动器读写。
   第一个块是引导块(Boot block)，系统引导的时候使用。第二个块是超级块(Super block)，记录了文件系统重要的信息，接下来的磁盘块存放 inode 和文件数据。
        在 VFS 里，每一个文件系统是由其超级块来表示的，这是因为超级块存放了一个文件系统的最重要的信息，通过超级块可以了解这个文件系统的基本构成。
   从一个文件系统的超级块出发，就可以访问文件系统中任何一个文件。因此，在Linux中文件系统的管理以超级块为单位，从超级块可以取得这个文件系统
   中任何一个文件的 inode，从文件的 inode 则可以对这个文件进行读写访问。
4.块设备
   块设备（blockdevice）
        --- 是一种具有一定结构的随机存取设备，对这种设备的读写是按块进行的，他使用缓冲区来存放暂时的数据，待条件成熟后，
            从缓存一次性写入设备或者从设备一次性读到缓冲区。
   字符设备（Character device）
        --- 是一个顺序的数据流设备，对这种设备的读写是按字符进行的，而且这些字符是连续地形成一个数据流。他不具备缓冲区，
            所以对这种设备的读写是实时的。

   扇区(Sectors)：任何块设备硬件对数据处理的基本单位。通常，1个扇区的大小为 512byte。（对设备而言）
   块 (Blocks)：由Linux制定对内核或文件系统等数据处理的基本单位。通常，1个块由1个或多个扇区组成。（对Linux操作系统而言）
   段(Segments)：由若干个相邻的块组成。是 Linux 内存管理机制中一个内存页或者内存页的一部分。

 
 
    文件的操作通过文件描述符属性判断文件的类型依次采用不同的方法，如块设备方法，字符设备方法，管道方法,正规文件等。
关于文件系统加载和卸载的函数有 sys_umount, sys_mount 和 mount_root 。在超级块中有字段 sb->s_isup 和 sb->s_imount,分别表示该
超级块中被安装的 i 节点和安装到的 i 节点。显然对于根文件来说，二者是一样的，都是第一个根节点。i 节点中有字段 inode->i_mount,
表示该节点被安装了文件系统。所以文件系统的安装与卸载就是读取超级块，设置标志位以及释放超级块，清除标志位的过 程。
对于安装根文件系统来说还要设置进程的根目录为根节点。有了超级块和位图，就可以访问文件系统了。
在 linux0.11中只支持 minix1.0，关于文件的操作遍布于内核之中。在采用了 vfs 虚拟文件系统之后，各种文件系统可以
通过这个统一的层被完美的支持。


在 linux0.11 中, 被加截的文件系统超级块存在 super_block[]中。该表有 8 项，因此最多加载 8 个文件系统。超级块在 mount_root() 中被初始化，在 read_super() 中会为新加
载的文件系统在表中设置一个超级块，并在 put_super() 中释放超级块表中指定的超级块项.

 * */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/hdreg.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>

#define MAJOR_NR 3
#include "blk.h"

void end_request(int uptodate)
{
	DEVICE_OFF(CURRENT->dev);
	if (CURRENT->bh) {
		CURRENT->bh->b_uptodate = uptodate;
		unlock_buffer(CURRENT->bh);
	}
	if (!uptodate) {
		printk(DEVICE_NAME " I/O error\n\r");
		printk("dev %04x, block %d\n\r",CURRENT->dev,
			CURRENT->bh->b_blocknr);
	}
	wake_up(&CURRENT->waiting);
	wake_up(&wait_for_request);
	CURRENT->dev = -1;
	CURRENT = CURRENT->next;
}
#define CMOS_READ(addr) ({ \
outb_p(0x80|addr,0x70); \
inb_p(0x71); \
})

/* Max read/write errors/sector */
#define MAX_ERRORS	7
#define MAX_HD		2

static void recal_intr(void);

static int recalibrate = 1;
static int reset = 1;

/*
 *  This struct defines the HD's and their types.
 */
struct hd_i_struct {
	int head,sect,cyl,wpcom,lzone,ctl;
	};
#ifdef HD_TYPE
struct hd_i_struct hd_info[] = { HD_TYPE };
#define NR_HD ((sizeof (hd_info))/(sizeof (struct hd_i_struct)))
#else
struct hd_i_struct hd_info[] = { {0,0,0,0,0,0},{0,0,0,0,0,0} };
static int NR_HD = 0;
#endif

static struct hd_struct {
	long start_sect;
	long nr_sects;
} hd[5*MAX_HD]={{0,0},};

#define port_read(port,buf,nr) \
__asm__("cld;rep;insw"::"d" (port),"D" (buf),"c" (nr))

#define port_write(port,buf,nr) \
__asm__("cld;rep;outsw"::"d" (port),"S" (buf),"c" (nr))

extern void hd_interrupt(void);
extern void rd_load(void);

/* This may be used only once, enforced by 'static int callable' */
int sys_setup(void * BIOS)
{
	static int callable = 1;
	int i,drive;
	unsigned char cmos_disks;
	struct partition *p;
	struct buffer_head * bh;

	if (!callable)
		return -1;
	callable = 0;
#ifndef HD_TYPE
	for (drive=0 ; drive<2 ; drive++) {
		hd_info[drive].cyl = *(unsigned short *) BIOS;
		hd_info[drive].head = *(unsigned char *) (2+BIOS);
		hd_info[drive].wpcom = *(unsigned short *) (5+BIOS);
		hd_info[drive].ctl = *(unsigned char *) (8+BIOS);
		hd_info[drive].lzone = *(unsigned short *) (12+BIOS);
		hd_info[drive].sect = *(unsigned char *) (14+BIOS);
		BIOS += 16;
	}
	if (hd_info[1].cyl)
		NR_HD=2;
	else
		NR_HD=1;
#endif
	for (i=0 ; i<NR_HD ; i++) {
		hd[i*5].start_sect = 0;
		hd[i*5].nr_sects = hd_info[i].head*
				hd_info[i].sect*hd_info[i].cyl;
	}

	/*
		We querry CMOS about hard disks : it could be that 
		we have a SCSI/ESDI/etc controller that is BIOS
		compatable with ST-506, and thus showing up in our
		BIOS table, but not register compatable, and therefore
		not present in CMOS.

		Furthurmore, we will assume that our ST-506 drives
		<if any> are the primary drives in the system, and 
		the ones reflected as drive 1 or 2.

		The first drive is stored in the high nibble of CMOS
		byte 0x12, the second in the low nibble.  This will be
		either a 4 bit drive type or 0xf indicating use byte 0x19 
		for an 8 bit type, drive 1, 0x1a for drive 2 in CMOS.

		Needless to say, a non-zero value means we have 
		an AT controller hard disk for that drive.

		
	*/

	if ((cmos_disks = CMOS_READ(0x12)) & 0xf0)
		if (cmos_disks & 0x0f)
			NR_HD = 2;
		else
			NR_HD = 1;
	else
		NR_HD = 0;
	for (i = NR_HD ; i < 2 ; i++) {
		hd[i*5].start_sect = 0;
		hd[i*5].nr_sects = 0;
	}
	for (drive=0 ; drive<NR_HD ; drive++) {
		if (!(bh = bread(0x300 + drive*5,0))) {
			printk("Unable to read partition table of drive %d\n\r",
				drive);
			panic("");
		}
		if (bh->b_data[510] != 0x55 || (unsigned char)
		    bh->b_data[511] != 0xAA) {
			printk("Bad partition table on drive %d\n\r",drive);
			panic("");
		}
		p = 0x1BE + (void *)bh->b_data;
		for (i=1;i<5;i++,p++) {
			hd[i+5*drive].start_sect = p->start_sect;
			hd[i+5*drive].nr_sects = p->nr_sects;
		}
		brelse(bh);
	}
	if (NR_HD)
		printk("Partition table%s ok.\n\r",(NR_HD>1)?"s":"");
	rd_load();//ram disk
	mount_root();
	return (0);
}

static int controller_ready(void)
{
	int retries=10000;

	while (--retries && (inb_p(HD_STATUS)&0xc0)!=0x40);
	return (retries);
}

static int win_result(void)
{
	int i=inb_p(HD_STATUS);

	if ((i & (BUSY_STAT | READY_STAT | WRERR_STAT | SEEK_STAT | ERR_STAT))
		== (READY_STAT | SEEK_STAT))
		return(0); /* ok */
	if (i&1) i=inb(HD_ERROR);
	return (1);
}

static void hd_out(unsigned int drive,unsigned int nsect,unsigned int sect,
		unsigned int head,unsigned int cyl,unsigned int cmd,
		void (*intr_addr)(void))
{
	register int port asm("dx");

	if (drive>1 || head>15)
		panic("Trying to write bad sector");
	if (!controller_ready())
		panic("HD controller not ready");
	do_hd = intr_addr;
	outb_p(hd_info[drive].ctl,HD_CMD);
	port=HD_DATA;
	outb_p(hd_info[drive].wpcom>>2,++port);
	outb_p(nsect,++port);
	outb_p(sect,++port);
	outb_p(cyl,++port);
	outb_p(cyl>>8,++port);
	outb_p(0xA0|(drive<<4)|head,++port);
	outb(cmd,++port);
}

static int drive_busy(void)
{
	unsigned int i;

	for (i = 0; i < 10000; i++)
		if (READY_STAT == (inb_p(HD_STATUS) & (BUSY_STAT|READY_STAT)))
			break;
	i = inb(HD_STATUS);
	i &= BUSY_STAT | READY_STAT | SEEK_STAT;
	if (i == ( READY_STAT | SEEK_STAT) )
		return(0);
	printk("HD controller times out\n\r");
	return(1);
}

static void reset_controller(void)
{
	int	i;

	outb(4,HD_CMD);
	for(i = 0; i < 100; i++) nop();
	outb(hd_info[0].ctl & 0x0f ,HD_CMD);
	if (drive_busy())
		printk("HD-controller still busy\n\r");
	if ((i = inb(HD_ERROR)) != 1)
		printk("HD-controller reset failed: %02x\n\r",i);
}

static void reset_hd(int nr)
{
	reset_controller();
	hd_out(nr,hd_info[nr].sect,hd_info[nr].sect,hd_info[nr].head-1,
		hd_info[nr].cyl,WIN_SPECIFY,&recal_intr);
}

void unexpected_hd_interrupt(void)
{
	printk("Unexpected HD interrupt\n\r");
}

static void bad_rw_intr(void)
{
	if (++CURRENT->errors >= MAX_ERRORS)
		end_request(0);
	if (CURRENT->errors > MAX_ERRORS/2)
		reset = 1;
}

static void read_intr(void)
{
	if (win_result()) {
		bad_rw_intr();
		do_hd_request();
		return;
	}
	port_read(HD_DATA,CURRENT->buffer,256);
	CURRENT->errors = 0;
	CURRENT->buffer += 512;
	CURRENT->sector++;
	if (--CURRENT->nr_sectors) {
		do_hd = &read_intr;
		return;
	}
	end_request(1);
	do_hd_request();
}

static void write_intr(void)
{
	if (win_result()) {
		bad_rw_intr();
		do_hd_request();
		return;
	}
	if (--CURRENT->nr_sectors) {
		CURRENT->sector++;
		CURRENT->buffer += 512;
		do_hd = &write_intr;
		port_write(HD_DATA,CURRENT->buffer,256);
		return;
	}
	end_request(1);
	do_hd_request();
}

static void recal_intr(void)
{
	if (win_result())
		bad_rw_intr();
	do_hd_request();
}

void do_hd_request(void)
{
	int i,r;
	unsigned int block,dev;
	unsigned int sec,head,cyl;
	unsigned int nsect;

	INIT_REQUEST;
	dev = MINOR(CURRENT->dev);
	block = CURRENT->sector;
	if (dev >= 5*NR_HD || block+2 > hd[dev].nr_sects) {
		end_request(0);
		goto repeat;
	}
	block += hd[dev].start_sect;
	dev /= 5;
	__asm__("divl %4":"=a" (block),"=d" (sec):"0" (block),"1" (0),
		"r" (hd_info[dev].sect));
	__asm__("divl %4":"=a" (cyl),"=d" (head):"0" (block),"1" (0),
		"r" (hd_info[dev].head));
	sec++;
	nsect = CURRENT->nr_sectors;
	if (reset) {
		reset = 0;
		recalibrate = 1;
		reset_hd(CURRENT_DEV);
		return;
	}
	if (recalibrate) {
		recalibrate = 0;
		hd_out(dev,hd_info[CURRENT_DEV].sect,0,0,0,
			WIN_RESTORE,&recal_intr);
		return;
	}	
	if (CURRENT->cmd == WRITE) {
		hd_out(dev,nsect,sec,head,cyl,WIN_WRITE,&write_intr);
		for(i=0 ; i<3000 && !(r=inb_p(HD_STATUS)&DRQ_STAT) ; i++)
			/* nothing */ ;
		if (!r) {
			bad_rw_intr();
			goto repeat;
		}
		port_write(HD_DATA,CURRENT->buffer,256);
	} else if (CURRENT->cmd == READ) {
		hd_out(dev,nsect,sec,head,cyl,WIN_READ,&read_intr);
	} else
		panic("unknown hd-command");
}

void hd_init(void)
{//do_hd_request
	blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;
	//blk_dev[MAJOR_NR].request_fn =do_hd_request;
	set_intr_gate(0x2E,&hd_interrupt);
	outb_p(inb_p(0x21)&0xfb,0x21);
	outb(inb_p(0xA1)&0xbf,0xA1);
}
