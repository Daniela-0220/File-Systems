#define FUSE_USE_VERSION 30
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "wfs.h"
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <libgen.h>
#include <assert.h>

struct wfs_private wp;
size_t inode_number = 100;

struct wfs_inode *find_inode_by_number(int number) {
    printf("number is: %d\n", number);
    struct wfs_log_entry *log_start = (struct wfs_log_entry *)((char *)wp.disk + sizeof(struct wfs_sb));
    struct wfs_log_entry *log_stop = (struct wfs_log_entry *)((char *)wp.disk + wp.sb.head);
    struct wfs_log_entry *lastest = NULL;

    while (log_start < log_stop) {
        if (log_start->inode.inode_number == number) {
            lastest = log_start;
        }
        log_start = (struct wfs_log_entry*)((char*)log_start + sizeof(struct wfs_inode) + log_start->inode.size);
    }
    if(lastest->inode.deleted){
        return NULL;
    }
    return (void*)lastest;
}


// Helper function to find the inode of a given file by its path
struct wfs_inode *find_inode_by_path(const char *path) {
    printf("start find node by path\n");

    // size_t ino = 0;
    struct wfs_log_entry* root = (void*)find_inode_by_number(0);

    // copy path to new_path
    char new_path[128];
    strncpy(new_path, path, sizeof(new_path)); 
    new_path[sizeof(new_path) - 1] = '\0'; 
    printf("new_path is: %s\n", new_path);

    char *token = strtok(new_path, "/");
    printf("ready to enter while\n");
    while (token != NULL) {
        size_t j = -1;
        printf("token is: %s\n", token);
        struct wfs_dentry* dentry = (void *)root->data;
        printf("have dentry, get inode number: %ld\n", j);
        size_t dentries = root->inode.size / sizeof(struct wfs_dentry);
        printf("size is: %d\n", root->inode.size);
        printf("dentries number is: %ld\n", dentries);

        for (size_t i = 0; i < dentries; i++) {
            printf("(dentry+i)->name is: %s\n", (dentry+i)->name);
            if (strcmp((dentry+i)->name, token) == 0) {
                j = (dentry+i)->inode_number;
                break;
            }
        }
        printf("ino with correct name is: %ld\n", j);
        if (j == -1) return NULL;
        root = (void*)find_inode_by_number(j);
        printf("new root's inode number is: %d\n", root->inode.inode_number);
        token = strtok(NULL, "/");   
    }
    if (root != NULL) {
        printf("root's inode mode %d\n", root->inode.mode);
    }
    printf("return find inode by path\n");
    return (struct wfs_inode*)root;   
}

static int wfs_getattr(const char *path, struct stat *stbuf) {
    // Implementation of getattr function to retrieve file attributes
    // Fill stbuf structure with the attributes of the file/directory indicated by path
    printf("wfs_getattr (before find_inode_by_path)\n");
    
    struct wfs_inode *i = find_inode_by_path(path);
    printf("wfs_getattr (after find_inode_by_path)\n");
    if (i == NULL) {
        return -ENOENT;
    }
    printf("get att 85 mode: %d\n", i->mode);
    memset(stbuf, 0, sizeof(*stbuf));
    stbuf->st_mode = i->mode;
    stbuf->st_size = i->size;
    stbuf->st_uid = i->uid;
    stbuf->st_gid = i->gid;
    stbuf->st_atime = i->atime;
    stbuf->st_ctime = i->ctime;
    stbuf->st_mtime = i->mtime;
    stbuf->st_nlink = i->links;
    stbuf->st_ino = i->inode_number;
    printf("getatt | stbuf->st_mode: %d\n", stbuf->st_mode);
    printf("wfs_getattr return\n");
    return 0; 
}


