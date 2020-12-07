#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

// Buddy allocator

static int nsizes; // the number of entries in bd_sizes array

#define in_range(a, b, x) (((x) >= (a)) && ((x) < (b)))
#define LEAF_SIZE 16                          // The smallest block size
#define MAXSIZE (nsizes - 1)                  // Largest index in bd_sizes array
#define BLK_SIZE(k) ((1L << (k)) * LEAF_SIZE) // Size of block at size k
#define HEAP_SIZE BLK_SIZE(MAXSIZE)
#define NBLK(k) (1 << (MAXSIZE - k))                   // Number of block at size k
#define ROUNDUP(n, sz) (((((n)-1) / (sz)) + 1) * (sz)) // Round up to the next multiple of sz
// 即四舍五入到下一个sz的倍数, 返回值一定大于等于n. 即: n<=k*size, k=n/size+1

typedef struct list Bd_list;

// The allocator has sz_info for each size k. Each sz_info has a free
// list, an array alloc to keep track which blocks have been
// allocated, and an split array to to keep track which blocks have
// been split.  The arrays are of type char (which is 1 byte), but the
// allocator uses 1 bit per block (thus, one char records the info of
// 8 blocks).
// 每一个sz_info都有一个空闲列表，一个用来跟踪哪些块被分配的数组alloc和一个用来跟踪哪些块被分割的数组split。
// 数组的类型是char（也就是1个byte），但分配器对每个块使用1个bit
// （因此，一个char记录了8个块的信息）。
struct sz_info
{
  // 声明链表
  Bd_list free;
  char *alloc;
  char *split;
};
typedef struct sz_info Sz_info;

static Sz_info *bd_sizes;
static void *bd_base; // start address of memory managed by the buddy allocator
static struct spinlock lock;

// Return 1 if bit at position index in array is set to 1
// 如果index的位置的bit是1就return 1;
int bit_isset(char *array, int index)
{
  // 整除8,得到在哪个char
  char b = array[index / 8];
  // 模8,得到在char的第几个bit
  char m = (1 << (index % 8));
  return (b & m) == m;
}

// Set bit at position index in array to 1
void bit_set(char *array, int index)
{
  // 整除8,得到在哪个char
  char b = array[index / 8];
  // 模8,得到在char的第几个bit
  char m = (1 << (index % 8));
  //该位置或上去
  array[index / 8] = (b | m);
}

// Clear bit at position index in array
void bit_clear(char *array, int index)
{
  char b = array[index / 8];
  char m = (1 << (index % 8));
  array[index / 8] = (b & ~m);
}
// 反转比index表示的块的更小一层的位置的bit对应的上一层的标识位
// 其实就是除了个2，懂得都懂
void bit_toggle(char *array, int index)
{
  index >>= 1;
  char m = (1 << (index % 8));
  array[index / 8] ^= m;
}
// 与bit_toggle一样
int bit_get(char *array, int index)
{
  index >>= 1;
  return bit_isset(array, index);
}

// Print a bit vector as a list of ranges of 1 bits
void bd_print_vector(char *vector, int len)
{
  int last, lb;

  last = 1;
  lb = 0;
  for (int b = 0; b < len; b++)
  {
    // 如果当前b位置上的bit值和上一个位置上的bit值相同则跳过
    // 这里就可以保证lb到b的区间里的bit值是一样的
    if (last == bit_isset(vector, b))
      continue;
    // 如果不一样的话，如果last是1,则说明前面的那段空间里是已经分配过的内存，而现在的b的位置开始是没有分配过的
    if (last == 1)
      //打印上一个b和现在的b
      printf(" [%d, %d)", lb, b);
    // lb保存的是上一个b
    lb = b;
    // last保存的是上一个b的位置上的bit的值
    last = bit_isset(vector, b);
  }
  // 考虑vector长度为0时 以及 b==len时的情况
  if (lb == 0 || last == 1)
  {
    printf(" [%d, %d)", lb, len);
  }
  printf("\n");
}

// Print buddy's data structures
void bd_print()
{
  for (int k = 0; k < nsizes; k++)
  {
    printf("size %d (blksz %d nblk %d): free list: ", k, BLK_SIZE(k), NBLK(k));
    lst_print(&bd_sizes[k].free);
    printf("  alloc:");
    bd_print_vector(bd_sizes[k].alloc, NBLK(k));
    if (k > 0)
    {
      printf("  split:");
      bd_print_vector(bd_sizes[k].split, NBLK(k));
    }
  }
}

// What is the first k such that 2^k >= n?
// 获得第一个大于n的2^k
int firstk(uint64 n)
{
  int k = 0;
  uint64 size = LEAF_SIZE;

  while (size < n)
  {
    k++;
    size *= 2;
  }
  return k;
}

// Compute the block index for address p at size k
// 计算大小为k的地址p在第几块内存中
int blk_index(int k, char *p)
{
  // n为p的地址偏移量
  int n = p - (char *)bd_base;
  return n / BLK_SIZE(k);
}

