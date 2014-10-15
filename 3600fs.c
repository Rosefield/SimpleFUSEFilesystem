/*
 * CS3600, Spring 2014
 * Project 2 Starter Code
 * (c) 2013 Alan Mislove
 *
 * This file contains all of the basic functions that you will need 
 * to implement for this project.  Please see the project handout
 * for more details on any particular function, and ask on Piazza if
 * you get stuck.
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

vcb head;
dnode root;
dirent root_dirent;

//Helper functions
int file_exists(dnode * dir, char * name) {
	if(dir->size == 0) {
		return 0;
	}

	
	if(dir->size < 110*16 +1) {
		for(int i = 0; i < 110; ++i) {
			dirent tmp;
			dread(dir->direct[i].block, (char *)&tmp);
			for(int x = 0; x < 16; ++x) {
				if(strcmp(tmp.entries[x].name, name) == 0) {
					return 1;
				}
			}
		}	
	}

	return 0;
}

/*
 * This is kind of dirty. Also makes sure name is a file (and not dir).
 */
int file_exists2(dnode * dir, char * name) {
	if(dir->size == 0) {
		return 0;
	}

	
	if(dir->size < 110*16 +1) {
		for(int i = 0; i < 110; ++i) {
			dirent tmp;
			dread(dir->direct[i].block, (char *)&tmp);
			for(int x = 0; x < 16; ++x) {
				if(strcmp(tmp.entries[x].name, name) == 0 &&
                   tmp.entries[x].type == DIRENTRY_FILE) {
					return 1;
				}
			}
		}	
	}

	return 0;
}


//Makes the 
void vcb_update_free() {
	free_block tmp;

	dread(head.free.block, (char *)&tmp);

    head.free = tmp.next;
    dwrite(0, (char*) &head);
}

void add_direntry(dnode * dir, unsigned int dir_block, direntry our_direntry) {
	
	for(int i = 0; i < 110; ++i) {
        /* we get the first root dirent for free (from 3600mkfs) but afterwards
           we need to allocate dirents
           
           if we're starting a new dirent, its block will be 0 from the initial
           format
         */
        if (dir->direct[i].block == 0) {
            dir->direct[i] = head.free;
            vcb_update_free();
            dwrite(dir_block, (char *) dir);
        }
		dirent tmp;
		dread(dir->direct[i].block, (char *)&tmp);
		for(int x = 0; x < 16; ++x) {
			if(!tmp.entries[x].block.valid) {
				tmp.entries[x] = our_direntry;
				dwrite(dir->direct[i].block, (char *)&tmp);
                dir->size++;
                dwrite(dir_block, (char*) dir);
				return;
			}
		}

	}	
}

/*
 * release_block -- free an arbitrary block, given number. create new free
 * block pointing to current head.free and write it. then update head.free
 * to point to newly replaced block
 */
