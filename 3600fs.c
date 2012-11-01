/*
 *   CS3600 Project 2: A User-Level File System
 */

#define FUSE_USE_VERSION 26

#ifdef linux
/* For pread()/pwrite() */
#define _XOPEN_SOURCE 500
#endif

#define _POSIX_C_SOURCE 199309

#include <time.h>
#include <fuse.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <assert.h>
#include <sys/statfs.h>

#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif

#include "3600fs.h"
#include "disk.h"
#include "inode.h"

vcb the_vcb;
dnode root;
int root_loaded = 0;

/*
 * Initialize filesystem. Read in file system metadata and initialize
 * memory structures. If there are inconsistencies, now would also be
 * a good time to deal with that. 
 *
 * HINT: You don't need to deal with the 'conn' parameter AND you may
 * just return NULL.
 *
 */
static void* vfs_mount(struct fuse_conn_info *conn) {
  fprintf(stderr, "vfs_mount called\n");

  // Do not touch or move this code; connects the disk
  dconnect();

  /* 3600: YOU SHOULD ADD CODE HERE TO CHECK THE CONSISTENCY OF YOUR DISK
           AND LOAD ANY DATA STRUCTURES INTO MEMORY */

  char tmp[BLOCKSIZE];
  memset(tmp,0,BLOCKSIZE);
  dread(0, tmp);

  memcpy (&the_vcb, tmp, sizeof(vcb));

  fprintf(stderr, "loaded the vcb\n");

  if(the_vcb.magic != 7021129){
    //TODO THROW ERROR HERE
    return NULL;
  }

  fprintf(stderr, "root block/valid: %u/%u\n", the_vcb.root.block, the_vcb.root.valid);
  fprintf(stderr, "free block/valid: %u/%u\n", the_vcb.free.block, the_vcb.free.valid);

  if (the_vcb.clean == 1){
    the_vcb.clean = 0;
  }else {
    // check the disk
    assure_integrity();
    // then set vcb to 0
    the_vcb.clean = 0;
  }

  //update the vcb
  memcpy(tmp, &the_vcb, sizeof(vcb));
  dwrite(0, tmp);


  return NULL;
}

static void check_blocks_dnode(blocknum blockNode, char *blockCheck)
{
  //Is the block valid and has it been checked already?
  if(blockNode.valid == 0 || *(blockCheck+blockNode.block) == 1)
    return;


  //It's valid. Track it
  *(blockCheck+blockNode.block) = 1;

  //Read it into memory
  dnode currNode;
  char tmp[BLOCKSIZE];
  memset(tmp, 0, BLOCKSIZE);
  dread(blockNode.block, tmp);
  memcpy(&currNode,tmp,sizeof(dnode));

  //Loop through all the dirents. Tracking each reference
  for(int i = 0; i < 116; i++)
    check_blocks_dirent(currNode.direct[i], blockCheck);

  //Check the single and double indirects. Tracking each reference
  check_blocks_indirect_dnode(currNode.single_indirect, 1, blockCheck);
  check_blocks_indirect_dnode(currNode.double_indirect, 2, blockCheck);
}

static void check_blocks_inode(blocknum blockNode, char *blockCheck)
{
  //Is the block valid and has it been checked already?
  if(blockNode.valid == 0 || *(blockCheck+blockNode.block) == 1)
    return;


  //It's valid. Track it
  *(blockCheck+blockNode.block) = 1;

  //Read it into memory
  inode currNode;
  char tmp[BLOCKSIZE];

  memset(tmp, 0, BLOCKSIZE);
  dread(blockNode.block, tmp);
  memcpy(&currNode,tmp,sizeof(inode));

  //Loop through all the data blocks. Tracking each reference
  for(int i = 0; i < 116; i++)
    check_blocks_data(currNode.direct[i], blockCheck);

  //Check the single and double indirects. Tracking each reference
  check_blocks_indirect_inode(currNode.single_indirect, 1, blockCheck);
  check_blocks_indirect_inode(currNode.double_indirect, 2, blockCheck);
}


static void check_blocks_direntry(blocknum blockDirentry, char* blockCheck)
{
  //Is the block valid and has it been checked already?
  if(blockDirentry.valid == 0 || *(blockCheck+blockDirentry.block) == 1)
    return;

  //It's valid. Track it
  *(blockCheck+blockDirentry.block) = 1;

  //Read it into memory
  direntry currDirentry;
  char tmp[BLOCKSIZE];
  memset(tmp, 0, BLOCKSIZE);
  dread(blockDirentry.block, tmp);
  memcpy(&currDirentry,tmp,sizeof(direntry));

  //If it's a directory, treat it like one and check it's references. Otherwise check files'
  if(currDirentry.type == 'd')
    check_blocks_dnode(currDirentry.block, blockCheck);
  else
    check_blocks_inode(currDirentry.block, blockCheck);
}


static void check_blocks_dirent(blocknum blockDirent, char* blockCheck)
{
  //Is the block valid and has it been checked already?
  if(blockDirent.valid == 0 || *(blockCheck+blockDirent.block) == 1)
    return;

  //It's valid. Track it
  *(blockCheck+blockDirent.block) = 1;

  //Read it into memory
  dirent currDirent;
  char tmp[BLOCKSIZE];
  memset(tmp, 0, BLOCKSIZE);
  dread(blockDirent.block, tmp);
  memcpy(&currDirent,tmp,sizeof(dirent));

  //Loop through each directory entry. Tracking each reference
  for(int i = 0; i < 8; i++)
    check_blocks_direntry(currDirent.entries[i].block, blockCheck);
}

static void check_blocks_data(blocknum blockData, char* blockCheck)
{
  if(blockData.valid == 0 || *(blockCheck+blockData.block) == 1)
    return;

  //It's valid. Track it
  *(blockCheck+blockData.block) = 1;
}

static void check_blocks_indirect_dnode(blocknum blockIndirect, int levels, char *blockCheck)
{
  //Is the block valid and has it been checked already?
  if(blockIndirect.valid == 0 || *(blockCheck+blockIndirect.block) == 1)
    return;

  //It's valid. Track it
  *(blockCheck+blockIndirect.block) = 1;


  //Sees how many levels of indirects it must go through. If none, treat it like a direct
  if(levels == 0)
  {
    check_blocks_dirent(blockIndirect, blockCheck);
  }
  else
  {
    //Read indirect block into memory
    indirect currIndirect;
    char tmp[BLOCKSIZE];
    memset(tmp, 0, BLOCKSIZE);
    dread(blockIndirect.block, tmp);
    memcpy(&currIndirect,tmp,sizeof(indirect));

    //Loop through each reference
    for(int i = 0; i < BLOCKSIZE/4; i++)
      check_blocks_indirect_dnode(currIndirect.blocks[i], levels-1, blockCheck);
  }
}


