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

#define NBUCKETS 13


extern uint ticks;
// struct {
//   struct spinlock lock;
//   struct buf buf[NBUF];

//   // Linked list of all buffers, through prev/next.
//   // Sorted by how recently the buffer was used.
//   // head.next is most recent, head.prev is least.
//   struct buf head;
// } bcache;

struct {
  struct spinlock global_lock;
  struct spinlock lock[NBUCKETS];
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf hash_buckets[NBUCKETS];
} bcache;
// void
// binit(void)
// {
//   struct buf *b;

//   initlock(&bcache.lock, "bcache");

//   // Create linked list of buffers
//   bcache.head.prev = &bcache.head;
//   bcache.head.next = &bcache.head;
//   for(b = bcache.buf; b < bcache.buf+NBUF; b++){
//     b->next = bcache.head.next;
//     b->prev = &bcache.head;
//     initsleeplock(&b->lock, "buffer");
//     bcache.head.next->prev = b;
//     bcache.head.next = b;
//   }
// }
void
binit(void)
{
  struct buf *b;
  
  initlock(&bcache.global_lock, "bcache_global");
  for (int i = 0; i < NBUCKETS; ++i) {
    char name[24];
    snprintf(name, 24, "bcache_%d", i);
    initlock(&bcache.lock[i], name);

    // Create linked list of buffers
    bcache.hash_buckets[i].prev = &bcache.hash_buckets[i];
    bcache.hash_buckets[i].next = &bcache.hash_buckets[i];

  }

  // when init, just allocate all bufs to one bucket
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.hash_buckets[0].next;
    b->prev = &bcache.hash_buckets[0];
    initsleeplock(&b->lock, "buffer");
    bcache.hash_buckets[0].next->prev = b;
    bcache.hash_buckets[0].next = b;
    // bcache.head.next->prev = b;
    // bcache.head.next = b;
  }
}

static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  uint idx = blockno % NBUCKETS;

  acquire(&bcache.lock[idx]);

  // Is the block already cached?
  for(b = bcache.hash_buckets[idx].next; b != &bcache.hash_buckets[idx]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock[idx]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  release(&bcache.lock[idx]);

  acquire(&bcache.global_lock);

  acquire(&bcache.lock[idx]);
  // because we release the lock just now 
  // we should check wether some other process has allocate a buf for this block again
  for(b = bcache.hash_buckets[idx].next; b != &bcache.hash_buckets[idx]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock[idx]);
      release(&bcache.global_lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  // now we can start to steal a block from other buckets
  for (int i = 0; i < NBUF; ++i){
    b = &bcache.buf[i];
    uint dst_idx = b->blockno % NBUCKETS;
    // note that we should check wether the old and new are the same bucket or not
    if (dst_idx != idx) {
      acquire(&bcache.lock[dst_idx]);
    }
    if (b->refcnt > 0) {
      if (dst_idx != idx) {
        release(&bcache.lock[dst_idx]);
      }
      continue;
    }
    // update the buf information
    b->dev = dev;
    b->blockno = blockno;
    b->valid = 0;
    b->refcnt = 1;
    // remove this buf from the old bucket and put it into the new bucket
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.hash_buckets[idx].next;
    b->prev = &bcache.hash_buckets[idx];
    bcache.hash_buckets[idx].next->prev = b;
    bcache.hash_buckets[idx].next = b;
    if (dst_idx != idx) {
      release(&bcache.lock[dst_idx]);
    }
    release(&bcache.lock[idx]);
    release(&bcache.global_lock);
    acquiresleep(&b->lock);
    return b;
  }
  // for(b = bcache.hash_buckets[idx].prev; b != &bcache.hash_buckets[idx]; b = b->prev){
  //   if (b->refcnt == 0) {
  //     b->dev = dev;
  //     b->blockno = blockno;
  //     b->valid = 0;
  //     b->refcnt = 1;
  //     release(&bcache.lock[idx]);
  //     acquiresleep(&b->lock);
  //     return b;
  // }
  panic("bget: no buffers");
}
// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
// static struct buf*
// bget(uint dev, uint blockno)
// {
//   struct buf *b;

//   acquire(&bcache.lock);

//   // Is the block already cached?
//   for(b = bcache.head.next; b != &bcache.head; b = b->next){
//     if(b->dev == dev && b->blockno == blockno){
//       b->refcnt++;
//       release(&bcache.lock);
//       acquiresleep(&b->lock);
//       return b;
//     }
//   }

//   // Not cached.
//   // Recycle the least recently used (LRU) unused buffer.
//   for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
//     if(b->refcnt == 0) {
//       b->dev = dev;
//       b->blockno = blockno;
//       b->valid = 0;
//       b->refcnt = 1;
//       release(&bcache.lock);
//       acquiresleep(&b->lock);
//       return b;
//     }
//   }
//   panic("bget: no buffers");
// }

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
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
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  uint idx = b->blockno % NBUCKETS;

  releasesleep(&b->lock);

  acquire(&bcache.lock[idx]);  
  b->refcnt--;
  b->timestamp = ticks;

  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.hash_buckets[idx].next;
    b->prev = &bcache.hash_buckets[idx];
    bcache.hash_buckets[idx].next->prev = b;
    bcache.hash_buckets[idx].next = b;
  }
  
  release(&bcache.lock[idx]);
}

void
bpin(struct buf *b) {
  uint idx = b->blockno % NBUCKETS;
  acquire(&bcache.lock[idx]);
  b->refcnt++;
  release(&bcache.lock[idx]);
}

void
bunpin(struct buf *b) {
  uint idx = b->blockno % NBUCKETS;
  acquire(&bcache.lock[idx]);
  b->refcnt--;
  release(&bcache.lock[idx]);
}


