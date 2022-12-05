#include <stdio.h>
#include "udp.h"
#include "ufs.h"

#define BUFFER_SIZE (1000)
int fileSystemImage;
super_t* superBlock;
char* inodeBitmap;
char* dataBitmap;
inode_t* inodes;

// server code
int main(int argc, char *argv[]) {

    //Setup
    if(argc!=3){
        printf("Invalid number of arguments\n");
        return 1;
    }

    int portnum = atoi(argv[1]);
    int sd = UDP_Open(portnum);
    assert(sd > -1);

    fileSystemImage = open(argv[2],O_SYNC);
    if(fileSystemImage==-1){
        printf("image does not exist\n");
        return 1;
    }

    //Read in super block
    superBlock = malloc(sizeof(super_t));
    assert(read(fileSystemImage,(void *) superBlock, sizeof(super_t))==sizeof(super_t));

    //Read in inode bitmap
    inodeBitmap = malloc(UFS_BLOCK_SIZE*superBlock->inode_bitmap_len);
    if(lseek(fileSystemImage,UFS_BLOCK_SIZE*superBlock->inode_bitmap_addr,SEEK_SET)<0){
        printf("inode bitmap read failed\n");
        return -1;
    }
    assert(read(fileSystemImage,(void*) inodeBitmap,UFS_BLOCK_SIZE*superBlock->inode_bitmap_len)==UFS_BLOCK_SIZE*superBlock->inode_bitmap_len);

    //Read in data bitmap
    dataBitmap = malloc(UFS_BLOCK_SIZE*superBlock->data_bitmap_len);
    if(lseek(fileSystemImage,UFS_BLOCK_SIZE*superBlock->data_bitmap_addr,SEEK_SET)<0){
        printf("data bitmap read failed\n");
        return -1;
    }
    assert(read(fileSystemImage,(void*) dataBitmap,UFS_BLOCK_SIZE*superBlock->data_bitmap_len)==UFS_BLOCK_SIZE*superBlock->data_bitmap_len);

    //Read in inodes
    inodes = malloc(UFS_BLOCK_SIZE*superBlock->inode_region_len);
    if(lseek(fileSystemImage,UFS_BLOCK_SIZE*superBlock->inode_region_addr,SEEK_SET)<0){
        printf("inodes read failed\n");
        return -1;
    }
    assert(read(fileSystemImage,(void*) inodes,UFS_BLOCK_SIZE*superBlock->inode_region_len)==UFS_BLOCK_SIZE*superBlock->inode_region_len);

    //Main loop
    while (1) {
        struct sockaddr_in addr;
        char message[BUFFER_SIZE];
        printf("server:: waiting...\n");
        int rc = UDP_Read(sd, &addr, message, BUFFER_SIZE);
        printf("server:: read message [size:%d contents:(%s)]\n", rc, message);
        if (rc > 0) {
                char reply[BUFFER_SIZE];
                sprintf(reply, "goodbye world");
                rc = UDP_Write(sd, &addr, reply, BUFFER_SIZE);
            printf("server:: reply\n");
        } 
    }
    return 0; 
}


int file_read(int inum, char *buffer, int offset, int nbytes){
    //inum is not out of range
    if(inum>=superBlock->inode_bitmap_len*UFS_BLOCK_SIZE*8){
        printf("inum out of range\n");
        return -1;
    }

    inode_t inode = inodes[inum];
    if(offset+nbytes>=inode.size){
        printf("Read too big\n");
        return -1;
    }
    
    return 0;
}

int file_write(int inum, char *buffer, int offset, int nbytes){
    return 0;
}


int create(int pinum, int type, char *name){
    return 0;
}

int unlink(int pinum, char *name){
    return 0;
}


int lookup(int pinum, char *name){
    return 0;
}

//Returns an int representing type+size of file
int stat(int inum){
    return 0;
}