static void check_blocks_indirect_inode(blocknum blockIndirect, int levels, char *blockCheck)
{
  //Is the block valid and has it been checked already?
  if(blockIndirect.valid == 0 || *(blockCheck+blockIndirect.block) == 1)
    return;

  //It's valid. Track it
  *(blockCheck+blockIndirect.block) = 1;

  //Sees how many levels of indirects it must go through. If none, treat it like a direct
  if(levels == 0)
  {
    check_blocks_data(blockIndirect, blockCheck);
  }
  else
  {
    //Read indirect block into memory
    indirect currIndirect;
    char tmp[BLOCKSIZE];
    memset(tmp, 0, BLOCKSIZE);
    dread(blockIndirect.block, tmp);
    memcpy(&currIndirect,tmp,sizeof(indirect));

    //Loop through each reference
    for(int i = 0; i < BLOCKSIZE/4; i++)
      check_blocks_indirect_inode(currIndirect.blocks[i], levels-1, blockCheck);
  }
}

static void check_blocks_free(blocknum blockFree, char *blockCheck)
{
  //Is the block valid and has it been checked already?
  if(blockFree.valid == 0 || *(blockCheck+blockFree.block) == 1)
    return;


  freeblock currBlock;
  char tmp[BLOCKSIZE];


  //Loop through all the free blocks until you hit a non-valid. Keeping track of valid references
  for(blocknum currBlocknum = blockFree; currBlocknum.valid; currBlocknum = currBlock.next)
  {

    //It's valid. Track it
    *(blockCheck+currBlocknum.block) = 1;

    memset(tmp, 0, BLOCKSIZE);
    dread(currBlocknum.block, tmp);
    memcpy(&currBlock,tmp,sizeof(freeblock));
  }
}

static void assure_integrity()
{
  //116 direct blocks
  int TOTALBLOCKS = 116;

  //BLOCKSIZE/4 in indirect block
  TOTALBLOCKS += BLOCKSIZE/4;

  //(BLOCKSIZE/4)^2 in double indirect block
  TOTALBLOCKS += pow((BLOCKSIZE/4), 2);

  //Create a list of all blocks referenced in some path from the_vcb
  char *blockCheck = calloc(TOTALBLOCKS, sizeof(char));
  check_blocks_free(the_vcb.free, blockCheck);
  check_blocks_dnode(the_vcb.root, blockCheck);

  //Now take that list and change all the nonreferenced to free blocks
  for(int i = 0; i < TOTALBLOCKS; i++)
  {
    if(blockCheck[i] == 1)
      continue;

    blocknum newBlock;
    newBlock.block = i;
    newBlock.valid = 1;
    free_block(newBlock);
  }
}

/*
 * Called when your file system is unmounted.
 *
 */
static void vfs_unmount (void *private_data) {
  fprintf(stderr, "vfs_unmount called\n");

  /* 3600: YOU SHOULD ADD CODE HERE TO MAKE SURE YOUR ON-DISK STRUCTURES
           ARE IN-SYNC BEFORE THE DISK IS UNMOUNTED (ONLY NECESSARY IF YOU
           KEEP DATA CACHED THAT'S NOT ON DISK */

  char tmp[BLOCKSIZE];
  memset(tmp, 0, BLOCKSIZE);
  the_vcb.clean = 1;
  memcpy(tmp, &the_vcb, sizeof(vcb));
  dwrite(0, tmp);

  // Do not touch or move this code; unconnects the disk
  dunconnect();
}

/* 
 *
 * Given an absolute path to a file/directory (i.e., /foo ---all
 * paths will start with the root directory of the CS3600 file
 * system, "/"), you need to return the file attributes that is
 * similar stat system call.
 *
 * HINT: You must implement stbuf->stmode, stbuf->st_size, and
 * stbuf->st_blocks correctly.
 *
 */
static int vfs_getattr(const char *path, struct stat *stbuf) {
  fprintf(stderr, "vfs_getattr called\n");

  // Do not mess with this code 
  stbuf->st_nlink = 1; // hard links
  stbuf->st_rdev  = 0;
  stbuf->st_blksize = BLOCKSIZE;

  /* 3600: YOU MUST UNCOMMENT BELOW AND IMPLEMENT THIS CORRECTLY */
  

  if(strcmp("/",path) == 0) {
    stbuf->st_mode = 0777 | S_IFDIR;
    stbuf->st_uid     = root.user;
    stbuf->st_gid     = root.group;
    stbuf->st_atime   = root.access_time.tv_sec;
    stbuf->st_mtime   = root.modify_time.tv_sec;
    stbuf->st_ctime   = root.create_time.tv_sec;
    stbuf->st_size    = root.size;
    stbuf->st_blocks  = root.size / BLOCKSIZE;
    if(root.size % BLOCKSIZE != 0) stbuf->st_blocks += 1;
    return 0;
  }
  
  blocknum trash;
  int trash2;
  direntry target_d = findFile(path, &trash, &trash2);

  // if the file wasn't found, throw the expected error
  if (target_d.block.valid == 0) return -ENOENT; 

  return getattr_from_direntry(target_d, stbuf);
}

// helper method for getattr
static int getattr_from_direntry(direntry target_d, struct stat *stbuf){
  inode target;


  // read the block
  char tmp[BLOCKSIZE];
  memset(tmp, 0, BLOCKSIZE);
  dread(target_d.block.block, tmp);
  memcpy(&target,tmp,sizeof(inode));

  if (target_d.type == 'd'){
    stbuf->st_mode = 0777 | S_IFDIR;
  }
  else {
    stbuf->st_mode = target.mode | S_IFREG;
  }
  stbuf->st_uid     = target.user;
  stbuf->st_gid     = target.group;
  stbuf->st_atime   = target.access_time.tv_sec;
  stbuf->st_mtime   = target.modify_time.tv_sec;
  stbuf->st_ctime   = target.create_time.tv_sec;
  stbuf->st_size    = target.size;
  stbuf->st_blocks  = target.size / BLOCKSIZE;
  if(target.size % BLOCKSIZE != 0) stbuf->st_blocks += 1;

  return 0;
}


/*
 * Given an absolute path to a directory (which may or may not end in
 * '/'), vfs_mkdir will create a new directory named dirname in that
 * directory.
 *
 * HINT: Don't forget to create . and .. while creating a
 * directory. You can ignore 'mode'.
 *
 */
  /* 3600: YOU CAN IGNORE THIS METHOD, UNLESS YOU ARE COMPLETING THE 
           EXTRA CREDIT PORTION OF THE PROJECT 
static int vfs_mkdir(const char *path, mode_t mode) {

  return -1;
} */

