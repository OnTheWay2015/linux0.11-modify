/*
 *  linux/kernel/system_call.s
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  system_call.s  contains the system-call low-level handling routines.
 * This also contains the timer-interrupt handler, as some of the code is
 * the same. The hd- and flopppy-interrupts are also here.
 *
 * NOTE: This code handles signal-recognition, which happens every time
 * after a timer-interrupt and after each system call. Ordinary interrupts
 * don't handle signal-recognition, as that would clutter them up totally
 * unnecessarily.
 *
 * Stack layout in 'ret_from_system_call':
 *
 *	 0(%esp) - %eax
 *	 4(%esp) - %ebx
 *	 8(%esp) - %ecx
 *	 C(%esp) - %edx
 *	10(%esp) - %fs
 *	14(%esp) - %es
 *	18(%esp) - %ds
 *	1C(%esp) - %eip
 *	20(%esp) - %cs
 *	24(%esp) - %eflags
 *	28(%esp) - %oldesp
 *	2C(%esp) - %oldss
 */

SIG_CHLD	= 17

EAX		= 0x00
EBX		= 0x04
ECX		= 0x08
EDX		= 0x0C
FS		= 0x10
ES		= 0x14
DS		= 0x18
EIP		= 0x1C
CS		= 0x20
EFLAGS		= 0x24
OLDESP		= 0x28
OLDSS		= 0x2C

state	= 0		# these are offsets into the task-struct.
counter	= 4
priority = 8
signal	= 12
sigaction = 16		# MUST be 16 (=len of sigaction)
blocked = (33*16)

# offsets within sigaction
sa_handler = 0
sa_mask = 4
sa_flags = 8
sa_restorer = 12

nr_system_calls = 72

/*
 * Ok, I get parallel printer interrupts while using the floppy for some
 * strange reason. Urgel. Now I just ignore them.
 */
.globl system_call,sys_fork,timer_interrupt,sys_execve
.globl hd_interrupt,floppy_interrupt,parallel_interrupt
.globl device_not_available, coprocessor_error

.align 2
bad_sys_call:
	movl $-1,%eax
	iret
.align 2
reschedule:
	pushl $ret_from_sys_call
	jmp schedule
	
	
# int 0x80 --linux 系统调用入口点(调用中断int 0x80，eax 中是调用号)。  	
.align 2
system_call:
    cmpl $nr_system_calls-1,%eax # 调用号如果超出范围的话就在eax 中置-1 并退出。  
	ja bad_sys_call
    push %ds # 保存原段寄存器值。  
	push %es
	push %fs
    pushl %edx # ebx,ecx,edx 中放着系统调用相应的C 语言函数的调用参数。  
	pushl %ecx		# push %ebx,%ecx,%edx as parameters
	pushl %ebx		# to the system call
	movl $0x10,%edx		# set up ds,es to kernel space    .faq 为什么是 0x10
    mov %dx,%ds # ds,es 指向内核数据段(全局描述符表中数据段描述符)。  
	mov %dx,%es
	movl $0x17,%edx		# fs points to local data space  .faq 为什么是0x17
    mov %dx,%fs # fs 指向局部数据段(局部描述符表中数据段描述符)。  

#Intel语法的间接内存引用的格式为：
#       section:[base+index*scale+displacement]
#而在AT&T语法中对应的形式为：
#       section:displacement(base,index,scale)

 # 下面这句操作数的含义是：调用地址 = _sys_call_table + %eax * 4。参见列表后的说明。  
    # 对应的C 程序中的sys_call_table 在include/linux/sys.h 中，其中定义了一个包括72 个  
    # 系统调用C 处理函数的地址数组表。  
    call sys_call_table(,%eax,4)  #sys_call_table 在 include/linux/sys.h 里定义,可调用的方法表
	pushl %eax  # 把系统调用号入栈。 
	movl current,%eax
    # 查看当前任务的运行状态。如果不在就绪状态(state 不等于0)就去执行调度程序。  
    # 如果该任务在就绪状态但counter[??]值等于0，则也去执行调度程序。  
	cmpl $0,state(%eax)		# state
	jne reschedule
	cmpl $0,counter(%eax)		# counter
	je reschedule
    # 以下这段代码执行从系统调用C 函数返回后，对信号量进行识别处理。  
