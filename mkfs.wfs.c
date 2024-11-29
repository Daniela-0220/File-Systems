#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include "wfs.h"

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <disk_path>\n", argv[0]);
        return 1;
    }

    char *disk_path = argv[1];
    int fd = open(disk_path, O_RDWR);

    if (fd < 0) {
        perror("Error opening disk image file");
        return 1;
    }

    struct stat st;
    stat(disk_path, &st);

    void *disk = mmap(0, st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (disk < (void*) 0){
        perror("");
        return errno;
    }

    // Initialize superblock
    struct wfs_sb *superblock = disk;
    superblock->magic = WFS_MAGIC;
    superblock->head = sizeof(*superblock);

    // // Initialize root directory
    // struct wfs_inode* root_inode = (void *)((char *)disk + superblock->head);
    // root_inode->deleted = 0;
    // root_inode->flags = 0;
    // root_inode->inode_number = 0;
    // root_inode->mode = S_IFDIR | 0755;
    // root_inode->uid = getuid();
    // root_inode->gid = getgid();
    // root_inode->atime = time(NULL);
    // root_inode->mtime = time(NULL);
    // root_inode->ctime = time(NULL);
    // root_inode->size = 0; // Size of a directory is typically 0
    // root_inode->links = 2; // implicit "." and ".." entries

    struct wfs_inode i ={
        .deleted = 0,
        .flags = 0,
        .inode_number = 0,
        .mode = S_IFDIR | 0755,
        .uid = getuid(),
        .gid = getgid(),
        .atime = time(NULL),
        .ctime = time(NULL),
        .mtime = time(NULL),
        .size = 0,
        .links = 2,
    };
    struct wfs_log_entry root = {
        .inode = i,
    };
    memcpy((void *)((char *)disk + superblock->head), &root, sizeof(root));

    superblock->head += sizeof(struct wfs_inode);

    munmap(disk, st.st_size);
    fsync(fd);
    close(fd);

    return 0;
}