/** Read directory
 *
 * Given an absolute path to a directory, vfs_readdir will return 
 * all the files and directories in that directory.
 *
 * HINT:
 * Use the filler parameter to fill in, look at fusexmp.c to see an example
 * Prototype below
 *
 * Function to add an entry in a readdir() operation
 *
 * @param buf the buffer passed to the readdir() operation
 * @param name the file name of the directory entry
 * @param stat file attributes, can be NULL
 * @param off offset of the next entry or zero
 * @return 1 if buffer is full, zero otherwise
 * typedef int (*fuse_fill_dir_t) (void *buf, const char *name,
 *                                 const struct stat *stbuf, off_t off);
 *         
 * Your solution should not need to touch offset and fi
 *
 */
 // TODO implement offset
static int vfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                       off_t offset, struct fuse_file_info *fi)
{
  fprintf(stderr, "vfs_readdir called\n");
  // for now, assume root
  load_root();
  
  char tmp[BLOCKSIZE];
  memset(tmp, 0, BLOCKSIZE);

  dirent contents;
  // for all the dirent blocks
  for (int i = 0; i < 116; i++){
    if(root.direct[i].valid == 0) {
      //fprintf(stderr, "direct %d is invalid\n", i);
      continue;
    }
    // load the ith dirent block
    memset(tmp, 0, BLOCKSIZE);
    dread(root.direct[i].block,tmp);
    memcpy(&contents, tmp, sizeof(dirent));
    fprintf(stderr, "dirent %d loaded\n", i);
    // for each entry in the dirent block
    for (int j = 0; j < 8; j++) {
      // continue if entry is invalid
      if (contents.entries[j].block.valid == 0) {
        //fprintf(stderr, "entry %d in dirent %d is invalid.\n", j ,i);
        continue;
      }
      // add to the list
      fprintf(stderr, "entry %d in dirent %d exists.\n", j, i);
      struct stat stbuf;
      getattr_from_direntry(contents.entries[j], &stbuf);
      filler(buf, contents.entries[j].name, &stbuf, 0);
    }
  }

  int full = readdir_indirect(root.single_indirect, buf, filler, offset);
  if(full) return -1;
  return readdir_dindirect(root.double_indirect, buf, filler, offset);
}


static int readdir_indirect(blocknum block, void *buf, fuse_fill_dir_t filler, off_t offset){
  if (block.valid == 0) return 0;

  char tmp[BLOCKSIZE];
  memset(tmp, 0, BLOCKSIZE);

  indirect ind;
  dread(block.block, tmp);
  memcpy(&ind, tmp, sizeof(indirect));

  dirent contents;
  for (int i = 0; i < 128; i++) {
    if (ind.blocks[i].valid == 0) continue;
    memset(tmp, 0, BLOCKSIZE);
    dread(ind.blocks[i].block, tmp);
    memcpy(&contents, tmp, sizeof(dirent));
    for(int j = 0; j < 8; i++){
      if(contents.entries[j].block.valid){
        struct stat stbuf;
        getattr_from_direntry(contents.entries[j], &stbuf);
        filler(buf, contents.entries[j].name, &stbuf, 0);
      }
    }
  }

  return 0;
}

static int readdir_dindirect(blocknum block, void *buf, fuse_fill_dir_t filler, off_t offset){
  if (block.valid == 0) return 0;

  char tmp[BLOCKSIZE];
  memset(tmp, 0, BLOCKSIZE);

  indirect ind;
  dread(block.block, tmp);
  memcpy(&ind, tmp, sizeof(indirect));

  for (int i = 0; i < 128; i++){
    int full = readdir_indirect(ind.blocks[i], buf, filler, offset);
    if (full) return -1;
  }

  return 0;
}

//Returns direntry of path
// if it can't be found, returns a direntry with an invalid block
static direntry findFile(const char *path, blocknum *dntnum, int *entry){
  fprintf(stderr, "findFile called\n");
  // for now, assuming root
  load_root();

  const char *filename = path + 1;

  char tmp[BLOCKSIZE];

  dirent contents;
  // for all the dirent blocks
  for (int i = 0; i < 116; i++){
    if(root.direct[i].valid == 0) {
//      fprintf(stderr, "direct %d is invalid\n", i);
      continue;
    }
    // load the ith dirent block
    memset(tmp, 0, BLOCKSIZE);
    dread(root.direct[i].block,tmp);
    memcpy(&contents, tmp, sizeof(dirent));
//    fprintf(stderr, "dirent %d loaded\n", i);
    // for each entry in the dirent block
    for (int j = 0; j < 8; j++) {
      // continue if entry is invalid
      if (contents.entries[j].block.valid == 0) {
        //fprintf(stderr, "entry %d in dirent %d is invalid.\n", j ,i);
        continue;
      }
      // compare the name
      if (strcmp(filename, contents.entries[j].name) == 0){
        // we found it! set the dirent block and return the direntry
        *entry = j;
        *dntnum = root.direct[i]; 
        return contents.entries[j];
      }
    }
  }
  direntry result = findfile_indirect(root.single_indirect, filename, dntnum, entry);
  if (result.block.valid) return result;
  return findfile_dindirect(root.double_indirect, filename, dntnum, entry);
}


static direntry findfile_indirect(blocknum block, const char *filename, blocknum *dntnum, int *entry){
  direntry invalid;
  invalid.block.valid &= 0;

  if (block.valid == 0) return invalid;

  char tmp[BLOCKSIZE];
  memset(tmp, 0, BLOCKSIZE);

  indirect ind;
  dread(block.block, tmp);
  memcpy(&ind, tmp, sizeof(indirect));

  dirent contents;
  for (int i = 0; i < 128; i++) {
    if (ind.blocks[i].valid == 0) continue;
    memset(tmp, 0, BLOCKSIZE);
    dread(ind.blocks[i].block, tmp);
    memcpy(&contents, tmp, sizeof(dirent));
    for(int j = 0; j < 8; i++){
      if(contents.entries[j].block.valid){
        if(strcmp(contents.entries[j].name, filename)) continue;
        *entry = j;
        *dntnum = ind.blocks[i];
        return contents.entries[j];
      }
    }
  }

  return invalid;
}

static direntry findfile_dindirect(blocknum block, const char *filename, blocknum *dntnum, int *entry){
  direntry invalid;
  invalid.block.valid &= 0;

  if (block.valid == 0) return invalid;

  char tmp[BLOCKSIZE];
  memset(tmp, 0, BLOCKSIZE);

  indirect ind;
  dread(block.block, tmp);
  memcpy(&ind, tmp, sizeof(indirect));

  for (int i = 0; i < 128; i++){
    direntry result = findfile_indirect(ind.blocks[i], filename, dntnum, entry);
    if (result.block.valid) return result;
  }

  return invalid;
}