static int wfs_mknod(const char *path, mode_t mode, dev_t rdev) {

    // Check if the file already exists
    printf("wfs_mknod 99 (before find_inode_by_path)\n");
    struct wfs_inode *existing_inode = find_inode_by_path(path);
    if (existing_inode != NULL && !existing_inode->deleted) {
        return -EEXIST;
    }

    printf("100\n");
    printf("mknod given path: %s\n", path);
    // Extract parent directory path and new file name from path
    // Duplicate the path to avoid modifying the original
    char *path_dup = strdup(path);
    if (path_dup == NULL) {
        return -ENOMEM;
    }

    // Duplicate again for dirname(), as it may modify the string
    char *path_dup2 = strdup(path);
    if (path_dup2 == NULL) {
        free(path_dup); // Free the first duplicate before returning
        return -ENOMEM;
    }

    char *dir_path = dirname(path_dup2);
    char *file_name = basename(path_dup);

    printf("mkdir dir_path: %s\n", dir_path);
    printf("mkdir dir_name: %s\n", file_name);

    // Find the inode of the parent directory
    printf("wfs_mknod 124 (before find_inode_by_path)\n");
    struct wfs_inode *parent_inode = find_inode_by_path(dir_path);
    if (parent_inode == NULL) {
        free(path_dup);
        return -ENOENT;
    }

    printf("parent inode's inode number: %d\n", parent_inode->inode_number);

    // Check that the parent is indeed a directory
    if (!S_ISDIR(parent_inode->mode)) {
        free(path_dup);
        return -ENOTDIR;
    }

    printf("130\n");
    // access original parent entry
    // want to update: create a new parent entry
    struct wfs_log_entry *new_parent_log_entry = (struct wfs_log_entry *)((char *)wp.disk + wp.sb.head);
    memcpy(&new_parent_log_entry->inode, parent_inode, sizeof(&parent_inode)); 
    struct wfs_log_entry *l = (void *)parent_inode;
    memcpy(&new_parent_log_entry->data, l->data, parent_inode->size); 

    // delete the old parent log entry
    parent_inode->deleted = 1;
    // edit time of new parent log entry
    new_parent_log_entry->inode.atime = time(NULL);
    new_parent_log_entry->inode.mtime = time(NULL);

    wp.sb.head += sizeof(struct wfs_log_entry) + new_parent_log_entry->inode.size;

    // create a new dentry, add new indo info
    struct wfs_dentry* added_entry_to_parent = (struct wfs_dentry *)((char *)wp.disk + wp.sb.head);
    added_entry_to_parent->inode_number = inode_number;
    inode_number += 1;
    new_parent_log_entry->inode.size += sizeof(struct wfs_dentry);
    printf("new inode number is: %ld\n", added_entry_to_parent->inode_number);
    strncpy(added_entry_to_parent->name, file_name, MAX_FILE_NAME_LEN);
    wp.sb.head += sizeof(struct wfs_dentry);
    
    // Create a new inode for the file
    struct wfs_inode new_inode = {
        .inode_number = added_entry_to_parent->inode_number, 
        .deleted = 0,
        .mode = S_IFREG,
        .uid = getuid(),
        .gid = getgid(),
        .size = 0, 
        .atime = time(NULL),
        .mtime = time(NULL),
        .ctime = time(NULL),
        .links = 1
    };

    // Now create the log entry for the new file
    struct wfs_log_entry *new_file_log_entry = (struct wfs_log_entry *)((char *)wp.disk + wp.sb.head);
    memcpy(&new_file_log_entry->inode, &new_inode, sizeof(new_inode));
    wp.sb.head += sizeof(struct wfs_log_entry);


    // // Ensure changes are written to disk
    // msync(disk_memory, superblock->head, MS_SYNC);

    printf("172\n");

    free(path_dup);
    return 0;
}

