/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 *   kernel/task.c
 *   Author: Naredula Janardhana Reddy  (naredula.jana@gmail.com, naredula.jana@yahoo.com)
 *
 */
#define DEBUG_ENABLE 1
#include "common.h"
//#include "task.h"
#include "interface.h"
#include "descriptor_tables.h"
#define MAGIC_CHAR 0xab
#define MAGIC_LONG 0xabababababababab

struct task_struct *g_current_tasks[MAX_CPUS];
struct task_struct *g_idle_tasks[MAX_CPUS];
struct mm_struct *g_kernel_mm = 0;

static queue_t run_queue;
static queue_t task_queue;
static queue_t timer_queue;

static spinlock_t sched_lock = SPIN_LOCK_UNLOCKED;
unsigned long g_pid = 0;
unsigned long g_jiffies = 0; /* increments for every 10ms =100HZ = 100 cycles per second  */

static struct task_struct *g_task_dead = 0; /* TODO : remove me later */



extern long *g_idle_stack;
void init_timer();
static int free_mm(struct mm_struct *mm);
static unsigned long push_to_userland();
static unsigned long _schedule(unsigned long flags);
static struct task_struct *alloc_task_struct(void) {
	return (struct task_struct *) mm_getFreePages(0, 2); /*WARNING: do not change the size it is related TASK_SIZE, 4*4k=16k page size */
}

static void free_task_struct(struct task_struct *p) {
	mm_putFreePages((unsigned long) p, 2);
	return;
}
/************************************************
  All the below function should be called with holding lock
  *********************************************** */
static inline int _add_to_runqueue(struct task_struct * p) /* Add at the first */
{
	if (p->magic_numbers[0] != MAGIC_LONG || p->magic_numbers[1] != MAGIC_LONG) /* safety check */
	{
		DEBUG(
				" Task Stack Got CORRUPTED task:%x :%x :%x \n", p, p->magic_numbers[0], p->magic_numbers[1]);
		BUG();
	}
	if (p->run_link.next == 0 && p->cpu == 0xffff) { /* Avoid adding self adding in to runqueue */
		list_add_tail(&p->run_link, &run_queue.head);
		return 1;
	}
#if 0
	if (g_current_tasks[0] == p || g_current_tasks[1] == p) //SAFE CHECK
	{
		BUG();
	}
#endif
	//BUG();
	return 0;
}

static inline struct task_struct *_get_from_runqueue() {
	struct task_struct *p;
	struct list_head *node;

	if (run_queue.head.next == &run_queue.head) {
		return 0;
	}
	node = run_queue.head.next;
	p = list_entry(node,struct task_struct, run_link);
	if (p == 0)
		BUG();
	return p;
}
static inline struct task_struct *_del_from_runqueue(struct task_struct * p) {
	if (p == 0) {
		struct list_head *node;

		node = run_queue.head.next;
		if (run_queue.head.next == &run_queue.head) {
			return 0;
		}

		p = list_entry(node,struct task_struct, run_link);
		if (p == 0)
			BUG();
	}
	list_del(&p->run_link);
	return p;
}

/******************************************  WAIT QUEUE *******************/
static void inline init_waitqueue(queue_t *waitqueue, char *name) {
	INIT_LIST_HEAD(&(waitqueue->head));
	waitqueue->name = name;
	return;
}

static void _add_to_waitqueue(queue_t *waitqueue, struct task_struct * p, long ticks) {

	long cum_ticks, prev_cum_ticks;
	struct list_head *pos, *prev_pos;
	struct task_struct *task;
	unsigned long flags;

	cum_ticks = 0;
	prev_cum_ticks = 0;
	if (waitqueue->head.next == &waitqueue->head) {
		p->sleep_ticks = ticks;
		list_add_tail(&p->wait_queue, &waitqueue->head);
	} else {
		prev_pos = &waitqueue->head;
		list_for_each(pos, &waitqueue->head) {
			task = list_entry(pos, struct task_struct, wait_queue);
			prev_cum_ticks = cum_ticks;
			cum_ticks = cum_ticks + task->sleep_ticks;

			if (cum_ticks > ticks) {
				p->sleep_ticks = (ticks - prev_cum_ticks);
				task->sleep_ticks = task->sleep_ticks - p->sleep_ticks;
				list_add(&p->wait_queue, prev_pos);
				goto last;
			}
			prev_pos = pos;
		}
		p->sleep_ticks = (ticks - cum_ticks);
		list_add_tail(&p->wait_queue, &waitqueue->head);
	}

last: ;

}

