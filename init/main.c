/*
 *  linux/init/main.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <unistd.h>
#include <time.h>



/*
 * we need this inline - forking from kernel space will result
 * in NO COPY ON WRITE (!!!), until an execve is executed. This
 * is no problem, but for the stack. This is handled by not letting
 * main() use the stack at all after fork(). Thus, no function
 * calls - which means inline code for fork too, as otherwise we
 * would use the stack upon exit from 'fork()'.
 *
 * Actually only pause and fork are needed inline, so that there
 * won't be any messing with the stack from main(), but we define
 * some others too.
 */

//unistd.h::_syscall 后面的数字为调用方法的参数个数
//调用的方法声明在 sys.h 

inline _syscall0(int,fork)
inline _syscall0(int,pause)
inline _syscall1(int,setup,void *,BIOS) 
inline _syscall0(int,sync)


extern void _exit(int status);

//正常使用时要用 inline 避免污染第一个进程的堆栈
//_syscall0(int,fork)
//_syscall0(int,pause)
//_syscall1(int,setup,void *,BIOS) 
//_syscall0(int,sync)


//execve.c 里声明了 execve方法
//_syscall3(int,execve,const char *,file,char **,argv,char **,envp)

#include <linux/tty.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <asm/system.h>
#include <asm/io.h>

#include <stddef.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>

#include <linux/fs.h>

//static char printbuf[1024];

extern int vsprintf();
extern void init(void);
extern void blk_dev_init(void);
extern void chr_dev_init(void);
extern void hd_init(void);
extern void floppy_init(void);
extern void mem_init(long start, long end);
extern long rd_init(long mem_start, int length);
extern long kernel_mktime(struct tm * tm);
extern long startup_time;

/*
 * This is set up by the setup-routine at boot-time
 */
#define EXT_MEM_K (*(unsigned short *)0x90002)
#define DRIVE_INFO (*(struct drive_info *)0x90080)
#define ORIG_ROOT_DEV (*(unsigned short *)0x901FC)

//#define ORIG_ROOT_DEV 0x301
//#define ORIG_ROOT_DEV 0x301


/*
 //运行时，按偏移地址取值
#defineORIG_ROOT_DEV (*(unsigned short *)0x901FC)        //60行处

ROOT_DEV= ORIG_ROOT_DEV;                                          //110行处

这里main.c中是直接从 0x901FC 处获取根文件系统的设备号的。但
是这里的地址并非是物理内存的实际地址，而是一个偏移地址。
因为此时CPU已经处于保护模式下运行，并且已经设置了全局描述符表（GDT）
和物理内存的页管理机制，这里和实模式下操作的地址是不一样的，要想得到此
时的实际物理地址，就需要知道此时的起始物理地址，然后加上该偏移地址，就是
根文件系统的设备号所在的物理内存中的位置了。而要想知道起始物理地址，就需
要了解在进入该main.c程序之前，操作系统对CPU的各个寄存器的赋值，从而根据
寄存器中的值，然后查找全局描述符表得到线性地址，然后再根据页目录表将线性地址
转化为实际内存中的物理地址。从而得到该main.c程序所操作的物理地址范围，
然后加上偏移地址0x901FC从而得到想要得到的地址。再根据系统在启动引导过程中
将系统参数保存在内存中的位置来进行对比，就可以知道linux操作系统初始化过程中，
为什么要到0x901FC地址处去获取根文件系统设备号了。?
 
* */


/*
 * Yeah, yeah, it's ugly, but I cannot find how to do this correctly
 * and this seems to work. I anybody has more info on the real-time
 * clock I'd be interested. Most of this was trial and error, and some
 * bios-listing reading. Urghh.
 */

#define CMOS_READ(addr) ({ \
outb_p(0x80|addr,0x70); \
inb_p(0x71); \
})

#define BCD_TO_BIN(val) ((val)=((val)&15) + ((val)>>4)*10)
static void time_init(void)
{
	struct tm time;

	do {
		time.tm_sec = CMOS_READ(0);
		time.tm_min = CMOS_READ(2);
		time.tm_hour = CMOS_READ(4);
		time.tm_mday = CMOS_READ(7);
		time.tm_mon = CMOS_READ(8);
		time.tm_year = CMOS_READ(9);
	} while (time.tm_sec != CMOS_READ(0));
	BCD_TO_BIN(time.tm_sec);
	BCD_TO_BIN(time.tm_min);
	BCD_TO_BIN(time.tm_hour);
	BCD_TO_BIN(time.tm_mday);
	BCD_TO_BIN(time.tm_mon);
	BCD_TO_BIN(time.tm_year);
	time.tm_mon--;
	startup_time = kernel_mktime(&time);
}