static int wfs_mkdir(const char *path, mode_t mode) {
    printf("start mkdir\n");
    // Check if the directory already exists
    // struct wfs_inode *existing_inode = find_inode_by_path(path);
    // // printf("existing inode is: %s")
    // if (existing_inode != NULL || !existing_inode->deleted) {
    //     return -EEXIST;
    // }

    printf("mkdir given path: %s\n", path);

    // Duplicate the path to avoid modifying the original
    char *path_dup = strdup(path);
    if (path_dup == NULL) {
        return -ENOMEM;
    }

    // Duplicate again for dirname(), as it may modify the string
    char *path_dup2 = strdup(path);
    if (path_dup2 == NULL) {
        free(path_dup); // Free the first duplicate before returning
        return -ENOMEM;
    }

    char *dir_path = dirname(path_dup2);
    char *dir_name = basename(path_dup);

    printf("mkdir dir_path: %s\n", dir_path);
    printf("mkdir dir_name: %s\n", dir_name);

    // Find the inode of the parent directory
    printf("wfs_mkdir 244 (before find_inode_by_path)\n");
    struct wfs_inode *parent_inode = find_inode_by_path(dir_path);
    printf("dir_path is: %s\n", dir_path);
    if (parent_inode == NULL) {
        free(path_dup);
        return -ENOENT;
    }

    printf("parent inode's inode number: %d\n", parent_inode->inode_number);

    // Check that the parent is indeed a directory
    if (!S_ISDIR(parent_inode->mode)) {
        free(path_dup);
        return -ENOTDIR;
    }

    printf("parent inode beginning type: %d\n", parent_inode->mode);

    struct wfs_inode new_parent_inode = {
        .inode_number = parent_inode->inode_number, 
        .deleted = 0,
        .mode = __S_IFDIR | mode,
        .uid = getuid(),
        .gid = getgid(),
        .size = parent_inode->size, 
        .atime = time(NULL),
        .mtime = time(NULL),
        .ctime = time(NULL),
        .links = 1
    };

    size_t new_size = sizeof(struct wfs_log_entry)+new_parent_inode.size+sizeof(struct wfs_dentry);
    struct wfs_log_entry *new_parent_log_entry = malloc(new_size);
    memcpy(new_parent_log_entry, &new_parent_inode, sizeof(struct wfs_inode));
    //memcpy(&new_parent_log_entry->inode, &new_parent_inode, sizeof(*parent_inode)); 
    struct wfs_log_entry *l = (void *)parent_inode;
    memcpy(&new_parent_log_entry->data, l->data, parent_inode->size); 

    // delete the old parent log entry
    parent_inode->deleted = 1;
    // edit time of new parent log entry
    new_parent_log_entry->inode.atime = time(NULL);
    new_parent_log_entry->inode.mtime = time(NULL);

    // create a new dentry, add new indo info
    struct wfs_dentry* added_entry_to_parent = (struct wfs_dentry *)((char *)new_parent_log_entry + sizeof(struct wfs_log_entry)+new_parent_inode.size);
    added_entry_to_parent->inode_number = inode_number;
    inode_number += 1;
    new_parent_log_entry->inode.size += sizeof(struct wfs_dentry);
    printf("new inode number is: %ld\n", added_entry_to_parent->inode_number);
    strncpy(added_entry_to_parent->name, dir_name, MAX_FILE_NAME_LEN);

    memcpy((struct wfs_dentry *)((char *)wp.disk + wp.sb.head), new_parent_log_entry, new_size);

    wp.sb.head += new_size;


    // Create a new inode for the file
    struct wfs_inode new_inode = {
        .inode_number = added_entry_to_parent->inode_number, 
        .deleted = 0,
        .mode = S_IFDIR | mode,
        .uid = getuid(),
        .gid = getgid(),
        .size = 0, 
        .atime = time(NULL),
        .mtime = time(NULL),
        .ctime = time(NULL),
        .links = 1
    };

    // Now create the log entry for the new file
    memcpy((struct wfs_log_entry *)((char *)wp.disk + wp.sb.head), &new_inode, sizeof(struct wfs_inode));
    wp.sb.head += sizeof(struct wfs_inode);
  
    printf("curr parent inode mode: %d\n", parent_inode->mode);
    printf("S_IFDIR: %d\n", S_IFDIR);
    printf("S_IFREG: %d\n", S_IFREG);

    free(path_dup);
    free(path_dup2);
    return 0;
}


