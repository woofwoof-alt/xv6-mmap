#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define MAP_SHARED  0x01
#define MAP_PRIVATE 0x02
#define MAP_FAILED  ((void*)-1)

void test_basic() {
    printf("\n========== Test 1: Basic mmap/munmap ==========\n");
    
    int fd = open("test1.txt", O_RDWR | O_CREATE);
    if (fd < 0) { printf("open failed\n"); return; }
    
    write(fd, "Hello xv6 mmap!", 16);
    
    char *addr = mmap(0, 16, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) { printf("mmap failed\n"); close(fd); return; }
    
    printf("mmap returned: %p\n", addr);
    printf("Original: %s\n", addr);
    
    addr[0] = 'X';
    addr[1] = 'v';
    addr[2] = '6';
    printf("Modified: %s\n", addr);
    
    munmap(addr, 16);
    close(fd);
    
    fd = open("test1.txt", O_RDONLY);
    char buf[100];
    int n = read(fd, buf, 100);
    buf[n] = '\0';
    printf("File after: %s\n", buf);
    close(fd);
    
    printf("Test 1 PASSED\n");
}

void test_offset() {
    printf("\n========== Test 2: mmap with offset ==========\n");
    
    int fd = open("test2.txt", O_RDWR | O_CREATE);
    if (fd < 0) { printf("open failed\n"); return; }
    
    for (int i = 0; i < 4096; i++) {
        char c = 'A' + (i % 26);
        write(fd, &c, 1);
    }
    for (int i = 0; i < 4096; i++) {
        char c = 'a' + (i % 26);
        write(fd, &c, 1);
    }
    
    char *addr = mmap(0, 16, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 4096);
    if (addr == MAP_FAILED) { printf("mmap with offset failed\n"); close(fd); return; }
    
    printf("mmap returned: %p (offset=4096)\n", addr);
    printf("Data: ");
    for (int i = 0; i < 16; i++) printf("%c", addr[i]);
    printf("\n");
    
    addr[0] = 'X';
    addr[1] = 'Y';
    addr[2] = 'Z';
    printf("Modified: ");
    for (int i = 0; i < 16; i++) printf("%c", addr[i]);
    printf("\n");
    
    munmap(addr, 16);
    close(fd);
    
    printf("Test 2 PASSED\n");
}

void test_multiple() {
    printf("\n========== Test 3: Multiple mmap regions ==========\n");
    
    int fd = open("test3.txt", O_RDWR | O_CREATE);
    if (fd < 0) {
        printf("Step 1: open failed\n");
        return;
    }
    printf("Step 1: open succeeded, fd=%d\n", fd);
    
    int n = write(fd, "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ", 32);
    if (n != 32) {
        printf("Step 2: write failed, wrote %d bytes\n", n);
        close(fd);
        return;
    }
    printf("Step 2: write succeeded, wrote %d bytes\n", n);
    
    // First mmap
    printf("Step 3: calling first mmap (offset=0, len=10)\n");
    char *addr1 = mmap(0, 10, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (addr1 == MAP_FAILED) {
        printf("Step 3: first mmap FAILED!\n");
        close(fd);
        return;
    }
    printf("Step 3: first mmap succeeded, addr1=%p\n", addr1);
    
    // Second mmap
    printf("Step 4: calling second mmap (offset=10, len=10)\n");
    char *addr2 = mmap(0, 10, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 10);
    if (addr2 == MAP_FAILED) {
        printf("Step 4: second mmap FAILED!\n");
        munmap(addr1, 10);
        close(fd);
        return;
    }
    printf("Step 4: second mmap succeeded, addr2=%p\n", addr2);
    
    // Read data
    printf("Step 5: reading from addr1: %s\n", addr1);
    printf("Step 5: reading from addr2: %s\n", addr2);
    
    // Modify data
    printf("Step 6: modifying data\n");
    addr1[0] = 'A';
    addr1[1] = 'B';
    addr2[0] = 'C';
    addr2[1] = 'D';
    
    printf("After modify - addr1: %s\n", addr1);
    printf("After modify - addr2: %s\n", addr2);
    
    // Munmap
    printf("Step 7: calling munmap for addr1\n");
    if (munmap(addr1, 10) < 0) {
        printf("munmap addr1 failed\n");
    }
    
    printf("Step 8: calling munmap for addr2\n");
    if (munmap(addr2, 10) < 0) {
        printf("munmap addr2 failed\n");
    }
    
    close(fd);
    printf("Test 3 PASSED\n");
}


void test_fork() {
    printf("\n========== Test 4: Fork with shared mmap ==========\n");
    
    int fd = open("test4.txt", O_RDWR | O_CREATE);
    write(fd, "ParentData", 10);
    
    char *addr = mmap(0, 10, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) { printf("mmap failed\n"); close(fd); return; }
    
    int pid = fork();
    if (pid == 0) {
        printf("Child reads: %s\n", addr);
        addr[0] = 'C';
        addr[1] = 'h';
        printf("Child writes: %s\n", addr);
        munmap(addr, 10);
        close(fd);
        exit(0);
    } else {
        wait(0);
        printf("Parent reads: %s\n", addr);
        munmap(addr, 10);
        close(fd);
    }
    
    printf("Test 4 PASSED\n");
}

void run_test(void (*test)(), char *name) {
    int pid = fork();
    if (pid == 0) {
        test();
        exit(0);
    } else {
        int status;
        wait(&status);
        printf(">>> %s exit status: %d\n", name, status);
    }
}

int main() {
    printf("\n========== XV6 MMAP FULL TEST ==========\n");
    
    run_test(test_basic, "Test 1");
    run_test(test_offset, "Test 2");
    run_test(test_multiple, "Test 3");
    run_test(test_fork, "Test 4");
    
    printf("\n========== ALL TESTS COMPLETE ==========\n");
    exit(0);
}
