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

// ========== THÊM REFERENCE COUNT ==========
#define REFCNT_MAX 32768  // (PHYSTOP - KERNBASE) / PGSIZE
struct refcount {
    int count;
    struct spinlock lock;
} refcnt[REFCNT_MAX];

// Lấy index của trang từ địa chỉ vật lý
static uint64
get_refindex(void *pa) {
    return ((uint64)pa - KERNBASE) / PGSIZE;
}

// Khởi tạo refcount cho trang
void
krefinit(void *pa) {
    uint64 idx = get_refindex(pa);
    initlock(&refcnt[idx].lock, "refcnt");
    refcnt[idx].count = 1;
}

// Tăng tham chiếu
void
krefinc(void *pa) {
    uint64 idx = get_refindex(pa);
    acquire(&refcnt[idx].lock);
    if (refcnt[idx].count > 0) {
        refcnt[idx].count++;
    }
    release(&refcnt[idx].lock);
}

// Giảm tham chiếu, trả về 1 nếu về 0
int
krefdec(void *pa) {
    uint64 idx = get_refindex(pa);
    int last = 0;
    acquire(&refcnt[idx].lock);
    if (refcnt[idx].count > 0) {
        refcnt[idx].count--;
        if (refcnt[idx].count == 0) {
            last = 1;
        }
    }
    release(&refcnt[idx].lock);
    return last;
}

// Lấy số tham chiếu hiện tại
int
krefget(void *pa) {
    uint64 idx = get_refindex(pa);
    int count;
    acquire(&refcnt[idx].lock);
    count = refcnt[idx].count;
    release(&refcnt[idx].lock);
    return count;
}
// ========== KẾT THÚC ==========


void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by pa,
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
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
