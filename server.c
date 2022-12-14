#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <unistd.h>
#include "udp.h"
#include "ufs.h"
#include "message.h"

#define BUFFER_SIZE (5000)
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
unsigned int get_bit(unsigned int *bitmap, int position) {
    int index = position / 32;
    int offset = 31 - (position % 32);
    return (bitmap[index] >> offset) & 0x1;
}

void set_bit(unsigned int *bitmap, int position) {
    int index = position / 32;
    int offset = 31 - (position % 32);
    bitmap[index] |= 0x1 << offset;
}

void clear_bit(unsigned int *bitmap, int position){
    int index = position / 32;
    int offset = 31 - (position % 32);
    bitmap[index] &= ~(0x1 << offset);
}
inode_t* get_inode(int inum){
    //inode is out of range
    if(inum>=s->num_inodes){
        fprintf(stderr,"inum out of range\n");
        return 0;
    }
    //Maybe we consult bitmap??????
    if(get_bit((unsigned int*)inode_bitmap,inum)==0) return 0;
    return &(inode_table[inum]);
}

char* get_pointer(inode_t* inode, int offset){
    return (char*)image+(inode->direct[offset/UFS_BLOCK_SIZE])*UFS_BLOCK_SIZE+offset%UFS_BLOCK_SIZE;
}

//Finds the first free byte in bitmap and sets it to used
int find_free(char* bitmap, int length){
    for(int i = 0; i<length; i++){
        if(get_bit((unsigned int*) bitmap, i)==0){
            set_bit((unsigned int*) bitmap, i);
            return i;
        }
    }
    return -1;
}

int file_read(int inum, char *buffer, int offset, int nbytes){
    inode_t* inode = get_inode(inum);
    if(inode==0) return -1;
    if(offset+nbytes>inode->size){
        fprintf(stderr,"Read too big\n");
        return -1;
    }
    
    // One block:
    if(offset%UFS_BLOCK_SIZE+nbytes<=UFS_BLOCK_SIZE){
        char* block = (char*)image+(inode->direct[offset/UFS_BLOCK_SIZE])*UFS_BLOCK_SIZE;
        memcpy((void*)buffer,(void*)(block+offset%UFS_BLOCK_SIZE),nbytes);
    }
    //Two blocks
    else{
        char* block1 = (char*)image+(inode->direct[offset/UFS_BLOCK_SIZE])*UFS_BLOCK_SIZE;
        char* block2 = (char*)image+(inode->direct[offset/UFS_BLOCK_SIZE+1])*UFS_BLOCK_SIZE;
        memcpy((void*)buffer,(void*)(block1+offset%UFS_BLOCK_SIZE),UFS_BLOCK_SIZE-offset%UFS_BLOCK_SIZE);
        memcpy((void*)(buffer+UFS_BLOCK_SIZE-offset%UFS_BLOCK_SIZE),(void*)(block2),nbytes-(UFS_BLOCK_SIZE-offset%UFS_BLOCK_SIZE));
    }

    return 0;
}


int file_write(int inum, char *buffer, int offset, int nbytes){
    inode_t* inode = get_inode(inum);
    if(inode==0) return -1;
    if(inode->type == UFS_DIRECTORY) return -1;
    if(nbytes>4096) return -1;
    if(offset+nbytes>DIRECT_PTRS*UFS_BLOCK_SIZE) return -1;
    //No need to allocate new block
    if((offset+nbytes)/UFS_BLOCK_SIZE==inode->size/UFS_BLOCK_SIZE){
        
        inode->size = MAX(inode->size,offset+nbytes);
    }
    //Allocate new block
    else{
        int data_block = find_free(data_bitmap,s->data_region_len);
        if(data_block<0) return -1;
        inode->direct[inode->size/UFS_BLOCK_SIZE+1] = data_block+s->data_region_addr;
        inode->size = offset+nbytes;
    }
    // One block:
        if(offset%UFS_BLOCK_SIZE+nbytes<=UFS_BLOCK_SIZE){
            char* block = (char*)image+(inode->direct[offset/UFS_BLOCK_SIZE])*UFS_BLOCK_SIZE;
            memcpy((void*)(block+offset%UFS_BLOCK_SIZE),(void*)buffer,nbytes);
        }
        //Two blocks
        else{
            char* block1 = (char*)image+(inode->direct[offset/UFS_BLOCK_SIZE])*UFS_BLOCK_SIZE;
            char* block2 = (char*)image+(inode->direct[offset/UFS_BLOCK_SIZE+1])*UFS_BLOCK_SIZE;
            memcpy((void*)(block1+offset%UFS_BLOCK_SIZE),(void*)buffer,UFS_BLOCK_SIZE-offset%UFS_BLOCK_SIZE);
            memcpy((void*)(block2),(void*)(buffer+UFS_BLOCK_SIZE-offset%UFS_BLOCK_SIZE),nbytes-(UFS_BLOCK_SIZE-offset%UFS_BLOCK_SIZE));
        }
    fprintf(stderr,"Wrote %d bytes at %d offset, new file size is now %d\n",nbytes,offset,inode->size);
    return 0;
}

