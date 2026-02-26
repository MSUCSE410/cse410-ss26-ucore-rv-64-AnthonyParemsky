#include "syscall.h"
#include "defs.h"
#include "loader.h"
#include "syscall_ids.h"
#include "timer.h"
#include "trap.h"

uint64 sys_write(int fd, uint64 va, uint len)
{
	debugf("sys_write fd = %d va = %x, len = %d", fd, va, len);
	if (fd != STDOUT)
		return -1;
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	int size = copyinstr(p->pagetable, str, va, MIN(len, MAX_STR_LEN));
	debugf("size = %d", size);
	for (int i = 0; i < size; ++i) {
		console_putchar(str[i]);
	}
	return size;
}

__attribute__((noreturn)) void sys_exit(int code)
{
	exit(code);
	__builtin_unreachable();
}

uint64 sys_sched_yield()
{
	yield();
	return 0;
}

uint64 sys_gettimeofday(TimeVal *val, int _tz) // TODO: implement sys_gettimeofday in pagetable. (VA to PA)
{
	struct proc *p = curr_proc();
	//Convert virtual address to physical address
	val = (TimeVal*)useraddr(p->pagetable,(uint64)val);
	// YOUR CODE
	val->sec = 0;
	val->usec = 0;

	/* The code in `ch3` will leads to memory bugs*/

	uint64 cycle = get_cycle();
	val->sec = cycle / CPU_FREQ;
	val->usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;
	return 0;
}

// TODO: add support for mmap and munmap syscall.
// hint: read through docstrings in vm.c. Watching CH4 video may also help.
// Note the return value and PTE flags (especially U,X,W,R)

uint64 sys_mmap(uint64 start,uint64 len, int port, int flag, int fd)
{
	//DEBUG
	//printf("start: %d len: %d port: %d flag: %d fd: %d\n",start,len,port,flag,fd);
	if(len == 0)
	{
		//DEBUG
		//printf("LEN IS 0\n");
		return -1;
	}


	//Port should only have 3 least sig bits on
	//error if not
	if(port & (~7))
	{
		//DEBUG
		//printf("BAD PORT\n");
		return -1;
	}

	//rwx 
	if(port == 0)
	{
		return -1;
	}

	if(start % PGSIZE)
	{
		//Not page aligned
		return -1;
	}

	struct proc *p = curr_proc();

	//DEBUG
	//printf("BEFORE CHECKING PAGE\n");
	//Check page va's to be allocated. 
	//Are they already in use?
	for(int i = 0;i < len;i += PGSIZE)
	{
		//DEBUG
		//printf("CHECKING PAGE\n");
		//Address isn't mapped, nothing to unmap
		if (walkaddr(p->pagetable, start) != 0)
		{
			return -1;
		}
	}

	for(int i = 0;i < len;i += PGSIZE)
	{
		uint64 pa = (uint64)kalloc();
		//printf("pa: %d\n",pa);
		if(pa == 0)
		{
			//Couldn't get a page
			return -1;
		}
		uint64 alloc_size = PGSIZE;
		if((len - i) < PGSIZE)
		{
			alloc_size = len - i;
		}
		int map_ret = mappages(p->pagetable,start+i,alloc_size,pa,(port << 1) | PTE_U);
		//printf("map_ret: %d\n",map_ret);
		if(map_ret == -1)
		{
			//Couldn't allocate a pte
			return -1;
		}
	}
	return 0;
}

uint64 sys_munmap(uint64 start,uint64 len)
{
	if(len == 0)
	{
		return 0;
	}

	if(start % PGSIZE)
	{
		//Not page aligned
		return -1;
	}

	if(len % PGSIZE)
	{
		//len not page aligned
		return -1;
	}

	struct proc *p = curr_proc();
	//Address isn't mapped, nothing to unmap
	if (walkaddr(p->pagetable, start) == 0)
	{
		return -1;
	}

	uint64 page_count = ((len-1) / PGSIZE) + 1;
	uvmunmap(p->pagetable,start,page_count,1);
	return 0;
//void uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
}

//Pre declare
struct TaskInfo;
/*
* LAB1: you may need to define sys_task_info here
*/
int sys_task_info(struct TaskInfo *ti)
{
	struct proc *p = curr_proc();

	ti = (struct TaskInfo*)useraddr(p->pagetable,(uint64)ti);
	//If this was called, the process must be running
	ti->status = Running;

	//Set all syscall quantities
	for(int i = 0;i < MAX_SYSCALL_NUM;i++)
	{
		ti->syscall_times[i] = p->syscall_times[i];
	}

	//Curr time, in ms
	int curr_time = (get_cycle()*1000) / CPU_FREQ;

	ti->time = curr_time - p->start_time;
	//ti->time = CPU_FREQ; test

	printf("TaskInfo time = %d, curr_time = %d, start_time = %d\n", ti->time,curr_time,p->start_time);
	//

	return 0;
}


extern char trap_page[];

void syscall()
{
	struct trapframe *trapframe = curr_proc()->trapframe;
	int id = trapframe->a7, ret;
	uint64 args[6] = { trapframe->a0, trapframe->a1, trapframe->a2,
			   trapframe->a3, trapframe->a4, trapframe->a5 };
	tracef("syscall %d args = [%x, %x, %x, %x, %x, %x]", id, args[0],
	       args[1], args[2], args[3], args[4], args[5]);
	/*
	* LAB1: you may need to update syscall counter for task info here
	*/
	curr_proc()->syscall_times[id] += 1;
	switch (id) {
	case SYS_write:
		ret = sys_write(args[0], args[1], args[2]);
		break;
	case SYS_exit:
		sys_exit(args[0]);
		// __builtin_unreachable();
	case SYS_sched_yield:
		ret = sys_sched_yield();
		break;
	case SYS_gettimeofday:
		ret = sys_gettimeofday((TimeVal *)args[0], args[1]);
		break;
	/*
	* LAB1: you may need to add SYS_taskinfo case here
	*/
	case SYS_task_info:
		ret = sys_task_info((struct TaskInfo*)args[0]);
		break;
	case SYS_munmap:
		ret = sys_munmap((uint64)args[0],(uint64)args[1]);
		break;
	case SYS_mmap:
		ret = sys_mmap((uint64)args[0],(uint64)args[1],args[2],args[3],args[4]);
		break;
	default:
		ret = -1;
		errorf("unknown syscall %d", id);
	}
	trapframe->a0 = ret;
	tracef("syscall ret %d", ret);
}