// Convert a block index at size k back into an address
// 和blk_index是互逆的操作
void *addr(int k, int bi)
{
  int n = bi * BLK_SIZE(k);
  return (char *)bd_base + n;
}

// allocate nbytes, but malloc won't return anything smaller than LEAF_SIZE
void *
bd_malloc(uint64 nbytes)
{
  int fk, k;

  acquire(&lock);

  // Find a free block >= nbytes, starting with smallest k possible
  fk = firstk(nbytes);

  // 这个循环用于找到符合fk大小需求的块，k为找到的存在于空闲链表中的块
  for (k = fk; k < nsizes; k++)
  {
    // 第k个大小的内存是否为空，如果不为空，就得申请更大的内存了
    if (!lst_empty(&bd_sizes[k].free))
      break;
  }
  // 如果k大于最大的能申请的内存，就完蛋了
  if (k >= nsizes)
  { // No free blocks?
    release(&lock);
    return 0;
  }

  // Found a block; pop it and potentially split it.
  // 把找到的k从空闲链表中pop出来
  char *p = lst_pop(&bd_sizes[k].free);
  // 由于拿到的这块内存要被分配了，所以要将这块标记为已分配
  // 在原来的代码中是像下面这样的
  // bit_set(bd_sizes[k].alloc, blk_index(k, p));
  // 导致的问题就是，每个内存块都有一个bit来指示被占用或释放。
  // 对于兄弟块，可以共用一个bit，用异或操作实现
  bit_toggle(bd_sizes[k].alloc, blk_index(k, p));
  // 如果分配到的块的大小k，是大于需求的fk的，那么要将其分割到合适的大小
  for (; k > fk; k--)
  {
    // split a block at size k and mark one half allocated at size k-1
    // and put the buddy on the free list at size k-1
    // 分割为1/2 balabala
    char *q = p + BLK_SIZE(k - 1); // p's buddy
    // 由于要分割，所以将这块标记为被分割
    bit_set(bd_sizes[k].split, blk_index(k, p));
    // 获取兄弟块中的一块(随便哪块都行), 原来是1表示还有1块空余,原来是0表示两块都是空的
    // 1去异或1,表示buddy两块都没了, 1去异或0表示还剩一块,都消耗了1块
    bit_toggle(bd_sizes[k - 1].alloc, blk_index(k - 1, p));
    lst_push(&bd_sizes[k - 1].free, q);
  }
  release(&lock);

  return p;
}

// Find the size of the block that p points to.
// 找出p指向的块的大小。
int size(char *p)
{
  for (int k = 0; k < nsizes; k++)
  {
    if (bit_isset(bd_sizes[k + 1].split, blk_index(k + 1, p)))
    {
      return k;
    }
  }
  return 0;
}

// Free memory pointed to by p, which was earlier allocated using
// bd_malloc.
void bd_free(void *p)
{
  void *q;
  int k;

  acquire(&lock);
  for (k = size(p); k < MAXSIZE; k++)
  {
    int bi = blk_index(k, p);
    int buddy = (bi % 2 == 0) ? bi + 1 : bi - 1;
    bit_toggle(bd_sizes[k].alloc, bi); // free p at size k
    if (bit_get(bd_sizes[k].alloc, buddy))
    {        // is buddy allocated?
      break; // break out of loop
    }
    // budy is free; merge with buddy
    q = addr(k, buddy);
    lst_remove(q); // remove buddy from free list
    if (buddy % 2 == 0)
    {
      p = q;
    }
    // at size k+1, mark that the merged buddy pair isn't split
    // anymore
    bit_clear(bd_sizes[k + 1].split, blk_index(k + 1, p));
  }
  lst_push(&bd_sizes[k].free, p);
  release(&lock);
}

// Compute the first block at size k that doesn't contain p
int blk_index_next(int k, char *p)
{
  int n = (p - (char *)bd_base) / BLK_SIZE(k);
  if ((p - (char *)bd_base) % BLK_SIZE(k) != 0)
    n++;
  return n;
}

int log2(uint64 n)
{
  int k = 0;
  while (n > 1)
  {
    k++;
    n = n >> 1;
  }
  return k;
}

// Mark memory from [start, stop), starting at size 0, as allocated.
void bd_mark(void *start, void *stop)
{
  int bi, bj;

  if (((uint64)start % LEAF_SIZE != 0) || ((uint64)stop % LEAF_SIZE != 0))
    panic("bd_mark");

  for (int k = 0; k < nsizes; k++)
  {
    bi = blk_index(k, start);
    bj = blk_index_next(k, stop);
    for (; bi < bj; bi++)
    {
      if (k > 0)
      {
        // if a block is allocated at size k, mark it as split too.
        bit_set(bd_sizes[k].split, bi);
      }
      bit_toggle(bd_sizes[k].alloc, bi);
    }
  }
}