/* delete from the  wait queue */
static int _del_from_waitqueue(queue_t *waitqueue, struct task_struct *p) {
	struct list_head *pos;
	struct task_struct *task;
	int ret = 0;
	unsigned long flags;

	if (p==0 || waitqueue==0) return 0;

	list_for_each(pos, &waitqueue->head) {
		task = list_entry(pos, struct task_struct, wait_queue);

		if (p == task) {
			pos = pos->next;
			if (pos != &waitqueue->head) {
				task = list_entry(pos, struct task_struct, wait_queue);
				task->sleep_ticks = task->sleep_ticks + p->sleep_ticks;
			}
			p->sleep_ticks = 0;
			ret = 1;
			list_del(&p->wait_queue);//TODO crash happening */
			return 1;
		}
	}

	return ret;
}

#define MAX_WAIT_QUEUES 50
static queue_t *wait_queues[MAX_WAIT_QUEUES]; /* TODO : this need to be locked */
int stat_wq_count = 0;
int sc_register_waitqueue(queue_t *waitqueue, char *name) {
	int i;
	unsigned long flags;

	spin_lock_irqsave(&sched_lock, flags);
	for (i = 0; i < MAX_WAIT_QUEUES; i++) {
		if (wait_queues[i] == 0) {
			init_waitqueue(waitqueue, name);
			wait_queues[i] = waitqueue;
			stat_wq_count++;
			goto last;
		}
	}
last:
    spin_unlock_irqrestore(&sched_lock, flags);
	return -1;
}
/* TODO It should be called from all the places where sc_register_waitqueue is called, currently it is unregister is called only from few places*/
int sc_unregister_waitqueue(queue_t *waitqueue) {
	int i;
	unsigned long flags;

	spin_lock_irqsave(&sched_lock, flags);
	for (i = 0; i < MAX_WAIT_QUEUES; i++) {
		if (wait_queues[i] == waitqueue) {
			/*TODO:  remove the tasks present in the queue */
			wait_queues[i] = 0;
			stat_wq_count--;
			goto last;
		}
	}
last:
    spin_unlock_irqrestore(&sched_lock, flags);
	return -1;
}
int sc_wakeUp(queue_t *waitqueue ) {
	int ret = 0;
	struct task_struct *task;

	unsigned long flags;
	if (waitqueue == NULL)
		waitqueue = &timer_queue;

	spin_lock_irqsave(&sched_lock, flags);
	while (waitqueue->head.next != &waitqueue->head) {
		task = list_entry(waitqueue->head.next, struct task_struct, wait_queue);
		if (_del_from_waitqueue(waitqueue, task) == 1) {
			task->state = TASK_RUNNING;
			if (task->run_link.next == 0 )
				_add_to_runqueue(task);
			ret++;
		}
	}
	spin_unlock_irqrestore(&sched_lock, flags);
	if (ret>0){/* wakeup any other idle cpus to serve the runqueue  */
		int i;
		for (i=0; i<getmaxcpus(); i++){
			if (g_current_task->cpu != i && g_current_tasks[i]==g_idle_tasks[i]){
				apic_send_ipi_vector(i,IPI_INTERRUPT);
				return ret;
			}
		}
	}

	return ret;
}
int sc_wait(queue_t *waitqueue, unsigned long ticks) {
	unsigned long flags;

	spin_lock_irqsave(&sched_lock, flags);
	g_current_task->state = TASK_INTERRUPTIBLE;
	_add_to_waitqueue(waitqueue, g_current_task, ticks);
	spin_unlock_irqrestore(&sched_lock, flags);

	sc_schedule();
	if (g_current_task->sleep_ticks <= 0)
		return 0;
	else
		return g_current_task->sleep_ticks;
}
unsigned long sc_sleep(long ticks) /* each tick is 100HZ or 10ms */
/* TODO : return number ticks elapsed  instead of 1*/
/* TODO : when multiple user level thread sleep it is not returning in correct time , it may be because the idle thread halting*/
{
	unsigned long flags;

	spin_lock_irqsave(&sched_lock, flags);
	g_current_task->state = TASK_INTERRUPTIBLE;
	_add_to_waitqueue(&timer_queue, g_current_task, ticks);
	spin_unlock_irqrestore(&sched_lock, flags);

	sc_schedule();
	return 1;
}
int Jcmd_threadlist_stat(char *arg1, char *arg2) {
	unsigned long flags;
	struct list_head *pos;
	struct task_struct *task;
	int i;

	ut_printf("pid state task ticks mm pgd mm_count cpu\n");
	spin_lock_irqsave(&sched_lock, flags);
	list_for_each(pos, &task_queue.head) {
		task = list_entry(pos, struct task_struct, task_link);
		if (is_kernelThread(task))
			ut_printf("[%d]", task->pid);
		else
			ut_printf("(%d)", task->pid);

		ut_printf("%d %x %x %x %x %d %s %d stat(%d) %d\n",task->state, task, task->ticks, task->mm, task->mm->pgd, task->mm->count.counter, task->name, task->sleep_ticks,
				task->stats.ticks_consumed,task->cpu);
	}
	for (i = 0; i < MAX_WAIT_QUEUES; i++) {
		if (wait_queues[i] == 0) continue;
		ut_printf(" %s: ",wait_queues[i]->name);
		list_for_each(pos, &wait_queues[i]->head) {
			task = list_entry(pos, struct task_struct, wait_queue);
			ut_printf(" %d(%s),",task->pid,task->name);
		}
		ut_printf("\n");
	}
	for (i=0; i<getmaxcpus(); i++){
		if (g_current_tasks[i]==g_idle_tasks[i])
			ut_printf("%d : <idle>\n",i);
		else
			ut_printf("%d : %s(%d)\n",i,g_current_tasks[i]->name,g_current_tasks[i]->pid);
	}
	spin_unlock_irqrestore(&sched_lock, flags);
	return 1;
}

