#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// demo code.
void echo() 
{
  int p[2];
  char buf[100];
  
  pipe(p);
  
  int pid = fork();
  if (pid == 0) {
    write(p[1], "ping", 4);
    write(p[1], "xiaying", 7);
  } 
  else {
    sleep(10);
    read(p[0], buf, 4);
    printf("Thread id %d: received %s\n", getpid(), buf);
    read(p[0], buf, 7);
    printf("Thread id %d: received %s\n", getpid(), buf);
  }
}

void exec_pipe(int fd)
{
    int num;
    read(fd, &num, 4);
    printf("Thead(%d) prime %d\n", getpid(), num);
    
    int p[2];
    pipe(p);
    int tmp = -1;
    while (1) {
        int n = read(fd, &tmp, 4);
        if (n<= 0) {
            break;
        }
        if (tmp % num != 0) {
            //printf("%d writing %d and n is: %d\n", getpid(), tmp, n);
            write(p[1], &tmp, 4);
        }
    }
    if (tmp == -1) {
        close(p[1]);
        close(p[0]);
        close(fd);
        return;
    }
    int pid = fork();
    if (pid == 0) {
        close(p[1]);
        close(fd);
        exec_pipe(p[0]);
        close(p[0]);
    }
    else {
        close(p[1]);
        close(p[0]);
        close(fd);
        wait(0);
    }
}

int
main(int argc, char *argv[])
{
  int p[2];
  pipe(p);
  for (int i = 2; i<35; i++) {
      int n = i;
      //printf("sending %d\n", n);
      write(p[1], &n, 4);
  }
  close(p[1]);
  exec_pipe(p[0]);
  close(p[0]);
  
  exit(1);
}

/*
$ primes
prime 2
prime 3
prime 5
prime 7
prime 11
prime 13
prime 17
prime 19
prime 23
prime 29
prime 31
*/