/*
 *  linux/kernel/fork.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  'fork.c' contains the help-routines for the 'fork' system call
 * (see also system_call.s), and some misc functions ('verify_area').
 * Fork is rather simple, once you get the hang of it, but the memory
 * management can be a bitch. See 'mm/mm.c': 'copy_page_tables()'
 */
#include <errno.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>
#include <asm/system.h>

extern void write_verify(unsigned long address);

long last_pid=0;

void verify_area(void * addr,int size)
{
	unsigned long start;

	start = (unsigned long) addr;
	size += start & 0xfff;
	start &= 0xfffff000;
    //printk("st[0x%x]sz[%d]gb[0x%x]\r\n",start,size,current->ldt[2]);
	start += get_base(current->ldt[2]);
    //printk("\r\n");
	while (size>0) {
		size -= 4096;
		write_verify(start);
		start += 4096;
	}
}

int copy_mem(int nr,struct task_struct * p)
{
	unsigned long volatile old_data_base;
    unsigned long volatile new_data_base;
    unsigned long volatile data_limit;
	unsigned long volatile old_code_base;
    unsigned long volatile new_code_base;
    unsigned long volatile code_limit;

	code_limit=get_limit(0x0f);
	data_limit=get_limit(0x17);
	old_code_base = get_base(current->ldt[1]);
	old_data_base = get_base(current->ldt[2]);
	if (old_data_base != old_code_base)
		panic("We don't support separate I&D");
	if (data_limit < code_limit)
		panic("Bad data_limit");
	new_data_base = nr * 0x4000000;
    new_code_base = nr * 0x4000000;
	p->start_code = new_code_base;
	set_base(p->ldt[1],new_code_base);
	set_base(p->ldt[2],new_data_base);
	if (copy_page_tables(old_data_base,new_data_base,data_limit)) {
		free_page_tables(new_data_base,data_limit);
		return -ENOMEM;
	}
	return 0;
}

/*
 *  Ok, this is the main fork-routine. It copies the system process
 * information (task[nr]) and sets up the necessary registers. It
 * also copies the data segment in it's entirety.
 */
