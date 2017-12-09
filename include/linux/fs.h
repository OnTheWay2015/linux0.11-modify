/*
 * This file has definitions for some important file table
 * structures etc.
 */

#ifndef _FS_H
#define _FS_H

#include <sys/types.h>

/* devices are as follows: (same as minix, so we can use the minix
 * file system. These are major numbers.)
 *
 * 0 - unused (nodev)
 * 1 - /dev/mem
 * 2 - /dev/fd
 * 3 - /dev/hd
 * 4 - /dev/ttyx
 * 5 - /dev/tty
 * 6 - /dev/lp
 * 7 - unnamed pipes
 */

#define IS_SEEKABLE(x) ((x)>=1 && (x)<=3)

#define READ 0
#define WRITE 1
#define READA 2		/* read-ahead - don't pause */
#define WRITEA 3	/* "write-ahead" - silly, but somewhat useful */

void buffer_init(long buffer_end);

#define MAJOR(a) (((unsigned)(a))>>8)
#define MINOR(a) ((a)&0xff)

#define NAME_LEN 14
#define ROOT_INO 1

#define I_MAP_SLOTS 8
#define Z_MAP_SLOTS 8
#define SUPER_MAGIC 0x137F

#define NR_OPEN 20
#define NR_INODE 32
#define NR_FILE 64
#define NR_SUPER 8
#define NR_HASH 307
#define NR_BUFFERS nr_buffers
#define BLOCK_SIZE 1024
#define BLOCK_SIZE_BITS 10
#ifndef NULL
#define NULL ((void *) 0)
#endif

#define INODES_PER_BLOCK ((BLOCK_SIZE)/(sizeof (struct d_inode)))
#define DIR_ENTRIES_PER_BLOCK ((BLOCK_SIZE)/(sizeof (struct dir_entry)))

#define PIPE_HEAD(inode) ((inode).i_zone[0])
#define PIPE_TAIL(inode) ((inode).i_zone[1])
#define PIPE_SIZE(inode) ((PIPE_HEAD(inode)-PIPE_TAIL(inode))&(PAGE_SIZE-1))
#define PIPE_EMPTY(inode) (PIPE_HEAD(inode)==PIPE_TAIL(inode))
#define PIPE_FULL(inode) (PIPE_SIZE(inode)==(PAGE_SIZE-1))
#define INC_PIPE(head) \
__asm__("incl %0\n\tandl $4095,%0"::"m" (head))

typedef char buffer_block[BLOCK_SIZE];

struct buffer_head {
	char * b_data;			/* pointer to data block (1024 bytes) */
	unsigned long b_blocknr;	/* block number  块号 */
	unsigned short b_dev;		/* device (0 = free) */
	unsigned char b_uptodate;
	unsigned char b_dirt;		/* 0-clean,1-dirty */
	unsigned char b_count;		/* users using this block */
	unsigned char b_lock;		/* 0 - ok, 1 -locked */
	struct task_struct * b_wait;
	struct buffer_head * b_prev;
	struct buffer_head * b_next;
	struct buffer_head * b_prev_free;
	struct buffer_head * b_next_free;
};

struct d_inode {
	unsigned short i_mode;
	unsigned short i_uid;
	unsigned long i_size;
	unsigned long i_time;
	unsigned char i_gid;
	unsigned char i_nlinks;
	unsigned short i_zone[9];
};

struct m_inode {
	unsigned short i_mode;
	unsigned short i_uid;
	unsigned long i_size;
	unsigned long i_mtime;
	unsigned char i_gid;
	unsigned char i_nlinks;
	unsigned short i_zone[9];
/* these are in memory also */
	struct task_struct * i_wait;
	unsigned long i_atime;
	unsigned long i_ctime;
	unsigned short i_dev;
	unsigned short i_num;
	unsigned short i_count;
	unsigned char i_lock;
	unsigned char i_dirt;
	unsigned char i_pipe;
	unsigned char i_mount;
	unsigned char i_seek;
	unsigned char i_update;
};

struct file {
	unsigned short f_mode;
	unsigned short f_flags;
	unsigned short f_count;
	struct m_inode * f_inode;
	off_t f_pos;
};


