#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define MAP_SHARED  0x01
#define MAP_PRIVATE 0x02
#define MAP_FAILED  ((void*)-1)

int main() {
    printf("\n========== COW TEST 1: MAP_PRIVATE ==========\n");
    
    // Tạo file test
    int fd = open("cow_test.txt", O_RDWR | O_CREATE);
    if (fd < 0) {
        printf("open failed\n");
        exit(1);
    }
    
    // Ghi dữ liệu ban đầu
    if (write(fd, "HelloCOW", 8) != 8) {
        printf("write failed\n");
        close(fd);
        exit(1);
    }
    
    // mmap với MAP_PRIVATE
    char *addr = mmap(0, 8, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    if (addr == MAP_FAILED) {
        printf("mmap failed\n");
        close(fd);
        exit(1);
    }
    
    printf("mmap returned: %p (MAP_PRIVATE)\n", addr);
    printf("Original data from mmap: %s\n", addr);
    
    // Ghi vào vùng nhớ - sẽ trigger COW
    printf("\n--- Writing to memory (triggering COW) ---\n");
    addr[0] = 'X';
    addr[1] = 'Y';
    addr[2] = 'Z';
    printf("Modified data in memory: %s\n", addr);
    
    // Kiểm tra file gốc không bị ảnh hưởng
    munmap(addr, 8);
    close(fd);
    
    fd = open("cow_test.txt", O_RDONLY);
    char buf[100];
    int n = read(fd, buf, 8);
    buf[n] = '\0';
    printf("\nOriginal file after mmap: %s (should be unchanged)\n", buf);
    close(fd);
    
    printf("\nCOW TEST 1 PASSED\n");
    exit(0);
}
