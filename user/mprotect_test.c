#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define PROT_EXEC   0x4
#define PROT_NONE   0x0
#define MAP_SHARED  0x01
#define MAP_PRIVATE 0x02
#define MAP_ANONYMOUS 0x20
#define MAP_FAILED  ((void*)-1)

int main() {
    printf("\n========== MPROTECT TEST ==========\n");
    
    // Tạo vùng nhớ anonymous
    int len = 4096;
    char *addr = mmap(0, len, PROT_READ | PROT_WRITE, 
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    
    if (addr == MAP_FAILED) {
        printf("mmap failed\n");
        exit(1);
    }
    
    printf("mmap returned: %p\n", addr);
    printf("Initial permissions: READ | WRITE\n");
    
    // Ghi dữ liệu
    addr[0] = 'A';
    addr[1] = 'B';
    addr[2] = 'C';
    printf("Wrote ABC at addr[0-2]\n");
    
    // Test 1: Đổi sang READ-ONLY
    printf("\n--- Test 1: Change to READ-ONLY ---\n");
    if (mprotect(addr, len, PROT_READ) < 0) {
        printf("mprotect failed\n");
    } else {
        printf("mprotect READ-ONLY success\n");
        
        // Đọc vẫn được
        printf("Read addr[0]: %c\n", addr[0]);
        
        // Ghi sẽ gây lỗi (page fault)
        printf("Trying to write... (will cause fault)\n");
        // addr[0] = 'X';  // Uncomment để test lỗi
    }
    
    // Test 2: Đổi sang READ-WRITE
    printf("\n--- Test 2: Change to READ-WRITE ---\n");
    if (mprotect(addr, len, PROT_READ | PROT_WRITE) < 0) {
        printf("mprotect failed\n");
    } else {
        printf("mprotect READ-WRITE success\n");
        addr[0] = 'X';
        addr[1] = 'Y';
        addr[2] = 'Z';
        printf("After write: %c%c%c\n", addr[0], addr[1], addr[2]);
    }
    
    // Test 3: Đổi sang PROT_NONE
    printf("\n--- Test 3: Change to PROT_NONE ---\n");
    if (mprotect(addr, len, PROT_NONE) < 0) {
        printf("mprotect failed\n");
    } else {
        printf("mprotect PROT_NONE success\n");
        printf("Trying to read... (will cause fault)\n");
        // printf("%c\n", addr[0]);  // Uncomment sẽ gây lỗi
    }
    
    munmap(addr, len);
    
    printf("\n========== MPROTECT TEST PASSED ==========\n");
    exit(0);
}