static int wfs_unlink(const char* path) {
    // Extract parent directory path and new directory name from path
    // Duplicate the path to avoid modifying the original
    char *path_dup = strdup(path);
    if (path_dup == NULL) {
        return -ENOMEM;
    }

    // Duplicate again for dirname(), as it may modify the string
    char *path_dup2 = strdup(path);
    if (path_dup2 == NULL) {
        free(path_dup); // Free the first duplicate before returning
        return -ENOMEM;
    }
    char *dir_path = dirname(path_dup); 
    char *file_name = basename(path_dup);

    printf("wfs_unlink 362 (before find_inode_by_path)\n");
    // Find the inode of the parent directory
    struct wfs_inode *parent_inode = find_inode_by_path(dir_path);
    if (parent_inode == NULL) {
        free(path_dup);
        return -ENOENT;
    }

    // Check that the parent is indeed a directory
    if (!S_ISDIR(parent_inode->mode)) {
        free(path_dup);
        return -ENOTDIR;
    }

    printf("wfs_unlink 376 (before find_inode_by_path)\n");
    // Find the inode of the file
    struct wfs_inode *file_inode = find_inode_by_path(path);
    if (parent_inode == NULL) {
        free(path_dup);
        return -ENOENT;
    }

    // Delete the file
    file_inode->deleted = 1;

    // Create a new inode for the file
    struct wfs_inode new_inode = {
        .inode_number = parent_inode->inode_number, 
        .deleted = 0,
        .mode = S_IFDIR,
        .uid = parent_inode->uid,
        .gid = parent_inode->gid,
        .size = 0, 
        .atime = time(NULL),
        .mtime = time(NULL),
        .ctime = time(NULL),
        .links = 1
    };    
    // Create a new direcotry log entry
    size_t new_size = sizeof(struct wfs_inode) + parent_inode->size - sizeof(struct wfs_dentry);
    struct wfs_log_entry *new = malloc(new_size);
    memcpy(&new->inode, &new_inode, sizeof(struct wfs_inode)); // copy old inode to new inode
    // Delete the old log entry
    parent_inode->deleted = 1;
    // Update head
    wp.sb.head += sizeof(struct wfs_inode);
    // Delete the correspond dentry from the data part
    struct wfs_dentry *start = (struct wfs_dentry*) ((struct wfs_log_entry*)parent_inode)->data;
    struct wfs_dentry *stop = (struct wfs_dentry*) (((struct wfs_log_entry*)parent_inode)->data + parent_inode->size);

    while (start < stop) {
        if (strcmp(start->name, file_name) == 0) {
        }
        else{
            struct wfs_dentry *now = malloc(sizeof(struct wfs_dentry));
            memcpy(now, start->name, MAX_FILE_NAME_LEN);
            now->inode_number = start->inode_number;
            memcpy((struct wfs_dentry *)((char*)wp.disk + wp.sb.head), now, sizeof(struct wfs_dentry));
            wp.sb.head += sizeof(struct wfs_dentry);
            new->inode.size += sizeof(struct wfs_dentry);
        }
        start = (struct wfs_dentry*)((char*)start + sizeof(struct wfs_dentry));
    }

    memcpy((struct wfs_log_entry *)((char*)wp.disk + wp.sb.head), new, new_size);

    return 0;
}


// Return one or more directory entries (struct dirent) to the caller.
static int wfs_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi){
    // Call the filler function with arguments of buf, the null-terminated filename, 
    // the address of your struct stat (or NULL if you have none), and the offset of the next directory entry.
    // filler help you put things into the buffer
    
    // filler(buf, ".", NULL, 0);
    // filler(buf, "..", NULL, 0);

    printf("readdir | path is: %s\n", path);
    printf("wfs_readdir (before find_inode_by_path)\n");
    struct wfs_inode *i = find_inode_by_path(path);
    printf("inode number found with path: %d\n", i->inode_number);
    struct wfs_log_entry *l = (void *) i;
    struct wfs_dentry *d = (void *) l->data;
    //check if dir TODO

      struct stat st1;
    memset(&st1, 0, sizeof(st1));
    st1.st_ino = d -> inode_number;
    //print("")
    st1.st_mode = find_inode_by_number(st1.st_ino) -> mode;
    filler(buf, ".", &st1, 0);

    // Duplicate the path to avoid modifying the original
    char *path_dup = strdup(path);
    if (path_dup == NULL) {
        return -ENOMEM;
    }
    char *dir_path = dirname(path_dup);
    struct wfs_inode *i_parent = find_inode_by_path(dir_path);
    struct wfs_log_entry *l_parent = (void *) i_parent;
    struct wfs_dentry *d_parent = (void *) l_parent->data;

    struct stat st2;
    memset(&st2, 0, sizeof(st2));
    st2.st_ino = d_parent -> inode_number;
    st2.st_mode = find_inode_by_number(st2.st_ino) -> mode;
    filler(buf, "..", &st2, 0);

    size_t entrynum = i->size / sizeof(struct wfs_dentry);
    assert(i->size % sizeof(struct wfs_dentry) == 0);
    for(size_t n=0; n<entrynum; n++) {
        struct stat st;
        memset(&st, 0, sizeof(st));
        st.st_ino = (d+n) -> inode_number;
        st.st_mode = find_inode_by_number(st.st_ino) -> mode;
        filler(buf, (d+n)->name, &st, 0);
    }

    return 0;
}

