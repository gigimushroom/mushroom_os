// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define HASH_TABLE_SIZE 503

struct ht_bucket {
  uint blockno;
  struct buf buf; // TODO: make it to linked list of buf
  struct spinlock bucket_lock;
};

struct {
  int size;
  int count;
  struct spinlock lock;
  struct ht_bucket buckets[HASH_TABLE_SIZE]; // array of pointers to ht_item
} bcache_ht;

void ht_init();
void ht_insert(uint bno, struct buf buffer);
struct buf* ht_find(uint bno);
void ht_delete(uint bno);
uint ht_hash(uint bno);

void ht_init()
{
  initlock(&bcache_ht.lock, "bcache");
  for (int i = 0; i < HASH_TABLE_SIZE; i++) {
    initsleeplock(&bcache_ht.buckets[i].buf.lock, "buffer"); 
  }
}

uint ht_hash(uint bno)
{
  return bno % HASH_TABLE_SIZE;
}

struct buf* ht_find(uint bno)
{
  uint hash = ht_hash(bno);
  struct buf *b = &bcache_ht.buckets[hash].buf;
  //printf("mushrroom2. ref: %d. dev:%d\n", b->refcnt, b->dev);
  if (b->refcnt > 0)
  {
    return b;
  }
  return 0;
}

void ht_insert(uint bno, struct buf buffer)
{
  uint hash = ht_hash(bno);
  struct buf *b = ht_find(bno);
  if (b && b->dev != buffer.dev) {
    printf("Found collision item block num:%d. Hash(%d)\n", bno, hash);
  }
  bcache_ht.buckets[hash].buf = buffer;
  //printf("Insert block num:%d. Hash(%d)\n", bno, hash);
}

void ht_delete(uint bno)
{

}

void ht_print()
{
  for (int i = 0; i < HASH_TABLE_SIZE; i++) {
    struct buf* b = &bcache_ht.buckets[i].buf;
    if (b && b->refcnt > 0) {
      printf("Buffer(%d) is valid. Hash(%d)\n", b->blockno, i);
    }
  }
}

// struct {
//   struct spinlock lock;
//   struct buf buf[NBUF];

//   // Linked list of all buffers, through prev/next.
//   // head.next is most recently used.
//   struct buf head;
// } bcache;

void
binit(void)
{
  // struct buf *b;

  // initlock(&bcache.lock, "bcache");

  // // Create linked list of buffers
  // bcache.head.prev = &bcache.head;
  // bcache.head.next = &bcache.head;
  // for(b = bcache.buf; b < bcache.buf+NBUF; b++){
  //   b->next = bcache.head.next;
  //   b->prev = &bcache.head;
  //   initsleeplock(&b->lock, "buffer");
  //   bcache.head.next->prev = b;
  //   bcache.head.next = b;
  // }


  printf(".......Start of hash table init.\n");

  ht_init();

  // struct buf test_buf;
  // test_buf.refcnt = 1;
  // ht_insert(3, test_buf);
  // ht_insert(6, test_buf);
  // ht_insert(38, test_buf);
  // ht_insert(1, test_buf);
  // ht_print();

  printf(".......End of hash table init.\n");
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  uint hash = ht_hash(blockno);

  acquire(&bcache_ht.buckets[hash].bucket_lock);

  // Is the block already cached?
  // for(b = bcache.head.next; b != &bcache.head; b = b->next){
  //   if(b->dev == dev && b->blockno == blockno){
  //     b->refcnt++;
  //     release(&bcache.lock);
  //     acquiresleep(&b->lock);
  //     return b;
  //   }
  // }
  struct buf *b = ht_find(blockno);
  if (b && b->dev == dev) {
    //printf("found buffer %d\n", blockno);
    b->refcnt++;
    release(&bcache_ht.buckets[hash].bucket_lock);
    acquiresleep(&b->lock);
    return b;
  }

  // Not cached; recycle an unused buffer.
  // for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
  //   if(b->refcnt == 0) {
  //     b->dev = dev;
  //     b->blockno = blockno;
  //     b->valid = 0;
  //     b->refcnt = 1;
  //     release(&bcache.lock);
  //     acquiresleep(&b->lock);
  //     return b;
  //   }
  // }
  

  struct buf* new_buf = &bcache_ht.buckets[hash].buf;
  new_buf->dev = dev;
  new_buf->blockno = blockno;
  new_buf->valid = 0;
  new_buf->refcnt = 1;
  ht_insert(blockno, *new_buf);
  release(&bcache_ht.buckets[hash].bucket_lock);
  acquiresleep(&new_buf->lock);
  return new_buf;
  //panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b->dev, b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b->dev, b, 1);
}

// Release a locked buffer.
// Move to the head of the MRU list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  acquire(&bcache_ht.lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // // no one is waiting for it.
    // b->next->prev = b->prev;
    // b->prev->next = b->next;
    // b->next = bcache.head.next;
    // b->prev = &bcache.head;
    // bcache.head.next->prev = b;
    // bcache.head.next = b;
  }
  
  release(&bcache_ht.lock);
}

void
bpin(struct buf *b) {
  acquire(&bcache_ht.lock);
  b->refcnt++;
  release(&bcache_ht.lock);
}

void
bunpin(struct buf *b) {
  acquire(&bcache_ht.lock);
  b->refcnt--;
  release(&bcache_ht.lock);
}


