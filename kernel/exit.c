/*
 *  linux/kernel/exit.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>
#include <signal.h>
#include <sys/wait.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <asm/segment.h>

int sys_pause(void);
int sys_close(int fd);

void release(struct task_struct * p)
{
	int i;

	if (!p)
		return;
	for (i=1 ; i<NR_TASKS ; i++)
		if (task[i]==p) {
			task[i]=NULL;
			free_page((long)p);
			schedule();
			return;
		}
	panic("trying to release non-existent task");
}

static int send_sig(long sig,struct task_struct * p,int priv)
{
	if (!p || sig<1 || sig>32)
		return -EINVAL;
	if (priv || (current->euid==p->euid) || suser())
		p->signal |= (1<<(sig-1));
	else
		return -EPERM;
	return 0;
}

static void kill_session(void)
{
	struct task_struct **p = NR_TASKS + task;
	
	while (--p > &FIRST_TASK) {
		if (*p && (*p)->session == current->session)
			(*p)->signal |= 1<<(SIGHUP-1);
	}
}

/*
 * XXX need to check permissions needed to send signals to process
 * groups, etc. etc.  kill() permissions semantics are tricky!
 */
int sys_kill(int pid,int sig)
{
	struct task_struct **p = NR_TASKS + task;
	int err, retval = 0;

    if (!pid){
        while (--p > &FIRST_TASK) {
            if (*p && (*p)->pgrp == current->pid)
            {
                err=send_sig(sig,*p,1);
                if (err)
                {
                    retval = err;
                }
            }
        } 
    }
    else if (pid>0)
    {
        while (--p > &FIRST_TASK) {
            if (*p && (*p)->pid == pid)
            {
                err=send_sig(sig,*p,0);
                if (err)
                {
                    retval = err;
                }
            }
        } 
    } 
    else if (pid == -1) 
    {
        while (--p > &FIRST_TASK)
        {
            err = send_sig(sig,*p,0);
            if (err)
            {
                retval = err;
            }
        }
    }
	else 
    {
        while (--p > &FIRST_TASK)
        {
            if (*p && (*p)->pgrp == -pid)
            {
                err = send_sig(sig,*p,0);
                if (err)
                {
                    retval = err;
                }
            }
        }
    } 
        
	return retval;
}

static void tell_father(int pid)
{
	int i;

	if (pid)
		for (i=0;i<NR_TASKS;i++) {
			if (!task[i])
				continue;
			if (task[i]->pid != pid)
				continue;
			task[i]->signal |= (1<<(SIGCHLD-1));
			return;
		}
/* if we don't find any fathers, we just release ourselves */
/* This is not really OK. Must change it to make father 1 */
	printk("BAD BAD - no father found\n\r");
	release(current);
}

int do_exit(long code)
{
	int i;

	free_page_tables(get_base(current->ldt[1]),get_limit(0x0f));
	free_page_tables(get_base(current->ldt[2]),get_limit(0x17));
	for (i=0 ; i<NR_TASKS ; i++)
		if (task[i] && task[i]->father == current->pid) {
			task[i]->father = 1;
			if (task[i]->state == TASK_ZOMBIE)
				/* assumption task[1] is always init */
				(void) send_sig(SIGCHLD, task[1], 1);
		}
	for (i=0 ; i<NR_OPEN ; i++)
		if (current->filp[i])
			sys_close(i);
	iput(current->pwd);
	current->pwd=NULL;
	iput(current->root);
	current->root=NULL;
	iput(current->executable);
	current->executable=NULL;
	if (current->leader && current->tty >= 0)
		tty_table[current->tty].pgrp = 0;
	if (last_task_used_math == current)
		last_task_used_math = NULL;
	if (current->leader)
		kill_session();
	current->state = TASK_ZOMBIE;
	current->exit_code = code;
	tell_father(current->father);
	schedule();
	return (-1);	/* just to suppress warnings */
}

int sys_exit(int error_code)
{
	return do_exit((error_code&0xff)<<8);
}


//成功执行时，返回状态改变的子进程标识。失败返回-1；如果指定WNOHANG标志，同时pid指定的进程状态没有发生变化，将返回0
//系统调用 waitpid(), 挂起当前进程，直到 pid 指定的子进程退出（终止）或者收到要求终止该进程的信号，或者是需要调用一个信号
//句柄（信号处理程序 ）。如果pid所指的子进程早已退出(已成所谓的僵死进程)，则本调用将立即返回。子进程使用的所有资源奖释放。
// pid > 0, 表示等待进程号等于 pid 的子进程
// pid=0 ,  表示等待进程组号等于当前进程组号的任何子进程
// pid < -1,表示等待进程组号等于 pid 绝对值的任何子进程
// pid =-1, 表示任何子进程
// options = WUNTRACED, 表示如果子进程是停止的，也马上返回 (无须跟踪)
// options = WNOHANG, 表示如果没有子进程退出或终止就马上返回
//  如果返回状态指针 *stat_addr 不为空,刚就奖状态信息保存到那里。
//  @pid, 进程号
//  @stat_addr 保存状态信息位置的指针
//  @options, waitpid 选项


// main() =>wait()=> waitpid(-1,wait_stat,0) => sys_waitpid()
int sys_waitpid(pid_t pid,unsigned long * stat_addr, int options)
{
	int flag, code;
	struct task_struct ** p;

	verify_area(stat_addr,4);
    //printk("1\n");
repeat:
	flag=0;
    //从任务组末端开始遍历，跳过空项，本进程项及非当前进程的子进程项。
        //printk("2\n");
	for(p = &LAST_TASK ; p > &FIRST_TASK ; --p) {
		if (!*p || *p == current)
			continue;
        //printk("3 p[%d]cur[%d]\n",(*p)->pid, current->pid);
		if ((*p)->father != current->pid)
			continue;
		if (pid>0) {
			if ((*p)->pid != pid)
				continue;
		} else if (!pid) {
			if ((*p)->pgrp != current->pgrp)
				continue;
		} else if (pid != -1) {
			if ((*p)->pgrp != -pid)
				continue;
		}
        //printk("5\n");
		switch ((*p)->state) {
			case TASK_STOPPED:
				if (!(options & WUNTRACED))
					continue;
				put_fs_long(0x7f,stat_addr);
				return (*p)->pid;
			case TASK_ZOMBIE:
				current->cutime += (*p)->utime;
				current->cstime += (*p)->stime;
				flag = (*p)->pid;
				code = (*p)->exit_code;
				release(*p);
				put_fs_long(code,stat_addr);
				return flag;
			default:
				flag=1;
				continue;
		}
        //printk("8\n");
	}
    //printk("9 f[%d]\n",flag);
	if (flag) {
		if (options & WNOHANG)
			return 0;
		current->state=TASK_INTERRUPTIBLE;
		schedule();
		if (!(current->signal &= ~(1<<(SIGCHLD-1))))
			goto repeat;
		else
			return -EINTR;
	}
	return -ECHILD;
}


