#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

# define MAX_ARGV_LEN 10
struct cmd {
  int type;
};

// Parsed command representation
#define EXEC  1
#define REDIR 2
#define PIPE  3

struct execcmd {
  int type;
  char *argv[MAX_ARGV_LEN];
  char *eargv[MAX_ARGV_LEN];
};

struct pipecmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct redircmd {
  int type;
  struct cmd * cmd;
  char* file;
  char* efile;
  int fd;
  int mode;
};

// cosntructor
struct cmd * execcmd(void) {
  struct execcmd *cmd;
  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = EXEC;
  // we cast back to struct cmd and return.
  return (struct cmd*)cmd;
}

struct cmd* pipecmd(void) {
  struct pipecmd *cmd;
  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = PIPE;
  return (struct cmd*) cmd;
}

struct cmd* redircmd(void) {
  struct redircmd *cmd;
  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = REDIR;
  return (struct cmd*) cmd;
}

char WHITECHARS[] = "\t\n ";

int
my_strlen(const char *s)
{
  int n;

  for(n = 0; s[n]; n++)
    ;
  return n;
}
char*
my_strchr(const char *s, char c)
{
  for(; *s; s++)
    if(*s == c)
      return (char*)s;
  return 0;
}

int
gettoken(char **ps, char *es, char **q, char **eq)
{
  char *s = *ps;
  while (s < es && my_strchr(WHITECHARS, *s))
    s++;
  if (q)
    *q = s;
  
  int ret = *s;
  // now we have a non-whitespace char that s points to.
  switch(*s)
  {
    case 0:
      break;
    case '|':
    case '<':
    case '>':
      s++; // skip it.
      break;
    default:
      ret = 'a';
      // loop str stops when see whitespace.
      while (s < es && !my_strchr(WHITECHARS, *s))
        s++;
      break;
  }
  if (eq)
    *eq = s;
  // Make new ps points to non-whitespace char.
  while (s < es && my_strchr(WHITECHARS, *s))
    s++;
  *ps = s;
  
  return ret;
}
  
void test_get_token()
{
/*
INPUT: ls xiaying | wc
(xiaying | wc)(ls xiaying | wc)( xiaying | wc)
(| wc)(xiaying | wc)( | wc)
(wc)(| wc)( wc)
()(wc)()
After null terminated eargv.
Done. (ls)()
Done. (xiaying)()
Done. (|)()
Done. (wc)()
Note:
eargv is array of pointers. We null the 1st char of each array.
So our argv can end properly.
*/
  char buf[100] = "ls xiaying | wc";
  char *argv[10];
  char *eargv[10];

  char *s = buf;
  char *q, *eq;
  char *es = s + my_strlen(s);
  int i = 0;
  while (gettoken(&s, es, &q, &eq) != '\0') {
    printf("(%s)(%s)(%s)\n", s, q, eq);
    argv[i] = q;
    eargv[i] = eq;
    i++;
  }
  
  for (int k=0; k<i; k++) {
    *eargv[k] = 0;
  }
  for (int j=0; j<i;j++) {
    printf("Done. (%s)(%s)\n", argv[j], eargv[j]);
  }
}

int
peek(char **ps, char *es, char *toks)
{
  // Move to non-space char, check if exists tokens specified in params.
  char *s;

  s = *ps;
  while(s < es && strchr(WHITECHARS, *s))
    s++;
  *ps = s;
  return *s && strchr(toks, *s);
}

struct cmd* parsecmd(char *s);
struct cmd*
nulterminate(struct cmd *cmd);

struct cmd* parsepipe(struct cmd* cmd, char **s) 
{
  //printf("pasing pipe...\n");
  struct cmd* ret = pipecmd();
  struct pipecmd *pc = (struct pipecmd*)ret;
  pc->left = cmd;
  pc->right = parsecmd(*s);
  
  return ret;
}

struct cmd* parseredir(struct cmd* cmd, char **ps, char *es) {
  if (!peek(ps, es, "<>")) {
    return cmd;
  }
  int fd;
  int mode;
  int tok = gettoken(ps, es, 0, 0);
  if (tok == '<') {
    // read mode
    fd = 0;
    mode = O_RDONLY;
  } else {
    // '>' is write mode
    fd = 1;
    mode = O_WRONLY|O_CREATE;
  }
  
  //printf("pasing redir: %s\n", *ps);
  char *q, *eq;
  gettoken(ps, es, &q, &eq);
  
