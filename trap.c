#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "file.h"


#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define PROT_EXEC   0x4
#define MAP_SHARED  0x01
#define MAP_PRIVATE 0x02
#define MAP_ANONYMOUS 0x20


extern pte_t* walk(pagetable_t, uint64, int);
extern void* kalloc(void);
extern void kfree(void*);
struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from, and returns to, trampoline.S
// return value is user satp for trampoline.S to switch to.
//
uint64
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);  //DOC: kernelvec

  struct proc *p = myproc();
  
  // save user program counter.
  p->trapframe->epc = r_sepc();
  
  if(r_scause() == 8){
    // system call

    if(killed(p))
      kexit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sepc, scause, and sstatus,
    // so enable only now that we're done with those registers.
    intr_on();

    syscall();
// Trong usertrap(), sau phần xử lý syscall và interrupt

} else if((which_dev = devintr()) != 0){
    // ok

// Trong usertrap(), thay thế phần xử lý page fault
// Trong kernel/trap.c - hàm usertrap()
// Phần xử lý exception (không phải syscall, không phải devintr)

} else {
    uint64 va = r_stval();
    uint64 scause = r_scause();
    struct proc *p = myproc();
    int handled = 0;
    
    // Debug: in thông tin page fault
    printf("\n=== PAGE FAULT ===\n");
    printf("pid=%d, scause=%lx (", p->pid, scause);
    if (scause == 13) printf("load page fault");
    else if (scause == 15) printf("store page fault");
    else printf("unknown");
    printf(")\n");
    printf("fault address=%p\n", (void*)va);
    printf("sepc=%p\n", (void*)r_sepc());
    
    // Chỉ xử lý load page fault (13) hoặc store page fault (15)
    if (scause == 13 || scause == 15) {
        
        // Duyệt tìm VMA chứa địa chỉ gây lỗi
        for (int i = 0; i < NVMA; i++) {
            if (p->vma[i].used && va >= p->vma[i].addr && 
                va < p->vma[i].addr + p->vma[i].len) {
                
                handled = 1;
                struct vm_area *vma = &p->vma[i];
                uint64 va_aligned = va & ~(PGSIZE - 1);
                
                printf("Found VMA[%d]: addr=%p, len=%d, flags=%x, prot=%x\n",
                       i, (void*)vma->addr, vma->len, vma->flags, vma->prot);
                
                // ============================================================
                // PHẦN 1: XỬ LÝ COPY-ON-WRITE (COW) CHO MAP_PRIVATE
                // ============================================================
                // Điều kiện: store fault + MAP_PRIVATE + trang đang READ-ONLY
                if (scause == 15 && (vma->flags & MAP_PRIVATE)) {
                    pte_t *pte = walk(p->pagetable, va_aligned, 0);
                    
                    if (pte && (*pte & PTE_V)) {
                        // Nếu trang chưa có quyền ghi (đang READ-ONLY)
                        if ((*pte & PTE_W) == 0) {
                            uint64 oldpa = PTE2PA(*pte);
                            int refcnt = krefget((void*)oldpa);
                            
                            printf("COW: va=%p, oldpa=%p, refcnt=%d\n", 
                                   (void*)va_aligned, (void*)oldpa, refcnt);
                            
                            if (refcnt > 1) {
                                // Có nhiều tiến trình đang dùng → copy trang
                                // Giảm refcount của trang cũ
                                krefdec((void*)oldpa);
                                
                                // Cấp phát trang mới
                                char *newmem = kalloc();
                                if (newmem == 0) {
                                    printf("usertrap: COW kalloc failed\n");
                                    p->killed = 1;
                                    break;
                                }
                                
                                // Copy dữ liệu
                                memmove(newmem, (void*)oldpa, PGSIZE);
                                
                                // Hủy ánh xạ cũ (không giải phóng)
                                uvmunmap(p->pagetable, va_aligned, 1, 0);
                                
                                // Map trang mới với quyền READ-WRITE
                                int perm = PTE_U | PTE_R | PTE_W;
                                if (mappages(p->pagetable, va_aligned, PGSIZE, 
                                             (uint64)newmem, perm) != 0) {
                                    kfree(newmem);
                                    p->killed = 1;
                                }
                                printf("COW: copied to new page %p\n", newmem);
                            } else {
                                // Chỉ còn một tiến trình dùng
                                *pte |= PTE_W;
                                printf("COW: set PTE_W directly\n");
                            }
                            break;
                        }
                    }
                    // Nếu trang chưa được cấp phát, rơi xuống xử lý bình thường
                }
                
                // ============================================================
                // PHẦN 2: XỬ LÝ MAP_ANONYMOUS (không có file)
                // ============================================================
                if (vma->flags & MAP_ANONYMOUS) {
                    printf("Handling MAP_ANONYMOUS: va=%p\n", (void*)va_aligned);
                    
                    char *mem = kalloc();
                    if (mem == 0) {
                        printf("usertrap: anonymous kalloc failed\n");
                        p->killed = 1;
                        break;
                    }
                    
                    memset(mem, 0, PGSIZE);
                    
                    int perm = PTE_U;
                    if (vma->prot & PROT_READ) perm |= PTE_R;
                    if ((vma->flags & MAP_SHARED) && (vma->prot & PROT_WRITE)) {
                        perm |= PTE_W;
                    }
                    if (vma->prot & PROT_EXEC) perm |= PTE_X;
                    
                    if (mappages(p->pagetable, va_aligned, PGSIZE, (uint64)mem, perm) != 0) {
                        kfree(mem);
                        p->killed = 1;
                    }
                    printf("MAP_ANONYMOUS: mapped %p to %p\n", (void*)va_aligned, mem);
                    break;
                }
                
                // ============================================================
                // PHẦN 3: XỬ LÝ FILE-BACKED (MAP_SHARED hoặc lần đầu MAP_PRIVATE)
                // ============================================================
                printf("Handling file-backed: va=%p\n", (void*)va_aligned);
                
                // Tính offset trong file
                uint64 offset = vma->offset + (va_aligned - vma->addr);
                
                printf("file_offset=%ld\n", offset);
                
                // Cấp phát trang vật lý
                char *mem = kalloc();
                if (mem == 0) {
                    printf("usertrap: kalloc failed\n");
                    p->killed = 1;
                    break;
                }
                
                memset(mem, 0, PGSIZE);
                
                // Đọc dữ liệu từ file (nếu có file)
                if (vma->file) {
                    ilock(vma->file->ip);
                    int n = readi(vma->file->ip, 0, (uint64)mem, offset, PGSIZE);
                    iunlock(vma->file->ip);
                    
                    if (n < 0) {
                        kfree(mem);
                        printf("usertrap: readi failed\n");
                        p->killed = 1;
                        break;
                    }
                    printf("readi returned %d bytes\n", n);
                }
                
                // Tính quyền trang
                int perm = PTE_U;
                if (vma->prot & PROT_READ) perm |= PTE_R;
                
                // MAP_SHARED và có quyền ghi: set PTE_W ngay
                if ((vma->flags & MAP_SHARED) && (vma->prot & PROT_WRITE)) {
                    perm |= PTE_W;
                    printf("Set PTE_W for MAP_SHARED\n");
                }
                // MAP_PRIVATE: KHÔNG set PTE_W ở lần đầu (để COW xử lý sau)
                
                if (vma->prot & PROT_EXEC) perm |= PTE_X;
                
                printf("perm=%x (R=%d, W=%d, U=%d)\n", perm, 
                       !!(perm & PTE_R), !!(perm & PTE_W), !!(perm & PTE_U));
                
                // Ánh xạ trang vào bảng trang
                if (mappages(p->pagetable, va_aligned, PGSIZE, (uint64)mem, perm) != 0) {
                    kfree(mem);
                    printf("usertrap: mappages failed\n");
                    p->killed = 1;
                }
                printf("File-backed: mapped %p to %p\n", (void*)va_aligned, mem);
                break;
            }
        }
        
        // Nếu không tìm thấy VMA nào chứa địa chỉ
        if (!handled) {
            printf("usertrap(): page fault at %p not in any VMA, pid=%d\n", 
                   (void*)va, p->pid);
            
            // In danh sách VMA hiện có để debug
            printf("Current VMA list:\n");
            for (int i = 0; i < NVMA; i++) {
                if (p->vma[i].used) {
                    printf("  VMA[%d]: addr=%p - %p, flags=%x\n", i,
                           (void*)p->vma[i].addr,
                           (void*)(p->vma[i].addr + p->vma[i].len),
                           p->vma[i].flags);
                }
            }
            p->killed = 1;
        }
        
    } else {
        // Các exception khác (không phải page fault)
        printf("usertrap(): unexpected scause %p pid=%d\n", (void*)scause, p->pid);
        printf("            sepc=%p stval=%p\n", (void*)r_sepc(), (void*)va);
        p->killed = 1;
    }
}
  if(killed(p))
    kexit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2)
    yield();

  prepare_return();

  // the user page table to switch to, for trampoline.S
  uint64 satp = MAKE_SATP(p->pagetable);

  // return to trampoline.S; satp value in a0.
  return satp;
}

//
// set up trapframe and control registers for a return to user space
//
void
prepare_return(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(). because a trap from kernel
  // code to usertrap would be a disaster, turn off interrupts.
  intr_off();

  // send syscalls, interrupts, and exceptions to uservec in trampoline.S
  uint64 trampoline_uservec = TRAMPOLINE + (uservec - trampoline);
  w_stvec(trampoline_uservec);

  // set up trapframe values that uservec will need when
  // the process next traps into the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    // interrupt or trap from an unknown source
    printf("scause=0x%lx sepc=0x%lx stval=0x%lx\n", scause, r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  if(cpuid() == 0){
    acquire(&tickslock);
    ticks++;
    wakeup(&ticks);
    release(&tickslock);
  }

  // ask for the next timer interrupt. this also clears
  // the interrupt request. 1000000 is about a tenth
  // of a second.
  w_stimecmp(r_time() + 1000000);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if(scause == 0x8000000000000009L){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000005L){
    // timer interrupt.
    clockintr();
    return 2;
  } else {
    return 0;
  }
}