int create(int pinum, int type, char *name){
    inode_t* parent_inode = get_inode(pinum);
    if(parent_inode==0) return -1;
    if(parent_inode->type!=UFS_DIRECTORY) return -1;
    if(strlen(name)>=28) return -1;

    //Check for same name
    for(int i = 0; i<parent_inode->size/sizeof(dir_ent_t); i++){
        dir_ent_t* directory_entry = (dir_ent_t*) get_pointer(parent_inode,i*sizeof(dir_ent_t));
        if(directory_entry->inum!=-1 && strcmp(directory_entry->name,name)==0){
            return 0;
        }
    }
    int flag = 0;
    //Find an open slot in the parent directory
    for(int i = 0; i<parent_inode->size/sizeof(dir_ent_t); i++){
        dir_ent_t* directory_entry = (dir_ent_t*) get_pointer(parent_inode,i*sizeof(dir_ent_t));
        if(directory_entry->inum==-1){
            strcpy(directory_entry->name,name);
            int index = find_free(inode_bitmap,s->num_inodes);
            if(index<0) return -1;
            directory_entry->inum = index;
            inode_t* inode = &inode_table[index];
            int data_block = find_free(data_bitmap,s->data_region_len);
            if(data_block<0) return -1;
            inode->direct[0] = data_block+s->data_region_addr;
            inode->size = 0;
            inode->type = type;
            if(inode->type==UFS_DIRECTORY){
                dir_ent_t* self = (dir_ent_t*)get_pointer(inode,0);
                sprintf(self->name,".");
                self-> inum = index;
                dir_ent_t* parent = (dir_ent_t*)get_pointer(inode, sizeof(dir_ent_t));
                sprintf(parent->name,"..");
                parent -> inum = pinum;
                inode->size = 2*sizeof(dir_ent_t);
            }
            flag = 1;
        }
    }

    //Didn't find empty slot within, so add onto the end
    if(flag!=1){   
        //If parent directory is full allocate a new block for it
        if(parent_inode->size%UFS_BLOCK_SIZE==0){
            int data_block = find_free(data_bitmap,s->data_region_len);
            if(data_block<0) return -1;
            parent_inode->direct[parent_inode->size/UFS_BLOCK_SIZE] = data_block+s->data_region_addr;
        }
        parent_inode->size +=sizeof(dir_ent_t);
        //Get last directory entry
        dir_ent_t* directory_entry = (dir_ent_t*) get_pointer(parent_inode,parent_inode->size-sizeof(dir_ent_t));
        strcpy(directory_entry->name,name);
        int index = find_free(inode_bitmap,s->num_inodes);
        if(index<0) return -1;
        directory_entry->inum = index;
        inode_t* inode = &inode_table[index];
        int data_block = find_free(data_bitmap,s->data_region_len);
        if(data_block<0) return -1;
        inode->direct[0] = data_block+s->data_region_addr;
        inode->size = 0;
        inode->type = type;
        if(inode->type == UFS_DIRECTORY){
            dir_ent_t* self = (dir_ent_t*)get_pointer(inode,0);
            sprintf(self->name,".");
            self-> inum = index;
            dir_ent_t* parent = (dir_ent_t*)get_pointer(inode, sizeof(dir_ent_t));
            sprintf(parent->name,"..");
            parent -> inum = pinum;
            inode->size = 2*sizeof(dir_ent_t);
        }
    }


    

    return 0;
}


int file_unlink(int pinum, char *name){
    inode_t* parent_inode = get_inode(pinum);
    if(parent_inode==0) return -1;
    if(parent_inode->type!=UFS_DIRECTORY) return -1;
    for(int i = 0; i<parent_inode->size/sizeof(dir_ent_t); i++){
        dir_ent_t* directory_entry = (dir_ent_t*) get_pointer(parent_inode,i*sizeof(dir_ent_t));
        fprintf(stderr,"Directory Entry %d Name: %s\n",i,directory_entry->name);
        if(directory_entry->inum!=-1&&strcmp(directory_entry->name,name)==0){
            //Check directory is not empty
            fprintf(stderr,"Found File\n");
            inode_t* inode = get_inode(directory_entry->inum);
            if(inode->type==UFS_DIRECTORY){
                for(int j = 2; j<inode->size/sizeof(dir_ent_t); j++){ 
                    dir_ent_t* entry = (dir_ent_t*) get_pointer(inode,j*sizeof(dir_ent_t));
                    if(entry->inum!=-1) return -1;
                }
            }

            //Unlink it
            directory_entry->inum = -1;
            for(int j = 0; j<=inode->size/UFS_BLOCK_SIZE; j++){
                clear_bit((unsigned int*)data_bitmap,inode->direct[j]-s->data_region_addr);
            }
            clear_bit((unsigned int*)inode_bitmap,directory_entry->inum);
            fprintf(stderr,"Successfully unlinked\n");
            return 0;
        }
    }
    return 0;
}


