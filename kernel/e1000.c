#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "e1000_dev.h"
#include "net.h"

#define TX_RING_SIZE 16
static struct tx_desc tx_ring[TX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *tx_mbufs[TX_RING_SIZE];

#define RX_RING_SIZE 16
static struct rx_desc rx_ring[RX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *rx_mbufs[RX_RING_SIZE];

// remember where the e1000's registers live.
static volatile uint32 *regs;

struct spinlock e1000_lock;

// called by pci_init().
// xregs is the memory address at which the
// e1000's registers are mapped.
void
e1000_init(uint32 *xregs)
{
  int i;

  initlock(&e1000_lock, "e1000");

  regs = xregs;

  // Reset the device
  regs[E1000_IMS] = 0; // disable interrupts
  regs[E1000_CTL] |= E1000_CTL_RST;
  regs[E1000_IMS] = 0; // redisable interrupts
  __sync_synchronize();

  // [E1000 14.5] Transmit initialization
  memset(tx_ring, 0, sizeof(tx_ring));
  for (i = 0; i < TX_RING_SIZE; i++) {
    tx_ring[i].status = E1000_TXD_STAT_DD;
    tx_mbufs[i] = 0;
  }
  regs[E1000_TDBAL] = (uint64) tx_ring;
  if(sizeof(tx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_TDLEN] = sizeof(tx_ring);
  regs[E1000_TDH] = regs[E1000_TDT] = 0;
  
  // [E1000 14.4] Receive initialization
  memset(rx_ring, 0, sizeof(rx_ring));
  for (i = 0; i < RX_RING_SIZE; i++) {
    rx_mbufs[i] = mbufalloc(0);
    if (!rx_mbufs[i])
      panic("e1000");
    rx_ring[i].addr = (uint64) rx_mbufs[i]->head;
  }
  regs[E1000_RDBAL] = (uint64) rx_ring;
  if(sizeof(rx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_RDH] = 0;
  regs[E1000_RDT] = RX_RING_SIZE - 1;
  regs[E1000_RDLEN] = sizeof(rx_ring);

  // filter by qemu's MAC address, 52:54:00:12:34:56
  regs[E1000_RA] = 0x12005452;
  regs[E1000_RA+1] = 0x5634 | (1<<31);
  // multicast table
  for (int i = 0; i < 4096/32; i++)
    regs[E1000_MTA + i] = 0;

  // transmitter control bits.
  regs[E1000_TCTL] = E1000_TCTL_EN |  // enable
    E1000_TCTL_PSP |                  // pad short packets
    (0x10 << E1000_TCTL_CT_SHIFT) |   // collision stuff
    (0x40 << E1000_TCTL_COLD_SHIFT);
  regs[E1000_TIPG] = 10 | (8<<10) | (6<<20); // inter-pkt gap

  // receiver control bits.
  regs[E1000_RCTL] = E1000_RCTL_EN | // enable receiver
    E1000_RCTL_BAM |                 // enable broadcast
    E1000_RCTL_SZ_2048 |             // 2048-byte rx buffers
    E1000_RCTL_SECRC;                // strip CRC
  
  // ask e1000 for receive interrupts.
  regs[E1000_RDTR] = 0; // interrupt after every received packet (no timer)
  regs[E1000_RADV] = 0; // interrupt after every packet (no timer)
  regs[E1000_IMS] = (1 << 7); // RXDW -- Receiver Descriptor Write Back
}

int
e1000_transmit(struct mbuf *m)
{
  //
  // Your code here.
  //
  // the mbuf contains an ethernet frame; program it into
  // the TX descriptor ring so that the e1000 sends it. Stash
  // a pointer so that it can be freed after sending.
  //
  acquire(&e1000_lock);
  // For transmitting, first get the current ring position, using E1000_TDT.
  int pos = regs[E1000_TDT];

  // Then check if the the ring is overflowing. If E1000_TXD_STAT_DD is 
  // not set in the current descriptor, a previous transmission is 
  // still in flight, so return an error.
  if ((tx_ring[pos].status & E1000_TXD_STAT_DD) == 0) {
    release(&e1000_lock);
    return - 1;
  }
  //printf("Send start....at %d\n", pos);

  // Otherwise, use mbuffree() to free the last mbuf that was transmitted 
  // with the current descriptor (if there was one).
  struct mbuf *b = tx_mbufs[pos];
  if (b)
    mbuffree(b);

  // Then fill in the descriptor, providing the new mbuf's head pointer 
  // and length.
  tx_ring[pos].addr = (uint64) m->head;
  tx_ring[pos].length = (uint64) m->len;

  // Set the necessary cmd flags (read the E1000 manual) 
  // and stash away a pointer to the new mbuf for later freeing.
  tx_ring[pos].cmd |= E1000_TXD_CMD_EOP;
  tx_ring[pos].cmd |= E1000_TXD_CMD_RS;
  tx_mbufs[pos] = m;

  // Finally, update the ring position by adding one to E1000_TDT 
  // modulo TX_RING_SIZE
  regs[E1000_TDT] = (pos + 1) % TX_RING_SIZE;
  release(&e1000_lock);
  return 0;
}

static void
e1000_recv(void)
{
  //
  // Your code here.
  //
  // Check for packets that have arrived from the e1000
  // Create and deliver an mbuf for each packet (using net_rx()).
  //
  // First get the next ring position, 
  // using E1000_RDT plus one modulo RX_RING_SIZE.
  acquire(&e1000_lock);
  int cur = (regs[E1000_RDT]+1) % RX_RING_SIZE;

  //printf("start....\n");
  for (int i=0; i <16; i++) {
    if (rx_ring[i].status) {
      //printf("status is 1 for index(%d)\n", i);
    }
  }
  //printf("end....head(%d), tail(%d)\n", regs[E1000_RDH], regs[E1000_RDT]);
  // Then check if a new packet is available by checking 
  // for the E1000_RXD_STAT_DD bit in the status portion 
  // of the descriptor. If not, stop.
  while ((rx_ring[cur].status & E1000_RXD_STAT_DD) != 0) {
    /*
    Otherwise, update the mbuf's length to the length reported 
    in the descriptor (e.g., use mbufput()). 
    Deliver the mbuf to the protocol layer using net_rx(). 
    (e1000_init() allocates an mbuf for each slot in the receive ring
    initially.)
    */
    int len = rx_ring[cur].length;
    //printf("I got a packet! Pos(%d). Len(%d)\n", cur, len);

    mbufput(rx_mbufs[cur], len);

    release(&e1000_lock);
    net_rx(rx_mbufs[cur]);
    acquire(&e1000_lock);

    /*
    Then allocate a new mbuf (because net_rx() maybe hanging on 
    to the mbuf passed to it) and program its head pointer into 
    the descriptor. Clear the descriptor's status bits to zero.
    */
    rx_mbufs[cur] = mbufalloc(0);
    rx_ring[cur].addr = (uint64) rx_mbufs[cur]->head;
    rx_ring[cur].status = 0; // clear bits

    // Finally, update the E1000_RDT register to the next position 
    // by writing to it.
    regs[E1000_RDT] = cur;
    cur = (regs[E1000_RDT]+1) % RX_RING_SIZE;
  }
  release(&e1000_lock);
}

void
e1000_intr(void)
{
  e1000_recv();
  // tell the e1000 we've seen this interrupt;
  // without this the e1000 won't raise any
  // further interrupts.
  regs[E1000_ICR];
}
