/*
 *  linux/boot/head.s
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  head.s contains the 32-bit startup code.
 *
 * NOTE!!! Startup happens at absolute address 0x00000000, which is also where
 * the page directory will exist. The startup code will be overwritten by
 * the page directory.
 */
.code32
.text
.globl idt,gdt,pg_dir,tmp_floppy_area,startup_32 
pg_dir:
startup_32:
    movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	mov %ax,%fs
	mov %ax,%gs
	lss stack_start,%esp
	call setup_idt
	call setup_gdt
	movl $0x10,%eax		# reload all the segment registers
	mov %ax,%ds		# after changing gdt. CS was already
	mov %ax,%es		# reloaded in 'setup_gdt'
	mov %ax,%fs
	mov %ax,%gs
	lss stack_start,%esp
	xorl %eax,%eax
1:	incl %eax		# check that A20 really IS enabled
	movl %eax,0x000000	# loop forever if it isn't
	cmpl %eax,0x100000
	je 1b
/*
 * NOTE! 486 should set bit 16, to check for write-protect in supervisor
 * mode. Then it would be unnecessary with the "verify_area()"-calls.
 * 486 users probably want to set the NE (#5) bit also, so as to use
 * int 16 for math errors.
 */
	movl %cr0,%eax		# check math chip
	andl $0x80000011,%eax	# Save PG,PE,ET
/* "orl $0x10020,%eax" here for 486 might be good */
	orl $2,%eax		# set MP
	movl %eax,%cr0
	call check_x87
	jmp after_page_tables

/*
 * We depend on ET to be correct. This checks for 287/387.
 */
check_x87:
	fninit
	fstsw %ax
	cmpb $0,%al
	je 1f			/* no coprocessor: have to set bits */
	movl %cr0,%eax
	xorl $6,%eax		/* reset MP, set EM */
	movl %eax,%cr0
	ret
.align 4
1:	.byte 0xDB,0xE4		/* fsetpm for 287, ignored by 387 */
	ret

/*
 *  setup_idt
 *
 *  sets up a idt with 256 entries pointing to
 *  ignore_int, interrupt gates. It then loads
 *  idt. Everything that wants to install itself
 *  in the idt-table may do so themselves. Interrupts
 *  are enabled elsewhere, when we can be relatively
 *  sure everything is ok. This routine will be over-
 *  written by the page tables.
 */
setup_idt:
	lea ignore_int,%edx
	movl $0x00080000,%eax
	movw %dx,%ax		/* selector = 0x0008 = cs */
	movw $0x8E00,%dx	/* interrupt gate - dpl=0, present */

	lea idt,%edi
	mov $256,%ecx
rp_sidt:
	movl %eax,(%edi)
	movl %edx,4(%edi)
	addl $8,%edi
	dec %ecx
	jne rp_sidt
	lidt idt_descr
	ret

/*
 *  setup_gdt
 *
 *  This routines sets up a new gdt and loads it.
 *  Only two entries are currently built, the same
 *  ones that were built in init.s. The routine
 *  is VERY complicated at two whole lines, so this
 *  rather long comment is certainly needed :-).
 *  This routine will beoverwritten by the page tables.
 */
setup_gdt:
	lgdt gdt_descr
	ret

/*
 * I put the kernel page tables right after the page directory,
 * using 4 of them to span 16 Mb of physical memory. People with
 * more than 16MB will have to expand this.
 */
.org 0x1000
pg0:

.org 0x2000
pg1:

.org 0x3000
pg2:

.org 0x4000
pg3:

.org 0x5000
/*
 * tmp_floppy_area is used by the floppy-driver when DMA cannot
 * reach to a buffer-block. It needs to be aligned, so that it isn't
 * on a 64kB border.
 */
tmp_floppy_area:
	.fill 1024,1,0

after_page_tables:
	pushl $0		# These are the parameters to main :-)
	pushl $0
	pushl $0
	pushl $L6		# return address for main, if it decides to.
	pushl $main     /* .faq 设置开启了分页机制，为什么 ret 时，还可找到 main?  16M内存,页表初始化和线性地址是统一的 */
	jmp setup_paging
L6:
	jmp L6			# main should never return here, but
				# just in case, we know what happens.

/* This is the default interrupt "handler" :-) */
int_msg:
	.asciz "Unknown interrupt\n\r"
.align 4
ignore_int:
	pushl %eax
	pushl %ecx
	pushl %edx
	push %ds
	push %es
	push %fs
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	mov %ax,%fs
	pushl $int_msg
	call printk
	popl %eax
	pop %fs
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %eax
	iret