ret_from_sys_call:
    # 首先判别当前任务是否是初始任务task0，如果是则不必对其进行信号量方面的处理，直接返回。  
    # task 对应C 程序中的task[]数组，直接引用task 相当于引用task[0]。  
	movl current,%eax		# task[0] cannot have signals
	cmpl task,%eax
    je 3f # 向前(forward)跳转到标号3。  
    # 通过对原调用程序代码选择符的检查来判断调用程序是否是超级用户。如果是超级用户就直接  
    # 退出中断，否则需进行信号量的处理。这里比较选择符是否为普通用户代码段的选择符0x000f  
    # (RPL=3，局部表，第1 个段(代码段))，如果不是则跳转退出中断程序。  
	cmpw $0x0f,CS(%esp)		# was old code segment supervisor ?
	jne 3f
    # 如果原堆栈段选择符不为0x17（也即原堆栈不在用户数据段中），则也退出。  
	cmpw $0x17,OLDSS(%esp)		# was stack segment = 0x17 ?
	jne 3f
    # 下面这段代码（109-120）的用途是首先取当前任务结构中的信号位图(32 位，每位代表1 种信号)，  
    # 然后用任务结构中的信号阻塞（屏蔽）码，阻塞不允许的信号位，取得数值最小的信号值，再把  
    # 原信号位图中该信号对应的位复位（置0），最后将该信号值作为参数之一调用do_signal()。  
    # do_signal()在（kernel/signal.c,82）中，其参数包括13 个入栈的信息。  
    movl signal(%eax),%ebx # 取信号位图??ebx，每1 位代表1 种信号，共32 个信号。  
    movl blocked(%eax),%ecx # 取阻塞（屏蔽）信号位图??ecx。  
    notl %ecx # 每位取反。  
    andl %ebx,%ecx # 获得许可的信号位图。  
    bsfl %ecx,%ecx # 从低位（位0）开始扫描位图，看是否有1 的位，  
    # 若有，则ecx 保留该位的偏移值（即第几位0-31）。  
    je 3f # 如果没有信号则向前跳转退出。  
    btrl %ecx,%ebx # 复位该信号（ebx 含有原signal 位图）。  
    movl %ebx,signal(%eax) # 重新保存signal 位图信息??current->signal。  
    incl %ecx # 将信号调整为从1 开始的数(1-32)。  
    pushl %ecx # 信号值入栈作为调用do_signal 的参数之一。  
    call do_signal # 调用C 函数信号处理程序(kernel/signal.c,82)  
    popl %eax # 弹出信号值。  
3:	popl %eax
	popl %ebx
	popl %ecx
	popl %edx
	pop %fs
	pop %es
	pop %ds
	iret

.align 2
coprocessor_error:
	push %ds
	push %es
	push %fs
	pushl %edx
	pushl %ecx
	pushl %ebx
	pushl %eax
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax
	mov %ax,%fs
	pushl $ret_from_sys_call
	jmp math_error

.align 2
device_not_available:
	push %ds
	push %es
	push %fs
	pushl %edx
	pushl %ecx
	pushl %ebx
	pushl %eax
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax
	mov %ax,%fs
	pushl $ret_from_sys_call
	clts				# clear TS so that we can use math
	movl %cr0,%eax
	testl $0x4,%eax			# EM (math emulation bit)
	je math_state_restore
	pushl %ebp
	pushl %esi
	pushl %edi
	call math_emulate
	popl %edi
	popl %esi
	popl %ebp
	ret

.align 2
timer_interrupt:
	push %ds		# save ds,es and put kernel data space
	push %es		# into them. %fs is used by system_call
	push %fs
	pushl %edx		# we save %eax,%ecx,%edx as gcc doesn't
	pushl %ecx		# save those across function calls. %ebx
	pushl %ebx		# is saved as we use that in ret_sys_call
	pushl %eax
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax
	mov %ax,%fs
	incl jiffies
	movb $0x20,%al		# EOI to interrupt controller #1
	outb %al,$0x20
	movl CS(%esp),%eax
	andl $3,%eax		# %eax is CPL (0 or 3, 0=supervisor)
	pushl %eax
	call do_timer		# 'do_timer(long CPL)' does everything from
	addl $4,%esp		# task switching to accounting ...
	jmp ret_from_sys_call

.align 2
sys_execve:
	lea EIP(%esp),%eax
	pushl %eax
	call do_execve
	addl $4,%esp
	ret

.align 2
sys_fork:
	call find_empty_process
	testl %eax,%eax
	js 1f
	push %gs
	pushl %esi
	pushl %edi
	pushl %ebp
	pushl %eax
	call copy_process
	addl $20,%esp
1:	ret

hd_interrupt:
	pushl %eax
	pushl %ecx
	pushl %edx
	push %ds
	push %es
	push %fs
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax
	mov %ax,%fs
	movb $0x20,%al
	outb %al,$0xA0		# EOI to interrupt controller #1
	jmp 1f			# give port chance to breathe
1:	jmp 1f
1:	xorl %edx,%edx
	xchgl do_hd,%edx
	testl %edx,%edx
	jne 1f
	movl $unexpected_hd_interrupt,%edx
1:	outb %al,$0x20
	call *%edx		# "interesting" way of handling intr.
	pop %fs
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %eax
	iret

floppy_interrupt:
	pushl %eax
	pushl %ecx
	pushl %edx
	push %ds
	push %es
	push %fs
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax
	mov %ax,%fs
	movb $0x20,%al
	outb %al,$0x20		# EOI to interrupt controller #1
	xorl %eax,%eax
	xchgl do_floppy,%eax
	testl %eax,%eax
	jne 1f
	movl $unexpected_floppy_interrupt,%eax
1:	call *%eax		# "interesting" way of handling intr.
	pop %fs
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %eax
	iret

parallel_interrupt:
	pushl %eax
	movb $0x20,%al
	outb %al,$0x20
	popl %eax
	iret