//超级块代表了整个文件系统，超级块是文件系统的控制块，有整个文件系统信息，
//一个文件系统所有的inode都要连接到超级块上，可以说，一个超级块就代表了一个文件系统。
//
//数据通常以文件的形式存储在设备上，因此文件系统的基本功能就是以某种格式存取/控制文件。
//0.11 版的内核中采用了 minix1.0 版的文件系统。在最新的 2.6 版内核中，借助于 VFS，系统支持 50 多种文件系统。
//首先介绍一下 minix 文件系统
//      minix 文件系统和标准 unix 文件系统基本相同。它由 6 个部分组成，分别是：引导块，超级块，i 节点位图， 逻辑块位图， i 节点，和数据区。
//  如果存放文件系统的设备不是引导设备，那么引导块可以为空。PC 机的块设备通常 以 512 字节为一个扇区，而文件系统则以盘块为单位使用之。
//  minix 中 1 个盘块等于 2 个扇区大小。 从引导块为第 0 个盘块开始计算。逻辑块可以为 2^n 个 盘块，minix 中逻辑块大小等于盘块。
//  所以 盘块 = 逻辑块 = 缓冲块 = 1024 字节。 超级块存放文件系统的整体信息。 i 节点位图描述了 i 节点的使用情况。
//      文件通常将控制信息和数据分开存放，i 节点就是存放文件的控制信息的，通常称之为 inode。 逻辑块位图则描述了逻辑块的使用情况。
//  linux 中的文件范围很广泛，不仅仅指普通文件。用 ls -l 命令可以发现显示的信息的最左边字符可以为 "-","d","s","p","b","c",
//  分别表示正规文件，目录文件，符号连接，命名管道，块设备，字符设备文件。紧跟在其后的 9 位字符可以为 r,w,x,s,S 等，
//  描述了文件的访问权限。 后面的信息有文件的用户名，组名，文件大小，修改日期等，这些信息当然是放在 inode 中的。
//  文件名除外。那么文件系统是如何被加载的呢？在系统启动过程中，具体是在任务 1 的 init() 函数中，通过 setup 系 统调用加载的，
//  该函数调用 mount_root() 函数读取根文件系统的超级块和根 inode 节点。

struct super_block {
    unsigned short s_ninodes; //节点数
    unsigned short s_nzones; //逻辑块数
    unsigned short s_imap_blocks; //i 节点位图所占的数据块数
    unsigned short s_zmap_blocks; //逻辑块位图所占用的数据块数
    unsigned short s_firstdatazone; //第一个数据逻辑块号
    unsigned short s_log_zone_size; //log2(数据块数/逻辑块)
    unsigned long s_max_size; //文件的最大长度
    unsigned short s_magic ;   //文件系统魔数
//以下的字段仅出现在内存中
    struct buffer_head* s_imap[8];// i 节点位图在缓冲区中的指针
    struct buffer_head* s_zmap[8];//逻辑块位图在缓冲区中的指针
    unsigned short s_dev;//超级块所在的设备号
    struct m_inode *s_isup;//被安装的文件系统的根节点
    struct m_inode *s_imount; //被安装到的 i 节点
    unsigned long s_time; //修改时间
    struct task_struct *s_wait; //等待该超级块的进程
    unsigned char s_lock; //被锁定标志
    unsigned char s_rd_only; //只读标志
    unsigned char s_dirt; //已修改标志
};

//struct super_block {
//	unsigned short s_ninodes;
//	unsigned short s_nzones;
//	unsigned short s_imap_blocks;
//	unsigned short s_zmap_blocks;
//	unsigned short s_firstdatazone;
//	unsigned short s_log_zone_size;
//	unsigned long s_max_size;
//	unsigned short s_magic;
///* These are only in memory */
//	struct buffer_head * s_imap[8];
//	struct buffer_head * s_zmap[8];
//	unsigned short s_dev;
//	struct m_inode * s_isup;
//	struct m_inode * s_imount;
//	unsigned long s_time;
//	struct task_struct * s_wait;
//	unsigned char s_lock;
//	unsigned char s_rd_only;
//	unsigned char s_dirt;
//};

struct d_super_block {
	unsigned short s_ninodes;
	unsigned short s_nzones;
	unsigned short s_imap_blocks;
	unsigned short s_zmap_blocks;
	unsigned short s_firstdatazone;
	unsigned short s_log_zone_size;
	unsigned long s_max_size;
	unsigned short s_magic;
};

struct dir_entry {
	unsigned short inode;
	char name[NAME_LEN];
};

extern struct m_inode inode_table[NR_INODE];
extern struct file file_table[NR_FILE];
extern struct super_block super_block[NR_SUPER];
extern struct buffer_head * start_buffer;
extern int nr_buffers;

extern void check_disk_change(int dev);
extern int floppy_change(unsigned int nr);
extern int ticks_to_floppy_on(unsigned int dev);
extern void floppy_on(unsigned int dev);
extern void floppy_off(unsigned int dev);
extern void truncate(struct m_inode * inode);
extern void sync_inodes(void);
extern void wait_on(struct m_inode * inode);
extern int bmap(struct m_inode * inode,int block);
extern int create_block(struct m_inode * inode,int block);
extern struct m_inode * namei(const char * pathname);
extern int open_namei(const char * pathname, int flag, int mode,
	struct m_inode ** res_inode);
extern void iput(struct m_inode * inode);
extern struct m_inode * iget(int dev,int nr);
extern struct m_inode * get_empty_inode(void);
extern struct m_inode * get_pipe_inode(void);
extern struct buffer_head * get_hash_table(int dev, int block);
extern struct buffer_head * getblk(int dev, int block);
extern void ll_rw_block(int rw, struct buffer_head * bh);
extern void brelse(struct buffer_head * buf);
extern struct buffer_head * bread(int dev,int block);
extern void bread_page(unsigned long addr,int dev,int b[4]);
extern struct buffer_head * breada(int dev,int block,...);
extern int new_block(int dev);
extern void free_block(int dev, int block);
extern struct m_inode * new_inode(int dev);
extern void free_inode(struct m_inode * inode);
extern int sync_dev(int dev);
extern struct super_block * get_super(int dev);
extern int ROOT_DEV;

extern void mount_root(void);

#endif
