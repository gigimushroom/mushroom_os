#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int p[2];
  char buf[100];
  //int n;
  
  pipe(p);
  
  int pid = fork();
  if (pid == 0) {
    write(p[1], "ping", 4);
    printf("Thread id %d: received ping\n", getpid());
  } 
  else {
    wait(0);
    read(p[0], buf, 4);
    printf("Thread id %d: received pong\n", getpid());
  }

  exit(1);
}