static unsigned long setup_userstack(unsigned char **argv, unsigned char **env, unsigned long *stack_len, unsigned long *t_argc, unsigned long *t_argv, unsigned long *p_aux) {
	int i, len, total_args = 0;
	int total_envs =0;
	unsigned char *p, *stack;
	unsigned long real_stack, addr;
	unsigned char *target_argv[12];
	unsigned char *target_env[12];

	if (argv == 0 && env == 0) {
		ut_printf(" ERROR  argv:0\n");
		return 0;
	}
	stack = (unsigned char *) mm_getFreePages(MEM_CLEAR, 0);
	p = stack + PAGE_SIZE;
	len = 0;
	real_stack = USERSTACK_ADDR + USERSTACK_LEN;

	for (i = 0; argv[i] != 0 && i < 10; i++) {
		total_args++;
		len = ut_strlen(argv[i]);
		if ((p - len - 1) > stack) {
			p = p - len - 1;
			real_stack = real_stack - len - 1;
			DEBUG(" argument :%d address:%x \n",i,real_stack);
			ut_strcpy(p, argv[i]);
			target_argv[i] = real_stack;
		} else {
			goto error;
		}
	}
	target_argv[i] = 0;

	for (i = 0; env[i] != 0 && i < 10; i++) {
		total_envs++;
		len = ut_strlen(env[i]);
		if ((p - len - 1) > stack) {
			p = p - len - 1;
			real_stack = real_stack - len - 1;
			DEBUG(" envs :%d address:%x \n",i,real_stack);
			ut_strcpy(p, env[i]);
			target_env[i] = real_stack;
		} else {
			goto error;
		}
	}
	target_env[i] = 0;

	addr = p;
	addr = (addr / 8) * 8;
	p = addr;

	p = p - (MAX_AUX_VEC_ENTRIES * 16);

	real_stack = USERSTACK_ADDR + USERSTACK_LEN + p - (stack + PAGE_SIZE);
	*p_aux = p;
	len = (1+total_args + 1 + total_envs+1) * 8; /* total_args+args+0+envs+0 */
	if ((p - len - 1) > stack) {
		unsigned long *t;

		p = p - (total_envs+1)*8;
		ut_memcpy(p, target_env, (total_envs+1)*8);

		p = p - (1+total_args+1)*8;
		ut_memcpy(p+8, target_argv, (total_args+1)*8);
		t = p;
		*t = total_args; /* store argc at the top of stack */

		DEBUG(" arg0:%x arg1:%x arg2:%x len:%d \n",target_argv[0],target_argv[1],target_argv[2],len);
		DEBUG(" arg0:%x arg1:%x arg2:%x len:%d \n",target_env[0],target_env[1],target_env[2],len);

		real_stack = real_stack - len;
	} else {
		goto error;
	}

	*stack_len = PAGE_SIZE - (p - stack);
	*t_argc = total_args;
	*t_argv = real_stack ;
	return stack;

	error: mm_putFreePages(stack, 0);
	return 0;
}
void SYS_sc_execve(unsigned char *file, unsigned char **argv, unsigned char **env) {
	struct file *fp;
	struct mm_struct *mm,*old_mm;
	unsigned long main_func;
	struct user_regs *p;
	unsigned long *tmp;
	unsigned long t_argc, t_argv, stack_len, tmp_stack, tmp_aux;

	SYSCALL_DEBUG("execve file:%s argv:%x env:%x \n",file,argv,env);
	/* create the argc and env in a temporray stack before we destory the old stack */
	t_argc = 0;
	t_argv = 0;
	tmp_stack = setup_userstack(argv, env, &stack_len, &t_argc, &t_argv, &tmp_aux);
	if (tmp_stack == 0) {
		return;
	}
	/* delete old vm and create a new one */

	mm = kmem_cache_alloc(mm_cachep, 0);
	if (mm == 0)
		BUG();
	ut_memset(mm,0,sizeof(struct mm_struct)); /* clear the mm struct */
	atomic_set(&mm->count,1);
	mm->pgd = 0;
	mm->mmap = 0;
	mm->fs.total = 3;
	ar_pageTableCopy(g_kernel_mm, mm); /* every process page table should have soft links to kernel page table */
	old_mm=g_current_task->mm;
	g_current_task->mm = mm;
	free_mm(old_mm);
	flush_tlb(mm->pgd);

	/* populate vm with vmaps */
	fp = fs_open(file, 0, 0);
	if (fp == 0) {
		ut_printf("Error :execve Failed to open the file :%s\n", file);
		return;
	}
	main_func = fs_loadElfLibrary(fp, tmp_stack + (PAGE_SIZE - stack_len), stack_len, tmp_aux);
	if (main_func == 0) {
		mm_putFreePages(tmp_stack, 0);
		return ;
	}
	mm_putFreePages(tmp_stack, 0);
	ut_strncpy(g_current_task->name, file, MAX_TASK_NAME);
	//Jcmd_vmaps_stat(0, 0);

	g_current_task->thread.userland.ip = main_func;
	g_current_task->thread.userland.sp = t_argv;
	g_current_task->thread.userland.argc = t_argc;
	g_current_task->thread.userland.argv = t_argv;

	g_current_task->thread.userland.user_stack = USERSTACK_ADDR + USERSTACK_LEN;
	g_current_task->thread.userland.user_ds = 0;
	g_current_task->thread.userland.user_es = 0;
	g_current_task->thread.userland.user_gs = 0;
	g_current_task->thread.userland.user_fs = 0;

	g_current_task->thread.userland.user_fs_base = 0;
	ar_updateCpuState(g_current_task);
	push_to_userland();
}

