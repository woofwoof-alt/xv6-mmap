#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define MAP_SHARED  0x01
#define MAP_PRIVATE 0x02
#define MAP_ANONYMOUS 0x20
#define MAP_FAILED  ((void*)-1)

int main() {
    printf("\n========== MAP_ANONYMOUS TEST ==========\n");
    
    // mmap không cần file, fd = -1
    int len = 4096;
    char *addr = mmap(0, len, PROT_READ | PROT_WRITE, 
                      MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    
    if (addr == MAP_FAILED) {
        printf("mmap anonymous failed\n");
        exit(1);
    }
    
    printf("mmap anonymous returned: %p\n", addr);
    
    // Kiểm tra vùng nhớ ban đầu toàn 0
    int all_zero = 1;
    for (int i = 0; i < len; i++) {
        if (addr[i] != 0) {
            all_zero = 0;
            break;
        }
    }
    printf("Initial memory is all zero: %s\n", all_zero ? "YES" : "NO");
    
    // Ghi dữ liệu
    for (int i = 0; i < 10; i++) {
        addr[i] = 'A' + i;
    }
    printf("Written data: ");
    for (int i = 0; i < 10; i++) {
        printf("%c", addr[i]);
    }
    printf("\n");
    
    // Đọc lại
    printf("Read back: ");
    for (int i = 0; i < 10; i++) {
        printf("%c", addr[i]);
    }
    printf("\n");
    
    munmap(addr, len);
    
    printf("MAP_ANONYMOUS TEST PASSED\n");
    exit(0);
}
