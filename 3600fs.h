/*
 * CS3600 Project 2: A User-Level File System
 */

#include "inode.h"
#include <fuse.h>

#ifndef __3600FS_H__
#define __3600FS_H__

static int getattr_from_direntry(direntry target_d, struct stat *stbuf);
static direntry findFile(const char *path);
static blocknum startFindPath(direntry startingDir, const char *path);
static blocknum findPath(const char *path);
static void load_root();


#endif