void release_block (unsigned int block) {
    /* new free block, pointing to current head.free */
    free_block tmp;
    tmp.next = head.free;
    dwrite(block, (char*) &tmp);
    /* set head.free to new */
    head.free.block = block;
    dwrite(0, (char*) &head);
}

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

    //read vcb

    dread(0, (char *)&head);

    if(head.magic != 0x77) {
        //throw an error
        printf("Magic is incorrect.\n");
        return NULL;
    }

    if(!head.free.valid) {
		printf("free block is wrong.\n");
		return NULL;
	}

	dread(head.root.block, (char *)&root);

	//Do something with root?

	dread(2, (char *)&root_dirent);

	//root dirent should have entries for "." and ".." pointing to the root dnode
	if(strcmp(root_dirent.entries[0].name,".") != 0 || root_dirent.entries[0].block.block != head.root.block) {
		printf("first entry should have \".\" and point to root dnode.\n");
		return NULL;
	}
	if(strcmp(root_dirent.entries[1].name,"..") != 0 || root_dirent.entries[1].block.block != head.root.block) {
		printf("first entry should have \"..\" and point to root dnode.\n");
		return NULL;
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

	//Add metadata about write state, a "clean" tag


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

	//until multidirectory is implemented, our_node is always the root dnode
	dnode our_node = root;
	//direntry our_direntry;
	const char * name;

	if(strcmp(path, "/") == 0) {
		our_node = root;
		name = "";
	} else {
		our_node = root;
		name = path + 1;
		
		//iterate through name to find nested directories

		//our_node.size is the number of direntries contained in direct, indirect, double indirect
		//The number of direntries that can be contained in just direct is 110 * 16
		if(our_node.size < 110*16 +1) {
			for(int i = 0; i < 110; ++i) {
				for(int x = 0; x < 16; ++x) {
					dirent tmp;
					dread(our_node.direct[i].block, (char *)&tmp);
					if(strcmp(tmp.entries[x].name, name) == 0) {
						dread(tmp.entries[x].block.block, (char *)&our_node);
						goto out;
					}
				}
			}

            /* If we get here, we checked every direntry in every direct */
            /* and found nothing that matched */
			return -ENOENT;
		//Other cases that involve using indirect blocks
		//go through direct[], indirect, double_indirect searching for name
		} else if (0) {


		}

		//I can't believe I am using a goto in a serious application
		//In case anything else needs to be done within the else block, leave the goto here
		
	}

	out:


  // Do not mess with this code 
  stbuf->st_nlink = 1; // hard links
  stbuf->st_rdev  = 0;
  stbuf->st_blksize = BLOCKSIZE;

  /* 3600: YOU MUST UNCOMMENT BELOW AND IMPLEMENT THIS CORRECTLY */
  
  
  if(strcmp(path,"/") == 0)
    stbuf->st_mode  = 0777 | S_IFDIR;
  else 
	//what would the mode `be?
    stbuf->st_mode  = our_node.mode | S_IFREG;

  stbuf->st_uid     = our_node.user; // file uid
  stbuf->st_gid     = our_node.group; // file gid
  stbuf->st_atime   = our_node.access_time.tv_sec; // access time 
  stbuf->st_mtime   = our_node.modify_time.tv_sec; // modify time
  stbuf->st_ctime   = our_node.create_time.tv_sec;// create time

	//What is the proper handling for stat of a directory?
	//Temporarily just check if the path is the root 
	if(strcmp(path, "/") == 0) {
		stbuf->st_size    = BLOCKSIZE; // file size
		stbuf->st_blocks  = 1; // file size in blocks
	} else {
		stbuf->st_size    = our_node.size; // file size
		stbuf->st_blocks  = our_node.size / BLOCKSIZE; // file size in blocks
	}

  return 0;
}

/*
 * Given an absolute path to a directory (which may or may not end in
 * '/'), vfs_mkdir will create a new directory named dirname in that
 * directory, and will create it with the specified initial mode.
 *
 * HINT: Don't forget to create . and .. while creating a
 * directory.
 */
/*
 * NOTE: YOU CAN IGNORE THIS METHOD, UNLESS YOU ARE COMPLETING THE 
 *       EXTRA CREDIT PORTION OF THE PROJECT.  IF SO, YOU SHOULD
 *       UN-COMMENT THIS METHOD.
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
 * Your solution should not need to touch fi
 *
 */
static int vfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                       off_t offset, struct fuse_file_info *fi)
{

    if (strcmp(path, "/") != 0) {
        return -1;
    }

    int outer_start = offset / 16;
    int inner_start, next;
    int counter = offset; /* number of entries processed */
    int is_first = 1;
    char *file;

    // only supports direct blocks atm
    // for each direct block starting from aaa
    for (int i = outer_start; i < 110; i++) {

        dirent tmp;
        dread(root.direct[i].block, (char*) &tmp);

        if (is_first) {
            inner_start = offset % 16;
            is_first = 0;
        } else {
            inner_start = 0;
        }

        // iterate through dirents
        for (int j = inner_start; j < 16; j++) {

            /* stop when we've processed all entries */
            if (counter == root.size) {
                goto out;
            }

            /* check validity */
            if (!tmp.entries[j].block.valid) {
                /* return 0; */
                continue;
            }

            file = tmp.entries[j].name;
            next = i*16 + j + 1;
            if (filler(buf, file, NULL, next)) {
                return 0;
            }

            counter++;

        }
    }

out:
    return 0;
}

/*
 * Given an absolute path to a file (for example /a/b/myFile), vfs_create 
 * will create a new file named myFile in the /a/b directory.
 *
 */
