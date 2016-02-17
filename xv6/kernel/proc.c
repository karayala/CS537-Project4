#include "types.h"
#include "defs.h"
#include "param.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;
  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
  release(&ptable.lock);

  // Allocate kernel stack if possible.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;
  
  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;
  
  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];
  
  p = allocproc();
  acquire(&ptable.lock);
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;
  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  
  // critical section ahead
  acquire(&ptable.lock);

  sz = proc->sz;
  if(n > 0){
    if((sz = allocuvm(proc->pgdir, sz, sz + n)) == 0) {
      return -1;
    }
  } else if(n < 0) {
    if((sz = deallocuvm(proc->pgdir, sz, sz + n)) == 0) {
      return -1;
    }
  }

  //TODO: Test Logic here.
  proc->sz = sz;
  struct proc *p = ptable.proc;
  while(p < &ptable.proc[NPROC]){
      if(p->parent == proc && p->pgdir == proc->pgdir)p->sz = proc->sz;
      p++;
  }
  release(&ptable.lock);
  
  switchuvm(proc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;

  // Allocate process.
  if((np = allocproc()) == 0)
    return -1;

  // Copy process state from p.
  if((np->pgdir = copyuvm(proc->pgdir, proc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = proc->sz;
  np->parent = proc;
  *np->tf = *proc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(proc->ofile[i])
      np->ofile[i] = filedup(proc->ofile[i]);
  np->cwd = idup(proc->cwd);
 
  pid = np->pid;
  np->state = RUNNABLE;
  safestrcpy(np->name, proc->name, sizeof(proc->name));
  return pid;
}

//-------------------------------------------------------------------
// clone() creates a new kernel thread by copying a previous process. 
// most of clone is copied over from fork(), with the exception of some of the
// state-copying code, and with a change that'll allow the new thread to work
// in its parents address space. the new thread should state executing at
// mem-addr specified by fcn.  
int
clone(void(*fcn)(void*), void *arg, void *stack) {
  int i, pid;
  struct proc *np;
  void *argPtr; //Holds the address of the argument within stack
  void *retPtr; //Holds the address of return within the stack (fake return).


  // Allocate process
  if((np = allocproc()) == 0)
    return -1;

  // removed the process state copying section of fork()

  // point the new process's page directory to its parent's. This'll
  // allow us to work in the same address space. Address spaces shared in clone() for threads.
  np->pgdir = proc->pgdir;
  // copy proc information
  np->stack = stack;   // copy the argument stack
  np->sz = proc->sz;   // set size of proc based off of its parent proc.
  np->parent = proc;   // tell the thread where its parent is. 
  *np->tf = *proc->tf; // copy the trap frame of the parent proc.
  //STACK ALLOCATION:*****************************
  //Stack stores the return address and the arguments of the argument.
  retPtr = stack + 4096 - 2 * sizeof(void*);	 //Points to where in the stack the return pointer is stored at. 
  argPtr = stack + 4096 - sizeof(void*); //Points to location in stack where the argument being passed in is stored at
  *(uint *)retPtr = 0xFFFFFFFF; //Address for return (return PC for main() call)
  // cprintf("*registry Pointer is %d\n", regPtr);
  *(uint *)argPtr = (uint)arg; //Address for the Argument.
  // cprintf("*Argument Pointer is %d\n", argPtr); //TODO: Remove all tester comments 
  //**********************************************

  //REGISTER ALLOCATION**********************************************
  np->tf->eax = 0;// Clear %eax so that fork return 0 in the child. 
  np->tf->esp = (int)stack; //point esp to top of current stack.
  memmove((void *)np->tf->esp, stack, PGSIZE); // move over desired memory to save contents of %esp.
  uint newESP = PGSIZE - 2 * sizeof(void *);   //This is the address of the new %esp. It's 1 page size ahead (but before the return address and the argument). 
  np->tf->esp = np->tf->esp + newESP; //Adding on the new size of the esp to the existing esp. 
  np->tf->ebp = np->tf->esp;  //Set Base pointer to the stack pointer (this will be restored). //TODO:
  np->tf->eip = (uint)fcn;    //Instruction Pointer points to the current instruction (fcn)
  // cprintf("*stack param evaluates to %d\n", stack);
  //****************************************************************
 
  // word for word copied from fork()
  for(i = 0; i < NOFILE; i++) {
    if(proc->ofile[i]){
      np->ofile[i] = filedup(proc->ofile[i]); 
    }
  }
  np->cwd = idup(proc->cwd);

  // word for word copied from fork()
  pid = np->pid;
  np->state = RUNNABLE; // place the thread in the runnable q so it can do the thing
  safestrcpy(np->name, proc->name, sizeof(proc->name));
  return pid;
}
//-------------------------------------------------------------------


//-------------------------------------------------------------------
// wait for a child thread in the same address space as the calling process. 
// on execution, we'll return the PID of the child, or we'll return -1 if there were
// no children that fit. We'll copy the location of the childs stack into the 
// argument stack.
int
join(void **stack) {
  struct proc *p;
  int havekids, pid;

  acquire(&ptable.lock);
  for(;;) {
    // scan through the table, find all of my children
    havekids = 0; 
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
      if(p->parent != proc || p->pgdir != proc->pgdir){ //TODO TEST 
	continue;
      }
      havekids = 1; 
      if(p->state == ZOMBIE) {
	// if we reach this point, I have found a child that is ready to be joined
	pid = p->pid;
	*stack = p->stack;
	kfree(p->kstack);
        p->kstack = 0;
        // freevm(p->pgdir); Thread suicide.
	p->state = UNUSED;
	p->pid = 0;
	p->parent = 0;
	p->name[0] = 0;
	p->killed = 0; // "He's dead Jim!"
	release(&ptable.lock);
	return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || proc->killed) {
      release(&ptable.lock);
      return -1; // return with an error code
    }
    
    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(proc, &ptable.lock); //DOC: wait-sleep
  }
}
//-------------------------------------------------------------------

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *p;
  int fd;

  if(proc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(proc->ofile[fd]){
      fileclose(proc->ofile[fd]);
      proc->ofile[fd] = 0;
    }
  }

  iput(proc->cwd);
  proc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(proc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == proc){
      p->parent = initproc;
      if(p->state == ZOMBIE) {
        wakeup1(initproc);
      }
    }

    // We need to compensate for threads in their own proc. This is the only section
    // of exit() we'll modify. Please don't fuck with anything else in exit(), or
    // the terrorists will win. Ok, maybe not, but it'll crash and idk how to fix it
    if(p->parent == proc && p->pgdir == proc->pgdir) {
      p->pgdir = copyuvm(proc->pgdir, proc->sz);
      p->parent = initproc;
      if(p->state == ZOMBIE) wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  proc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;

  acquire(&ptable.lock);
  for(;;){
    // scan through the table, find all of my children
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != proc || p->pgdir == proc->pgdir){ //Accointing for threads
        continue;
      }
      havekids = 1;
      if(p->state == ZOMBIE){
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
	// I removed the freevm(p->pgdir) instruction. We don't want a single
	// thread to be able to free the memory for every other threads pgdir! -P
        p->state = UNUSED;
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || proc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(proc, &ptable.lock);  //DOC: wait-sleep
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;

  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      proc = p;
      switchuvm(p);
      p->state = RUNNING;
      swtch(&cpu->scheduler, proc->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      proc = 0;
    }
    release(&ptable.lock);

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state.
void
sched(void)
{
  int intena;

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(cpu->ncli != 1)
    panic("sched locks");
  if(proc->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = cpu->intena;
  swtch(&proc->context, cpu->scheduler);
  cpu->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  proc->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);
  
  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  if(proc == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }

  // Go to sleep.
  proc->chan = chan;
  proc->state = SLEEPING;
  sched();

  // Tidy up.
  proc->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];
  
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}


