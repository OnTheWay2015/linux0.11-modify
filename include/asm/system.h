//1、创建进程 0，并让进程 0 具备在 32 位保护模式下在主机中运算的能力；
//2、以进程 0 为母本创建进程 1，使进程 1 不仅具有进程 0 的能力，还有以文件方式与外设进行数据交互的能力；
//3、以进程 1 为母本创建进程 2，使进程 2 在拥有进程1全面能力和环境的情况下，进一步具备支持人机交互的能力，最终实现“怠速”。
//按照规则，除了进程 0，其他所有的进程都应该是由父进程在用户态下完成创建的，所以为了遵守这个规则,
//进程 0 要先变成用户态才能创建进程 1，方法就是调用move_to_user_mode()函数。

#define move_to_user_mode() \
__asm__ ("movl %%esp,%%eax\n\t" \
	"pushl $0x17\n\t" \
	"pushl %%eax\n\t" \
	"pushfl\n\t" \
	"pushl $0x0f\n\t" \
	"pushl $1f\n\t" \
	"iret\n" \
	"1:\tmovl $0x17,%%eax\n\t" \
	"movw %%ax,%%ds\n\t" \
	"movw %%ax,%%es\n\t" \
	"movw %%ax,%%fs\n\t" \
	"movw %%ax,%%gs" \
	:::"ax")

//由于在sched_init()中已经设置了标志寄存器中的vm标志为0，所以 iret 调用后不会发生任务切换，而是继续执行 EIP 指向的指令故继续执行
// 1标号的代码，开始执行任务0，任务0的 堆栈段选择符为 0x17， 在sched_init()中已设置了任务0 的任务描述符和局部描述符为 INIT_TASK
//
//iret 
	//"pushl $0x17\n\t" // ss 段选择子
	//"pushl %%eax\n\t" // esp
	//"pushfl\n\t"      // eflags 
	//"pushl $0x0f\n\t" // cs
	//"pushl $1f\n\t"   // eip  偏移量,  后面的标号 1 

// 在 iret 之前，之前 5 个 push 压入的数据会出栈，分别赋给 ss，esp，eflags，cs，eip，
// 与通常中断引起的压栈顺序是一样的。
// “pushl $0x17/n/t ”代表的是 SS 段选择符。0x17 中的 17 用二进制表示是 00010111。
// 最后两位表示特权级是用户态还是内核态。linux0.1.1中特权级分为 0,1,2,3 共 4 个特权级，
// 用户态是 3，内核态是 0。因此它表示的是用户态。倒数第三位是 1，表示从 LDT 中获取描述符，
// 因此第 4~5 位的 10 表示从 LDT 的第 2 项（从第 0 项开始）中得到有关用户栈段的描述符。
//同理，“ pushl $0x0f/n/t ” 代表的是 CS 段选择符，0x0f 中的 0f 为 00001111，也是用户特权级，
//从 LDT 中的第 1 项（从第 0 项开始）中取得用户态代码段描述符。
//当 iret 返回时，程序将数据出栈给 ss，esp，eflags，cs，eip，之后就可以使进程进入用户态。

#define sti() __asm__ ("sti"::)
#define cli() __asm__ ("cli"::)
#define nop() __asm__ ("nop"::)

#define iret() __asm__ ("iret"::)

#define _set_gate(gate_addr,type,dpl,addr) \
__asm__ ("movw %%dx,%%ax\n\t" \
	"movw %0,%%dx\n\t" \
	"movl %%eax,%1\n\t" \
	"movl %%edx,%2" \
	: \
	: "i" ((short) (0x8000+(dpl<<13)+(type<<8))), \
	"o" (*((char *) (gate_addr))), \
	"o" (*(4+(char *) (gate_addr))), \
	"d" ((char *) (addr)),"a" (0x00080000))

//%0, %1, %2, 按顺序在输出例和输入例的参数序号(当前输出例没有参数),    (理解: %0为中断属性, %1为IDT存的方法址址, 内容是 0x00080000+ ((char*) (addr)),这样 addr 的方法和中断就绑定了,方法一般是在 .s 文件里实现 , %2为 IDT 属性地址 )
// "i" 立即数
// "o" 操作数为内存变量，但其寻址方式是偏移量类型，也即是基址寻址或者是基址加变址寻址
//"d" 对应 edx
//"a" 对应 eax


#define set_intr_gate(n,addr) \
	_set_gate(&idt[n],14,0,addr)

#define set_trap_gate(n,addr) \
	_set_gate(&idt[n],15,0,addr)

#define set_system_gate(n,addr) \
	_set_gate(&idt[n],15,3,addr)

//段描述符长度 64bit
#define _set_seg_desc(gate_addr,type,dpl,base,limit) {\
	*(gate_addr) = ((base) & 0xff000000) | \
		(((base) & 0x00ff0000)>>16) | \
		((limit) & 0xf0000) | \
		((dpl)<<13) | \
		(0x00408000) | \
		((type)<<8); \
	*((gate_addr)+1) = (((base) & 0x0000ffff)<<16) | \
		((limit) & 0x0ffff); }

