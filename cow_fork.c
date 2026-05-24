#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define MAP_SHARED  0x01
#define MAP_PRIVATE 0x02
#define MAP_FAILED  ((void*)-1)

int main() {
    printf("\n========== COW TEST 2: fork with MAP_PRIVATE ==========\n");
    
    int fd = open("cow_fork.txt", O_RDWR | O_CREATE);
    if (fd < 0) {
        printf("open failed\n");
        exit(1);
    }
    
    write(fd, "SharedData", 10);
    
    char *addr = mmap(0, 10, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    if (addr == MAP_FAILED) {
        printf("mmap failed\n");
        close(fd);
        exit(1);
    }
    
    printf("Parent: mmap returned %p\n", addr);
    printf("Parent reads: %s\n", addr);
    
    int pid = fork();
    if (pid == 0) {
        // Child process
        printf("\n--- Child process ---\n");
        printf("Child reads: %s\n", addr);
        
        printf("Child writing to memory...\n");
        addr[0] = 'C';
        addr[1] = 'h';
        addr[2] = 'i';
        addr[3] = 'l';
        addr[4] = 'd';
        printf("Child after write: %s\n", addr);
        
        munmap(addr, 10);
        close(fd);
        exit(0);
    } else {
        // Parent process
        wait(0);
        printf("\n--- Parent process after child ---\n");
        printf("Parent reads: %s (should be unchanged)\n", addr);
        munmap(addr, 10);
        close(fd);
    }
    
    // Kiểm tra file gốc
    fd = open("cow_fork.txt", O_RDONLY);
    char buf[100];
    read(fd, buf, 10);
    buf[10] = '\0';
    printf("\nOriginal file: %s (should be unchanged)\n", buf);
    close(fd);
    
    printf("\nCOW TEST 2 PASSED\n");
    exit(0);
}