int copy_process(int nr,long ebp,long edi,long esi,long gs,long none,
		long ebx,long ecx,long edx,
		long fs,long es,long ds,
		long eip,long cs,long eflags,long esp,long ss)
{
    //printk("copy_process act  int nr[%d]\nlong ebp[0x%x]\nlong edi[0x%x]\nlong esi[0x%x]\nlong gs[0x%x]\nlong none[0x%x]\n long ebx[0x%x]\nlong ecx[0x%x]\nlong edx[0x%x]\nlong fs[0x%x]\nlong es[0x%x]\nlong ds[0x%x]\n long eip[0x%x]\nlong cs[0x%x]\nlong eflags[0x%x]\nlong esp[0x%x]\nlong ss[0x%x])!\n\n\n",nr, ebp,edi,esi,gs,none,ebx,ecx,edx,fs,es,ds,eip,cs,eflags,esp,ss);
	struct task_struct *p;
	int i;
	struct file *f;

	p = (struct task_struct *) get_free_page();
	if (!p)
    {
        //printk("copy_process ERR 1!\n");
		return -EAGAIN;
    }
	
    
    task[nr] = p;
	//*p = *current; /* NOTE! this doesn't copy the supervisor stack */
    //__asm__("cld");
    
    unsigned char *p1, *p2;
    p1 = (unsigned char*)p;
    p2 = (unsigned char*)current;
    
    //重要: 结构体复制有bug, 所以用这种形式
    for(i = 0;i < sizeof(*p); i++)
    {
        *p1++ = *p2++;
    }

	p->state = TASK_UNINTERRUPTIBLE;
	p->pid = last_pid;
	p->father = current->pid;
    
    //p->counter = p->priority;
    p->counter = p->priority;
	p->signal = 0;
	p->alarm = 0;
	p->leader = 0;		/* process leadership doesn't inherit */
	p->utime = p->stime = 0;
	p->cutime = p->cstime = 0;
	p->start_time = jiffies;
	p->tss.back_link = 0;
	p->tss.esp0 = PAGE_SIZE + (long) p;
	p->tss.ss0 = 0x10;
	p->tss.eip = eip;
	p->tss.eflags = eflags;
	p->tss.eax = 0;
	p->tss.ecx = ecx;
	p->tss.edx = edx;
	p->tss.ebx = ebx;
	p->tss.esp = esp;
	p->tss.ebp = ebp;
	p->tss.esi = esi;
	p->tss.edi = edi;
	p->tss.es = es & 0xffff;
	p->tss.cs = cs & 0xffff;
	p->tss.ss = ss & 0xffff;
	p->tss.ds = ds & 0xffff;
	p->tss.fs = fs & 0xffff;
	p->tss.gs = gs & 0xffff;
	p->tss.ldt = _LDT(nr);//0x38/0011 1000  ==>errcode  0x14/0001 0100
	
    //p->tss.ldt = 0x1f;  //0x1f/0001 1111  ==>errcode  0x1C/0001 1100
    //p->tss.ldt = 0x3f;  //0x3f/0011 1111  ==>errcode  0x3C/0011 1100

	p->tss.trace_bitmap = 0x80000000;
	if (last_task_used_math == current)
		__asm__("clts ; fnsave %0"::"m" (p->tss.i387));
	if (copy_mem(nr,p)) {
		task[nr] = NULL;
		free_page((long) p);
        //printk("copy_process ERR 2!\n");
		return -EAGAIN;
	}
	for (i=0; i<NR_OPEN;i++)
    {
        f=p->filp[i];
		if (f)
        {
			f->f_count++;
        }
    }
	if (current->pwd)
		current->pwd->i_count++;
	if (current->root)
		current->root->i_count++;
	if (current->executable)
		current->executable->i_count++;
	//printk("&p->tss[0x%x]\n",&(p->tss));
	set_tss_desc(gdt+(nr<<1)+FIRST_TSS_ENTRY,&(p->tss));
	set_ldt_desc(gdt+(nr<<1)+FIRST_LDT_ENTRY,&(p->ldt));
	p->state = TASK_RUNNING;	/* do this last, just in case */
    //printk("copy_process task [%p] currpid[%ld] copied pid[%ld] counter[%ld]  cur_priority[%ld]  priority[%ld] state[%ld] signal[%ld] blocked[%ld]  \n", p,(current)->pid,(p)->pid, (p)->counter,  (current)->priority, (p)->priority, (p)->state, (p)->signal, (p)->blocked );

	//printk("ldt1[0x%x]\n",(current->ldt[1]));
	//printk("ldt2[0x%x]\n",(current->ldt[2]));
    
    //printk("back_link[0x%x]\n",p->tss.back_link);
	//printk("esp0[0x%x]\n",p->tss.esp0);
	//printk("ss0[0x%x]\n",p->tss.ss0);
	//printk("esp1[0x%x]\n",p->tss.esp1);
	//printk("ss1[0x%x]\n",p->tss.ss1);
	//printk("esp2[0x%x]\n",p->tss.esp2);
	//printk("ss2[0x%x]\n",p->tss.ss2);
	//printk("cr3[0x%x]\n",p->tss.cr3);
	//printk("eip[0x%x]\n",p->tss.eip);
	//printk("eflags[0x%x]\n",p->tss.eflags);
	//printk("eax[0x%x]\n",p->tss.eax);
    //printk("ecx[0x%x]\n",p->tss.ecx);
    //printk("edx[0x%x]\n",p->tss.edx);
    //printk("ebx[0x%x]\n",p->tss.ebx);
	//printk("esp[0x%x]\n",p->tss.esp);
	//printk("ebp[0x%x]\n",p->tss.ebp);
	//printk("esi[0x%x]\n",p->tss.esi);
	//printk("edi[0x%x]\n",p->tss.edi);
	//printk("es[0x%x]\n",p->tss.es);
	//printk("cs[0x%x]\n",p->tss.cs);
	//printk("ss[0x%x]\n",p->tss.ss);
	//printk("ds[0x%x]\n",p->tss.ds);
	//printk("fs[0x%x]\n",p->tss.fs);
	//printk("gs[0x%x]\n",p->tss.gs);
	//printk("ldt[0x%x]\n",p->tss.ldt);
	return last_pid;
}

int find_empty_process(void)
{
	int i;

	repeat:
        //设定 进程 pid
		if ((++last_pid)<0) last_pid=1;
		for(i=0 ; i<NR_TASKS ; i++)
        {
			if (task[i] && task[i]->pid == last_pid) goto repeat;
        }

	for(i=1 ; i<NR_TASKS ; i++)
    {
		if (!task[i])
        {
            //printk("find_empty_process i[%d] pid[%ld]\n", i, last_pid);
			return i;
        }
    }
	return -EAGAIN;
}
