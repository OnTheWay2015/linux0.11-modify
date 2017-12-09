#ifndef _SCHED_H
#define _SCHED_H


// 人工定义最大 64个任务进程
#define NR_TASKS 64
#define HZ 100

#define FIRST_TASK task[0]
#define LAST_TASK task[NR_TASKS-1]

#include <linux/head.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <signal.h>

#if (NR_OPEN > 32)
#error "Currently the close-on-exec-flags are in one word, max 32 files/proc"
#endif



#define TASK_RUNNING 0              //就绪，可运行状态，个人理解为正在运行或者等待运行的任务
#define TASK_INTERRUPTIBLE 1        //可中断睡眠状态可有信号，wake_up 唤醒，从 sys_pause，sys_waitpid，interruptiable_sleep_on 进入
#define TASK_UNINTERRUPTIBLE  2     //不可中断睡眠，只能由sleep_on()进入，wake_up函数唤醒
#define TASK_ZOMBIE              3  //僵死状态，不能再被调度
#define TASK_STOPPED            4   //暂停状态，0.11没有实现


#ifndef NULL
#define NULL ((void *) 0)
#endif

extern int copy_page_tables(unsigned long from, unsigned long to, long size);
extern int free_page_tables(unsigned long from, unsigned long size);

extern void sched_init(void);
extern void schedule(void);
extern void trap_init(void);
extern void panic(const char * str);
extern int tty_write(unsigned minor,char * buf,int count);

typedef int (*fn_ptr)();

struct i387_struct {
	long	cwd;
	long	swd;
	long	twd;
	long	fip;
	long	fcs;
	long	foo;
	long	fos;
	long	st_space[20];	/* 8*10 bytes for each FP-reg = 80 bytes */
};

struct tss_struct {
	long	back_link;	/* 16 high bits zero */
	long	esp0;
	long	ss0;		/* 16 high bits zero */
	long	esp1;
	long	ss1;		/* 16 high bits zero */
	long	esp2;
	long	ss2;		/* 16 high bits zero */
	long	cr3;
	long	eip;
	long	eflags;
	long	eax,ecx,edx,ebx;
	long	esp;
	long	ebp;
	long	esi;
	long	edi;
	long	es;		/* 16 high bits zero */
	long	cs;		/* 16 high bits zero */
	long	ss;		/* 16 high bits zero */
	long	ds;		/* 16 high bits zero */
	long	fs;		/* 16 high bits zero */
	long	gs;		/* 16 high bits zero */
	long	ldt;		/* 16 high bits zero */
	long	trace_bitmap;	/* bits: trace 0, bitmap 16-31 */
	struct i387_struct i387;
};

struct task_struct {
/* these are hardcoded - don't touch */
	long state;	/* -1 unrunnable, 0 runnable, >0 stopped */
	long counter;
	long priority;
	long signal; //signal是任务接受到的信号图表，每一位代表一个信号，0表示没有信号，1表示有信号，
    
	struct sigaction sigaction[32];
	long blocked;	/* bitmap of masked signals */ //blocked是任务当前屏蔽的信号图表，0表示没有屏蔽，1表示屏蔽信号。
/* various fields */
	int exit_code;
	unsigned long start_code,end_code,end_data,brk,start_stack;
	long pid,father,pgrp,session,leader;
	unsigned short uid,euid,suid;
	unsigned short gid,egid,sgid;

    long alarm; // alarm是 alarm 信号。

	long utime,stime,cutime,cstime,start_time;
	unsigned short used_math;
/* file system info */
	int tty;		/* -1 if no tty, so it must be signed */
	unsigned short umask;
	struct m_inode * pwd;
	struct m_inode * root;
	struct m_inode * executable;
	unsigned long close_on_exec;
	struct file * filp[NR_OPEN];
/* ldt for this task 0 - zero 1 - cs 2 - ds&ss */
	struct desc_struct ldt[3];
/* tss for this task */
	struct tss_struct tss;
};

/*
 *  INIT_TASK is used to set up the first task table, touch at
 * your own risk!. Base=0, limit=0x9ffff (=640kB)
 */
#define INIT_TASK \
/* state etc */	{ 0,15,15, \
/* signals */	0,{{},},0, \
/* ec,brk... */	0,0,0,0,0,0, \
/* pid etc.. */	0,-1,0,0,0, \
/* uid etc */	0,0,0,0,0,0, \
/* alarm */	0,0,0,0,0,0, \
/* math */	0, \
/* fs info */	-1,0022,NULL,NULL,NULL,0, \
/* filp */	{NULL,}, \
	{ \
		{0,0}, \
/* ldt */	{0x9f,0xc0fa00}, \
		{0x9f,0xc0f200}, \
	}, \
/*tss*/	{0,PAGE_SIZE+(long)&init_task,0x10,0,0,0,0,(long)&pg_dir,\
	 0,0,0,0,0,0,0,0, \
	 0,0,0x17,0x17,0x17,0x17,0x17,0x17, \
	 _LDT(0),0x80000000, \
		{} \
	}, \
}

