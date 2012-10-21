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
    memcpy(tmp, &the_vcb, sizeof(vcb));
    dwrite(0, tmp);
  }else {
    // check the disk
    // then set vcb to 0
  }


  return NULL;
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
  
  direntry target_d = findFile(path);

  fprintf(stderr, "target acquired!\n");
  
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
    //fprintf(stderr, "dirent %d loaded\n", i);
    // for each entry in the dirent block
    for (int j = 0; j < 64; j++) {
      // continue if entry is invalid
      if (contents.entries[j].block.valid == 0) {
        //fprintf(stderr, "entry %d in dirent %d is invalid.\n", j ,i);
        continue;
      }
      // add to the list
      struct stat stbuf;
      getattr_from_direntry(contents.entries[j], &stbuf);
      if(filler(buf, contents.entries[j].name, &stbuf, 0))
        return 1;
    }
  }
  // check for indirects, and then search those
  return 0;
}



//Returns direntry of path
// if it can't be found, returns a direntry with an invalid block
static direntry findFile(const char *path){
  fprintf(stderr, "findFile called\n");
  // for now, assuming root
  load_root();

  char *filename;
  filename = path++;

  char tmp[BLOCKSIZE];

  dirent contents;
  // for all the dirent blocks
  for (int i = 0; i < 116; i++){
    if(root.direct[i].valid == 0) {
      fprintf(stderr, "direct %d is invalid\n", i);
      continue;
    }
    // load the ith dirent block
    memset(tmp, 0, BLOCKSIZE);
    dread(root.direct[i].block,tmp);
    memcpy(&contents, tmp, sizeof(dirent));
    fprintf(stderr, "dirent %d loaded\n", i);
    // for each entry in the dirent block
    for (int j = 0; j < 64; j++) {
      // continue if entry is invalid
      if (contents.entries[j].block.valid == 0) {
        //fprintf(stderr, "entry %d in dirent %d is invalid.\n", j ,i);
        continue;
      }
      // compare the name
      if (strcmp(filename, contents.entries[j].name) == 0){
        // we found it! return the direntry
        return contents.entries[j];
      }
    }
  }
  // TODO check for indirects, and then search those

  direntry invalid;
  invalid.block.valid |= 0;
  return invalid;
}




//Returns blocknum pointing to DNODE of path within startingDir
static blocknum startFindPath(direntry startingDir, const char *path)
{
  dnode dirMeta;
  char tmp[BLOCKSIZE];

  //Get rid of absolute path
  if(*path == '/') path++;

  //Check if we are currently in the correct directory
  if(strlen(path) == 0)
    return startingDir.block;

  //If not in the correct directory, get meta data
  memset(tmp, 0, BLOCKSIZE);
  dread(startingDir.block.block, tmp);
  memcpy(&dirMeta, tmp, sizeof(dnode));
  //If permissions don't match up, kick em out.
  if(dirMeta.user != getuid() && dirMeta.group != getgid())
  {
    blocknum noPerms;
    noPerms.block = -2;
    noPerms.valid |= 1;
    return noPerms;
  }

  //If permissions do match up, try to traverse
  int i = 0;
  char *firstSlash = strchr(path, '/');
  if(firstSlash == NULL) firstSlash = path+strlen(path);

  char targetDir[(firstSlash-path)+1];
  strncpy(targetDir, path, firstSlash-path);
  targetDir[(firstSlash-path)] = '\0';

  for(i = 0; i < 116; i++)
  {
    //If the block isn't valid, jump
    if(dirMeta.direct[i].valid == 0) continue;

    //If the names match traverse
    direntry myDir;

    memset(tmp, 0, BLOCKSIZE);
    dread(dirMeta.direct[i].block, tmp);
    memcpy(&myDir, tmp, sizeof(direntry));

    if(strcmp(myDir.name, targetDir) == 0)
        return startFindPath(myDir, firstSlash);

  }

  //Directory not found
  blocknum nonExistant;
  nonExistant.valid = 0;
  return nonExistant;
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

//Returns blocknum pointing to DNODE of path
static blocknum findPath(const char *path)
{
  direntry rootDir;
  char tmp[BLOCKSIZE];
    
  memset(tmp, 0, BLOCKSIZE);
  memcpy(&rootDir, tmp, sizeof(direntry));
          
  return startFindPath(rootDir, path);

}

/*
 * Given an absolute path to a file (for example /a/b/myFile), vfs_create 
 * will create a new file named myFile in the /a/b directory.
 *
 * HINT: Your solution should ignore rdev
 *
 */
static int vfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
  char *lastSlash = strrchr(path, '/');
  if(lastSlash == NULL) lastSlash = path;
  char tmp[BLOCKSIZE];

  //Is there any free space?
  if(the_vcb.free.valid == 0)
    return -1;

  //is the filename too long?
  if(strlen(lastSlash+1) >= 59)
    return -2;

  //Find the first free blocks and put something in them
  blocknum firstFree = the_vcb.free;
  freeblock firstFreeBlock;

  memset(tmp, 0, BLOCKSIZE);
  dread(firstFree.block, tmp);
  memcpy(&firstFreeBlock, tmp, sizeof(freeblock));

  blocknum secondFree = firstFreeBlock.next;
  freeblock secondFreeBlock;

  memset(tmp, 0, BLOCKSIZE);
  dread(secondFree.block, tmp);
  memcpy(&secondFreeBlock, tmp, sizeof(freeblock));

  the_vcb.free = secondFreeBlock.next;

  //What directory is this file in?
  char targetDir[(lastSlash-path)+1];
  strncpy(targetDir, path, lastSlash-path);
  targetDir[(lastSlash-path)] = '\0';

  //Pull the directory into memory. Checking for errors
  blocknum dirBlock = findPath(targetDir);
  if(dirBlock.valid == 0 || dirBlock.block == -1 || dirBlock.block == -2)
    return -2;


  dnode dirNode;

  memset(tmp, 0, BLOCKSIZE);
  dread(dirBlock.block, tmp);
  memcpy(&dirNode, tmp, sizeof(dnode));

  //Find the first invalid entry
  int i;
  blocknum invalidBlock;
  invalidBlock.valid = 0;
  for(i = 0; i < 116; i++)
  {
    if(dirNode.direct[i].valid == 0)
    {
      invalidBlock.valid |= 1;
      break;
    }
  }

  //Did we find any invalid entries? 
  if(invalidBlock.valid == 0)
    // here, start with indirects
    return -3;

  invalidBlock.block = firstFree.block;
  invalidBlock.valid |= 1;
  dirNode.direct[i] = invalidBlock;


  //Create new directory entry
  direntry newEntry;
  strncpy(newEntry.name, lastSlash+1, strlen(lastSlash+1));
  newEntry.type = 1;
  newEntry.block = secondFree;

  //Create new file inode
  inode newNode;
  struct timespec currTime;
  clock_gettime(CLOCK_REALTIME, &currTime);
  newNode.size = BLOCKSIZE;
  newNode.user = getuid();
  newNode.group = getgid();
  newNode.mode = mode;
  newNode.access_time = currTime;
  newNode.modify_time = currTime;
  newNode.create_time = currTime;

  //Write everything to disk

  memset(tmp, 0, BLOCKSIZE);
  memcpy(tmp, &newNode, sizeof(inode));

  dwrite(secondFree.block, tmp);

  memset(tmp, 0, BLOCKSIZE);
  memcpy(tmp, &newEntry, sizeof(direntry));
  dwrite(firstFree.block, tmp);

  memset(tmp, 0, BLOCKSIZE);
  memcpy(tmp, &dirNode, sizeof(dnode));
  dwrite(dirBlock.block, tmp);

  memset(tmp, 0, BLOCKSIZE);
  memcpy(tmp, &the_vcb, sizeof(vcb));
  dwrite(0, tmp);

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

    return 0;
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

  return 0;
}