// loads root to memory if not already loaded
static void load_root(){
  if (root_loaded) return;
  char tmp[BLOCKSIZE];
  memset(tmp, 0, BLOCKSIZE);
  dread(the_vcb.root.block, tmp);
  memcpy(&root, tmp, sizeof(dnode));
  root_loaded = 1;
  fprintf(stderr, "root loaded!\n");
  return;
}

// gets the next free block from the vcb
// sets the vcb's free block to the subsequent one
static blocknum get_next_free_block(){
  fprintf(stderr, "get next free block called\n");
  blocknum target = the_vcb.free;

  // write the address of the next free block to the vcb
  char tmp[BLOCKSIZE];
  memset(tmp, 0, BLOCKSIZE);
  dread(target.block, tmp);
  freeblock oldfree;
  memcpy(&oldfree, tmp, sizeof(freeblock));
  the_vcb.free = oldfree.next;
  memset(tmp, 0, BLOCKSIZE);
  memcpy(tmp, &the_vcb, sizeof(vcb));
  dwrite(0, tmp);

  return target;
}

// could be optimized
static blocknum create_new_dirent(){
  fprintf(stderr, "making a new dirent block.\n");
  blocknum target = get_next_free_block();
  // to make sure
  target.valid |= 1;
  dirent dnt;
  for(int i = 0; i < 8; i++){
    dnt.entries[i].block.valid &= 0;
  }
  char tmp[BLOCKSIZE];
  memset(tmp, 0, BLOCKSIZE);
  memcpy(tmp, &dnt, sizeof(dirent));
  dwrite(target.block, tmp);
  return target;
}


  // optimize this later
  // want lambdas!
static blocknum get_next_empty_dirent(int *entry) {
  char tmp[BLOCKSIZE];

  //for now, assume root
  load_root();
  // in the future, these will be passed into the function
  dnode *dir = &root;
  blocknum dir_block = the_vcb.root;

  dirent dnt;
  int first_free = -1;
  // first search for direct dirents
  for(int i = 0; i < 116; i++){
    if((*dir).direct[i].valid == 0) {
      if(first_free == -1) first_free = i;
      continue;
    }
    memset(tmp,0, BLOCKSIZE);
    dread((*dir).direct[i].block, tmp);
    memcpy(&dnt, tmp, sizeof(dirent));
    for (int j = 0; j < 8; j++){
      if(dnt.entries[j].block.valid) continue;
      fprintf(stderr, "found a partial dirent block: %d\n", (*dir).direct[i].block);
      *entry = j;
      return (*dir).direct[i];
    }
  }
  if (first_free != -1){
    (*dir).direct[first_free] = create_new_dirent();
    fprintf(stderr, "Made new dirent block: %d\n", (*dir).direct[first_free].block);
    // write updated dir to disk
    memset(tmp, 0, BLOCKSIZE);
    memcpy(tmp, dir, sizeof(dnode));
    dwrite(dir_block.block, tmp);

    // return the right information
    *entry = 0;
    return (*dir).direct[first_free];
  }

  // if there is no single indirect
  if ((*dir).single_indirect.valid == 0){
    // get free blocks
    blocknum single_target = get_next_free_block();
    blocknum newdnt_target = create_new_dirent();

    // update directory
    (*dir).single_indirect = single_target;
    memset(tmp, 0, BLOCKSIZE);
    memcpy(tmp, dir, sizeof(dnode));
    dwrite(dir_block.block, tmp);

    // create single indirect
    indirect single;
    single.blocks[0] = newdnt_target;
    for(int i = 1; i < 128; i++){
      single.blocks[i].valid &= 0;
    }
    // write single indirect
    memset(tmp, 0, BLOCKSIZE);
    memcpy(tmp, &single, sizeof(indirect));
    dwrite(single_target.block, tmp);

    // return address of new dirent
    *entry = 0;
    return newdnt_target;
  }
  // if there is a single indirect
  // load it
  indirect single;
  memset(tmp, 0, BLOCKSIZE);
  dread((*dir).single_indirect.block, tmp);
  memcpy(&single, tmp, sizeof(indirect));

  // check for free and partial dirents
  for(int i = 0; i < 128; i++){
    if(single.blocks[i].valid == 0) {
      if(first_free == -1) first_free = i;
      continue;
    }
    memset(tmp,0, BLOCKSIZE);
    dread(single.blocks[i].block, tmp);
    memcpy(&dnt, tmp, sizeof(dirent));
    for (int j = 0; j < 8; j++){
      if(dnt.entries[j].block.valid) continue;
      *entry = j;
      return single.blocks[i];
    }
  }
  // if there are no partial dirents
  // and at least one free dirent
  if (first_free != -1){
    single.blocks[first_free] = create_new_dirent();
    memset(tmp, 0, BLOCKSIZE);
    memcpy(&single, tmp, sizeof(indirect));
    dwrite((*dir).single_indirect.block, tmp);
    *entry = 0;
    return single.blocks[first_free];
  }

  // if there isn't a double indirect
  if ((*dir).double_indirect.valid == 0){
    // get free blocks
    blocknum double_target = get_next_free_block();
    blocknum single_target = get_next_free_block();
    blocknum newdnt_target = create_new_dirent();

    // update directory
    (*dir).double_indirect = double_target;
    memset(tmp, 0, BLOCKSIZE);
    memcpy(tmp, dir, sizeof(dnode));
    dwrite(dir_block.block, tmp);

    // create double indirect
    indirect dind;
    dind.blocks[0] = single_target;
    for(int i = 1; i < 128; i++){
      dind.blocks[i].valid &= 0;
    }
    // write double indirect
    memset(tmp, 0, BLOCKSIZE);
    memcpy(tmp, &dind, sizeof(indirect));
    dwrite(double_target.block, tmp);

    // create single indirect
    single.blocks[0] = newdnt_target;
    for(int i = 1; i < 128; i++){
      single.blocks[i].valid &= 0;
    }
    // write single indirect
    memset(tmp, 0, BLOCKSIZE);
    memcpy(tmp, &single, sizeof(indirect));
    dwrite(single_target.block, tmp);

    // return address of new dirent
    *entry = 0;
    return newdnt_target;
  }
  // if there is a double indirect
  // load it
  indirect dind;
  memset(tmp, 0, BLOCKSIZE);
  dread((*dir).double_indirect.block, tmp);
  memcpy(&dind, tmp, sizeof(indirect));

  int first_free_single = -1;
  // check for free and partial indirects and dirents
  for(int i = 0; i < 128; i++){
    if(dind.blocks[i].valid == 0) {
      if(first_free_single == -1) first_free_single = i;
      continue;
    }
    memset(tmp,0, BLOCKSIZE);
    dread(dind.blocks[i].block, tmp);
    memcpy(&single, tmp, sizeof(indirect));
    for(int j = 0; j < 128; j++){
      if(single.blocks[j].valid == 0) {
        if(first_free == -1) first_free = j;
        continue;
      }
      memset(tmp,0, BLOCKSIZE);
      dread(single.blocks[j].block, tmp);
      memcpy(&dnt, tmp, sizeof(dirent));
      for (int k = 0; k < 8; k++){
        if(dnt.entries[k].block.valid) continue;
        *entry = k;
        return single.blocks[j];
      }
    }
    if (first_free != -1){
      single.blocks[first_free] = create_new_dirent();
      memset(tmp, 0, BLOCKSIZE);
      memcpy(&single, tmp, sizeof(indirect));
      dwrite(dind.blocks[i].block, tmp);
      *entry = 0;
      return single.blocks[first_free];
    }

  }
  // if there are no partial singles and at least one free single
  if (first_free_single != -1){
    blocknum single_target = get_next_free_block();
    blocknum newdnt_target = create_new_dirent();

    // update double indirect
    dind.blocks[first_free_single] = single_target;
    memset(tmp, 0, BLOCKSIZE);
    memcpy(tmp, &dind, sizeof(indirect));
    dwrite((*dir).double_indirect.block, tmp);

    // create single indirect
    single.blocks[0] = newdnt_target;
    for(int i = 1; i < 128; i++){
      single.blocks[i].valid &= 0;
    }
    // write single indirect
    memset(tmp, 0, BLOCKSIZE);
    memcpy(tmp, &single, sizeof(indirect));
    dwrite(single_target.block, tmp);

    // return address of new dirent
    *entry = 0;
    return newdnt_target;
  }


  // if we get here, the directory is at capacity;
  blocknum invalid;
  invalid.valid &= 0;
  return invalid;
}

