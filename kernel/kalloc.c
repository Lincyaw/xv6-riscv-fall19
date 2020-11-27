// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.
/*

*/
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run
{
  struct run *next;
};
// 分配器的数据结构是由空闲物理页组成的链表。每个空闲页在列表里都是struct run。
// 因为空闲页里什么都没有，所以空闲页的run数据结构就保存在空闲页自身里。
// 这个空闲列表使用自旋锁进行保护。
struct
{
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];

void kinit()
{
  for (int i = 0; i < NCPU; i++)
    initlock(&kmem[i].lock, "kmem");
  // 把空闲内存加到链表里: 把每个空闲页逐一加到链表里来实现此功能的
  freerange(end, (void *)PHYSTOP);
}

void freerange(void *pa_start, void *pa_end)
{

  char *p;
  // 由于PTE（页表条目）只能指向4K对齐的物理地址
  // 所以使用宏PGROUNDUP来确保空闲内存是4K对齐的
  p = (char *)PGROUNDUP((uint64)pa_start);
  // freerange将end~PHYSTOP之间的地址空间按照页面大小（单页面大小为4096Bytes，即4KB）
  // 切分并调用kfree()将页面从头部插入到链表kmem.freelist中进行管理。
  // 其中PHYSTOP是KERNBASE+128M，KERNBASE是0x80000000L，这里end并不是KERNBASE，
  // 而是《xv6 book》Figure 3.3中Kernel data之后可以用来分配的内存位置，
  // 也因此真正能分配出去的物理内存并没有128M。分配器刚开始是无内存可用的，
  // 通过对kfree的调用使得它拥有了可以管理的内存。
  for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
//释放v指向的物理内存页，通常应该由调用kalloc()返回。 (例外情况是在初始化分配器的时候，参见上面的kinit。)
void kfree(void *pa)
{
  push_off();
  int id = cpuid();
  pop_off();

  struct run *r;

  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  // 首先将 char *v 开始的页物理内存初始化为1，这是为了让之前使用它的代码不能再读取到有效的内容，
  // 期望这些代码能尽早崩溃以发现问题所在。然后将这空闲页物理内存加到链表头。
  memset(pa, 1, PGSIZE);

  r = (struct run *)pa;

  acquire(&kmem[id].lock);
  r->next = kmem[id].freelist;
  kmem[id].freelist = r;
  release(&kmem[id].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
// 移除并返回空闲链表头的第一个元素，即给调用者分配1页物理内存。
void *
kalloc(void)
{
  struct run *r;
  push_off();
  int id = cpuid();
  pop_off();

  acquire(&kmem[id].lock); //这行
  r = kmem[id].freelist;   // K
  if (r)
    kmem[id].freelist = r->next;
  release(&kmem[id].lock); // 这行
  // 如果上面两行被注释掉了,那么就可能有两个CPU，
  // 记为CPU0和CPU1，同时执行到第K行。然后，两个CPU就会从freelist中拿出同一个内存块，
  // 这就会导致两个进程共用一块内存，但对于进程来说它认为它得到的内存是独享的，
  // 这样A进程再往这块内存中写数据时，会把B之前写进去的数据破坏掉，这显然不是我们所希望的。
  // 所以kalloc()里从freelist中取内存块的操作需要锁，CPU0在取的时候CPU1陷入等待，知道CPU0把
  // freelist更新完解锁后，CPU1再进去取，从而保证每个内存块只被一个进程取走。

  // 窃取其他CPU
  if (!r)
  {
    for (int i = 0; i < NCPU; i++)
    {
      acquire(&kmem[i].lock);
      r = kmem[i].freelist;
      if (r)
        kmem[i].freelist = r->next;
      release(&kmem[i].lock);

      if (r)
        break;
    }
  }
  if (r)
    memset((char *)r, 5, PGSIZE); // fill with junk
  return (void *)r;
}