/**
 * This function deletes the last component of the path (e.g., /a/b/c you 
 * need to remove the file 'c' from the directory /a/b).
 */
static int vfs_delete(const char *path)
{
  char *lastSlash = strrchr(path, '/');
  if(lastSlash == NULL) lastSlash = path;
  char tmp[BLOCKSIZE];

  /* 3600: NOTE THAT THE BLOCKS CORRESPONDING TO THE FILE SHOULD BE MARKED
           AS FREE, AND YOU SHOULD MAKE THEM AVAILABLE TO BE USED WITH OTHER FILES */

  //What directory is this file in?
  char targetDir[(lastSlash-path)+1];
  strncpy(targetDir, path, lastSlash-path);
  targetDir[(lastSlash-path)] = '\0';


  blocknum dirBlock = findPath(targetDir);

  //Was there an error finding the directory?
  if(dirBlock.valid == 0 || dirBlock.block == -1 || dirBlock.block == -2)
    return -2;

  //Load directory into memory
  dnode dirNode;
  blocknum oldBlock;

  memset(tmp, 0, BLOCKSIZE);
  dread(dirBlock.block, tmp);
  memcpy(&dirNode, tmp, sizeof(dnode));
  int i;

  for(i = 0; i < 119; i++)
  {
    //If the block isn't valid, jump
    if(dirNode.direct[i].valid == 0) continue;

    //If the names match traverse
    direntry myDir;

    memset(tmp, 0, BLOCKSIZE);
    dread(dirNode.direct[i].block, tmp);
    memcpy(&myDir, tmp, sizeof(direntry));

    if(strcmp(myDir.name, lastSlash+1) == 0)
    {
      oldBlock = dirNode.direct[i];
      dirNode.direct[i].valid = 0;
      break;
    }

  }

  //Make a free block to replace inode. Add to free block list
  freeblock myFree;
  myFree.next = the_vcb.free;

  the_vcb.free = oldBlock;

  //Write new stuff to disk
  memset(tmp, 0, BLOCKSIZE);
  memcpy(tmp, &myFree, sizeof(freeblock));

  dwrite(oldBlock.block, tmp);

  memset(tmp, 0, BLOCKSIZE);
  memcpy(tmp, &dirNode, sizeof(dnode));
  dwrite(dirBlock.block, tmp);

  memset(tmp, 0, BLOCKSIZE);
  memcpy(tmp, &the_vcb, sizeof(vcb));
  dwrite(0, tmp);




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

    return 0;
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

    return 0;
}

/*
 * This function will change the user and group of the file
 * to be uid and gid.  This should only update the file's owner
 * and group.
 */
static int vfs_chown(const char *file, uid_t uid, gid_t gid)
{

    return 0;
}

/*
 * This function will update the file's last accessed time to
 * be ts[0] and will update the file's last modified time to be ts[1].
 */
static int vfs_utimens(const char *file, const struct timespec ts[2])
{

    return 0;
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