// If a block is marked as allocated and the buddy is free, put the
// buddy on the free list at size k.
//如果一个块被标记为已分配，而好友是空闲的，则将好友放在空闲列表中，大小为k。
int bd_initfree_pair(int k, int bi, void *allow_left, void *allow_right)
{
  int buddy = (bi % 2 == 0) ? bi + 1 : bi - 1;
  int free = 0;
  // 如果有一个是空闲的
  if (bit_get(bd_sizes[k].alloc, bi))
  {
    // one of the pair is free
    free = BLK_SIZE(k);
    // 如果buddy是在对应的区间内
    if (in_range(allow_left, allow_right, addr(k, buddy)))
      lst_push(&bd_sizes[k].free, addr(k, buddy)); // put buddy on free list
    else
      lst_push(&bd_sizes[k].free, addr(k, bi)); // put bi on free list
  }
  return free;
}

// Initialize the free lists for each size k.  For each size k, there
// are only two pairs that may have a buddy that should be on free list:
// bd_left and bd_right.
// 初始化每个大小k的自由列表。
// 对于每个大小k，只有两个对可能有一个好友应该在自由列表中：bd_left和bd_right。
int bd_initfree(void *bd_left, void *bd_right, void *allow_left, void *allow_right)
{
  int free = 0;

  for (int k = 0; k < MAXSIZE; k++)
  { // skip max size
    int left = blk_index_next(k, bd_left);
    int right = blk_index(k, bd_right);
    free += bd_initfree_pair(k, left, allow_left, allow_right);
    if (right <= left)
      continue;
    free += bd_initfree_pair(k, right, allow_left, allow_right);
  }
  return free;
}

// Mark the range [bd_base,p) as allocated
int bd_mark_data_structures(char *p)
{
  int meta = p - (char *)bd_base;
  printf("bd: %d meta bytes for managing %d bytes of memory\n", meta, BLK_SIZE(MAXSIZE));
  bd_mark(bd_base, p);
  return meta;
}

// Mark the range [end, HEAPSIZE) as allocated
int bd_mark_unavailable(void *end, void *left)
{
  int unavailable = BLK_SIZE(MAXSIZE) - (end - bd_base);
  if (unavailable > 0)
    unavailable = ROUNDUP(unavailable, LEAF_SIZE);
  printf("bd: 0x%x bytes unavailable\n", unavailable);

  void *bd_end = bd_base + BLK_SIZE(MAXSIZE) - unavailable;
  bd_mark(bd_end, bd_base + BLK_SIZE(MAXSIZE));
  return unavailable;
}

// Initialize the buddy allocator: it manages memory from [base, end).
// bd_malloc()对应于分配过程，bd_free()对应于释放过程。
// bd_init()主要做了以下工作：
// 起始地址/末地址16字节地址对齐
// 初始化元数据区
// 标记元数据区
// 标记无效区
// 初始化各层空闲链表
void bd_init(void *base, void *end)
{
  char *p = (char *)ROUNDUP((uint64)base, LEAF_SIZE);
  int sz;

  initlock(&lock, "buddy");
  bd_base = (void *)p;

  // compute the number of sizes we need to manage [base, end)
  nsizes = log2(((char *)end - p) / LEAF_SIZE) + 1;
  if ((char *)end - p > BLK_SIZE(MAXSIZE))
  {
    nsizes++; // round up to the next power of 2
  }

  printf("bd: memory sz is %d bytes; allocate an size array of length %d\n",
         (char *)end - p, nsizes);

  // allocate bd_sizes array
  bd_sizes = (Sz_info *)p;
  p += sizeof(Sz_info) * nsizes;
  memset(bd_sizes, 0, sizeof(Sz_info) * nsizes);

  // initialize free list and allocate the alloc array for each size k
  for (int k = 0; k < nsizes; k++)
  {
    lst_init(&bd_sizes[k].free);
    sz = sizeof(char) * ROUNDUP(NBLK(k), 16) / 16;
    bd_sizes[k].alloc = p;
    memset(bd_sizes[k].alloc, 0, sz);
    p += sz;
  }

  // allocate the split array for each size k, except for k = 0, since
  // we will not split blocks of size k = 0, the smallest size.
  for (int k = 1; k < nsizes; k++)
  {
    sz = sizeof(char) * (ROUNDUP(NBLK(k), 8)) / 8;
    bd_sizes[k].split = p;
    memset(bd_sizes[k].split, 0, sz);
    p += sz;
  }
  p = (char *)ROUNDUP((uint64)p, LEAF_SIZE);

  // done allocating; mark the memory range [base, p) as allocated, so
  // that buddy will not hand out that memory.
  int meta = bd_mark_data_structures(p);

  // mark the unavailable memory range [end, HEAP_SIZE) as allocated,
  // so that buddy will not hand out that memory.
  int unavailable = bd_mark_unavailable(end, p);
  void *bd_end = bd_base + BLK_SIZE(MAXSIZE) - unavailable;

  // initialize free lists for each size k
  int free = bd_initfree(p, bd_end, p, end);

  // check if the amount that is free is what we expect
  if (free != BLK_SIZE(MAXSIZE) - meta - unavailable)
  {
    printf("free %d %d\n", free, BLK_SIZE(MAXSIZE) - meta - unavailable);
    panic("bd_init: free mem");
  }
}
