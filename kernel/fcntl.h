#define O_RDONLY  0x000
#define O_WRONLY  0x001
#define O_RDWR    0x002
#define O_CREATE  0x200

// for mmap 
// prot indicates whether memory should be mapped read,
// write, and/or executable
#define PROT_READ   0x000
#define PROT_WRITE  0x001
// Shared is to mwrite back to file if modified
// primate means should not write back.
#define MAP_SHARED  0x000
#define MAP_PRIVATE 0x201