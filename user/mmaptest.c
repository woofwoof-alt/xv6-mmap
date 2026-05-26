#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define MAP_SHARED  0x01
#define MAP_FAILED  ((void*)-1)

int main() {
  // Xóa file cũ nếu tồn tại
  unlink("test.txt");
  
  // Tạo file mới với quyền đọc + ghi
  int fd = open("test.txt", O_RDWR | O_CREATE);
  if (fd < 0) {
    printf("Cannot create test.txt\n");
    exit(1);
  }
  
  // Ghi dữ liệu ban đầu
  if (write(fd, "Hello xv6 mmap!", 16) != 16) {
    printf("Write failed\n");
    close(fd);
    exit(1);
  }
  
  // Mmaps file
  void* addr = mmap(0, 16, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  
  if (addr == MAP_FAILED) {
    printf("mmap failed\n");
    close(fd);
    exit(1);
  }
  
  printf("mmap returned %p\n", addr);
  
  // Đọc và sửa dữ liệu
  char *data = (char*)addr;
  printf("Data from mmap: %s\n", data);
  
  data[0] = 'X';
  data[1] = 'v';
  data[2] = '6';
  printf("Modified data: %s\n", data);
  
  // Munmap (ghi lại file)
  munmap(addr, 16);
  close(fd);
  
  // Kiểm tra file
  fd = open("test.txt", O_RDONLY);
  char buf[100];
  int n = read(fd, buf, sizeof(buf));
  buf[n] = '\0';
  printf("File content after mmap: %s\n", buf);
  close(fd);
  
  printf("Test complete.\n");
  exit(0);
}
