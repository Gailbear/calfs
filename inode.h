/* Inode struct definitions */

#include <sys/types.h>
#include <time.h>

#ifndef __INODE_H__
#define __INODE_H__

typedef struct blocknum_t {
  int block:31;
  int valid:1;
} blocknum;

typedef struct vcb_t {
  // filesystem identifier
  int magic;

  int blocksize;

  // location of root DNODE
  blocknum root;

  // location of first free block
  blocknum free;

  char clean;

  // disk name - fills up the correct number of bytes to
  // make this block 512 bytes.
  char name[495];
} vcb;

typedef struct dnode_t {
  unsigned int size;
  uid_t user;
  gid_t group;
  mode_t mode;
  time_t access_time;
  time_t modify_time;
  time_t create_time;

  // 512 - 9 * 4 = 476 / 4 = 119
  blocknum direct[119];
  blocknum single_indirect;
  blocknum double_indirect;
} dnode;

typedef struct indirect_t {
  blocknum blocks[128];
} indirect;

typedef struct direntry_t {
  char name[59];
  char type;
  blocknum block;
} direntry;

typedef struct dirent_t {
  direntry entries[64];
} dirent;

typedef struct inode_t {
  unsigned int size;
  uid_t user;
  gid_t group;
  mode_t mode;
  time_t access_time;
  time_t modify_time;
  time_t create_time;

  blocknum direct[119];
  blocknum single_indirect;
  blocknum double_indirect;
} inode;

typedef struct db_t {
  char data[512];
} db;

typedef struct freeblock_t {
  blocknum next;
  char junk[508];
} freeblock;

#endif