/*****************************************************************************/  
/* 功能:  设置存储段描述符,把指定属性的段描述符放入gate_addr处                  */  
/* 参数:      gate_addr       段描述符的目的地址                                    */  
/*          type            描述符中类型域，具体见80386基础一节中的表格         */  
/*          dpl         描述符中特权级                                      */  
/*          base            段基地址，这是线性地址                              */  
/*          limit           段限长                                              */  
/* 返回:  (无)                                                                  */  
/*****************************************************************************/  
//#define _set_seg_desc(gate_addr,type,dpl,base,limit) {/  
//    // 把段描述符的第4－7字节放入gate_addr处  
//    *((gate_addr)+1) = ((base) & 0xff000000) | / // base的31..24位放入gate_addr的31..24位  
//        (((base) & 0x00ff0000)>>16) | /   //base的23..16位放入gate_addr的7..0位  
//        ((limit) & 0xf0000) | / //limit的19..16位放入gate_addr的19..16位  
//        ((dpl)<<13) | /       // dpl放入gate_addr的14..13位  
//        (0x00408000) | /    // 把P位和D位设置位1，G置为 0  
//        ((type)<<8); /        // type放入gate_addr的11..8位  
//        // 把段描述符的第0－3字节放入gate_addr+1处  
//        *(gate_addr)= (((base) & 0x0000ffff)<<16) | /  //base的15..0放入gate+1的31..15位  
//        ((limit) & 0x0ffff); }  //limit的15..0位放入gate+1的15..0位  





// __asm__ 是宏; type 是 _set_tssldt_desc 输入的参数，在下面和别的字符串拼成一个字符串，作为__asm__的输入字符串参数
#define _set_tssldt_desc(n,addr,type) \
__asm__ ("movw $104,%1\n\t" \
	"movw %%ax,%2\n\t" \
	"rorl $16,%%eax\n\t" \
	"movb %%al,%3\n\t" \
    "movb $" type ",%4\n\t" \
	"movb $0x00,%5\n\t" \
	"movb %%ah,%6\n\t" \
	"rorl $16,%%eax" \
	::"a" (addr), "m" (*(n)), "m" (*(n+2)), "m" (*(n+4)), \
	 "m" (*(n+5)), "m" (*(n+6)), "m" (*(n+7)) \
	)




#define set_tss_desc(n,addr) _set_tssldt_desc(((char *) (n)),addr,"0x89")
#define set_ldt_desc(n,addr) _set_tssldt_desc(((char *) (n)),addr,"0x82")

//
//
    /*****************************************************************************/  
    /* 功能:  设置系统段描述符，把指定属性的段描述符放入GDT中                    */  
    /*      表项n对应的地址处，                                               */  
    /* 参数:      n   GDT中表项n对应的地址                                         */  
    /*          addr 系统段的基地址，这是一个线性地址                            */  
    /*          type    描述符中类型域，具体见80386基础一节中的表格                 */  
    /*              0x89表示386TSS段描述符，0x82表示LDT段                  */  
    /*              这里8是为了设置P位为1                                         */  
    /* 返回:  (无)                                                                  */  
    /*****************************************************************************/  
// %0       寄存器eax  addr  
// %1－%6    物理地址    符号项n地址－n+7的地址  
//#define _set_tssldt_desc(n,addr,type)   
//__asm__ ("movw $104,%1\n\t" \   // 把TSS的限长104字节放入n地址处，  
//        // 这样ldt的限长也定为104，这没有关系，因为linux0.11  
//        // 中一个任务的ldt只有3个表项  
//        "movw %%ax,%2\n\t" \        // 把addr的15..0位（在ax中）放入n+2处  
//        "rorl $16,%%eax\n\t" \      // 把addr的高16位（eax中）放入ax中  
//        "movb %%al,%3\n\t" \        // addr的23..16位放入n+4中  
//        "movb $" type ",%4\n\t" \   // 把type字段放入n+5中  
//        "movb $0x00,%5\n\t" \       // 把G置为 0，说明粒度是字节。  
//        // 因为限长定死为104，所以高位肯定是0  
//        "movb %%ah,%6\n\t" \        // 把addr的31..24位放入n+7中  
//        "rorl $16,%%eax" \          // eax清0  
//        ::"a" (addr), "m" (*(n)), "m" (*(n+2)), "m" (*(n+4)), 
//        "m" (*(n+5)), "m" (*(n+6)), "m" (*(n+7))   
//        )  
//#define set_tss_desc(n,addr) _set_tssldt_desc(((char *) (n)),addr,"0x89")  
//#define set_ldt_desc(n,addr) _set_tssldt_desc(((char *) (n)),addr,"0x82") 