static long memory_end = 0;
static long buffer_memory_end = 0;
static long main_memory_start = 0;

struct drive_info { char dummy[32]; } drive_info;


/* This really IS void, no error here. */
void main(void)		
{			
//startup_32 ==> after_page_tables ==> setup_paging ==>  main()

    /* The startup routine assumes (well, ...) this */
/*
 * Interrupts are still disabled. Do necessary setups, then
 * enable them
 */
 	
    ROOT_DEV = ORIG_ROOT_DEV;
 	drive_info = DRIVE_INFO;
	memory_end = (1<<20) + (EXT_MEM_K<<10);
	memory_end &= 0xfffff000;
	if (memory_end > 16*1024*1024)
		memory_end = 16*1024*1024;
	if (memory_end > 12*1024*1024) 
		buffer_memory_end = 4*1024*1024;
	else if (memory_end > 6*1024*1024)
		buffer_memory_end = 2*1024*1024;
	else
		buffer_memory_end = 1*1024*1024;
	main_memory_start = buffer_memory_end;
#ifdef RAMDISK
	main_memory_start += rd_init(main_memory_start, RAMDISK*1024);
#endif
	mem_init(main_memory_start,memory_end);  //内存

        //printk(" 0 main act!!! !");
    trap_init();        // 陷阱门（硬件中断向量）初始化

        //printk(" 1 main act!!! !");
    blk_dev_init();        // 块设备初始化

    chr_dev_init();        // 字符设备初始化

        //printk(" 2 main act   printk err!!!!");
    tty_init();         // tty 初始化

    time_init();        // 设置开机启动时间,startup_time

        //printk(" 3 main act   printk OK!!!!");
    sched_init();        // 调度程序初始化  0x80系统中断初始化

    buffer_init(buffer_memory_end);    // 缓冲管理初始化，建内存链表等.

    hd_init();         // 硬盘初始化

    floppy_init();        // 软盘初始化

    sti();          // 设置完成，开启中断。

       //printk("4 main act!!! !");
    
    move_to_user_mode();      // 移到用户模式
    // pid =fork(); 
    //pid < 0 出错. 
    //pid==0 当前为子进程. 
    //pid>0当前为主进程
	if (!fork()) {		/* we count on this going ok */
        //printk("main act sub init!!");
        // 1 号进程
        init();
	}
/*
 *   NOTE!!   For any other task 'pause()' would mean we have to get a
 * signal to awaken, but task0 is the sole exception (see 'schedule()')
 * as task 0 gets activated at every idle moment (when no other tasks
 * can run). For task0 'pause()' just means we go check if some other
 * task can run, and if not we return here.
 */
	
    for(;;) pause();
}


static char * argv_rc[] = { "/bin/sh", NULL };
static char * envp_rc[] = { "HOME=/", NULL };

static char * argv[] = { "-/bin/sh",NULL };
static char * envp[] = { "HOME=/usr/root", NULL };


//printk的使用范围要明确，这里不可以使用 printk了
void init(void)
{
    //printk("init act!!");
    int pid,i;
	setup((void *) &drive_info);//挂载文件系统  在用户模式下在此函数中调用 mount_root
	(void) open("/dev/tty0",O_RDWR,0);
	(void) dup(0);
	(void) dup(0);
	if (!(pid=fork())) {
        // 2 号进程
		close(0);
		if (open("/etc/rc",O_RDONLY,0))
			_exit(1);
		execve("/bin/sh",argv_rc,envp_rc);
		_exit(2);
	}
	if (pid>0)
    {//等待 2 号进程结束 (上面 fork 那个) , wait() 等待子进程停止或终止, 返回 该子进程的 pid 
		while (pid != wait(&i))
        {
        }
    }
			/* nothing */;
	while (1) {
		if ((pid=fork())<0) {
			continue;
		}
		if (!pid) {
			close(0);close(1);close(2);
			setsid();
			(void) open("/dev/tty0",O_RDWR,0);
			(void) dup(0);
			(void) dup(0);
			_exit(execve("/bin/sh",argv,envp));
		}
		while (1)
			if (pid == wait(&i))
				break;
		sync();
	}
	_exit(0);	/* NOTE! _exit, not exit() */
}