struct user_regs {
	struct gpregs gpres;
	struct intr_stack_frame isf;
};
#define DEFAULT_RFLAGS_VALUE (0x10202)
#define GLIBC_PASSING 1
extern void enter_userspace();
static unsigned long push_to_userland() {
	struct user_regs *p;
	int cpuid=getcpuid();
	DEBUG(" from PUSH113_TO_USERLAND :%d\n",cpuid);
	/* From here onwards DO NOT  call any function that consumes stack */
	asm("cli");
	asm("movq %%rsp,%0" : "=m" (p));
	p = p - 1;
	asm("subq $0xa0,%rsp");
	p->gpres.rbp = 0;
#ifndef GLIBC_PASSING
	p->gpres.rsi=g_current_task->thread.userland.sp; /* second argument to main i.e argv */
	p->gpres.rdi=g_current_task->thread.userland.argc; /* first argument argc */
#else
	p->gpres.rsi = 0;
	p->gpres.rdi = 0;
#endif
	p->gpres.rdx = 0;
	p->gpres.rbx = p->gpres.rcx = 0;
	p->gpres.rax = p->gpres.r10 = 0;
	p->gpres.r11 = p->gpres.r12 = 0;
	p->gpres.r13 = p->gpres.r14 = 0;
	p->gpres.r15 = p->gpres.r9 = 0;
	p->gpres.r8 = 0;
	p->isf.rip = g_current_task->thread.userland.ip;
	p->isf.rflags = DEFAULT_RFLAGS_VALUE;
	p->isf.rsp = g_current_task->thread.userland.sp;
	p->isf.cs = GDT_SEL(UCODE_DESCR) | SEG_DPL_USER;
	p->isf.ss = GDT_SEL(UDATA_DESCR) | SEG_DPL_USER;

	g_cpu_state[cpuid].user_fs = 0;
	g_cpu_state[cpuid].user_gs = 0;
	g_cpu_state[cpuid].user_fs_base = 0;

	enter_userspace();
}
static int free_mm(struct mm_struct *mm) {
	int i, total_fds;

	DEBUG("freeing the mm :%x counter:%x \n",mm,mm->count.counter);

	if (mm->count.counter == 0)
		BUG();
	atomic_dec(&mm->count);
	if (mm->count.counter > 0)
		return 0;

	vm_munmap(mm, 0, 0xffffffff);
	ar_pageTableCleanup(mm, 0, 0xfffffffff);
	for (i = 3; i < mm->fs.total; i++) {
		DEBUG("FREEing the files :%d \n",i);
		fs_close(mm->fs.filep[i]);
	}
	mm->fs.total = 0;
	kmem_cache_free(mm_cachep, mm);
	return 1;
}
#define CLONE_VM 1
unsigned long sc_createKernelThread(int(*fn)(void *), unsigned char *args, unsigned char *thread_name) {
	unsigned long pid;
	unsigned long flags;
	struct list_head *pos;
	struct task_struct *task;

	pid = SYS_sc_clone(fn, 0, CLONE_VM, args);

	if (thread_name == 0)
		return pid;
	spin_lock_irqsave(&sched_lock, flags);
	list_for_each(pos, &task_queue.head) {
		task = list_entry(pos, struct task_struct, task_link);
		if (task->pid == pid) {
			ut_strncpy(task->name, thread_name, MAX_TASK_NAME);
			goto last;
		}
	}
last:
	spin_unlock_irqrestore(&sched_lock, flags);

	return pid;
}
static void schedule_secondHalf(){
	spin_unlock_irqrestore(&sched_lock, g_current_task->flags);
	g_current_task->thread.real_ip(0);
}
unsigned long SYS_sc_clone(int(*fn)(void *), void *child_stack, int clone_flags, void *args) {
	struct task_struct *p;
	struct mm_struct *mm;
	unsigned long flags;

	SYSCALL_DEBUG("clone fn:%x child_stack:%x flags:%x args:%x \n",fn,child_stack,clone_flags,args);
	/* Initialize the stack  */
	p = alloc_task_struct();
	if (p == 0)
		BUG();
	ut_memset((unsigned char *) p, MAGIC_CHAR, TASK_SIZE);
	/* Initialize mm */
	if (clone_flags | CLONE_VM) /* parent and child run in the same vm */
	{
		mm = g_current_task->mm;
		atomic_inc(&mm->count);
		DEBUG("clone  CLONE_VM the mm :%x counter:%x \n",mm,mm->count.counter);
	} else {
		int i;
		mm = kmem_cache_alloc(mm_cachep, 0);
		if (mm == 0)
			BUG();
		atomic_set(&mm->count,1);
		DEBUG("clone  the mm :%x counter:%x \n",mm,mm->count.counter);
		mm->pgd = 0;
		for (i=0; i<MAX_FDS;i++)
			mm->fs.filep[i]=0;
		mm->fs.total = 3;
		ar_pageTableCopy(g_current_task->mm, mm);
		mm->mmap = 0;
	}
	/* initialize task struct */
	p->mm = mm;
	p->ticks = 0;
	p->pending_signal = 0;
	p->cpu = getcpuid();
	if (g_current_task->mm != g_kernel_mm) { /* user level thread */
		p->thread.userland.ip = fn;
		p->thread.userland.sp = child_stack;
		p->thread.userland.argc = 0;/* TODO */
		p->thread.userland.argv = 0; /* TODO */
		BRK;// TODO need to add the unlock to push_to_userland
		p->thread.ip = (void *) push_to_userland;
	} else { /* kernel level thread */
		p->thread.ip = (void *) schedule_secondHalf;
		save_flags(p->flags);
		p->thread.argv = args;
		p->thread.real_ip =fn;
	}
	p->thread.sp = (addr_t) p + (addr_t) TASK_SIZE - (addr_t)160; /* 160 bytes are left at the bottom of the stack */
	p->state = TASK_RUNNING;
	ut_strncpy(p->name, g_current_task->name, MAX_TASK_NAME);

	/* link to queue */
	g_pid++;
	if (g_pid==0) g_pid++;
	p->pid = g_pid;
	p->cpu =0xffff;
	p->ppid = g_current_task->pid;
	p->run_link.next = 0;
	p->run_link.prev = 0;
	p->task_link.next = 0;
	p->task_link.prev = 0;
	p->wait_queue.next = p->wait_queue.prev = NULL;

	p->thread.userland.user_fs = 0;
	p->thread.userland.user_fs_base = 0;

	p->stats.ticks_consumed = 0;

	spin_lock_irqsave(&sched_lock, flags);
	list_add_tail(&p->task_link, &task_queue.head);
	if (_add_to_runqueue(p)==0) {
		BUG();
	}
	spin_unlock_irqrestore(&sched_lock, flags);

	return p->pid;
}
unsigned long SYS_sc_fork() {
	SYSCALL_DEBUG("fork \n");
	return SYS_sc_clone(0, 0, CLONE_VM, 0);
}
int SYS_sc_exit(int status) {
	unsigned long flags;
	SYSCALL_DEBUG("sys exit : status:%d \n",status);
	ar_updateCpuState(g_current_task);

	spin_lock_irqsave(&sched_lock, flags);
	g_current_task->state = TASK_DEAD;
	spin_unlock_irqrestore(&sched_lock, flags);

	sc_schedule();
	return 0;
}
int SYS_sc_kill(unsigned long pid, unsigned long signal) {
	unsigned long flags;
	struct list_head *pos;
	struct task_struct *task;

	SYSCALL_DEBUG("kill pid:%d signal:%d \n",pid,signal);

	spin_lock_irqsave(&sched_lock, flags);
	list_for_each(pos, &task_queue.head) {
		task = list_entry(pos, struct task_struct, task_link);
		if (task->pid == pid) {
			task->pending_signal = 1;
			break;
		}
	}
	spin_unlock_irqrestore(&sched_lock, flags);
	return 1;
}
/******************* schedule related functions **************************/