/*
 * Given an absolute path to a file (for example /a/b/myFile), vfs_create 
 * will create a new file named myFile in the /a/b directory.
 *
 * HINT: Your solution should ignore rdev
 *
 */
static int vfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
  fprintf(stderr, "vfs_create called with path %s\n", path);

  blocknum trash;
  int trash2;

  direntry existing_file = findFile(path, &trash, &trash2);

  // if the file already exists, return the expected error
  if(existing_file.block.valid) {
    return -EEXIST;
  }

  // assuming root, for now
  load_root();

  const char *filename = path + 1;

  // assuming permissions
  // assuming filename of a reasonable length
  // assuming room

  // make the inode
  inode newfile;
  struct timespec mytime;
  clock_gettime(CLOCK_REALTIME, &mytime);
  newfile.access_time = mytime;
  newfile.create_time = mytime;
  newfile.modify_time = mytime;
  newfile.size = 0;
  newfile.user = getuid();
  newfile.group = getgid();
  newfile.mode = mode;

  for (int i = 0; i < 116; i++){
    newfile.direct[i].valid &= 0;
  }
  newfile.single_indirect.valid &= 0;
  newfile.double_indirect.valid &= 0;

  blocknum target = get_next_free_block();

  // write the inode to the disk
  char tmp[BLOCKSIZE];
  memset(tmp, 0, BLOCKSIZE);
  memcpy(tmp, &newfile, sizeof(inode));
  dwrite(target.block, tmp);

  // make the direntry
  direntry newde;
  newde.block = target;
  newde.block.valid |= 1;
  newde.type = 'f';
  strcpy(newde.name, filename);

  // find an empty dirent space
  int index = 0;
  blocknum dirent_target = get_next_empty_dirent(&index);

  if (dirent_target.valid == 0) {
    // there's no more room in the directory
    fprintf(stderr, "No more room in directory.\n");
    return -1;
  }

  // load the dirent
  fprintf(stderr, "loading dirent\n");
  dirent dnt;
  memset(tmp, 0, BLOCKSIZE);
  dread(dirent_target.block, tmp);
  memcpy(&dnt, tmp, sizeof(dirent));
  
  // modify the dirent
  fprintf(stderr, "modifying dirent\n");
  dnt.entries[index] = newde;

  // write the dirent
  fprintf(stderr, "saving dirent\n");
  memset(tmp, 0, BLOCKSIZE);
  memcpy(tmp, &dnt, sizeof(dirent));
  dwrite(dirent_target.block, tmp);

  return 0;
}

/*
 * The function vfs_read provides the ability to read data from 
 * an absolute path 'path,' which should specify an existing file.
 * It will attempt to read 'size' bytes starting at the specified
 * offset (offset) from the specified file (path)
 * on your filesystem into the memory address 'buf'. The return 
 * value is the amount of bytes actually read; if the file is 
 * smaller than size, vfs_read will simply return the most amount
 * of bytes it could read. 
 *
 * HINT: You should be able to ignore 'fi'
 *
 */
static int vfs_read(const char *path, char *buf, size_t size, off_t offset,
                    struct fuse_file_info *fi)
{
  fprintf(stderr, "vfs_read called\n");
  blocknum trash;
  int trash2;
  direntry target_direntry = findFile(path, &trash, &trash2);

  // check for valid
  if(target_direntry.block.valid == 0)
    return -1;

  // check for file
  if(target_direntry.type == 'd')
    return -1;

  fprintf(stderr, "file is valid. loading meta data.\n");

  // read the inode to target
  inode target;
  char tmp[BLOCKSIZE];
  memset(tmp,0,BLOCKSIZE);
  dread(target_direntry.block.block, tmp);
  memcpy(&target, tmp, sizeof(inode));

  fprintf(stderr, "meta data loaded.\n");
  // can't read the file if the offset is past the size of the file
  if (offset > target.size) return -1;

  int block_offset = offset/512;
  int byte_offset = offset % 512;

  int bytes_read = 0;

  fprintf(stderr, "starting read.\n");

  db loaded;
  for(int i = block_offset; i < 116; i++){
    if(target.direct[i].valid == 0) continue;
    memset(tmp, 0, BLOCKSIZE);
    dread(target.direct[i].block, tmp);
    memcpy(&loaded, tmp, sizeof(db));
    byte_offset = block_offset != i ? 0 : byte_offset;
    int limit = size - bytes_read <= 512 ? size - bytes_read : 512;
    for(int j = byte_offset; j < limit; j++){
      buf[bytes_read] = loaded.data[j];
      bytes_read ++;
      if(bytes_read > target.size) return bytes_read;
    }
    if (size <= bytes_read)
      return bytes_read;
  }
  
  block_offset = block_offset - 116 < 0 ? 0 : block_offset - 116;
  
