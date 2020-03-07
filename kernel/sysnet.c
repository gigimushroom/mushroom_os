//
// network system calls.
//

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "net.h"

struct sock {
  struct sock *next; // the next socket in the list
  uint32 raddr;      // the remote IPv4 address
  uint16 lport;      // the local UDP port number
  uint16 rport;      // the remote UDP port number
  struct spinlock lock; // protects the rxq
  struct mbufq rxq;  // a queue of packets waiting to be received
};

static struct spinlock lock;
static struct sock *sockets;

void
sockinit(void)
{
  initlock(&lock, "socktbl");
}

int
sockalloc(struct file **f, uint32 raddr, uint16 lport, uint16 rport)
{
  struct sock *si, *pos;

  si = 0;
  *f = 0;
  if ((*f = filealloc()) == 0)
    goto bad;
  if ((si = (struct sock*)kalloc()) == 0)
    goto bad;

  // initialize objects
  si->raddr = raddr;
  si->lport = lport;
  si->rport = rport;
  initlock(&si->lock, "sock");
  mbufq_init(&si->rxq);
  (*f)->type = FD_SOCK;
  (*f)->readable = 1;
  (*f)->writable = 1;
  (*f)->sock = si;

  // add to list of sockets
  acquire(&lock);
  pos = sockets;
  while (pos) {
    if (pos->raddr == raddr &&
        pos->lport == lport &&
	pos->rport == rport) {
      release(&lock);
      goto bad;
    }
    pos = pos->next;
  }
  si->next = sockets;
  sockets = si;
  release(&lock);
  return 0;

bad:
  if (si)
    kfree((char*)si);
  if (*f)
    fileclose(*f);
  return -1;
}

//
// Your code here.
//
// Add and wire in methods to handle closing, reading,
// and writing for network sockets.
//

int sockread(struct file *f, uint64 addr, int n){
  // check if rxq is empty using mbufq_empty(), 
  // and if it is, use sleep() to wait until an mbuf is enqueued.
  //printf("sock read size %d\n", n);
  acquire(&f->sock->lock);
  while (mbufq_empty(&f->sock->rxq)) {
    sleep(&f->sock->rxq, &f->sock->lock);
  }
  // Using mbufq_pophead, pop the mbuf from rxq and use copyout() 
  // to move its payload into user memory. 
  struct mbuf * m = mbufq_pophead(&f->sock->rxq);
  if (!m) {
    printf("what the heck\n");
    return -1;
  }
  struct proc* p = myproc();
  int len = n > m->len ? m->len : n;
  //printf("sock read size %d IS WAKEN UP! this buffer is size %d\n", n, m->len);
  if (copyout(p->pagetable, addr, m->head, len) == -1) {
    release(&f->sock->lock);
    mbuffree(m);
    return -1;
  }

  release(&f->sock->lock);
  // Free the mbuf using mbuffree() to finish
  mbuffree(m);
  //printf("sock read is finished with size %d\n", len);
  return len;
}

int sockwrite(struct file *f, uint64 addr, int n) {
  // allocate a new mbuf, taking care to leave enough 
  // headroom for the UDP, IP, and Ethernet headers.
  //printf("sock write size %d\n", n);
  int head_size = sizeof(struct udp) + sizeof(struct ip) + sizeof(struct eth);
  struct mbuf *m = mbufalloc(head_size);

  mbufput(m, n);
  struct proc* p = myproc();
  // Use mbufput() and copyin() to transfer the payload from user memory into the mbuf.
  if (copyin(p->pagetable, m->head, addr, n) == -1) {
    mbuffree(m);
    return -1;
  }

  // send the mbuf.
  struct sock * s = f->sock;
  net_tx_udp(m, s->raddr, s->lport, s->rport);
  return n;
}

void sockclose(struct file *f) {
  // remove the socket from the sockets list. 
  struct sock *s = f->sock;
  struct sock *p = sockets;
  if (s==p) {
    //printf("Close the only one socket\n");
    sockets = 0;
  } else {
    //printf("TODO: more sokcets in list.\n");

  }

  // Then, free the socket object. Be careful to free any mbufs 
  // that have not been read first, before freeing the struct sock.
  if (mbufq_empty(&s->rxq)) {
    struct mbuf *m = mbufq_pophead(&s->rxq);
    while(m) {
      mbuffree(m);
    }
  }
  kfree(s);
}

// called by protocol handler layer to deliver UDP packets
void
sockrecvudp(struct mbuf *m, uint32 raddr, uint16 lport, uint16 rport)
{
  //
  // Your code here.
  //
  // Find the socket that handles this mbuf and deliver it, waking
  // any sleeping reader. Free the mbuf if there are no sockets
  // registered to handle it.
  //
  //printf(".........sockrecvudp\n");
  struct sock *s = sockets;
  acquire(&lock);
  while (s) {
    if (s->raddr == raddr &&
        s->lport == lport &&
	      s->rport == rport) {
      break;
    }
    s = s->next;
  }
  release(&lock);

  if (s) {
    acquire(&s->lock);
    mbufq_pushtail(&s->rxq, m);
    //printf("so.... this buffer size is %d\n", m->len);
    release(&s->lock);
    wakeup(&s->rxq);
    return;
  }

  mbuffree(m);
}
