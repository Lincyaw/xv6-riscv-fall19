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
/*
  bcache维护了一个由静态数组struct buf buf[NBUF]组成的双向链表，
  它以块为单位，每次读入或写出一个磁盘块，放到一个内存缓存块中（bcache.buf），
  同时自旋锁bcache.lock用于用户互斥访问。
  所有对缓存块的访问都是通过bcache.head引用链表来实现的，而不是buf数组。
*/
#define NBUCKETS 13
struct
{
  // struct spinlock mainLock;
  struct spinlock lock[NBUCKETS];
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // head.next is most recently used.
  struct buf head[NBUCKETS];
} bcache;

void binit(void)
{
  // 初始化缓存
  struct buf *b;
  for (int i = 0; i < NBUCKETS; i++)
  {
    initlock(&bcache.lock[i], "bcache");
    bcache.head[i].prev = &bcache.head[i];
    bcache.head[i].next = &bcache.head[i];
  }

  // Create linked list of buffers
  for (b = bcache.buf; b < bcache.buf + NBUF; b++)
  {
    b->next = bcache.head[0].next;
    b->prev = &bcache.head[0];
    initsleeplock(&b->lock, "buffer");
    bcache.head[0].next->prev = b;
    bcache.head[0].next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
//  在设备dev上的缓冲区缓存中查找块，
// 如果没有找到，分配一个缓冲区。在任何一种情况下，返回锁定的缓冲区。
static struct buf *
bget(uint dev, uint blockno)
{
  struct buf *b;
  int currentBuf = blockno % NBUCKETS;
  acquire(&bcache.lock[currentBuf]);

  // Is the block already cached?
  for (b = bcache.head[currentBuf].next; b != &bcache.head[currentBuf]; b = b->next)
  {
    if (b->dev == dev && b->blockno == blockno)
    {
      b->refcnt++;
      release(&bcache.lock[currentBuf]);
      acquiresleep(&b->lock);
      return b;
    }
  }
  // Not cached; recycle an unused buffer.
  int nextBuf = (currentBuf + 1) % NBUCKETS;
  for (; nextBuf != currentBuf; nextBuf = (nextBuf + 1) % NBUCKETS)
  {
    acquire(&bcache.lock[nextBuf]);
    // 头结点是最经常使用的.因为上面找不到可用的,所以从最不常用的开始替换
    for (b = bcache.head[nextBuf].prev; b != &bcache.head[nextBuf]; b = b->prev)
    {
      // 找到空闲的buf
      if (b->refcnt == 0)
      {
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;

        // 从原来bucket的链表中断开
        b->next->prev = b->prev;
        b->prev->next = b->next;
        release(&bcache.lock[nextBuf]);

        // 插入到blockno对应的bucket中去
        b->next = bcache.head[currentBuf].next;
        b->prev = &bcache.head[currentBuf];
        bcache.head[currentBuf].next->prev = b;
        bcache.head[currentBuf].next = b;
        release(&bcache.lock[currentBuf]);

        acquiresleep(&b->lock);
        return b;
      }
    }
    // 如果当前bucket里没有找到，在转到下一个bucket之前，记得释放当前bucket的锁
    release(&bcache.lock[nextBuf]);
  }
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf *
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if (!b->valid)
  {
    virtio_disk_rw(b->dev, b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void bwrite(struct buf *b)
{
  if (!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b->dev, b, 1);
}

// Release a locked buffer.
// Move to the head of the MRU list.
void brelse(struct buf *b)
{
  if (!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);
  int currentBuf = (b->blockno) % NBUCKETS;
  acquire(&bcache.lock[currentBuf]);
  b->refcnt--;
  if (b->refcnt == 0)
  {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head[currentBuf].next;
    b->prev = &bcache.head[currentBuf];
    bcache.head[currentBuf].next->prev = b;
    bcache.head[currentBuf].next = b;
  }

  release(&bcache.lock[currentBuf]);
}

void bpin(struct buf *b)
{
  int currentBuf = (b->blockno) % NBUCKETS;
  acquire(&bcache.lock[currentBuf]);
  b->refcnt++;
  release(&bcache.lock[currentBuf]);
}

void bunpin(struct buf *b)
{
  int currentBuf = (b->blockno) % NBUCKETS;
  acquire(&bcache.lock[currentBuf]);
  b->refcnt--;
  release(&bcache.lock[currentBuf]);
}