  if(target.single_indirect.valid){
    indirect single;
    memset(tmp, 0, BLOCKSIZE);
    dread(target.single_indirect.block, tmp);
    memcpy(&single, tmp, sizeof(indirect));

    
    for(int i = block_offset; i < 128; i++){
      if(single.blocks[i].valid == 0) continue;
      memset(tmp, 0, BLOCKSIZE);
      dread(single.blocks[i].block, tmp);
      memcpy(&loaded, tmp, sizeof(db));
      byte_offset = block_offset != i ? 0 : byte_offset;
      int limit = size - bytes_read <= 512 ? size - bytes_read : 512;
      for(int j = byte_offset; j < limit; j++){
        buf[bytes_read] = loaded.data[j];
        bytes_read ++;
        if(bytes_read > target.size) return bytes_read;
      }
      if (size <= bytes_read)
        return bytes_read;
    }
  }

  block_offset = block_offset - 128 < 0 ? 0 : block_offset - 128;

  if(target.double_indirect.valid){
    indirect dind;
    memset(tmp, 0, BLOCKSIZE);
    dread(target.double_indirect.block, tmp);
    memcpy(&dind, tmp, sizeof(indirect));
      
    for(int k = 0; k < 128; k++){
      if(block_offset > 128){
        block_offset -= 128;
        continue;
      }
      if(dind.blocks[k].valid){
        indirect single;
        memset(tmp, 0, BLOCKSIZE);
        dread(dind.blocks[k].block, tmp);
        memcpy(&single, tmp, sizeof(indirect));
        
        for(int i = block_offset; i < 128; i++){
          block_offset = 0;
          if(single.blocks[i].valid == 0) continue;
          memset(tmp, 0, BLOCKSIZE);
          dread(single.blocks[i].block, tmp);
          memcpy(&loaded, tmp, sizeof(db));
          byte_offset = block_offset != i ? 0 : byte_offset;
          int limit = size - bytes_read <= 512 ? size - bytes_read : 512;
          for(int j = byte_offset; j < limit; j++){
            buf[bytes_read] = loaded.data[j];
            bytes_read ++;
            if(bytes_read > target.size) return bytes_read;
          }
          if (size <= bytes_read)
            return bytes_read;
        }
      }
    }
  }

  // no error handling here.
  return bytes_read;
}

/*
 * The function vfs_write will attempt to write 'size' bytes from 
 * memory address 'buf' into a file specified by an absolute 'path'.
 * It should do so starting at the specified offset 'offset'.  If
 * offset is beyond the current size of the file, you should pad the
 * file with 0s until you reach the appropriate length.
 *
 * You should return the number of bytes written.
 *
 * HINT: Ignore 'fi'
 */
static int vfs_write(const char *path, const char *buf, size_t size,
                     off_t offset, struct fuse_file_info *fi)
{

  /* 3600: NOTE THAT IF THE OFFSET+SIZE GOES OFF THE END OF THE FILE, YOU
           MAY HAVE TO EXTEND THE FILE (ALLOCATE MORE BLOCKS TO IT). */

  blocknum trash;
  int trash2;
  direntry target_direntry = findFile(path, &trash, &trash2);

  // check for valid
  if(target_direntry.block.valid == 0)
    return -1;

  // check for file
  if(target_direntry.type == 'd')
    return -1;

  // read the inode to target
  inode target;
  char tmp[BLOCKSIZE];
  memset(tmp,0,BLOCKSIZE);
  dread(target_direntry.block.block, tmp);
  memcpy(&target, tmp, sizeof(inode));

  indirect single;
  indirect dind;

  int single_loaded = 0;
  int double_loaded = 0;

  int blocks_allocated = target.size/BLOCKSIZE;

  while(offset + size > blocks_allocated * BLOCKSIZE) {
    blocknum newblock_target = get_next_free_block();
    db newblock;
    for(int i = 0; i < 512; i++){
      newblock.data[i] = 0;
    }
    // write newblock
    // could theoretically be optimized
    memset(tmp, 0, BLOCKSIZE);
    memcpy(tmp, &newblock, sizeof(db));
    dwrite(newblock_target.block, tmp);

    // put newblock in inode
    if(blocks_allocated < 116) {
      target.direct[blocks_allocated] = newblock_target;
    }
    else {
      int index = blocks_allocated - 116;
      if (index < 128) {
        if (single_loaded == 0) {
          if (target.single_indirect.valid == 0){
            blocknum single_target = get_next_free_block();
            for(int i = 0; i < 128; i++){
              single.blocks[i].valid &= 0;
            }
            target.single_indirect = single_target;
          }
          else {
            memset(tmp, 0, BLOCKSIZE);
            dread(target.single_indirect.block, tmp);
            memcpy(&single, tmp, sizeof(indirect));
          }
          single_loaded = 1;
        }
        single.blocks[index] = newblock_target;
      }
      else {
        index -= 128;
        if (double_loaded == 0){
          if (target.double_indirect.valid == 0){
            blocknum double_target = get_next_free_block();
            for(int i = 0; i < 128; i++){
              dind.blocks[i].valid &= 0;
            }
            target.double_indirect = double_target;
          }
          else {
            memset(tmp, 0, BLOCKSIZE);
            dread(target.double_indirect.block, tmp);
            memcpy(&dind, tmp, sizeof(indirect));
          }
          double_loaded = 1;
        }
        int i;
        for(i = 0; i < 128; i++){
          if (index < 128) break;
          index -= 128;
        }
        if (i == 128){
          // reached max file capacity
          return -1;
        }
        indirect tmpsingle;
        if(dind.blocks[i].valid == 0){
          blocknum single_target = get_next_free_block();
          for(int j = 0; j < 128; j++){
            tmpsingle.blocks[j].valid &= 0;
          }
          dind.blocks[i] = single_target;
        }
        else {
          memset(tmp, 0, BLOCKSIZE);
          dread(dind.blocks[i].block, tmp);
          memcpy(&tmpsingle, tmp, sizeof(indirect));
        }
        tmpsingle.blocks[blocks_allocated] = newblock_target;
        // write back to the tmp single
        memset(tmp, 0, BLOCKSIZE);
        memcpy(tmp, &tmpsingle, sizeof(indirect));
        dwrite(dind.blocks[i].block, tmp);
      }
    }
    blocks_allocated ++;
  }

  if(single_loaded) {
    memset(tmp, 0, BLOCKSIZE);
    memcpy(tmp, &single, sizeof(indirect));
    dwrite(target.single_indirect.block, tmp);
  }
  if(double_loaded) {
    memset(tmp, 0, BLOCKSIZE);
    memcpy(tmp, &dind, sizeof(indirect));
    dwrite(target.double_indirect.block, tmp);
  }

  int block_offset = offset/512;
  int byte_offset = offset % 512;
  int bytes_written = 0;
  int blocks_to_write = size/512;

  // preload double and single indirects, if needed
  if (double_loaded == 0 && block_offset + blocks_to_write  >= 244) {
    memset(tmp, 0, BLOCKSIZE);
    dread(target.double_indirect.block, tmp);
    memcpy(&dind, tmp, sizeof(indirect));
    double_loaded = 1;
  }
  if (single_loaded == 0 && ((block_offset >= 116 && block_offset < 244) || (block_offset + blocks_to_write >= 116 && block_offset + blocks_to_write < 244))){
    memset(tmp, 0, BLOCKSIZE);
    dread(target.single_indirect.block, tmp);
    memcpy(&single, tmp, sizeof(indirect));
  }

  db loaded;
  blocknum db_number;
  for(int i = block_offset; i <= block_offset + blocks_to_write; i++){
    memset(tmp, 0, BLOCKSIZE);
    if(i < 116) db_number = target.direct[i];
    else if (i < 244) db_number = single.blocks[i-116];
    else {
      indirect tmp_single;
      dread(dind.blocks[(i-244)/128].block, tmp);
      memcpy(&tmp_single, tmp, sizeof(indirect));
      memset(tmp, 0, BLOCKSIZE);
      db_number = tmp_single.blocks[(i-244)%128];
    }
    dread(db_number.block, tmp);
    memcpy(&loaded, tmp, sizeof(db));
    int limit = size - bytes_written > 512 - byte_offset ? 512 - byte_offset : size - bytes_written;
    byte_offset = i == block_offset ? byte_offset : 0;
    for (int j = byte_offset; j < limit; j++){
      loaded.data[j] = buf[bytes_written];
      bytes_written ++;
    }
    memset(tmp, 0, BLOCKSIZE);
    memcpy(tmp, &loaded, sizeof(db));
    dwrite(db_number.block, tmp);
  }

  target.size += bytes_written;
  memset(tmp, 0, BLOCKSIZE);
  memcpy(tmp, &target, sizeof(inode));
  dwrite(target_direntry.block.block, tmp);

  return bytes_written;
}

