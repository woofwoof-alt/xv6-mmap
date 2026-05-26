cat > README.md << 'EOF'
# xv6-mmap: Memory-mapped file implementation for xv6-riscv

## Project Structure

### Kernel Files (`/kernel`)
| File | Description |
|------|-------------|
| `syscall.h` | System call numbers for mmap, munmap, mprotect |
| `syscall.c` | System call dispatch table |
| `sysfile.c` | mmap, munmap, mprotect implementation |
| `proc.h` | VMA (Virtual Memory Area) structure |
| `proc.c` | Process management with VMA (fork, exit) |
| `trap.c` | Page fault handler for lazy loading and COW |
| `kalloc.c` | Physical memory with reference counting |
| `defs.h` | Function declarations |
| `vm.c` | Virtual memory utilities |

### User Files (`/user`)
| File | Description |
|------|-------------|
| `usys.pl` | User stub for system calls |
| `user.h` | User space declarations |
| `mmaptest.c` | Basic mmap test |
| `mmaptest_full.c` | Comprehensive test suite |
| `cow_test.c` | Copy-on-Write test |
| `cow_fork.c` | Fork with COW test |
| `mmap_anon.c` | MAP_ANONYMOUS test |
| `mprotect_test.c` | mprotect test |

## Features
- mmap/munmap with VMA
- MAP_SHARED with disk sync
- MAP_PRIVATE with Copy-on-Write (COW)
- MAP_ANONYMOUS zero-filled memory
- mprotect permission change
- Lazy loading via page fault
- Reference count for physical pages
- Fork and exit support
EOF