// Return the number of bytes that have been read successfully
static int wfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {

    // Find the inode associated with the path
    printf("wfs_read 451 (before find_inode_by_path)\n");
    struct wfs_inode *inode = find_inode_by_path(path);

    // File does not exist while trying to read a file
    if (!inode) {
        return -ENOENT; 
    }

    // If the inode is marked as deleted, return an error
    if (inode->deleted) {
        return -ENOENT;
    }

    // If the file is a directory, return an error
    if (S_ISDIR(inode->mode)) {
        return -EISDIR;
    }

    // Calculate the actual size to read, which should less than the file size minus the offset
    size_t actual_size = inode->size - offset;
    if (size > actual_size) {
        size = actual_size;
    }

    // If the offset is beyond the end of the file, nothing should been read, return 0
    if (offset > inode->size) {
        offset = inode->size;
    }

    // Locate the data in the log entry
    //char *data_ptr = (char *)inode + sizeof(struct wfs_inode); // data is directly after the inode
    struct wfs_log_entry *l = (void *) inode;
    char *data_ptr = l->data;
    // Copy the requested data to the buffer
    memcpy(buf, data_ptr + offset, size);

    return size;
}


// As for read above, except that it can't return 0.
static int wfs_write(const char* path, const char *buf, size_t size, off_t offset, struct fuse_file_info* fi) {
    printf("wfs_write 493 (before find_inode_by_path)\n");
    // Find the inode associated with the path
    struct wfs_inode *old_inode = find_inode_by_path(path);

    // Calculate the actual size to read, which should less than the file size minus the offset
    size_t actual_size = old_inode->size - offset;
    if (size > actual_size) {
        size = actual_size;
    }
    printf("size: %ld\n", size);
    // If the offset is beyond the end of the file, only write the full size
    if (offset >= old_inode->size) {
        offset = old_inode->size;
    }
    printf("offset: %ld\n", offset);
    // Create a new inode for the file
    struct wfs_inode new_inode = {
        .inode_number = old_inode->inode_number, 
        .deleted = 0,
        .mode = S_IFREG,
        .uid = old_inode->uid,
        .gid = old_inode->gid,
        .size = size, 
        .atime = time(NULL),
        .mtime = time(NULL),
        .ctime = time(NULL),
        .links = 1
    };
    // Create a new log entry
    struct wfs_log_entry *new = malloc(sizeof(struct wfs_inode) + size);
    printf("malloc!\n");
    memcpy(&new->inode, &new_inode, sizeof(struct wfs_inode)); // copy old inode to new inode
    printf("memcpy inode\n");
    // Delete the old log entry
    old_inode->deleted = 1;
    // copy buffer data to new data
    memcpy(new->data, buf + offset, size);
    printf("memcpy data\n");
    // copy whole entry to specific place
    memcpy((struct wfs_log_entry *)((char *)wp.disk + wp.sb.head), new, sizeof(struct wfs_inode) + size);
    printf("memcpy log entry\n");
    // Increment the head pointer
    wp.sb.head += sizeof(struct wfs_inode) + size; 
    printf("end of write\n");

    return size;
}

static struct fuse_operations my_operations = {
    .getattr = wfs_getattr,
    .read = wfs_read,
    .write = wfs_write,
    .mknod = wfs_mknod,
    .readdir = wfs_readdir,
    .mkdir = wfs_mkdir,
    .unlink = wfs_unlink
};
 

int main(int argc, char *argv[]) {
    
    // Filter argc and argv here and then pass it to fuse_main
    if (argc < 3 || (argv[argc-2][0] == '-') || (argv[argc-1][0] == '-')) {
        fprintf(stderr, "Usage: mount.wfs [FUSE options] disk_path mount_point\n");
        exit(EXIT_FAILURE);
    }

    const char *disk_path = argv[argc-2];

    // Open the disk image file
    int fd = open(disk_path, O_RDWR);
    if (fd == -1) {
        perror("Error opening disk image");
        exit(EXIT_FAILURE);
    }

    wp.fd = fd;

    struct stat st;
    stat(disk_path, &st);

    wp.len = st.st_size;

    wp.disk = mmap(0, st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (wp.disk < (void*) 0){
        perror("");
        return errno;
    }

    wp.sb.magic = ((struct wfs_sb*) wp.disk) -> magic;
    wp.sb.head = ((struct wfs_sb*)wp.disk)->head;

    if(((struct wfs_sb*)wp.disk)->magic != WFS_MAGIC){
        return errno;
    }

    // Remove disk_path from arguments
    argv[argc - 2] = argv[argc - 1];
    argv[argc - 1] = NULL;
    argc--;

    // Initialize FUSE with specified operations
    fuse_main(argc, argv, &my_operations, NULL);
    ((struct wfs_sb*)wp.disk)->head = wp.sb.head;

    fsync(fd);
    close(fd);
    return 0;
}