static struct task_struct *__switch_to(struct task_struct *prev_p, struct task_struct *next_p) {
	return prev_p;
}
#ifdef ARCH_X86_64
#define __STR(x) #x
#define STR(x) __STR(x)

#define __PUSH(x) "pushq %%" __STR(x) "\n\t"
#define __POP(x)  "popq  %%" __STR(x) "\n\t"

#define SAVE_CONTEXT \
	__PUSH(rsi) __PUSH(rdi) \
__PUSH(r12) __PUSH(r13) __PUSH(r14) __PUSH(r15)  \
__PUSH(rdx) __PUSH(rcx) __PUSH(r8) __PUSH(r9) __PUSH(r10) __PUSH(r11)  \
__PUSH(rbx) __PUSH(rbp) 
#define RESTORE_CONTEXT \
	__POP(rbp) __POP(rbx) \
__POP(r11) __POP(r10) __POP(r9) __POP(r8) __POP(rcx) __POP(rdx) \
__POP(r15) __POP(r14) __POP(r13) __POP(r12) \
__POP(rdi) __POP(rsi)
#define switch_to(prev, next, last)              \
	do {                                                                    \
		/*                                                              \
		 * Context-switching clobbers all registers, so we clobber      \
		 * them explicitly, via unused output variables.                \
		 * (EAX and EBP is not listed because EBP is saved/restored     \
		 * explicitly for wchan access and EAX is the return value of   \
		 * __switch_to())                                               \
		 */                                                             \
		unsigned long rbx, rcx, rdx, rsi, rdi;                          \
		\
		asm volatile("pushfq\n\t"               /* save    flags */     \
				SAVE_CONTEXT \
				"pushq %%rbp\n\t"          /* save    EBP   */     \
				"movq %%rsp,%[prev_sp]\n\t"        /* save    ESP   */ \
				"movq %[next_sp],%%rsp\n\t"        /* restore ESP   */ \
				"movq $1f,%[prev_ip]\n\t"  /* save    EIP   */     \
				"pushq %[next_ip]\n\t"     /* restore EIP   */     \
				"jmp __switch_to\n"        /* regparm call  */     \
				"1:\t"                                             \
				"popq %%rbp\n\t"           /* restore EBP   */     \
				RESTORE_CONTEXT \
				"popfq\n"                  /* restore flags */     \
				\
				/* output parameters */                            \
				: [prev_sp] "=m" (prev->thread.sp),                \
				[prev_ip] "=m" (prev->thread.ip),                \
				"=a" (last),                                     \
				\
				/* clobbered output registers: */                \
				"=b" (rbx), "=c" (rcx), "=d" (rdx),              \
				"=S" (rsi), "=D" (rdi)                           \
		\
		\
		/* input parameters: */                          \
		: [next_sp]  "m" (next->thread.sp),                \
		[next_ip]  "m" (next->thread.ip),                \
		\
		/* regparm parameters for __switch_to(): */      \
		[prev]     "a" (prev),                           \
		[next]     "d" (next)                            \
		\
		\
		: /* reloaded segment registers */                 \
		"memory");                                      \
	} while (0)                      
