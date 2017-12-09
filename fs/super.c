/*
 *  linux/fs/super.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * super.c contains code to handle the super-block tables.
 */
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/system.h>

#include <errno.h>
#include <sys/stat.h>

int sync_dev(int dev);
void wait_for_keypress(void);

/* set_bit uses setb, as gas doesn't recognize setc */
#define set_bit(bitnr,addr) ({ \
register int __res __asm__("ax"); \
__asm__("bt %2,%3;setb %%al":"=a" (__res):"a" (0),"r" (bitnr),"m" (*(addr))); \
__res; })

//fs.h
struct super_block super_block[NR_SUPER];
/* this is initialized in init/main.c */
int ROOT_DEV = 0;

static void lock_super(struct super_block * sb)
{
	cli();
	while (sb->s_lock)
		sleep_on(&(sb->s_wait));
	sb->s_lock = 1;
	sti();
}

static void free_super(struct super_block * sb)
{
	cli();
	sb->s_lock = 0;
	wake_up(&(sb->s_wait));
	sti();
}

static void wait_on_super(struct super_block * sb)
{
	cli();
	while (sb->s_lock)
		sleep_on(&(sb->s_wait));
	sti();
}

struct super_block * get_super(int dev)
{
	struct super_block * s;

	if (!dev)
		return NULL;
	s = 0+super_block;
	while (s < NR_SUPER+super_block)
		if (s->s_dev == dev) {
			wait_on_super(s);
			if (s->s_dev == dev)
				return s;
			s = 0+super_block;
		} else
			s++;
	return NULL;
}

void put_super(int dev)
{
	struct super_block * sb;
	//struct m_inode * inode;
	int i;

	if (dev == ROOT_DEV) {
		printk("root diskette changed: prepare for armageddon\n\r");
		return;
	}
	if (!(sb = get_super(dev)))
		return;
	if (sb->s_imount) {
		printk("Mounted disk changed - tssk, tssk\n\r");
		return;
	}
	lock_super(sb);
	sb->s_dev = 0;
	for(i=0;i<I_MAP_SLOTS;i++)
		brelse(sb->s_imap[i]);
	for(i=0;i<Z_MAP_SLOTS;i++)
		brelse(sb->s_zmap[i]);
	free_super(sb);
	return;
}

//读取指定设备的超级块
//如果指定设备 dev 上的文件系统超级块已经在超级块表中存在，则直接返回该超级块项的指针
//否则就从设备 dev 上读取超级块到缓冲中，并复制到超级块表中。并返回超级块指针
static struct super_block * read_super(int dev)
{
    struct super_block * s;
    struct buffer_head * bh;
    int i,block;

    if (!dev)
    {
        //printk("1 read_super \n");
        return NULL;
    }
    check_disk_change(dev);
    s = get_super(dev);
    if (s)
    {// 超级块存在于超级表中
        //printk("2 read_super \n");
        return s;
    }

    //在超级表中获取一个空闲项
    for (s = 0+super_block ;; s++) {
        if (s >= NR_SUPER+super_block)
        {
            //printk("3 read_super \n");
            return NULL;
        }
        if (!s->s_dev)
        {
            break;
        }
    }
    
    //该超级块就用来指定 设备 dev 的文件系统, 下面几个是该超级块内存字段的初始化
    s->s_dev = dev; 
    s->s_isup = NULL;
    s->s_imount = NULL;
    s->s_time = 0;
    s->s_rd_only = 0;
    s->s_dirt = 0;

    //锁定该超级块
    lock_super(s);
   
    //从设备上读取超级块信息到 bh 指向的缓冲块中.
    //超级块位于块设备的第 2 个逻辑块 ( 1号块 )中， 第一个是引导盘块( 0号块 ). 
    if (!(bh = bread(dev,1))) { //读取 1 号磁盘块。 磁盘块号从 0 号计起
        //读取失败, 释放该块, 并返回空
        s->s_dev=0;
        free_super(s);
        //printk("4 read_super \n");
        return NULL;
    }


    //*((struct d_super_block *) s) =
    //    *((struct d_super_block *) bh->b_data);
   
     


    unsigned char *p1, *p2;
    p1 = (unsigned char*)s;
    p2 = (unsigned char*)bh->b_data;
    
    //重要: 结构体复制有bug, 所以用这种形式
    for(i = 0;i < sizeof(struct d_super_block); i++)
    {
        //printk( "char[0x%x]\n\r", *p2);
        *p1++ = *p2++;
    }
    
    
    brelse(bh);
    if (s->s_magic != SUPER_MAGIC) {
        s->s_dev = 0;
        free_super(s);
        //printk("5 read_super m[0x%x] M[0x%x]\n", s->s_magic, SUPER_MAGIC);
        return NULL;
    }
    for (i=0;i<I_MAP_SLOTS;i++)
        s->s_imap[i] = NULL;
    for (i=0;i<Z_MAP_SLOTS;i++)
        s->s_zmap[i] = NULL;
    block=2;
    for (i=0 ; i < s->s_imap_blocks ; i++)
    {
        s->s_imap[i]=bread(dev,block);
        if (s->s_imap[i])
        {
            block++;
        }
        else
        {
            break;
        }
    }
    for (i=0 ; i < s->s_zmap_blocks ; i++)
    {
        s->s_zmap[i]=bread(dev,block);
        if (s->s_zmap[i])
        {
            block++;
        }
        else
        {
            break;
        }
    }
    if (block != 2+s->s_imap_blocks+s->s_zmap_blocks) {
        for(i=0;i<I_MAP_SLOTS;i++)
            brelse(s->s_imap[i]);
        for(i=0;i<Z_MAP_SLOTS;i++)
            brelse(s->s_zmap[i]);
        s->s_dev=0;
        free_super(s);
        //printk("6 read_super \n");
        return NULL;
    }
    s->s_imap[0]->b_data[0] |= 1;
    s->s_zmap[0]->b_data[0] |= 1;
    free_super(s);
    //printk("7 read_super \n");
    return s;
}

