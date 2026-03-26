#include "syscall.h"
#include "console.h"
#include "defs.h"
#include "loader.h"
#include "syscall_ids.h"
#include "timer.h"
#include "trap.h"

uint64 console_write(uint64 va, uint64 len)
{
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	int size = copyinstr(p->pagetable, str, va, MIN(len, MAX_STR_LEN));
	tracef("write size = %d", size);
	for (int i = 0; i < size; ++i) {
		console_putchar(str[i]);
	}
	return len;
}

uint64 console_read(uint64 va, uint64 len)
{
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	tracef("read size = %d", len);
	for (int i = 0; i < len; ++i) {
		int c = consgetc();
		str[i] = c;
	}
	copyout(p->pagetable, va, str, len);
	return len;
}

uint64 sys_write(int fd, uint64 va, uint64 len)
{
	if (fd < 0 || fd > FD_BUFFER_SIZE)
		return -1;
	struct proc *p = curr_proc();
	struct file *f = p->files[fd];
	if (f == NULL) {
		errorf("invalid fd %d\n", fd);
		return -1;
	}
	switch (f->type) {
	case FD_STDIO:
		return console_write(va, len);
	case FD_INODE:
		return inodewrite(f, va, len);
	default:
		panic("unknown file type %d\n", f->type);
	}
}

