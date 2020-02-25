//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  if(argint(n, &fd) < 0)
    return -1;
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd] == 0){
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

uint64
sys_read(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;

  return filewrite(f, p, n);
}

uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

uint64
sys_fstat(void)
{
  struct file *f;
  uint64 st; // user pointer to struct stat

  if(argfd(0, 0, &f) < 0 || argaddr(1, &st) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
uint64
sys_link(void)
{
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  if(argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  begin_op(ROOTDEV);
  if((ip = namei(old)) == 0){
    end_op(ROOTDEV);
    return -1;
  }

  ilock(ip);
  if(ip->type == T_DIR){
    iunlockput(ip);
    end_op(ROOTDEV);
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op(ROOTDEV);

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op(ROOTDEV);
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

uint64
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  if(argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op(ROOTDEV);
  if((dp = nameiparent(path, name)) == 0){
    end_op(ROOTDEV);
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !isdirempty(ip)){
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if(ip->type == T_DIR){
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op(ROOTDEV);

  return 0;

bad:
  iunlockput(dp);
  end_op(ROOTDEV);
  return -1;
}

static struct inode*
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) != 0){
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0)
    panic("create: ialloc");

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if(type == T_DIR){  // Create . and .. entries.
    dp->nlink++;  // for ".."
    iupdate(dp);
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      panic("create dots");
  }

  if(dirlink(dp, name, ip->inum) < 0)
    panic("create: dirlink");

  iunlockput(dp);

  return ip;
}

uint64
sys_open(void)
{
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;

  if((n = argstr(0, path, MAXPATH)) < 0 || argint(1, &omode) < 0)
    return -1;

  begin_op(ROOTDEV);

  if(omode & O_CREATE){
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op(ROOTDEV);
      return -1;
    }
  } else {
    if((ip = namei(path)) == 0){
      end_op(ROOTDEV);
      return -1;
    }
    ilock(ip);
    if(ip->type == T_DIR && omode != O_RDONLY){
      iunlockput(ip);
      end_op(ROOTDEV);
      return -1;
    }
  }

  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){
    iunlockput(ip);
    end_op(ROOTDEV);
    return -1;
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op(ROOTDEV);
    return -1;
  }

  if(ip->type == T_DEVICE){
    f->type = FD_DEVICE;
    f->major = ip->major;
    f->minor = ip->minor;
  } else {
    f->type = FD_INODE;
  }
  f->ip = ip;
  f->off = 0;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  iunlock(ip);
  end_op(ROOTDEV);

  return fd;
}

uint64
sys_mkdir(void)
{
  char path[MAXPATH];
  struct inode *ip;

  begin_op(ROOTDEV);
  if(argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op(ROOTDEV);
    return -1;
  }
  iunlockput(ip);
  end_op(ROOTDEV);
  return 0;
}

uint64
sys_mknod(void)
{
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  begin_op(ROOTDEV);
  if((argstr(0, path, MAXPATH)) < 0 ||
     argint(1, &major) < 0 ||
     argint(2, &minor) < 0 ||
     (ip = create(path, T_DEVICE, major, minor)) == 0){
    end_op(ROOTDEV);
    return -1;
  }
  iunlockput(ip);
  end_op(ROOTDEV);
  return 0;
}

uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();
  
  begin_op(ROOTDEV);
  if(argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0){
    end_op(ROOTDEV);
    return -1;
  }
  ilock(ip);
  if(ip->type != T_DIR){
    iunlockput(ip);
    end_op(ROOTDEV);
    return -1;
  }
  iunlock(ip);
  iput(p->cwd);
  end_op(ROOTDEV);
  p->cwd = ip;
  return 0;
}

uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  if(argstr(0, path, MAXPATH) < 0 || argaddr(1, &uargv) < 0){
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv)){
      goto bad;
    }
    if(fetchaddr(uargv+sizeof(uint64)*i, (uint64*)&uarg) < 0){
      goto bad;
    }
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if(argv[i] == 0)
      panic("sys_exec kalloc");
    if(fetchstr(uarg, argv[i], PGSIZE) < 0){
      goto bad;
    }
  }

  int ret = exec(path, argv);

  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

 bad:
  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

uint64
sys_pipe(void)
{
  uint64 fdarray; // user pointer to array of two integers
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  if(argaddr(0, &fdarray) < 0)
    return -1;
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if(copyout(p->pagetable, fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
     copyout(p->pagetable, fdarray+sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0){
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
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
  printf("addr(%p), size(%d), prot(%d), flags(%p), fd(%d), offset(%d). Current Max(%p). MAXVA(%p)\n",
          addr, size, prot, flags, fd, offset, cur_max, MAXVA);
        
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
    p->cur_max = start_addr - 1;
    
    printf("mmap max va(%p). VMA start(%p), end(%p). Inode(%d)\n", 
            MAXVA, vm->start_ad, vm->end_ad, vm->file->ip->inum);
  } else {
    return 0xffffffffffffffff;
  }

  return start_addr;
}

//void _write_back()

uint64
sys_munmap(void) {
  uint64 addr;
  int size;

  if(argaddr(0, &addr) < 0 || argint(1, &size) < 0){
    return -1;
  }
  
  uint64 start_base = PGROUNDDOWN(addr);
  uint64 end_base = PGROUNDUP(addr + size - 1);
  
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
    return -1;
  }
  // 4 cases
  // first part is un-map
  if (vm->start_ad == start_base && end_base < vm->end_ad) {
    vm->start_ad = end_base;
    vm->len -= (end_base - start_base);
  } else if (vm->start_ad < start_base && end_base == vm->end_ad){
    // last part is un-map
    vm->end_ad = start_base;
    vm->len = (end_base - start_base);
  } else if (vm->start_ad == start_base && vm->end_ad == end_base) {
    // exact size
    vm->file->ref--;
    vm->file->off = 0;
    vm->valid = 0;
    vm->len = 0;
  } else if (vm->start_ad < start_base && end_base < vm->end_ad) {
    uint64 cache_end = vm->end_ad;
    // un-map in the middle
    vm->end_ad = start_base;
    vm->len = start_base - vm->start_ad;
    
    // find another empty VMA and set the 2nd part.
    struct vm_area_struct *vm2 = 0;
    for (int i=0; i<100; i++) {
      if (p->vma[i].valid == 1 && 
          p->vma[i].start_ad <=  start_base &&
          end_base <= p->vma[i].end_ad) {
        vm2 = &p->vma[i];
        break;
      }
    }
    if (vm2) {
      vm2->valid = 1;
      vm2->start_ad = end_base;
      vm2->end_ad = cache_end;
      vm2->len = cache_end - end_base;
      vm2->prot = vm->prot;
      vm2->flags = vm->flags;
      vm2->fd = vm->fd;
      vm2->file = vm->file;
      vm2->file->ref++;
    }
  }
  if (vm->flags & MAP_SHARED) {
    printf("....We need to write back file\n");
    // need to fix offset...
    filewrite(vm->file, start_base, size);
  }
  uvmunmap(p->pagetable, start_base, size, 1);
  
  return size;
}