static int vfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
	//No space left in the filesystem, cannot create a new file
	if(!head.free.valid) {
		return -1;
	}  

	char * name = path + 1;

	//Feature add: for multidirectory, create directories as needed so that path can be made	

	if(file_exists(&root, name)) {
		return -EEXIST;
	}

	//create inode for file
	inode our_inode;

	memset(&our_inode, 0, BLOCKSIZE);

	our_inode.size = 0;
	our_inode.user = geteuid();
	our_inode.group = getegid();
	our_inode.mode = mode;
	
	struct timespec time;
	clock_gettime(CLOCK_REALTIME, &time);

	our_inode.access_time = time;
	our_inode.create_time = time;
	our_inode.modify_time = time;

	//direct, indirect, double indirect have no data at the start

	//get a block for the inode from the vcb free list
	blocknum block_inode = head.free;

	vcb_update_free();
	dwrite(block_inode.block, (char *)&our_inode);


	//create direntry for inode
	direntry our_direntry;
	//direntry has field char * name[27], therefore cannot have a name of longer
	//than 27 characters \0 included
	strncpy(our_direntry.name, name, 27);
	our_direntry.type = DIRENTRY_FILE;	
	our_direntry.block = block_inode;	

	//add direntry to dir's direct/indirect/double indirect blocks
    // 1 is block of root dnode
	add_direntry(&root, 1, our_direntry);

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

  /* 3600: NOTE THAT THE BLOCKS CORRESPONDING TO THE FILE SHOULD BE MARKED
           AS FREE, AND YOU SHOULD MAKE THEM AVAILABLE TO BE USED WITH OTHER FILES */

    //make sure file exists and is a file (not dir)

    char *name = path + 1;
    if (!file_exists2(&root, name)) {
        return -1;
    }

    //remove file's entry from directory
    /* TODO: this only works for directs, not indirects */
    for (int i = 0; i < 110; i++) {
    /* for each direct: */
        dirent tmp;
        dread(root.direct[i].block, (char*) &tmp);
        for (int j = 0; j < 16; j++) {
        /* for each direntry: */
            if (strcmp(tmp.entries[j].name, name) == 0) {
            /* if direntry.name == path: */
                inode name_inode;
                /* blk = direntry.block */
                /* inode = dread(blk) */
                dread(tmp.entries[j].block.block, (char*) &name_inode);
                // free all of the file's data blocks
                for(int x = 0; x < 110; x++) {
                /* for each of inode's directs: */
                    int done_flag = 0; // TODO this is really bad
                    dirent name_dirent;
                    dread(name_inode.direct[x].block, (char*) &name_dirent);
                    for (int y = 0; y < 16; j++) {
                    /* for each direntry: */
                        /* check if valid */
                        if (!name_dirent.entries[y].block.valid) {
                            done_flag = 1; // break out of both loops
                            break;
                        }
                        /* free the data block */
                        release_block(name_dirent.entries[y].block.block);
                    }
                    if (done_flag) {
                        /* we've reached the end of data blocks and don't */
                        /* need to keep iterating through inode directs */
                        break;
                    }
                }

                /* free the inode itself */
                release_block(tmp.entries[j].block.block);
                
                /* clear the direntry in the dirent */
                direntry empty;
                memset(&empty, 0, sizeof(empty));
                tmp.entries[j] = empty;

                /* write to disk */
                dwrite(root.direct[i].block, (char*) &tmp);

                /* decrement dnode size */
                root.size--;
                dwrite(1, (char*) &root);


                return 0;
            }
        }
    }

    /* shouldn't get here */
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
 *     .mkdir	 = vfs_mkdir,
 */
static struct fuse_operations vfs_oper = {
    .init    = vfs_mount,
    .destroy = vfs_unmount,
    .getattr = vfs_getattr,
    .readdir = vfs_readdir,
    .create	 = vfs_create,
    .read	 = vfs_read,
    .write	 = vfs_write,
    .unlink	 = vfs_delete,
    .rename	 = vfs_rename,
    .chmod	 = vfs_chmod,
    .chown	 = vfs_chown,
    .utimens	 = vfs_utimens,
    .truncate	 = vfs_truncate,
};

int main(int argc, char *argv[]) {
    /* Do not modify this function */
    umask(0);
    if ((argc < 4) || (strcmp("-s", argv[1])) || (strcmp("-d", argv[2]))) {
      printf("Usage: ./3600fs -s -d <dir>\n");
      exit(-1);
    }
    return fuse_main(argc, argv, &vfs_oper, NULL);
}