uint64 sys_read(int fd, uint64 va, uint64 len)
{
	if (fd < 0 || fd > FD_BUFFER_SIZE)
		return -1;
	struct proc *p = curr_proc();
	struct file *f = p->files[fd];
	if (f == NULL) {
		errorf("invalid fd %d\n", fd);
		return -1;
	}
	switch (f->type) {
	case FD_STDIO:
		return console_read(va, len);
	case FD_INODE:
		return inoderead(f, va, len);
	default:
		panic("unknown file type %d\n", f->type);
	}
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

uint64 sys_gettimeofday(uint64 val, int _tz)
{
	struct proc *p = curr_proc();
	uint64 cycle = get_cycle();
	TimeVal t;
	t.sec = cycle / CPU_FREQ;
	t.usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;
	copyout(p->pagetable, val, (char *)&t, sizeof(TimeVal));
	return 0;
}

uint64 sys_getpid()
{
	return curr_proc()->pid;
}

uint64 sys_getppid()
{
	struct proc *p = curr_proc();
	return p->parent == NULL ? IDLE_PID : p->parent->pid;
}

uint64 sys_clone()
{
	debugf("fork!");
	return fork();
}

static inline uint64 fetchaddr(pagetable_t pagetable, uint64 va)
{
	uint64 *addr = (uint64 *)useraddr(pagetable, va);
	return *addr;
}

uint64 sys_exec(uint64 path, uint64 uargv)
{
	struct proc *p = curr_proc();
	char name[MAX_STR_LEN];
	copyinstr(p->pagetable, name, path, MAX_STR_LEN);
	uint64 arg;
	static char strpool[MAX_ARG_NUM][MAX_STR_LEN];
	char *argv[MAX_ARG_NUM];
	int i;
	for (i = 0; uargv && (arg = fetchaddr(p->pagetable, uargv));
	     uargv += sizeof(char *), i++) {
		copyinstr(p->pagetable, (char *)strpool[i], arg, MAX_STR_LEN);
		argv[i] = (char *)strpool[i];
	}
	argv[i] = NULL;
	return exec(name, (char **)argv);
}

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
		return -1;
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

uint64 sys_wait(int pid, uint64 va)
{
	struct proc *p = curr_proc();
	int *code = (int *)useraddr(p->pagetable, va);
	return wait(pid, code);
}

uint64 sys_spawn(uint64 va)
{
	//Create child process (similar to fork)
	struct proc *np;
	struct proc *p = curr_proc();
	//errorf("val1: %d\n", va);
	// Allocate process.
	if ((np = allocproc()) == 0) {
		panic("allocproc\n");
	}
	// Cause fork to return 0 in the child.
	np->trapframe->a0 = 0;
	np->parent = p;
	np->state = RUNNABLE;
	add_task(np);

	char path[200];
	copyinstr(p->pagetable,path,va,200);

	infof("exec : %s\n", path);
	struct inode *ip;
	//errorf("Path: %s\n",path);
	if ((ip = namei(path)) == 0) {
		errorf("invalid file name %s\n", path);
		return -1;
	}
	bin_loader(ip, np);
	iput(ip);
	//return push_argv(p, 0);
	return 0;

}

uint64 sys_set_priority(long long prio)
{
	//DEBUG
	//printf("prio: %d max: %d",prio,INT_MAX);
	if(prio < 2 || prio > INT_MAX)
	{
		return -1;
	}
	struct proc *p = curr_proc();
	p->prio = prio;
	return prio;
}

uint64 sys_openat(uint64 va, uint64 omode, uint64 _flags)
{
	struct proc *p = curr_proc();
	char path[200];
	copyinstr(p->pagetable, path, va, 200);
	return fileopen(path, omode);
}

uint64 sys_close(int fd)
{
	if (fd < 0 || fd > FD_BUFFER_SIZE)
		return -1;
	struct proc *p = curr_proc();
	struct file *f = p->files[fd];
	if (f == NULL) {
		errorf("invalid fd %d", fd);
		return -1;
	}
	fileclose(f);
	p->files[fd] = 0;
	return 0;
}

int sys_fstat(int fd,uint64 stat){
	struct proc *p = curr_proc();
	struct file *f = p->files[fd];
	//invalid fd
	if (f == NULL) {
		errorf("invalid fd %d", fd);
		return -1;
	}
	struct Stat* st = (struct Stat*)useraddr(p->pagetable,(uint64)stat);
	//invalid stat
	if(st == 0)
	{
		return -1;
	}

	st->dev = f->ip->dev;
	st->ino = f->ip->inum;
	if(f->ip->type == T_DIR)
	{
		st->mode = 0x040000;
	}
	else if(f->ip->type == T_FILE)
	{
		st->mode = 0x100000;
	}
	else
	{
		st->mode = 0;
	}
	st->nlink = f->ip->nlink;
	return 0;
}

int sys_linkat(int olddirfd, uint64 oldpath, int newdirfd, uint64 newpath, uint64 flags){

	struct proc *p = curr_proc();
	oldpath = useraddr(p->pagetable,(uint64)oldpath);
	newpath = useraddr(p->pagetable,(uint64)newpath);
	//int test = strncmp((char*)oldpath,(char*)newpath,DIRSIZ);
	//errorf("strncmp: %d\n",test);
	if(!strncmp((char*)oldpath,(char*)newpath,DIRSIZ))
	{
		return -1;
	}
	struct inode* ip = namei((char*)oldpath);
	ip->nlink += 1;
	//iput(ip);
	struct inode* dp = root_dir();
	//errorf("IP3: %d\n",ip);
	return dirlink(dp,(char*)newpath,ip->inum);
}

int sys_unlinkat(int dirfd, uint64 name, uint64 flags){
	struct proc *p = curr_proc();
	name = useraddr(p->pagetable,(uint64)name);
	struct inode* dp = root_dir();
	struct inode* ip = dirlookup(dp,(char*)name, 0);
	if(ip == 0)
	{
		return -1;
	}
	return dirunlink(dp,(char*)name,ip->inum);
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
	curr_proc()->syscall_times[id] += 1;
	switch (id) {
	case SYS_write:
		ret = sys_write(args[0], args[1], args[2]);
		break;
	case SYS_read:
		ret = sys_read(args[0], args[1], args[2]);
		break;
	case SYS_openat:
		ret = sys_openat(args[0], args[1], args[2]);
		break;
	case SYS_close:
		ret = sys_close(args[0]);
		break;
	case SYS_exit:
		sys_exit(args[0]);
		// __builtin_unreachable();
	case SYS_sched_yield:
		ret = sys_sched_yield();
		break;
	case SYS_setpriority:
		ret = sys_set_priority(args[0]);
		break;
	case SYS_gettimeofday:
		ret = sys_gettimeofday(args[0], args[1]);
		break;
	case SYS_getpid:
		ret = sys_getpid();
		break;
	case SYS_getppid:
		ret = sys_getppid();
		break;
	case SYS_clone: // SYS_fork
		ret = sys_clone();
		break;
	case SYS_execve:
		ret = sys_exec(args[0], args[1]);
		break;
	case SYS_wait4:
		ret = sys_wait(args[0], args[1]);
		break;
	case SYS_fstat:
	    ret = sys_fstat(args[0],args[1]);
		break;
	case SYS_linkat:
	    ret = sys_linkat(args[0],args[1],args[2],args[3],args[4]);
		break;
	case SYS_unlinkat:
	    ret = sys_unlinkat(args[0],args[1],args[2]);
		break;
	case SYS_spawn:
		ret = sys_spawn(args[0]);
		break;
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
