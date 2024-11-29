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

struct wfs_private wp;

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <disk_path>\n", argv[0]);
        return 1;
    }
    char *disk_path = argv[1];

    struct wfs_log_entry *log_start = (struct wfs_log_entry *)((char *)wp.disk + sizeof(struct wfs_sb));
    struct wfs_log_entry *log_stop = (struct wfs_log_entry *)((char *)wp.disk + wp.sb.head);
    struct wfs_log_entry *lastest = NULL;

    while (log_start < log_stop) {
        if (log_start->inode.deleted == 1) {
            
        }
        log_start = (struct wfs_log_entry*)((char*)log_start + sizeof(struct wfs_inode) + log_start->inode.size);
    }
}
