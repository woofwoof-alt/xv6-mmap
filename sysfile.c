// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
//#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"

#include "file.h"
#include "proc.h"     // cho myproc()
#include "fcntl.h"

// MMAP: Số lượng VMA tối đa (phải khớp với proc.h)
#define NVMA 16
// MMAP: định nghĩa hằng số
#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define PROT_EXEC   0x4
#define MAP_SHARED  0x01
#define MAP_PRIVATE 0x02
#define MAP_FAILED  ((void*)-1)

pte_t* walk(pagetable_t, uint64, int);

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  argint(n, &fd);
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

  argaddr(1, &p);
  argint(2, &n);
  if(argfd(0, 0, &f) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;
  
  argaddr(1, &p);
  argint(2, &n);
  if(argfd(0, 0, &f) < 0)
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

  argaddr(1, &st);
  if(argfd(0, 0, &f) < 0)
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

  begin_op();
  if((ip = namei(old)) == 0){
    end_op();
    return -1;
  }

  ilock(ip);
  if(ip->type == T_DIR){
    iunlockput(ip);
    end_op();
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

  end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
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

  begin_op();
  if((dp = nameiparent(path, name)) == 0){
    end_op();
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

  end_op();

  return 0;

bad:
  iunlockput(dp);
  end_op();
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

  if((ip = ialloc(dp->dev, type)) == 0){
    iunlockput(dp);
    return 0;
  }

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if(type == T_DIR){  // Create . and .. entries.
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      goto fail;
  }

  if(dirlink(dp, name, ip->inum) < 0)
    goto fail;

  if(type == T_DIR){
    // now that success is guaranteed:
    dp->nlink++;  // for ".."
    iupdate(dp);
  }

  iunlockput(dp);

  return ip;

 fail:
  // something went wrong. de-allocate ip.
  ip->nlink = 0;
  iupdate(ip);
  iunlockput(ip);
  iunlockput(dp);
  return 0;
}

uint64
sys_open(void)
{
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;

  argint(1, &omode);
  if((n = argstr(0, path, MAXPATH)) < 0)
    return -1;

  begin_op();

  if(omode & O_CREATE){
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {
    if((ip = namei(path)) == 0){
      end_op();
      return -1;
    }
    ilock(ip);
    if(ip->type == T_DIR && omode != O_RDONLY){
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){
    iunlockput(ip);
    end_op();
    return -1;
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  if(ip->type == T_DEVICE){
    f->type = FD_DEVICE;
    f->major = ip->major;
  } else {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  if((omode & O_TRUNC) && ip->type == T_FILE){
    itrunc(ip);
  }

  iunlock(ip);
  end_op();

  return fd;
}

uint64
sys_mkdir(void)
{
  char path[MAXPATH];
  struct inode *ip;

  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_mknod(void)
{
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  begin_op();
  argint(1, &major);
  argint(2, &minor);
  if((argstr(0, path, MAXPATH)) < 0 ||
     (ip = create(path, T_DEVICE, major, minor)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();
  
  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  if(ip->type != T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(p->cwd);
  end_op();
  p->cwd = ip;
  return 0;
}

uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  argaddr(1, &uargv);
  if(argstr(0, path, MAXPATH) < 0) {
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
      goto bad;
    if(fetchstr(uarg, argv[i], PGSIZE) < 0)
      goto bad;
  }

  int ret = kexec(path, argv);

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

  argaddr(0, &fdarray);
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
// mmap and munmap system calls (stub)
#define MAP_FAILED ((void*)-1)
// mmap protection flags
#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define PROT_EXEC   0x4

// mmap flags
#define MAP_SHARED  0x01
#define MAP_PRIVATE 0x02
#define MAP_ANONYMOUS 0x20
uint64
sys_mmap(void)
{
    uint64 addr;
    int length, prot, flags, fd, offset;
    struct file *f;
    struct proc *p = myproc();
    
    // Lấy tham số từ user
    argaddr(0, &addr);
    argint(1, &length);
    argint(2, &prot);
    argint(3, &flags);
    argfd(4, &fd, &f);
    argint(5, &offset);
    
    // ========== KIỂM TRA THAM SỐ CƠ BẢN ==========
    if (length <= 0) {
        printf("mmap: invalid length %d\n", length);
        return (uint64)MAP_FAILED;
    }
    
    // ========== XỬ LÝ MAP_ANONYMOUS ==========
    int is_anonymous = 0;
    
    // Nếu fd == -1 hoặc flags có MAP_ANONYMOUS
    if (fd == -1 || (flags & MAP_ANONYMOUS)) {
        is_anonymous = 1;
        f = 0;                      // Không có file
        flags |= MAP_ANONYMOUS;     // Đánh dấu anonymous
        offset = 0;                 // offset phải là 0
        
        // Anonymous: bỏ qua kiểm tra file
    }
    // ========== KẾT THÚC MAP_ANONYMOUS ==========
    
    // ========== KIỂM TRA FILE-BACKED ==========
    if (!is_anonymous) {
        // Kiểm tra offset phải là bội số của PGSIZE
        if (offset % PGSIZE != 0) {
            printf("mmap: offset must be page-aligned\n");
            return (uint64)MAP_FAILED;
        }
        
        // Kiểm tra fd hợp lệ
        if (f == 0) {
            printf("mmap: invalid fd\n");
            return (uint64)MAP_FAILED;
        }
        
        // Kiểm tra quyền đọc file
        if ((prot & PROT_READ) && f->readable == 0) {
            printf("mmap: file not readable\n");
            return (uint64)MAP_FAILED;
        }
        
        // Kiểm tra quyền ghi file cho MAP_SHARED
        if ((prot & PROT_WRITE) && (flags & MAP_SHARED) && f->writable == 0) {
            printf("mmap: file not writable for MAP_SHARED\n");
            return (uint64)MAP_FAILED;
        }
    }
    // ========== KẾT THÚC KIỂM TRA ==========
    
    // ========== TÌM VMA TRỐNG ==========
    int empty_slot = -1;
    for (int i = 0; i < NVMA; i++) {
        if (p->vma[i].used == 0) {
            empty_slot = i;
            break;
        }
    }
    
    if (empty_slot == -1) {
        printf("mmap: no free VMA slot (max %d)\n", NVMA);
        return (uint64)MAP_FAILED;
    }
    // ========== KẾT THÚC ==========
    
    // ========== TÍNH TOÁN ĐỊA CHỈ ẢO ==========
    // Làm tròn length lên bội số của PGSIZE
    int aligned_length = length;
    if (aligned_length % PGSIZE != 0) {
        aligned_length = (aligned_length + PGSIZE - 1) & ~(PGSIZE - 1);
    }
    
    // Tìm địa chỉ ảo trống, bắt đầu từ 0x60000000
    uint64 va = 0x60000000;
    for (int i = 0; i < NVMA; i++) {
        if (p->vma[i].used && p->vma[i].addr + p->vma[i].len > va) {
            va = (p->vma[i].addr + p->vma[i].len + PGSIZE - 1) & ~(PGSIZE - 1);
        }
    }
    // ========== KẾT THÚC ==========
    
    // ========== LƯU VMA ==========
    p->vma[empty_slot].used = 1;
    p->vma[empty_slot].addr = va;
    p->vma[empty_slot].len = aligned_length;
    p->vma[empty_slot].prot = prot;
    p->vma[empty_slot].flags = flags;
    p->vma[empty_slot].file = f;
    p->vma[empty_slot].offset = offset;
    
    // Tăng tham chiếu file (nếu có)
    if (f) {
        filedup(f);
    }
    // ========== KẾT THÚC ==========
    
    printf("mmap: slot=%d, va=%p, len=%d, flags=%x, prot=%x, %s\n",
           empty_slot, (void*)va, aligned_length, flags, prot,
           is_anonymous ? "anonymous" : "file-backed");
    
    return va;
}

uint64
sys_munmap(void)
{
    uint64 addr;
    int length;
    struct proc *p = myproc();
    int found = -1;
    
    argaddr(0, &addr);
    argint(1, &length);
    
    printf("sys_munmap: addr=%p, length=%d\n", (void*)addr, length);
    
    if (length <= 0 || addr % PGSIZE != 0) {
        return -1;
    }
    
    // Tìm VMA chứa địa chỉ
    for (int i = 0; i < NVMA; i++) {
        if (p->vma[i].used && addr >= p->vma[i].addr && 
            addr < p->vma[i].addr + p->vma[i].len) {
            found = i;
            break;
        }
    }
    
    if (found == -1) {
        printf("munmap: address not in any VMA\n");
        return -1;
    }
    
    struct vm_area *vma = &p->vma[found];
    uint64 start_va = addr;
    uint64 end_va = start_va + PGROUNDUP(length);
    
    if (end_va > vma->addr + vma->len) {
        end_va = vma->addr + vma->len;
    }
    
    printf("munmap: unmap [%p, %p) from VMA[%d]\n", 
           (void*)start_va, (void*)end_va, found);
    
    // ========== GHI LẠI FILE NẾU MAP_SHARED ==========
    if ((vma->flags & MAP_SHARED) && (vma->prot & PROT_WRITE) && vma->file) {
        begin_op();
        for (uint64 va = start_va; va < end_va; va += PGSIZE) {
            pte_t *pte = walk(p->pagetable, va, 0);
            if (pte && (*pte & PTE_V) && (*pte & PTE_W)) {
                uint64 pa = PTE2PA(*pte);
                uint64 offset = vma->offset + (va - vma->addr);
                
                ilock(vma->file->ip);
                writei(vma->file->ip, 0, pa, offset, PGSIZE);
                iunlock(vma->file->ip);
            }
            uvmunmap(p->pagetable, va, 1, 0);
        }
        end_op();
    }
    // ========== KẾT THÚC ==========
    
    // ========== XỬ LÝ MAP_PRIVATE (COW) ==========
    else if ((vma->flags & MAP_PRIVATE) && vma->file) {
        for (uint64 va = start_va; va < end_va; va += PGSIZE) {
            pte_t *pte = walk(p->pagetable, va, 0);
            if (pte && (*pte & PTE_V)) {
                uint64 pa = PTE2PA(*pte);
                // Giảm refcount (sẽ được giải phóng nếu count=0)
                krefdec((void*)pa);
                uvmunmap(p->pagetable, va, 1, 0);
            }
        }
    }
    // ========== KẾT THÚC ==========
    
    // ========== XỬ LÝ MAP_ANONYMOUS ==========
    else if (vma->flags & MAP_ANONYMOUS) {
        for (uint64 va = start_va; va < end_va; va += PGSIZE) {
            pte_t *pte = walk(p->pagetable, va, 0);
            if (pte && (*pte & PTE_V)) {
                uint64 pa = PTE2PA(*pte);
                // Giải phóng trang trực tiếp (không có file)
                kfree((void*)pa);
                uvmunmap(p->pagetable, va, 1, 0);
            }
        }
    }
    // ========== KẾT THÚC ==========
    
    // Cập nhật VMA
    if (start_va == vma->addr && end_va == vma->addr + vma->len) {
        // Unmap toàn bộ
        if (vma->file) {
            fileclose(vma->file);
        }
        vma->used = 0;
        printf("munmap: freed entire VMA slot %d\n", found);
    } else if (start_va == vma->addr) {
        // Unmap từ đầu
        vma->addr = end_va;
        vma->len -= (end_va - start_va);
        vma->offset += (end_va - start_va);
        printf("munmap: shrunk VMA from start\n");
    } else if (end_va == vma->addr + vma->len) {
        // Unmap đến cuối
        vma->len -= (end_va - start_va);
        printf("munmap: shrunk VMA from end\n");
    }
    
    return 0;
}
