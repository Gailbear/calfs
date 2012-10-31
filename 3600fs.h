/*
 * CS3600 Project 2: A User-Level File System
 */

#include "inode.h"
#include <fuse.h>

#ifndef __3600FS_H__
#define __3600FS_H__

static int getattr_from_direntry(direntry target_d, struct stat *stbuf);
static direntry findFile(const char *path, blocknum *dntnum, int *entry);
static direntry findfile_indirect(blocknum block, const char *path, blocknum *dntnum, int *entry);
static direntry findfile_dindirect(blocknum block, const char *path, blocknum *dntnum, int *entry);
static int readdir_indirect(blocknum block, void *buf, fuse_fill_dir_t filler, off_t offset);
static int readdir_dindirect(blocknum block, void *buf, fuse_fill_dir_t filler, off_t offset);

static void check_blocks_dnode(blocknum blockNode, char *blockCheck);
static void check_blocks_inode(blocknum blockNode, char *blockCheck);
static void check_blocks_direntry(blocknum blockDirentry, char* blockCheck);
static void check_blocks_dirent(blocknum blockDirent, char* blockCheck);
static void check_blocks_indirect_dnode(blocknum blockIndirect, int levels, char *blockCheck);
static void check_blocks_indirect_inode(blocknum blockIndirect, int levels, char *blockCheck);
static void check_blocks_free(blocknum blockFree, char *blockCheck);
static void assure_integrity();

//static blocknum startFindPath(direntry startingDir, const char *path);
//static blocknum findPath(const char *path);
static void load_root();
static void free_block(blocknum b);


#endif
