#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
  if (argc < 2) {
      printf("xargs cmd 123");
      exit(1);
  }
  
  char cmd[10];
  strcpy(cmd, argv[1]);
  char *argv_cmd[argc];
  int index = 0;
  for (int i=2; i<argc; i++) {
      argv_cmd[index] = argv[i];
      index++;
  }
  argv_cmd[argc-1] = 0;
  
  // create a new argv.
  char ch[1];
  char **arr;
  arr =  (char **)malloc(sizeof(char *) * 10);
  for (int k = 0; k<10; k++) {
    arr[k]=(char *)malloc(sizeof(char) * 50);
  }
  
  int row = 0;
  int col = 1;
  if (index >= 1) {
    col = 2;
  }
  while (1) {
    int n = read(0, ch, sizeof(ch));
    if (n <= 0) {
      exit(1);
    }

    if (ch[0] == '\n') {
      col++;
      arr[col] = 0;

      // fork and exec the arg list
      strcpy(arr[0], cmd);
      if (index >= 1)
        strcpy(arr[1], argv_cmd[0]);
      int child = fork();
      if (child == 0) {
          exec(cmd, arr);
          printf("exec failed!\n");
      }

      wait(0);
      
      // Clear the buffer
      row = 0;
      col = 1;
      if (index >= 1) {
        col = 2;
      }
      int index;
      int jdex;
      for( index = 1; index < 10; index++){
        for( jdex = 0; jdex < 50; jdex++){
          if (arr[index][jdex] != 0)
            arr[index][jdex]=0;
        }
      }
      
      // Do we hold and exec all cmds at the end?
    }
    else if (ch[0] == ' ') {
      arr[col][row] = 0;
      col++;
      row = 0;
    }
    else {
      // Add char to the current line buffer
      //printf("adding %c. col(%d), row(%d)\n", ch[0], col, row);
      arr[col][row] = ch[0];
      row++;
    }
  }
  exit(1);
}