#else
#endif
void ipi_interrupt(){ /* Do nothing, this is just wake up the core when it is executing HLT instruction  */

}
void init_tasking() {
	int i;
	unsigned long task_addr;

	g_kernel_mm = kmem_cache_alloc(mm_cachep, 0);
	if (g_kernel_mm == 0)
		return;
	for (i = 0; i < MAX_WAIT_QUEUES; i++) {
		wait_queues[i] = 0;
	}

	INIT_LIST_HEAD(&(run_queue.head));
	INIT_LIST_HEAD(&(task_queue.head));
	sc_register_waitqueue(&timer_queue,"timer");

	atomic_set(&g_kernel_mm->count,1);
	g_kernel_mm->mmap = 0x0;
	g_kernel_mm->pgd = (unsigned char *) g_kernel_page_dir;
	g_kernel_mm->fs.total = 3;

    task_addr=(unsigned long )((unsigned long )(&g_idle_stack)+TASK_SIZE) & (~((unsigned long )(TASK_SIZE-1)));
    ut_printf(" Task Addr start :%x  stack:%x current:%x\n",task_addr,&task_addr,g_current_task);
	for (i = 0; i < MAX_CPUS; i++) {
		g_idle_tasks[i] = (unsigned char *)(task_addr)+i*TASK_SIZE;
		g_idle_tasks[i]->ticks = 0;
		g_idle_tasks[i]->magic_numbers[0] = g_idle_tasks[i]->magic_numbers[1] = MAGIC_LONG;
		g_idle_tasks[i]->stats.ticks_consumed = 0;
		g_idle_tasks[i]->state = TASK_RUNNING;
		g_idle_tasks[i]->pid = 0;  /* only idle tasks will have id==0 */
		g_idle_tasks[i]->cpu = i;
		g_idle_tasks[i]->mm = g_kernel_mm; /* TODO increse the corresponding count */
		g_current_tasks[i] = g_idle_tasks[i];
		ut_strncpy(g_idle_tasks[i]->name, "idle", MAX_TASK_NAME);
		g_pid++;
	}

    g_current_task->cpu=0;

	ar_archSetUserFS(0);
//	init_timer(); //currently apic timer is in use
	ar_registerInterrupt(IPI_INTERRUPT, &ipi_interrupt, "IPI");
}
static void _delete_task(struct task_struct *task) {
	if (task ==0) return;
	list_del(&task->wait_queue);
	list_del(&task->run_link);
	list_del(&task->task_link);

	free_mm(task->mm);
	free_task_struct(task);
}
#ifdef SMP
/* getcpuid func is defined in smp code */
#else
int getcpuid(){
	return 0;
}
int getmaxcpus(){
	return 1;
}
#endif