/**
 * This function deletes the last component of the path (e.g., /a/b/c you 
 * need to remove the file 'c' from the directory /a/b).
 */
static int vfs_delete(const char *path)
{
  // for now, assuming root
  load_root();
  
  // find and load the file
  blocknum target_dirent_block;
  int entry;
  direntry target_direntry = findFile(path, &target_dirent_block, &entry);

  if (target_direntry.block.valid == 0){
    // could not find file
    return -1;
  }

  inode target;
  char tmp[BLOCKSIZE];
  memset(tmp, 0, BLOCKSIZE);
  dread(target_direntry.block.block, tmp);
  memcpy(&target, tmp, sizeof(inode));


  //  check for datablocks used by file, remove those
  for (int i = 0; i < 116; i++){
    if(target.direct[i].valid){
      free_block(target.direct[i]);
    }
  }
  if(target.single_indirect.valid){
    // load single indirect
    indirect single;
    memset(tmp, 0, BLOCKSIZE);
    dread(target.single_indirect.block, tmp);
    memcpy(&single, tmp, sizeof(indirect));

    for(int i = 0; i < 128; i++){
      if(single.blocks[i].valid){
        free_block(single.blocks[i]);
      }
    }

    free_block(target.single_indirect);
  }

  if(target.double_indirect.valid){
    indirect dind;
    memset(tmp, 0, BLOCKSIZE);
    dread(target.double_indirect.block, tmp);
    memcpy(&dind, tmp, sizeof(indirect));

    indirect single;
    for(int j = 0; j < 128; j++){
      if(dind.blocks[j].valid == 0) continue;
      memset(tmp, 0, BLOCKSIZE);
      dread(dind.blocks[j].block, tmp);
      memcpy(&single, tmp, sizeof(indirect));

      for(int i = 0; i < 128; i++){
        if(single.blocks[i].valid){
          free_block(single.blocks[i]);
        }
      }
      free_block(dind.blocks[j]);
    }
    free_block(target.double_indirect);
  }

  // free inode
  free_block(target_direntry.block);

  // write the vcb
  memset(tmp, 0, BLOCKSIZE);
  memcpy(tmp, &the_vcb, sizeof(vcb));
  dwrite(0, tmp);

  // load the dirent
  dirent dnt;
  memset(tmp, 0, BLOCKSIZE);
  dread(target_dirent_block.block, tmp);
  memcpy(&dnt, tmp, sizeof(dirent));

  // change the dirent
  dnt.entries[entry].block.valid &= 0;

  // write the dirent
  memset(tmp, 0, BLOCKSIZE);
  memcpy(tmp, &dnt, sizeof(dirent));
  dwrite(target_dirent_block.block, tmp);

  return 0;
}

/*
 * The function rename will rename a file or directory named by the
 * string 'oldpath' and rename it to the file name specified by 'newpath'.
 *
 * HINT: Renaming could also be moving in disguise
 *
 */
static int vfs_rename(const char *from, const char *to)
{
  blocknum dirent_block;
  int entry;
  direntry target = findFile(from, &dirent_block, &entry);

  if(target.block.valid){
     // load dirent

     // change dirent

     // write dirent
     return 0;
  }

  return -1;
}


/*
 * This function will change the permissions on the file
 * to be mode.  This should only update the file's mode.  
 * Only the permission bits of mode should be examined 
 * (basically, the last 16 bits).  You should do something like
 * 
 * fcb->mode = (mode & 0x0000ffff);
 *
 */

static int vfs_chmod(const char *file, mode_t mode)
{
  //find the block
  blocknum trash;
  int trash2;
  direntry target_d = findFile(file, &trash, &trash2);

  //handle broken file here?

  inode target;
  char tmp[BLOCKSIZE];


  // read the block
  memset(tmp, 0, BLOCKSIZE);
  dread(target_d.block.block, tmp);
  memcpy(&target,tmp,sizeof(inode));

  // change the block's mode
  target.mode = (mode & 0x0000ffff);

  // write the block back to disk
  memset(tmp, 0, BLOCKSIZE);
  memcpy(tmp, &target, sizeof(inode));
  dwrite(target_d.block.block, tmp);

  return 0;
}

/*
 * This function will change the user and group of the file
 * to be uid and gid.  This should only update the file's owner
 * and group.
 */
