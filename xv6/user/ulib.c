#include "types.h"
#include "stat.h"
#include "fcntl.h"
#include "user.h"
#include "x86.h"
#define PGSIZE 4096

char*
strcpy(char *s, char *t)
{
  char *os;

  os = s;
  while((*s++ = *t++) != 0)
    ;
  return os;
}

int
strcmp(const char *p, const char *q)
{
  while(*p && *p == *q)
    p++, q++;
  return (uchar)*p - (uchar)*q;
}

uint
strlen(char *s)
{
  int n;

  for(n = 0; s[n]; n++)
    ;
  return n;
}

void*
memset(void *dst, int c, uint n)
{
  stosb(dst, c, n);
  return dst;
}

char*
strchr(const char *s, char c)
{
  for(; *s; s++)
    if(*s == c)
      return (char*)s;
  return 0;
}

char*
gets(char *buf, int max)
{
  int i, cc;
  char c;

  for(i=0; i+1 < max; ){
    cc = read(0, &c, 1);
    if(cc < 1)
      break;
    buf[i++] = c;
    if(c == '\n' || c == '\r')
      break;
  }
  buf[i] = '\0';
  return buf;
}

int
stat(char *n, struct stat *st)
{
  int fd;
  int r;

  fd = open(n, O_RDONLY);
  if(fd < 0)
    return -1;
  r = fstat(fd, st);
  close(fd);
  return r;
}

int
atoi(const char *s)
{
  int n;

  n = 0;
  while('0' <= *s && *s <= '9')
    n = n*10 + *s++ - '0';
  return n;
}

void*
memmove(void *vdst, void *vsrc, int n)
{
  char *dst, *src;
  
  dst = vdst;
  src = vsrc;
  while(n-- > 0)
    *dst++ = *src++;
  return vdst;
}


// new definitions

// wrapper for clone, this'll allow user threads to clone themselves.
int
thread_create(void(*fcn)(void *), void *arg) {
  // create a new stack and allocate space for it.
  void *stack =  malloc(2 * PGSIZE); //TODO: Define PGSIZE later instead of using 4096.
  
  //MAKE SURE STACK IS PAGE-ALLIGNED:
    //To do this, a differentiator is created (an indicator of how far off/how far set
    //the stack currently is by moding the stack's address by 4096.
    //Once this is done, that amount is added to 4096 (amount of a full stack).
    //Before this is doen, 
  //*********************************
    uint pageDiff = (uint)stack%PGSIZE;
    if(pageDiff){stack = stack + (PGSIZE - pageDiff);} //This if statement prevents thread leaks. 
  //********************************* 
  
  // return the cloned thread.
  return (clone(fcn, arg, stack));
}

// wrapper for join() syscall.
int
thread_join() {
  void *stack;
  int joinRet = join(&(stack));
  free(stack);
  return joinRet;
}

// lock initialization function
void
lock_init(lock_t *lock) {
  lock->flag = 0; // set flag to 0 since we only want one active hold on lock
}

// acquire function for lock, uses atomic exchange to ensure mutex
void
lock_acquire(lock_t *lock) {
  while(xchg((volatile uint*)&lock->flag, 1) == 1); 
}

// release function for lock
void
lock_release(lock_t *lock) {
  lock->flag = 0;
}