  struct cmd* ret = redircmd();
  struct redircmd *rc = (struct redircmd*)ret;
  rc->fd = fd;
  rc->cmd = cmd;
  rc->mode = mode;
  rc->file = q;
  rc->efile = eq;
  
  return ret;
}

struct cmd* parsecmd(char *s) 
{
  // ctor() of execcmd returns a cmd ptr. We need to case it back to use.
  // but we still want to return the raw cmd ptr.
  struct cmd* ret = execcmd();
  struct execcmd *ec = (struct execcmd*)ret;
  
  char *q, *eq;
  char *es = s + my_strlen(s);
  int i = 0;
  while (!peek(&s, es, "|")) {
    if (gettoken(&s, es, &q, &eq) == '\0') {
      break;
    }
    ec->argv[i] = q;
    ec->eargv[i] = eq;
    i++;
    ret = parseredir(ret, &s, es);
  }
  // parse pipe
  if (peek(&s, es, "|")) {
    // Skip the '|' char.
    gettoken(&s, es, 0, 0);
    ret = parsepipe(ret, &s);
  }
  
  ec->argv[i] = 0;
  ec->eargv[i] = 0;
  nulterminate(ret);
  return ret;
}

// NUL-terminate all the counted strings.
struct cmd*
nulterminate(struct cmd *cmd)
{
  // Copy from sh.c
  int i;
  struct execcmd *ecmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if(cmd == 0)
    return 0;

  switch(cmd->type){
    case EXEC:
      ecmd = (struct execcmd*)cmd;
      // It is critical to set the 1st char of the argv[k] ptr points to
      // as null, not the whole pointer to null!
      for(i=0; ecmd->argv[i]; i++)
        *ecmd->eargv[i] = 0;
      break;

    case REDIR:
      rcmd = (struct redircmd*)cmd;
      nulterminate(rcmd->cmd);
      *rcmd->efile = 0;
      break;

    case PIPE:
      pcmd = (struct pipecmd*)cmd;
      nulterminate(pcmd->left);
      nulterminate(pcmd->right);
      break;
  }
  return cmd;
}

void runcmd(struct cmd *cmd) {
  int type = cmd->type;
  //printf("Type is %d\n", type);
  if (type == 0)
    return;
  // If not defined here, got err: a label can only be part of a statement 
  // and a declaration is not a statement.
  struct execcmd *ec;
  struct pipecmd *pc;
  struct redircmd *rc;
  
  switch(type)
  {
    case EXEC:
      ec = (struct execcmd*)cmd;
      exec(ec->argv[0], ec->argv);
      break;
    case PIPE:
      pc = (struct pipecmd *) cmd;
      int p[2];
      pipe(p);
      if (fork() == 0) {
        close(1);
        //http://man7.org/linux/man-pages/man2/dup.2.html
        dup(p[1]); // stdout
        close(p[0]);
        close(p[1]);
        runcmd(pc->left);
      }
      if (fork() == 0) {
        close(0);
        dup(p[0]);
        close(p[1]);
        close(p[0]);
        runcmd(pc->right);
      }
      close(p[0]);
      close(p[1]);
      wait(0);
      wait(0);
      break;
    case REDIR:
      rc = (struct redircmd *) cmd;
      close(rc->fd);
      if(open(rc->file, rc->mode) < 0){
        fprintf(2, "open %s failed\n", rc->file);
        exit(1);
      }
      runcmd(rc->cmd);
      break;
    default:
      printf("unknown type!\n");
      break;
  }
}

int main(void)
{
    static char buf[1000];
    
    int fd;
    // Ensure that three file descriptors are open.
    while((fd = open("console", O_RDWR)) >= 0){
      if(fd >= 3){
        close(fd);
        break;
      }
    }
    
    while(1) {
      memset(buf, 0, sizeof(buf));
      printf("@ ");
      gets(buf, sizeof(buf));
      if (buf[0] == 0) // EOF
      {
        printf("\n");
        break;
      }
        
      //printf("buf is %s", buf);
      if (strcmp("quit()\n", buf) == 0) {
        printf("Exit nsh..\n");
        break;
      }
      if (fork() == 0)
        runcmd(parsecmd(buf));
      wait(0);
    }
    
    exit(1);
}


/*
STATUS
$ testsh nsh
simple echo: PASS
simple grep: PASS
two commands: PASS
output redirection: PASS
input redirection: PASS
both redirections: testsh: saw expected output, but too much else as well
FAIL
simple pipe: PASS
pipe and redirects: PASS
lots of commands: FAIL

failed some tests
*/