extern struct task_struct *task[NR_TASKS]; // 定义任务数组
extern struct task_struct *last_task_used_math; // 使用过协处理器的任务指针
extern struct task_struct *current; // 当前任务指针
extern long volatile jiffies; // 定义开机以来时钟滴答数
extern long startup_time;// 开机时间


#define CURRENT_TIME (startup_time+jiffies/HZ)

extern void add_timer(long jiffies, void (*fn)(void));
extern void sleep_on(struct task_struct ** p);
extern void interruptible_sleep_on(struct task_struct ** p);
extern void wake_up(struct task_struct ** p);

/*
 * Entry into gdt where to find first TSS. 0-nul, 1-cs, 2-ds, 3-syscall
 * 4-TSS0, 5-LDT0, 6-TSS1 etc ...
 */
//全局表中，第一个局部描述符表(LDT) 描述符的选择符索引号
#define FIRST_TSS_ENTRY 4

#define FIRST_LDT_ENTRY (FIRST_TSS_ENTRY+1)

// _TSS 计算第 n 个 TSS 任务描术符相对于 gdt 的偏移位置. FIRST_TSS_ENTRY<<3 = FIRST_TSS_ENTRY * 8,(前面4个描述符大小 每个描述符 8字节)  
// (((unsigned long) n)<<4) = n * 16, 每个任务使用一个 TSS和 LDT 描述符, 16字节. 
#define _TSS(n) ((((unsigned long) n)<<4)+(FIRST_TSS_ENTRY<<3))
#define _LDT(n) ((((unsigned long) n)<<4)+(FIRST_LDT_ENTRY<<3))

#define ltr(n) __asm__("ltr %%ax"::"a" (_TSS(n)))
#define lldt(n) __asm__("lldt %%ax"::"a" (_LDT(n)))

#define str(n) \
__asm__("str %%ax\n\t" \
	"subl %2,%%eax\n\t" \
	"shrl $4,%%eax" \
	:"=a" (n) \
	:"a" (0),"i" (FIRST_TSS_ENTRY<<3))



/*
 *	switch_to(n) should switch tasks to task nr n, first
 * checking that n isn't the current task, in which case it does nothing.
 * This also clears the TS-flag if the task we switched to has used
 * tha math co-processor latest.
 */
//clts 清任务切换标志(CR0)
//TS：CR0的位3是任务已切换（Task Switched）标志。
//该标志用于推迟保存任务切换时的协处理器内容，直到新任务开始实际执行协处理器指令。
//处理器在每次任务切换时都会设置该标志，并且在执行协处理器指令时测试该标志。
//如果设置了TS标志并且CR0的 EM 标志为0，那么在执行任何协处理器指令之前会产生一个设备不存在异常。
//如果设置了TS标志但没有设置CR0的 MP 和 EM 标志，那么在执行协处理器指令WAIT/FWAIT之前不会产生设备不存在异常。
//如果设置了 EM 标志，那么TS标志对协处理器指令的执行无影响，
// __tmp.a 为偏移地址  __tmp.b 为段选择子
//
//
//重要:
//      每个描述符占8个字节，第一个状态段是第四个，所以<<3，得到第一个任务描述符在GDT中的位置，
//      而每个任务使用一个 tss 和 ldt，占 16 字节，所以 << 4，两者相加得到任务 n 的 tss 在 GDT 中的位置。另外 ecx 指向要切换过去的新任务。
//       现在开始理解代码，首先声明了一个 _tmp 的结构，这个结构里面包括两个 long 型，32位机里面 long 占32位，声明这个结构主要与 ljmp 这个长跳指令有关，
//       这个指令有两个参数，一个参数是段选择符，另一个是偏移地址，所以这个_tmp就是保存这两个参数。再比较任务n是不是当前任务，如果不是则跳转到标号 1，
//       否则交互 ecx 和 current 的内容，交换后的结果为 ecx 指向当前进程，current 指向要切换过去的新进程，在执行长跳，%0 代表输出输入寄存器列表中使用的第一
//       个寄存器，即"m"(*&__tmp.a)， 这个寄存器保存了*&__tmp.a，而_tmp.a存放的是32位偏移，_tmp.b存放的是新任务的tss段选择符，
//       长跳到段选择符会造成任务切换，这个是x86的硬件原理。
#define switch_to(n) {\
struct {long a,b;} __tmp; \
__asm__("cmpl %%ecx,current\n\t" \
	"je 1f\n\t" \
	"movw %%dx,%1\n\t" \
	"xchgl %%ecx,current\n\t" \
	"ljmp %0\n\t" \
	"cmpl %%ecx,last_task_used_math\n\t" \
	"jne 1f\n\t" \
	"clts\n" \
	"1:" \
	::"m" (*&__tmp.a),"m" (*&__tmp.b), \
	"d" (_TSS(n)),"c" ((long) task[n])); \
}

