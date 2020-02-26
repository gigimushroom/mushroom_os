#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "proc.h"
#include "file.h"
#include "fcntl.h"

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

void print(pagetable_t);

/*
 * create a direct-map page table for the kernel and
 * turn on paging. called early, in supervisor mode.
 * the page allocator is already initialized.
 */
void
kvminit()
{
  kernel_pagetable = (pagetable_t) kalloc();
  memset(kernel_pagetable, 0, PGSIZE);

  // uart registers
  kvmmap(UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface 0
  kvmmap(VIRTION(0), VIRTION(0), PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface 1
  kvmmap(VIRTION(1), VIRTION(1), PGSIZE, PTE_R | PTE_W);

  // CLINT
  kvmmap(CLINT, CLINT, 0x10000, PTE_R | PTE_W);

  // PLIC
  kvmmap(PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap((uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
}

// Switch h/w page table register to the kernel's page table,
// and enable paging.
void
kvminithart()
{
  w_satp(MAKE_SATP(kernel_pagetable));
  sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..39 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..12 -- 12 bits of byte offset within the page.
static pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if(va >= MAXVA)
    panic("walk");

  for(int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if(*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if(pte == 0)
    return 0;
  if((*pte & PTE_V) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void
kvmmap(uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(kernel_pagetable, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// translate a kernel virtual address to
// a physical address. only needed for
// addresses on the stack.
// assumes va is page aligned.
uint64
kvmpa(uint64 va)
{
  uint64 off = va % PGSIZE;
  pte_t *pte;
  uint64 pa;
  
  pte = walk(kernel_pagetable, va, 0);
  if(pte == 0)
    panic("kvmpa");
  if((*pte & PTE_V) == 0)
    panic("kvmpa");
  pa = PTE2PA(*pte);
  return pa+off;
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  a = PGROUNDDOWN(va);
  last = PGROUNDDOWN(va + size - 1);
  for(;;){
    if((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    //printf("PTE addr(%p)\n", PTE2PA(*pte));
    if(*pte & PTE_V) {
     panic("remap"); 
    }
    *pte = PA2PTE(pa) | perm | PTE_V;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// Remove mappings from a page table. The mappings in
// the given range must exist. Optionally free the
// physical memory.
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 size, int do_free)
{
  uint64 a, last;
  pte_t *pte;
  uint64 pa;

  a = PGROUNDDOWN(va);
  last = PGROUNDDOWN(va + size - 1);
  for(;;){
    if((pte = walk(pagetable, a, 0)) == 0)
      panic("uvmunmap: walk");
    if((*pte & PTE_V) == 0){
      printf("va=%p pte=%p\n", a, *pte);
      panic("uvmunmap: not mapped");
    }
    if(PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
    if(do_free){
      pa = PTE2PA(*pte);
      kfree((void*)pa);
    }
    *pte = 0;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
}

// create an empty user page table.
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t) kalloc();
  if(pagetable == 0)
    panic("uvmcreate: out of memory");
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void
uvminit(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W|PTE_R|PTE_X|PTE_U);
  memmove(mem, src, sz);
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  char *mem;
  uint64 a;

  if(newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  a = oldsz;
  for(; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_W|PTE_X|PTE_R|PTE_U) != 0){
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
    return oldsz;

  uint64 newup = PGROUNDUP(newsz);
  if(newup < PGROUNDUP(oldsz))
    uvmunmap(pagetable, newup, oldsz - newup, 1);

  return newsz;
}

// TODO(xiaying): fix the freewalk panic properly.

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
static void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V){
      //panic("freewalk: leaf");
    }
  }
  kfree((void*)pagetable);
}

// Free user memory pages,
// then free page-table pages.
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, 0, sz, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char*)pa, PGSIZE);
    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
      kfree(mem);
      goto err;
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i, 1);
  return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  uint64 n, va0, pa0;
  int got_null = 0;

  while(got_null == 0 && max > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > max)
      n = max;

    char *p = (char *) (pa0 + (srcva - va0));
    while(n > 0){
      if(*p == '\0'){
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if(got_null){
    return 0;
  } else {
    return -1;
  }
}

/*
void *mmap(void *addr, size_t length, int prot, int flags,
           int fd, off_t offset);
*/
uint64
sys_mmap(void) {
/*
find an unused region in the process's address space 
in which to map the file, and add a VMA to the process's table
of mapped regions. 
*/
// rounddown addr
// find unused region
// the dump solution is loop each of them find the invalid one
// set the content
// In page fault, manually check memory region for each.

  uint64 addr;
  int size, prot, flags, fd, offset;

  if(argaddr(0, &addr) < 0){
    return -1;
  }
  if(argint(1, &size) < 0 || argint(2, &prot) < 0 || argint(3, &flags) < 0){
    return -1;
  }
  if(argint(4, &fd) < 0 || argint(5, &offset) < 0){
    return -1;
  }
  
  struct proc *p = myproc();
  
  struct file *f = p->ofile[fd];
  // check that mmap doesn't allow read/write mapping of a
  // file opened read-only.
  if (flags & MAP_SHARED) {
    if (!(f->writable) && (prot & PROT_WRITE)) {
      printf("File is read-only, but we mmap with write permission and flag. %d vs %p\n",
            f->writable, prot);
      return 0xffffffffffffffff;
    }
  }
  
  uint64 cur_max = p->cur_max;
  //printf("addr(%p), size(%d), prot(%d), flags(%p), fd(%d), offset(%d). Current Max(%p). MAXVA(%p)\n",
  //        addr, size, prot, flags, fd, offset, cur_max, MAXVA);
        
  uint64 start_addr = PGROUNDDOWN(cur_max - size);
  
  struct vm_area_struct *vm = 0;
  for (int i=0; i<100; i++) {
    if (p->vma[i].valid == 0) {
      vm = &p->vma[i];
      break;
    }
  }
  if (vm) {
    vm->valid = 1;
    vm->start_ad = start_addr;
    vm->end_ad = cur_max;
    vm->len = size;
    vm->prot = prot;
    vm->flags = flags;
    vm->fd = fd;
    vm->file = p->ofile[fd];
    vm->file->ref++;

    // reset process current max available
    p->cur_max = start_addr;

    printf("mmap is set. max va(%p). VMA start(%p), end(%p). Inode(%d)\n", 
            MAXVA, vm->start_ad, vm->end_ad, vm->file->ip->inum);
  } else {
    return 0xffffffffffffffff;
  }

  return start_addr;
}

uint64
sys_munmap(void) {
  uint64 addr;
  int size;

  if(argaddr(0, &addr) < 0 || argint(1, &size) < 0){
    return -1;
  }
  
  uint64 start_base = PGROUNDDOWN(addr);
  uint64 end_base = PGROUNDDOWN(addr + size);
  
  struct proc *p = myproc();
  struct vm_area_struct *vm = 0;
  for (int i=0; i<100; i++) {
    if (p->vma[i].valid == 1 && 
        p->vma[i].start_ad <=  start_base &&
        end_base <= p->vma[i].end_ad) {
      vm = &p->vma[i];
      break;
    }
  }
  if (!vm) {
    printf("Cannot found VMA. start base(%p), end base(%p)\n",
          start_base, end_base);
    return -1;
  }
  
  //printf("un-map. start base(%p), end base(%p). vm start(%p), vm end(%p)\n",
  //       start_base, end_base, vm->start_ad, vm->end_ad);

  if (vm->flags & MAP_SHARED) {
    printf("....We need to write back file\n");
    struct file *f = vm->file;
    begin_op(f->ip->dev);
    ilock(f->ip);
    writei(f->ip, 1, vm->start_ad, 0, vm->len);
    iunlock(f->ip);
    end_op(f->ip->dev);
  }

  pte_t *pte;
  for(int i = start_base; i <= end_base; i+=PGSIZE){
    if((pte = walk(p->pagetable, i, 0)) == 0) {
      if(*pte & PTE_V) {
        uvmunmap(p->pagetable, i, PGSIZE, 0);
      } else {
        printf("un-map an invalid page is no-op!\n");
      }
    }
  }

  // 4 cases
  // first part is un-map
  if (vm->start_ad == start_base && end_base < vm->end_ad) {
    vm->start_ad = end_base;
    vm->len -= size;
  } else if (vm->start_ad < start_base && end_base == vm->end_ad){
    // last part is un-map
    vm->end_ad = start_base - 1;
    vm->len = size;
  } else if (vm->start_ad == start_base && vm->end_ad == end_base) {
    // exact size
    vm->file->ref--;
    //vm->file->off = 0;
    vm->valid = 0;
    vm->len = 0;
  } else if (vm->start_ad < start_base && end_base < vm->end_ad) {
    printf("this is very tricky...need to fix offset\n");
    // uint64 cache_end = vm->end_ad;
    // // un-map in the middle
    // vm->end_ad = start_base;
    // vm->len = start_base - vm->start_ad;
    
    // // find another empty VMA and set the 2nd part.
    // struct vm_area_struct *vm2 = 0;
    // for (int i=0; i<100; i++) {
    //   if (p->vma[i].valid == 1 && 
    //       p->vma[i].start_ad <=  start_base &&
    //       end_base <= p->vma[i].end_ad) {
    //     vm2 = &p->vma[i];
    //     break;
    //   }
    // }
    // if (vm2) {
    //   vm2->valid = 1;
    //   vm2->start_ad = end_base;
    //   vm2->end_ad = cache_end;
    //   vm2->len = cache_end - end_base;
    //   vm2->prot = vm->prot;
    //   vm2->flags = vm->flags;
    //   vm2->fd = vm->fd;
    //   vm2->file = vm->file;
    //   vm2->file->ref++;
    // }
  } else {
    printf("Err. start base(%p), end base(%p). vm start(%p), vm end(%p)",
         start_base, end_base, vm->start_ad, vm->end_ad);
  }
  //printf("Un-map is done. New VMA start(%p), vm end(%p)\n",
  //       vm->start_ad, vm->end_ad);

  return size;
}

void free_all_vma(pagetable_t pagetable, uint64 start, uint64 end) {
  pte_t *pte;
  for(int i = start; i <= end; i+=PGSIZE) {
    if((pte = walk(pagetable, i, 0)) == 0) {
      if(*pte & PTE_V) {
        uvmunmap(pagetable, i, PGSIZE, 0);
      }
    }
  }
}

void copy_vma(struct vm_area_struct *dst, struct vm_area_struct *src) {
  dst->valid = 1;
  dst->start_ad = src->start_ad;
  dst->end_ad = src->end_ad;
  dst->len = src->len;
  dst->prot = src->prot;
  dst->flags = src->flags;
  dst->fd = src->fd;
  dst->file = src->file;
  dst->file->ref++;
}