void sc_schedule() {
	unsigned long intr_flags;
	int cpuid=getcpuid();

	if (!g_current_task) {
		BUG();
		return;
	}

	if (g_current_task->cpu!=cpuid || g_current_task->magic_numbers[0] != MAGIC_LONG || g_current_task->magic_numbers[1] != MAGIC_LONG) /* safety check */
	{
		DEBUG(" Task Stack got CORRUPTED task:%x :%x :%x \n",g_current_task,g_current_task->magic_numbers[0],g_current_task->magic_numbers[1]);
		BUG();
	}

	spin_lock_irqsave(&sched_lock, intr_flags);
	intr_flags=_schedule(intr_flags);
	spin_unlock_irqrestore(&sched_lock, intr_flags);

	if (1)
	{
		struct task_struct *task=0;
		spin_lock_irqsave(&sched_lock, intr_flags);
		if (g_task_dead!=0) {
			task=g_task_dead;
			g_task_dead=0;
		}else{
			task=0;
		}
		spin_unlock_irqrestore(&sched_lock, intr_flags);
		if (task != 0){
			_delete_task(task);
		}
	}
}
void sc_check_signal() {

	/* Handle any pending signal */
	if (g_current_task->pending_signal == 0) {
		return;
	}
	if (is_kernelThread(g_current_task)) {
		ut_printf(" WARNING: kernel thread cannot be killed \n");
	} else {
		g_current_task->state = TASK_DEAD;
	}
	g_current_task->pending_signal = 0;
    sc_schedule();
}

