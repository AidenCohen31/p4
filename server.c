#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include "udp.h"
#include "ufs.h"
#include "message.h"

#define BUFFER_SIZE (1000)
int fileSystemImage;
super_t* s;
char* inode_bitmap;
char* data_bitmap;
inode_t* inode_table;
void* image;
int sd;

void intHandler(int dummy) {
    UDP_Close(sd);
    exit(130);
}


int file_read(int inum, char *buffer, int offset, int nbytes){
    //inum is not out of range
    if(inum>=s->num_inodes){
        printf("inum out of range\n");
        return -1;
    }

    //Maybe we consult bitmap??????

    inode_t inode = inode_table[inum];
    if(offset+nbytes>=inode.size){
        printf("Read too big\n");
        return -1;
    }
    
    // One block:
    if(offset%UFS_BLOCK_SIZE+nbytes<=UFS_BLOCK_SIZE){
        char* block = (char*)image+(inode.direct[offset/UFS_BLOCK_SIZE])*UFS_BLOCK_SIZE;
        memcpy((void*)(block+offset%UFS_BLOCK_SIZE),(void*)buffer,nbytes);
    }
    //Two blocks
    else{
        char* block1 = (char*)image+(inode.direct[offset/UFS_BLOCK_SIZE])*UFS_BLOCK_SIZE;
        char* block2 = (char*)image+(inode.direct[offset/UFS_BLOCK_SIZE+1])*UFS_BLOCK_SIZE;
        memcpy((void*)(block1+offset%UFS_BLOCK_SIZE),(void*)buffer,UFS_BLOCK_SIZE-offset%UFS_BLOCK_SIZE);
        memcpy((void*)(block2),(void*)(buffer+UFS_BLOCK_SIZE-offset%UFS_BLOCK_SIZE),nbytes-(UFS_BLOCK_SIZE-offset%UFS_BLOCK_SIZE));
    }

    return 0;
}

int file_write(int inum, char *buffer, int offset, int nbytes){
    return 0;
}


int create(int pinum, int type, char *name){
    return 0;
}

int file_unlink(int pinum, char *name){
    return 0;
}


int lookup(int pinum, char *name){
    //inum is not out of range
    if(pinum>=s->num_inodes){
        printf("inum out of range\n");
        return -1;
    }

    //Maybe we consult bitmap??????

    inode_t inode = inode_table[pinum];
    if(inode.type!=UFS_DIRECTORY) return -1;
    dir_ent_t* directory = (dir_ent_t*)(inode.direct[0]*UFS_BLOCK_SIZE+(char*)image);
    if(strcmp(directory->name,name)==0) return directory->inum;
    return 0;
}

//Returns an int representing type+size of file
int file_stat(int inum){
    //inum is not out of range
    if(inum>=s->num_inodes){
        printf("inum out of range\n");
        return -1;
    }

    inode_t inode = inode_table[inum];
    return inode.size*2+inode.type;
}




// server code
int main(int argc, char *argv[]) {
    signal(SIGINT, intHandler);
    //Setup
    if(argc!=3){
        printf("Invalid number of arguments\n");
        return 1;
    }

    int portnum = atoi(argv[1]);
    sd = UDP_Open(portnum);
    assert(sd > -1);

    printf("File Name: %s\n",argv[2]);
    fileSystemImage = open(argv[2],O_RDWR|O_SYNC);
    if(fileSystemImage==-1){
        printf("image does not exist\n");
        return -1;
    }
    struct stat sbuf;
    int rc = fstat(fileSystemImage,&sbuf);
    assert(rc>-1);
    image = mmap(NULL,(int)sbuf.st_size,PROT_READ|PROT_WRITE,MAP_SHARED,fileSystemImage,0);
    assert(image!=MAP_FAILED);

    //Get superblock
    s = (super_t *) image;
    printf("inode bitmap address %d [len %d]\n", s->inode_bitmap_addr, s->inode_bitmap_len);
    printf("data bitmap address %d [len %d]\n", s->data_bitmap_addr, s->data_bitmap_len);

    //Get inode table
    inode_table = image + (s->inode_region_addr * UFS_BLOCK_SIZE);
    inode_t *root_inode = inode_table;
    printf("\nroot type:%d root size:%d\n", root_inode->type, root_inode->size);
    printf("direct pointers[0]:%d [1]:%d\n", root_inode->direct[0], root_inode->direct[1]);

    //Get inode bitmap
    inode_bitmap = image+(s->inode_bitmap_addr*UFS_BLOCK_SIZE);

    //Get data bitmap
    data_bitmap = image+(s->data_bitmap_addr*UFS_BLOCK_SIZE);
    
    //Main loop
    while (1) {
        struct sockaddr_in addr;
        message_t* message = malloc(sizeof(message_t));
        printf("server:: waiting...\n");
        int rc = UDP_Read(sd, &addr, (char*)message, BUFFER_SIZE);
        printf("server:: read message [size:%d contents:(%s)]\n", rc, message->buffer);
        if (rc > 0) {
            switch(message->mtype){
                case MFS_INIT:
                case MFS_LOOKUP:
                case MFS_STAT:
                case MFS_READ:
                    
                case MFS_CRET:
                case MFS_UNLINK:
                case MFS_SHUTDOWN:
                    exit(0);
            }
            char reply[BUFFER_SIZE];
            sprintf(reply, "goodbye world");
            rc = UDP_Write(sd, &addr, reply, BUFFER_SIZE);
            printf("server:: reply\n");
        } 
    }
    return 0; 
}