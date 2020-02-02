// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

// struct {
//   struct spinlock lock;
//   struct run *freelist;
// } kmem;

struct kmem_cpu{
  struct spinlock lock;
  struct run *freelist;
};

struct kmem_cpu mem_cpus[NCPU];

int getcpuid() 
{
  push_off();
  int hart = cpuid();
  pop_off();
  return hart;
}

void acquire_my_lock() 
{
  int id = getcpuid();
  acquire(&mem_cpus[id].lock);
}

void release_my_lock() 
{
  int id = getcpuid();
  release(&mem_cpus[id].lock);
}

void
kinit()
{
  for (int i = 0; i < NCPU; i++) {
    initlock(&mem_cpus[i].lock, "kmem");
  }
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
    
  printf("Done free range in cpu %d.\n", getcpuid());
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  push_off();
  int id = cpuid();
  
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;
  
  acquire(&mem_cpus[id].lock);
  // critical section to store to own cpu free list
  r->next = mem_cpus[id].freelist;
  mem_cpus[id].freelist = r;
  release(&mem_cpus[id].lock);
  
  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  push_off();
  int id = cpuid();
  
  struct run *r;
  
  acquire(&mem_cpus[id].lock);
  
  r = mem_cpus[id].freelist;
  if(r) {
    mem_cpus[id].freelist = r->next;
  } 
  // We have to release the lock here
  // if we hold the lock and try to steal other cpu's freelist,
  // we will get deadlock!
  // ex: CPU a holds lock A, to grab lock B, 
  // CPU 2 holds B, try to grab lock A.
  release(&mem_cpus[id].lock);
  
  if (!r) {
    // We need to borrow from other cpu's free list.
    //printf("CPU %d needs to borrow memory\n", id);
    for (int i =0; i < NCPU; i++) {
      if (i == id) {
        continue;
      }
      acquire(&mem_cpus[i].lock);
      struct run *f = mem_cpus[i].freelist;
      if (f) {
        //printf("CPU %d got free memory from CPU %d\n", id, i);
        r = f;
        mem_cpus[i].freelist = f->next;
      }
      release(&mem_cpus[i].lock);
      if (r)
        break;
    }
  }

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
    
  pop_off();
  return (void*)r;
}