/*
 * Setup_paging
 *
 * This routine sets up paging by setting the page bit
 * in cr0. The page tables are set up, identity-mapping
 * the first 16MB. The pager assumes that no illegal
 * addresses are produced (ie >4Mb on a 4Mb machine).
 *
 * NOTE! Although all physical memory should be identity
 * mapped by this routine, only the kernel page functions
 * use the >1Mb addresses directly. All "normal" functions
 * use just the lower 1Mb, or the local data space, which
 * will be mapped to some other place - mm keeps track of
 * that.
 *
 * For those with more memory than 16 Mb - tough luck. I've
 * not got it, why should you :-) The source is here. Change
 * it. (Seriously - it shouldn't be too difficult. Mostly
 * change some constants etc. I left it at 16Mb, as my machine
 * even cannot be extended past that (ok, but it was cheap :-)
 * I've tried to show which constants to change by having
 * some kind of marker at them (search for "16Mb"), but I
 * won't guarantee that's all :-( )
 */
.align 4
setup_paging:
    /* pg_dir 在地址 0x0 处, 后续清空操作，为赋值准备 */
    /*清空页表目录1024(从 pg_dir 开始) 一个一级页表 + 四个二级页表*4*1024  stosl 一次处理4字节, ecx设置次数  1024*5  */
    movl $1024*5,%ecx		/* 5 pages - pg_dir+4 page tables */
	xorl %eax,%eax
	xorl %edi,%edi			/* pg_dir is at 0x000 */
	cld;rep;stosl     /*  STOSL 指令相当于将 EAX 中的值保存到 ES:EDI 指向的地址中  若设置 STD 指令, 则EDI自减 4, 没有设置，则是 CLD, EDI自加 4 */
                        /* rep 重复下一条指令到 ecx 到 0 为止*/ 

    /*设置一级页表 pg_dir 目录项,共四项(四个二级项表), 每项4字节,每项指向每个二级页表起始物理地址 */
    movl $pg0+7,pg_dir		/* set present bit/user r/w */
	movl $pg1+7,pg_dir+4		/*  --------- " " --------- */
	movl $pg2+7,pg_dir+8		/*  --------- " " --------- */
	movl $pg3+7,pg_dir+12		/*  --------- " " --------- */

    /*页表项都是 4 字节大小 按需取对应的位数值来使用 */
    /* cr0 未开启分页项 => 线性地址解析  =>  物理地址 */
    /* 开启分页时的线性地址 如下: */
    /* cr3 指定一级页表(当前线性和物理是一样的 )物理地址  =>  cr0 开启分页项 => 线性地址解析  =>   一级页表 ( 线性地址高 10位 在一级页表索引 )  -> 取一级页表目录项[32位] 
       获得 二级页表物理地址    ->(使用线性地址 中间 10位在二级页表索引 ) 取页二级页表项 获得 内存页 起始物理基地址 (32位, 其中 低 12 位在地址计算中不生效 ) +  偏移(12)  => 物理地址    */
    /*初始化页表项*/ 
    movl $pg3+4092,%edi   /* .faq 为什么要设置 这个值到 edi?  使用 std, 则 edi 从高向低处理(自减4), 设置edi初始值,  一次处理4字节，所以初始值为pg3+4092, 当前四个页表的最后四字节*/
	movl $0xfff007,%eax		/*  16Mb - 4096 + 7 (r/w user,p) .faq 表示16M里的项表项的最后一个,每个页大小是0x1000,所以后面循环设置项表项时，每次减 0x1000为每个页的首地址，放在页表项里    */
	std
1:	stosl			/* fill pages backwards - more efficient :-)   .faq 循环处理 4字节 把 eax拷贝到 edi, edi每次减4 */
	subl $0x1000,%eax /*.faq*/
	jge 1b

	xorl %eax,%eax		/* pg_dir is at 0x0000 */
	movl %eax,%cr3		/* cr3 - page directory start 设置一级页表起始地址 */
	movl %cr0,%eax          
	orl $0x80000000,%eax /* 组织开启分页标识 G? */
	movl %eax,%cr0		/* set paging (PG) bit  设置开启分页功能 */
	ret			/* this also flushes prefetch-queue */

.align 4
.word 0
idt_descr:
	.word 256*8-1		# idt contains 256 entries
	.long idt
.align 4
.word 0
gdt_descr:
	.word 256*8-1		# so does gdt (not that that's any
	.long gdt		# magic number, but it works for me :^)

	.align 8
idt:	.fill 256,8,0		# idt is uninitialized    填充 252 次 每次 8 字节，填充值为 0 */

/* .quad 定义64bit数据*/
gdt:	.quad 0x0000000000000000	/* NULL descriptor 处理器约定第一个不使用 */
    
    /* 数据段和代码段的基址是一样的，只是访问属性不一样 */
	.quad 0x00c09a0000000fff	/* 16Mb 设置数据段  */
	.quad 0x00c0920000000fff	/* 16Mb 设置代码段  */
	
    .quad 0x0000000000000000	/* TEMPORARY - don't use */
	.fill 252,8,0			/* space for LDT's and TSS's etc   填充 252 次 每次 8 字节，填充值为 0 */


