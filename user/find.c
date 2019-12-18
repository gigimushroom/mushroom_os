#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

char*
fmtname(char *path)
{
  static char buf[DIRSIZ+1];
  char *p;

  // Find first character after last slash.
  for(p=path+strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;

  // Return blank-padded name.
  if(strlen(p) >= DIRSIZ)
    return p;
  memmove(buf, p, strlen(p));
  //memset(buf+strlen(p), ' ', DIRSIZ-strlen(p));
  return buf;
}

void
ls(char *path, char *filename)
{
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  if((fd = open(path, 0)) < 0){
    fprintf(2, "ls: cannot open %s\n", path);
    return;
  }

  if(fstat(fd, &st) < 0){
    fprintf(2, "ls: cannot stat %s\n", path);
    close(fd);
    return;
  }

  switch(st.type){
  case T_FILE:
    printf("%s %d %d %l\n", fmtname(path), st.type, st.ino, st.size);
    break;

  case T_DIR:
    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
      printf("ls: path too long\n");
      break;
    }
    strcpy(buf, path);
    p = buf+strlen(buf);
    *p++ = '/';
    while(read(fd, &de, sizeof(de)) == sizeof(de)){
      if(de.inum == 0)
        continue;
      //(xiaying): This mmemory move the filename we just read to p.
      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;
      if(stat(buf, &st) < 0) {
        printf("ls: cannot stat %s\n", buf);
        continue;
      }
      //printf("%s vs %s\n", p, filename);
      if (st.type == T_DIR && *p != '.') {
          //printf("search directory: %s\n", p);
          ls(p, filename);
      } else if (strcmp(p, filename) == 0) {
        printf("./%s\n", buf);
      }
    }
    break;
  }
  close(fd);
}

int
main(int argc, char *argv[])
{
  if(argc != 3){
    printf("Usage: find . b\n");
    exit(1);
  }
  
  ls(argv[1], argv[2]);
  exit(1);
}


/*

$ mkdir a
$ echo > a/v
$ find . v
./a/v
$ mkdir c
$ echo > c/v
$ find . v
./a/v
./c/v

*/