static int vfs_chown(const char *file, uid_t uid, gid_t gid)
{
  //find the block
  blocknum trash;
  int trash2;
  direntry target_d = findFile(file, &trash, &trash2);

  //handle broken file here?

  inode target;
  char tmp[BLOCKSIZE];


  // read the block
  memset(tmp, 0, BLOCKSIZE);
  dread(target_d.block.block, tmp);
  memcpy(&target,tmp,sizeof(inode));

  // change the block's mode
  target.user = uid;
  target.group = gid;

  // write the block back to disk
  memset(tmp, 0, BLOCKSIZE);
  memcpy(tmp, &target, sizeof(inode));
  dwrite(target_d.block.block, tmp);

  return 0;
}

/*
 * This function will update the file's last accessed time to
 * be ts[0] and will update the file's last modified time to be ts[1].
 */
static int vfs_utimens(const char *file, const struct timespec ts[2])
{
  //find the block
  blocknum trash;
  int trash2;
  direntry target_d = findFile(file, &trash, &trash2);

  //handle broken file here?

  inode target;
  char tmp[BLOCKSIZE];


  // read the block
  memset(tmp, 0, BLOCKSIZE);
  dread(target_d.block.block, tmp);
  memcpy(&target,tmp,sizeof(inode));

  // change the block's mode
  target.access_time = ts[0];
  target.modify_time = ts[1];

  // write the block back to disk
  memset(tmp, 0, BLOCKSIZE);
  memcpy(tmp, &target, sizeof(inode));
  dwrite(target_d.block.block, tmp);

  return 0;
}

// does NOT write the_vcb to disk
static void free_block(blocknum b){
  if(b.valid){
    freeblock newfree;
    newfree.next = the_vcb.free;
    the_vcb.free = b;
    char tmp[BLOCKSIZE];
    memset(tmp, 0, BLOCKSIZE);
    memcpy(tmp, &newfree, sizeof(freeblock));
    dwrite(b.block, tmp);
  }
}

/*
 * This function will truncate the file at the given offset
 * (essentially, it should shorten the file to only be offset
 * bytes long).
 */
static int vfs_truncate(const char *file, off_t offset)
{

  /* 3600: NOTE THAT ANY BLOCKS FREED BY THIS OPERATION SHOULD
           BE AVAILABLE FOR OTHER FILES TO USE. */

  blocknum trash;
  int trash2;
  direntry target_direntry = findFile(file, &trash, &trash2);

  // check for valid
  if(target_direntry.block.valid == 0)
    return -1;

  // check for file
  if(target_direntry.type == 'd')
    return -1;

  // read the inode to target
  inode target;
  char tmp[BLOCKSIZE];
  memset(tmp,0,BLOCKSIZE);
  dread(target_direntry.block.block, tmp);
  memcpy(&target, tmp, sizeof(inode));

  // offset is larger than file
  if (target.size < offset) return -1;
  target.size = offset;

  // figure out how many blocks to keep
  // does ceil work with integer division?
  int blocks_to_keep = ceil(offset/512);


  // TODO fix imprecise handling of partial block truncation
  
  for(int i = blocks_to_keep  ; i < 116; i++){
    free_block(target.direct[i]);
    target.direct[i].valid &= 0;
  }
  blocks_to_keep -= 116;

  if(target.single_indirect.valid) {
    indirect single;
    memset(tmp, 0, BLOCKSIZE);
    dread(target.single_indirect.block, tmp);
    memcpy(&single, tmp, sizeof(indirect));

    int block_offset = blocks_to_keep > 0 ? blocks_to_keep : 0;

    for(int i = block_offset ; i < 128; i++){
      free_block(single.blocks[i]);
      single.blocks[i].valid &= 0;
    }

    if (blocks_to_keep <= 0){
      free_block(target.single_indirect);
      target.single_indirect.valid &= 0;
    }
    else {
        // write modified single indirect to disk
        memset(tmp, 0, BLOCKSIZE);
        memcpy(tmp, &single, sizeof(indirect));
        dwrite(target.single_indirect.block, tmp);
    }
  }

  blocks_to_keep -= 128;

  if(target.double_indirect.valid) {
    indirect dind;
    indirect single;
    memset(tmp, 0, BLOCKSIZE);
    dread(target.double_indirect.block, tmp);
    memcpy(&dind, tmp, sizeof(indirect));

    int block_offset = blocks_to_keep > 0 ? blocks_to_keep : 0;

    for(int i = 0 ; i < 128; i++){
      if(block_offset >= 128) {
        block_offset -= 128;
        continue;
      }
      if(dind.blocks[i].valid){
        memset(tmp, 0, BLOCKSIZE);
        dread(dind.blocks[i].block, tmp);
        memcpy(&single, tmp, sizeof(indirect));
        for(int j = block_offset ; j < 128; j++){
          free_block(single.blocks[j]);
          single.blocks[j].valid &= 0;
        }
        if(blocks_to_keep <= i * 128) {
          free_block(dind.blocks[i]);
          dind.blocks[i].valid &= 0;
        }
        else {
          block_offset = 0;
          // write modified single indirect to disk
          memset(tmp, 0, BLOCKSIZE);
          memcpy(tmp, &single, sizeof(indirect));
          dwrite(dind.blocks[i].block, tmp);
          
        }
      }
    }
    if(blocks_to_keep <= 0){
      free_block(target.double_indirect);
      target.double_indirect.valid &= 0;
    }else{
        // write modified double indirect to disk
        memset(tmp, 0, BLOCKSIZE);
        memcpy(tmp, &dind, sizeof(indirect));
        dwrite(target.double_indirect.block, tmp);
    }
  }

  // write target to disk
  memset(tmp, 0, BLOCKSIZE);
  memcpy(tmp, &target, sizeof(inode));
  dwrite(target_direntry.block.block, tmp);

  // write updated vcb to disk
  memset(tmp, 0, BLOCKSIZE);
  memcpy(tmp, &the_vcb, sizeof(vcb));
  dwrite(0, tmp);

  return 0;
}


/*
 * You shouldn't mess with this; it sets up FUSE
 *
 * NOTE: If you're supporting multiple directories for extra credit,
 * you should add 
 *
 *     .mkdir  = vfs_mkdir,
 */
static struct fuse_operations vfs_oper = {
    .init    = vfs_mount,
    .destroy = vfs_unmount,
    .getattr = vfs_getattr,
    .readdir = vfs_readdir,
    .create  = vfs_create,
    .read  = vfs_read,
    .write   = vfs_write,
    .unlink  = vfs_delete,
    .rename  = vfs_rename,
    .chmod   = vfs_chmod,
    .chown   = vfs_chown,
    .utimens   = vfs_utimens,
    .truncate  = vfs_truncate,
};

int main(int argc, char *argv[]) {
    /* Do not modify this function */
    umask(0);
    if ((argc < 3) || (strcmp("-d", argv[1]))) {
      printf("Usage: ./3600fs -d <dir>\n");
      exit(-1);
    }
    return fuse_main(argc, argv, &vfs_oper, NULL);
}