int sys_umount(char * dev_name)
{
	struct m_inode * inode;
	struct super_block * sb;
	int dev;

	if (!(inode=namei(dev_name)))
		return -ENOENT;
	dev = inode->i_zone[0];
	if (!S_ISBLK(inode->i_mode)) {
		iput(inode);
		return -ENOTBLK;
	}
	iput(inode);
	if (dev==ROOT_DEV)
		return -EBUSY;
	if (!(sb=get_super(dev)) || !(sb->s_imount))
		return -ENOENT;
	if (!sb->s_imount->i_mount)
		printk("Mounted inode has i_mount=0\n");
	for (inode=inode_table+0 ; inode<inode_table+NR_INODE ; inode++)
		if (inode->i_dev==dev && inode->i_count)
				return -EBUSY;
	sb->s_imount->i_mount=0;
	iput(sb->s_imount);
	sb->s_imount = NULL;
	iput(sb->s_isup);
	sb->s_isup = NULL;
	put_super(dev);
	sync_dev(dev);
	return 0;
}

int sys_mount(char * dev_name, char * dir_name, int rw_flag)
{
	struct m_inode * dev_i, * dir_i;
	struct super_block * sb;
	int dev;

	if (!(dev_i=namei(dev_name)))
		return -ENOENT;
	dev = dev_i->i_zone[0];
	if (!S_ISBLK(dev_i->i_mode)) {
		iput(dev_i);
		return -EPERM;
	}
	iput(dev_i);
	if (!(dir_i=namei(dir_name)))
		return -ENOENT;
	if (dir_i->i_count != 1 || dir_i->i_num == ROOT_INO) {
		iput(dir_i);
		return -EBUSY;
	}
	if (!S_ISDIR(dir_i->i_mode)) {
		iput(dir_i);
		return -EPERM;
	}
	if (!(sb=read_super(dev))) {
		iput(dir_i);
		return -EBUSY;
	}
	if (sb->s_imount) {
		iput(dir_i);
		return -EBUSY;
	}
	if (dir_i->i_mount) {
		iput(dir_i);
		return -EPERM;
	}
	sb->s_imount=dir_i;
	dir_i->i_mount=1;
	dir_i->i_dirt=1;		/* NOTE! we don't iput(dir_i) */
	return 0;			/* we do that in umount */
}

void mount_root(void)
{
	int i,free;
	struct super_block * p;
	struct m_inode * mi;
    
    //若磁盘 i 节点结构不是 32 字节，报错. 防止修改代码时出现不一致情况
	if (32 != sizeof (struct d_inode))
    {
		panic("bad i-node size");
    }
    //首先初始化文件表数组(共 64 项, 即系统同时只能打开 64 个文件)和超级块表。这里将所有
    //文件结构中的引用计数设置为 0 (表示空闲), 并把超级块表中各项结构的设备字段初始化为 0 (也表示空闲)
    //如果根文件系统所在设备是软盘的话， 就提示 "插入根文件系统盘 并按回车键", 并等待按键
	
    //初始化文件表
    for(i=0;i<NR_FILE;i++)
    {
		file_table[i].f_count=0;
    }
	if (MAJOR(ROOT_DEV) == 2) {
		printk("Insert root floppy and press ENTER");
		wait_for_keypress();
	}
	//初始化超级块表
    for(p = &super_block[0] ; p < &super_block[NR_SUPER] ; p++) {
		p->s_dev = 0;
		p->s_lock = 0;
		p->s_wait = NULL;
	}
    
    //安装根文件系统
	if (!(p=read_super(ROOT_DEV)))
		panic("Unable to mount root");
	if (!(mi=iget(ROOT_DEV,ROOT_INO)))
		panic("Unable to read root i-node");
	mi->i_count += 3 ;	/* NOTE! it is logically used 4 times, not 1 */
	p->s_isup = p->s_imount = mi;
	current->pwd = mi;
	current->root = mi;
	free=0;
	i=p->s_nzones;
	while (-- i >= 0)
		if (!set_bit(i&8191,p->s_zmap[i>>13]->b_data))
			free++;
	//printk("%d/%d free blocks\n\r",free,p->s_nzones);
	free=0;
	i=p->s_ninodes+1;
	while (-- i >= 0)
		if (!set_bit(i&8191,p->s_imap[i>>13]->b_data))
			free++;
	//printk("%d/%d free inodes\n\r",free,p->s_ninodes);
}