int lookup(int pinum, char *name){
    inode_t* parent_inode = get_inode(pinum);
    if(parent_inode==0 || parent_inode->type!=UFS_DIRECTORY) return -1;
    for(int i = 0; i<parent_inode->size/sizeof(dir_ent_t); i++){
        dir_ent_t* directory_entry = (dir_ent_t*) get_pointer(parent_inode,i*sizeof(dir_ent_t));
        //fprintf(stderr,"Directory Entry %d Name: %s\n",i,directory_entry->name);
        if(directory_entry->inum!=-1&&strcmp(directory_entry->name,name)==0){
            return directory_entry->inum;
        }
    }
    return -1;
}

//Returns an int representing type+size of file
int file_stat(int inum){
    //inum is not out of range
    inode_t* inode = get_inode(inum);
    if(inode==0) return -1;
    return inode->size*2+inode->type;
}

// server code
int main(int argc, char *argv[]) {
    signal(SIGINT, intHandler);
    //Setup
    if(argc!=3){
        fprintf(stderr,"Invalid number of arguments\n");
        return 1;
    }

    int portnum = atoi(argv[1]);
    sd = UDP_Open(portnum);
    assert(sd > -1);

    fileSystemImage = open(argv[2],O_RDWR|O_SYNC);
    //fileSystemImage = open("test.img",O_RDWR|O_SYNC);
    
    if(fileSystemImage==-1){
        fprintf(stderr,"image does not exist\n");
        return -1;
    }
    struct stat sbuf;
    int rc = fstat(fileSystemImage,&sbuf);
    assert(rc>-1);
    image = mmap(NULL,(int)sbuf.st_size,PROT_READ|PROT_WRITE,MAP_SHARED,fileSystemImage,0);
    assert(image!=MAP_FAILED);

    //Get superblock
    s = (super_t *) image;

    //Get inode table
    inode_table = image + (s->inode_region_addr * UFS_BLOCK_SIZE);

    //Get inode bitmap
    inode_bitmap = image+(s->inode_bitmap_addr*UFS_BLOCK_SIZE);

    //Get data bitmap
    data_bitmap = image+(s->data_bitmap_addr*UFS_BLOCK_SIZE);
    
    //Main loop
    while (1) {
        struct sockaddr_in addr;
        int result;
        message_t* message = malloc(sizeof(message_t));
        //fprintf(stderr,"server:: waiting...\n");
        int rc = UDP_Read(sd, &addr, (char*)message, BUFFER_SIZE);
        //fprintf(stderr,"server:: read message [size:%d contents:(%s)]\n", rc, message->buffer);
        if (rc > 0) {
            switch(message->mtype){
                case MFS_INIT:
                    break;
                case MFS_LOOKUP:
                    result = lookup(message->inum,message->name);
                    message->inum = result;
                    rc = UDP_Write(sd, &addr, (char*)message, BUFFER_SIZE);
                    fprintf(stderr,"Server: Lookup Reply\n");
                    break;
                case MFS_STAT:
                    result = file_stat(message->inum);
                    if(result==-1) message-> rc = -1;
                    else{
                        message->type = result&1;
                        message->nbytes = result/2;
                    }
                    rc = UDP_Write(sd, &addr, (char*)message, BUFFER_SIZE);
                    fprintf(stderr,"Server: Stat Reply %d %d\n",message->type,message->nbytes);
                    break;
                case MFS_WRITE:
                    result = file_write(message->inum, message->buffer,message->offset,message->nbytes);
                    message->rc = result;
                    rc = UDP_Write(sd, &addr, (char*)message, BUFFER_SIZE);
                    fprintf(stderr,"Server: Write Reply\n");
                    break;
                case MFS_READ:
                    result = file_read(message->inum,message->buffer,message->offset,message->nbytes);
                    message->rc = result;
                    rc = UDP_Write(sd, &addr, (char*)message, BUFFER_SIZE);
                    fprintf(stderr,"Server: Read reply\n");
                    break;
                case MFS_CRET:
                    result = create(message->inum,message->type,message->name);
                    message->rc = result;
                    rc = UDP_Write(sd,&addr,(char*)message,BUFFER_SIZE);
                    fprintf(stderr,"Server: Create Reply\n");
                    break;
                case MFS_UNLINK:
                    result = file_unlink(message->inum,message->name);
                    message->rc = result;
                    rc = UDP_Write(sd,&addr,(char*)message,BUFFER_SIZE);
                    fprintf(stderr,"Server: Unlink Reply\n");
                    break;
                case MFS_SHUTDOWN:
                    exit(0);
                    break;
            }
        } 
        free(message);
    }
    return 0; 
}