/*
 * CS3600 Project 2: A User-Level File System
 *
 * This program is intended to format your disk file, and should be executed
 * BEFORE any attempt is made to mount your file system.  It will not, however
 * be called before every mount (you will call it manually when you format 
 * your disk file).
 */

#include <math.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>

#include "3600fs.h"
#include "disk.h"
#include "inode.h"



void myformat(int size) {
  // Do not touch or move this function
  dcreate_connect();

  /* 3600: FILL IN CODE HERE.  YOU SHOULD INITIALIZE ANY ON-DISK
           STRUCTURES TO THEIR INITIAL VALUE, AS YOU ARE FORMATTING
           A BLANK DISK.  YOUR DISK SHOULD BE size BLOCKS IN SIZE. */

  if (size < 4) perror("Not a big enough size for a disk.");

  vcb the_vcb;
  the_vcb.magic = 7021129;
  the_vcb.blocksize = BLOCKSIZE;
  the_vcb.root.block = 1;
  the_vcb.root.valid |= 1;
  the_vcb.free.block = 3;
  the_vcb.free.valid |= 1;
  strcpy(the_vcb.name, "my disk");

  char tmp[BLOCKSIZE];
  memcpy(tmp, &the_vcb, sizeof(vcb));
  if (dwrite(0, tmp) < 0) perror("Error while writing to disk");

  dnode root;
  root.user = 0;
  root.group = 0;
  root.mode = (mode_t) 07771;
  struct timespec mytime;
  clock_gettime(CLOCK_REALTIME, &mytime);
  root.access_time = mytime;
  root.modify_time = mytime;
  root.create_time = mytime;

  root.direct[0].block = 2;
  root.direct[0].valid |= 1;
  for(int i = 1; i < 116; i++) {
    root.direct[i].valid |= 0;
  }
  root.single_indirect.valid |= 0;
  root.double_indirect.valid |= 0;

  memcpy(tmp, &root, sizeof(dnode));
  if (dwrite(1, tmp) < 0) perror("Error while writing to disk");

  dirent root_dirent;
  strcpy(root_dirent.entries[0].name, ".");
  root_dirent.entries[0].type = 'd';
  root_dirent.entries[0].block.block = 1;
  root_dirent.entries[0].block.valid |= 1;

  strcpy(root_dirent.entries[1].name, "..");
  root_dirent.entries[1].type = 'd';
  root_dirent.entries[1].block.block = 1;
  root_dirent.entries[1].block.valid |= 1;
  
  for (int i = 2; i < 64; i++){
    root_dirent.entries[i].block.valid &= 0;
  }

  memcpy(tmp, &root_dirent, sizeof(dirent));
  if (dwrite(2, tmp) < 0) perror("Error while writing to disk");

  for(int i = 3; i < size-1; i++){
    freeblock dummy;
    dummy.next.block = i+1;
    dummy.next.valid |= 1;
    memcpy(tmp, &dummy, sizeof(freeblock));
    if (dwrite(i, tmp) < 0) perror("Error while writing to disk");
  }
  freeblock last;
  last.next.valid &= 0;
  memcpy(tmp, &last, sizeof(freeblock));
  if (dwrite(size-1, tmp) < 0) perror("Error while writing to disk");

  

  /* 3600: AN EXAMPLE OF READING/WRITING TO THE DISK IS BELOW - YOU'LL
           WANT TO REPLACE THE CODE BELOW WITH SOMETHING MEANINGFUL.

  // first, create a zero-ed out array of memory  
  char *tmp = (char *) malloc(BLOCKSIZE);
  memset(tmp, 0, BLOCKSIZE);

  // now, write that to every block
  for (int i=0; i<size; i++) 
    if (dwrite(i, tmp) < 0) 
      perror("Error while writing to disk");

  // voila! we now have a disk containing all zeros
*/
  // Do not touch or move this function
  dunconnect();
}

int main(int argc, char** argv) {
  // Do not touch this function
  if (argc != 2) {
    printf("Invalid number of arguments \n");
    printf("usage: %s diskSizeInBlockSize\n", argv[0]);
    return 1;
  }

  unsigned long size = atoi(argv[1]);
  printf("Formatting the disk with size %lu \n", size);
  myformat(size);
}