//TSS 全称为task state segment，是指在操作系统进程管理的过程中，进程切换时的任务现场信息。 　　
//  X86体系从硬件上支持任务间的切换。为此目的，它增设了一个新段：任务状态段(TSS)，它和数据段、代码段一样也是一种段，记录了任务的状态信息。　　
//  与其它段一样，TSS也有描述它的结构：TSS描述符表，它记录了一个TSS的信息，同时还有一个 TR 寄存器，它指向当前任务的 TSS。
//  任务切换的时候，CPU会将原寄存器的内容写出到相应的TSS，同时将新TSS的内容填到寄存器中，这样就实现了任务的切换。
//  TSS在任务切换过程中起着重要作用，通过它实现任务的挂起和恢复。所谓任务切换是指挂起当前正在执行的任务，
//  恢复或启动执行另一个任务。
//      Linux任务切换是通过switch_to这个宏来实现的，它利用长跳指令， 当长跳指令的 操作数 是 TSS 描述符 的时候，
//  就会引起 CPU 的任务的切换，此时，CPU将所有寄存器的状态保存到当前任务寄存器TR所指向的TSS段中，然后利用长跳指令的操作数(TSS描述符)
//      找到新任务的TSS段，并将其中的内容填写到各个寄存器中，最后，将新任务的TSS选择符更新到TR中。这样系统就开始运行新切换的任务了。
//      由此可见，通过在TSS中保存任务现场各寄存器状态的完整映象，实现了任务的切换。 task_struct中的tss成员就是记录TSS段内容的。
//      当进程被切换前，该进程用tss_struct保存处理器的所有寄存器的当前值。当进程重新执行时，CPU利用tss恢复寄存器状态

//current 是引用的前面 c代友定义的变量
//"movw %%dx,%1\n\t"   段选择子设置, 把  _TSS(n) [%dx]  放到 __tmp.b [%1] 

//.faq 任务有切换时，先要设置 CR0标志么？
//长跳到段选择符会造成任务切换，这个是x86的硬件原理。
// <linux 0.11内核完全注释> p78 
//TS, CR0 的位3是任务已切换(Task SWitched)标志。该标志用于推迟保存任务切换时的协处理器内容，直到新任务开始实际执行协处理器指令。
//处理器在每次任务切换时都会设置该标志，并且在执行协处理器指令时测试该标志。
//在任务切换时，处理器并不自动保存协处理器的上下文，而是会设置TS标志。这个标志会
//使得处理器在执行新任务指令流的作任何时候遇到一条协处理器指令时产生设备不存在的异常。
//设备不存在的异常的处理程序可使用clts指令清除ts标志,并且保存协处理器的上下文。如果任务
//从没有使用过协处理器，那么相应协处理器上下文就不用保存。
//clts  Clear Task-Switched Flag in CR0
//  The processor sets the TS flag every time a task switch occurs. The flag is used to synchronize
//  the saving of FPU context in multitasking applications. 

//::"m" (*&__tmp.a),"m" (*&__tmp.b),   //.faq 这是什么写法? 

#define PAGE_ALIGN(n) (((n)+0xfff)&0xfffff000)

#define _set_base(addr,base) \
__asm__("movw %%dx,%0\n\t" \
	"rorl $16,%%edx\n\t" \
	"movb %%dl,%1\n\t" \
	"movb %%dh,%2" \
	::"m" (*((addr)+2)), \
	  "m" (*((addr)+4)), \
	  "m" (*((addr)+7)), \
	  "d" (base) \
	)

#define _set_limit(addr,limit) \
__asm__("movw %%dx,%0\n\t" \
	"rorl $16,%%edx\n\t" \
	"movb %1,%%dh\n\t" \
	"andb $0xf0,%%dh\n\t" \
	"orb %%dh,%%dl\n\t" \
	"movb %%dl,%1" \
	::"m" (*(addr)), \
	  "m" (*((addr)+6)), \
	  "d" (limit) \
	)

#define set_base(ldt,base) _set_base( ((char *)&(ldt)) , base )
#define set_limit(ldt,limit) _set_limit( ((char *)&(ldt)) , (limit-1)>>12 )

#define _get_base(addr) ({\
unsigned long __base; \
__asm__("movb %3,%%dh\n\t" \
	"movb %2,%%dl\n\t" \
	"shll $16,%%edx\n\t" \
	"movw %1,%%dx" \
	:"=&d" (__base) \
	:"m" (*((addr)+2)), \
	 "m" (*((addr)+4)), \
	 "m" (*((addr)+7))); \
__base;})

#define get_base(ldt) _get_base( ((char *)&(ldt)) )

#define get_limit(segment) ({ \
unsigned long __limit; \
__asm__("lsll %1,%0\n\tincl %0":"=r" (__limit):"r" (segment)); \
__limit;})

#endif
