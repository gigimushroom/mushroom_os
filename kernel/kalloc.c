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

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

#define total_pages_num 32691
static int refc[32692];

int get_ref_index(void *pa)
{
  if ((char*)pa < end) {
    return -1;
  }
  char *e = (char*)PGROUNDDOWN((uint64)pa);
  char *s = (char*)PGROUNDUP((uint64)end);
  int index = (e - s) / PGSIZE;
  return index;
}

int get_ref_count(void *pa)
{
  int index = get_ref_index(pa);
  //printf("PA(%p) index(%d) ref count(%d)\n", pa, index, refc[index]);
  if (index == -1) {
    // access kernel pages, return 0
    return 0;
  }
  return refc[index];
}

void add_ref(void *pa) {
  int index = get_ref_index(pa);
  if (index == -1) {
    return;
  }
  refc[index] = refc[index] + 1;
  //printf("Add ref:index %d, ref c: %d\n", index, refc[index]);
}

void dec_ref(void *pa) {
  int index = get_ref_index(pa);
  if (index == -1) {
    return;
  }
  int cur_count = refc[index];
  if (cur_count <= 0) {
    panic("def a freed page!");
  }
  refc[index] = cur_count - 1;
  if (refc[index] == 0) {
    // we need to free page
    //printf("Dec ref: index %d, ref c: %d\n", index, refc[index]);
    kfree(pa);
  }
}

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
  memset(refc, 0, sizeof(refc));
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  int size = 0;
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    kfree(p);
    size++;
  }
  // Total 32691 of pages
  char *s = (char*)PGROUNDUP((uint64)pa_start);
  char *e = (char*)PGROUNDDOWN((uint64)pa_end);
  printf("total size of page %d. Counter is %d\n", (e - s) / PGSIZE, size);
  //printf("%d\n", get_ref_count(e+100));
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r) {
    kmem.freelist = r->next;
    add_ref(r);
  }

  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  else {
    //panic("no memory\n");
  }
  return (void*)r;
}