static unsigned long  _schedule(unsigned long flags) {
	struct task_struct *prev, *next;
	int cpuid=getcpuid();


	g_current_task->ticks++;
	prev = g_current_task;

	next = _del_from_runqueue(0);
	if (next == 0 && (prev->state!=TASK_RUNNING))
		next = g_idle_tasks[cpuid];

	if (next ==0 ) /* by this point , we will always have some next */
		next = prev;

	g_current_tasks[cpuid] = next;
#ifdef SMP   // SAFE Check
	if (g_current_tasks[0]==g_current_tasks[1]){
		while(1);
	}
#endif



	/* if prev and next are same then return */
	if (prev == next) {
		return flags;
	}
	/* if  prev and next are having same address space , then avoid tlb flush */
	next->counter = 5; /* 50 ms time slice */
	if (prev->mm->pgd != next->mm->pgd) /* TODO : need to make generic */
	{
		flush_tlb(next->mm->pgd);
	}
	if (prev->state == TASK_DEAD) {
		if (g_task_dead != NULL){
			BUG();
		}
		g_task_dead = prev;
	}else if (prev->pid!=0 && prev->state==TASK_RUNNING){ /* some other cpu  can pickup this task , running task and idle task should not be in a run equeue even though there state is running */
		if (prev->run_link.next != 0){ /* Prev should not be on the runqueue */
			BUG();
		}
		list_add_tail(&prev->run_link, &run_queue.head);
    }
	next->cpu=cpuid; /* get cpuid based on this */
	/* update the cpu state  and tss state for system calls */
	ar_updateCpuState(next);
	ar_setupTssStack((unsigned long) next + TASK_SIZE);


	prev->flags = flags;
	prev->cpu=0xffff;
	/* finally switch the task */
	switch_to(prev, next, prev);
	/* from the next statement onwards should not use any stack variables, new threads launched will not able see next statements*/
	return g_current_task->flags;
}

void do_softirq() {
	if (g_current_task->counter <= 0) {
		sc_schedule();
	}
}

void timer_callback(registers_t regs) {
	int i;
	unsigned long flags;

	/* 1. increment timestamp */
	if (getcpuid()==0){
	    g_jiffies++;
	}
	g_current_task->counter--;
	g_current_task->stats.ticks_consumed++;

	spin_lock_irqsave(&sched_lock, flags);

	/* 2. Test of wait queues for any expiry. time queue is one of the wait queue  */
	for (i = 0; i < MAX_WAIT_QUEUES; i++) {
		if (wait_queues[i] == 0)
			continue;
		if (wait_queues[i]->head.next != &(wait_queues[i]->head)) {
			struct task_struct *task;
			task = list_entry(wait_queues[i]->head.next, struct task_struct, wait_queue);

			task->sleep_ticks--;
			if (task->sleep_ticks <= 0) {
				_del_from_waitqueue(wait_queues[i], task);
				task->state = TASK_RUNNING;
				if (task->run_link.next == 0)
					_add_to_runqueue(task);
				else
					BUG();
			}
		}
	}
	spin_unlock_irqrestore(&sched_lock, flags);
	do_softirq();
}


