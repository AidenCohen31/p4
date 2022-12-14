#include <sys/select.h>
#include <time.h>
#include <stdio.h>
#include "mfs.h"
#include "udp.h"
#include "message.h"
struct sockaddr_in addrSnd,addrRcv;
int sd;
// Takes a host name and port number and uses those to find the server exporting the file system.
int MFS_Init(char *hostname, int port){
    fprintf(stderr,"Init Hostname: %s port: %d\n",hostname,port);
    //WTF
    int MIN_PORT = 19483;
    int MAX_PORT = 48823;

    srand(time(0));
    int port_num = (rand() % (MAX_PORT - MIN_PORT) + MIN_PORT);

    // Bind random client port number
    sd  = UDP_Open(port_num);
    int rc = UDP_FillSockAddr(&addrSnd, hostname, port);
    assert(rc>-1);
    return 0;
}
// Takes the parent inode number (which should be the inode number of a directory) and looks up the entry name in it. 
// The inode number of name is returned. 
// Success: return inode number of name
// Failure: return -1. Failure modes: invalid pinum, name does not exist in pinum.
int MFS_Lookup(int pinum, char *name){
    fprintf(stderr,"Lookup pinum: %d name: %s\n",pinum,name);
    message_t message;
    message.mtype = MFS_LOOKUP;
    message.inum = pinum;
    strcpy(message.name,name);
    int rc = UDP_Write(sd,&addrSnd,(char*)(&message),sizeof(message_t));
    if(rc<0){
        printf("Client failed to send\n");
        return -1;
    }
    printf("client:: wait for reply...\n");
    rc = UDP_Read(sd, &addrRcv, (char *) &message, sizeof(message_t));
    if(message.rc!=0) return -1;
    return message.inum;
}

// Returns some information about the file specified by inum. Upon success, return 0, otherwise -1. 
// The exact info returned is defined by MFS_Stat_t. Failure modes: inum does not exist. File and directory sizes are described below.
int MFS_Stat(int inum, MFS_Stat_t *m){
    fprintf(stderr,"Stat inum: %d\n",inum);
    message_t message;
    message.mtype = MFS_STAT;
    message.inum = inum;
    int rc = UDP_Write(sd,&addrSnd,(char*)(&message),sizeof(message_t));
    if(rc<0){
        printf("Client failed to send\n");
        return -1;
    }
    printf("client:: wait for reply...\n");
    rc = UDP_Read(sd, &addrRcv, (char *) &message, sizeof(message_t));
    if(message.rc!=0) return -1;
    m->type = message.type;
    m->size = message.nbytes;
    fprintf(stderr,"Stat returned type %d size %d\n",m->type,m->size);
    return 0;
}

// Writes a buffer of size nbytes (max size: 4096 bytes) at the byte offset specified by offset. 
// Returns 0 on success, -1 on failure. Failure modes: invalid inum, invalid nbytes, invalid offset, not a regular file (because you can't write to directories).
int MFS_Write(int inum, char *buffer, int offset, int nbytes){
    fprintf(stderr,"Write inum: %d offset: %d bytes: %d\n",inum,offset,nbytes);
    if(nbytes>4096) return -1;
    message_t message;
    message.mtype = MFS_WRITE;
    message.inum = inum;
    message.offset = offset;
    message.nbytes = nbytes;
    memcpy(message.buffer,buffer,nbytes);
    int rc = UDP_Write(sd,&addrSnd,(char*)(&message),sizeof(message_t));
    if(rc<0){
        printf("Client failed to send\n");
        return -1;
    }
    printf("client:: wait for reply...\n");
    rc = UDP_Read(sd, &addrRcv, (char *) &message, sizeof(message_t));
    if(message.rc!=0) return -1;
    return 0;
}

// Reads nbytes of data (max size 4096 bytes) specified by the byte offset offset into the buffer from file specified by inum. 
// The routine should work for either a file or directory; directories should return data in the format specified by MFS_DirEnt_t. 
// Success: 0, failure: -1. Failure modes: invalid inum, invalid offset, invalid nbytes.
int MFS_Read(int inum, char *buffer, int offset, int nbytes){
    fprintf(stderr,"Read inum: %d offset: %d bytes: %d",inum,offset,nbytes);
    message_t message;
    message.mtype = MFS_READ;
    message.inum = inum;
    message.offset = offset;
    message.nbytes = nbytes;
    int rc = UDP_Write(sd,&addrSnd,(char*)(&message),sizeof(message_t));
    if(rc<0){
        printf("Client failed to send\n");
        return -1;
    }
    printf("client:: wait for reply...\n");
    rc = UDP_Read(sd, &addrRcv, (char *) &message, sizeof(message_t));
    if(message.rc!=0) return -1;
    memcpy(buffer,message.buffer,nbytes);
    return 0;
}

// Makes a file (type == MFS_REGULAR_FILE) or directory (type == MFS_DIRECTORY) in the parent directory specified by pinum of name name.
// Returns 0 on success, -1 on failure. Failure modes: pinum does not exist, or name is too long. If name already exists, return success.
int MFS_Creat(int pinum, int type, char *name){
    fprintf(stderr,"Create pinum: %d type: %d name: %s",pinum,type,name);
    if(strlen(name)>=28) return -1;
    message_t message;
    message.mtype = MFS_CRET;
    message.inum = pinum;
    message.type = type;
    strcpy(message.name,name);
    int rc = UDP_Write(sd,&addrSnd,(char*)(&message),sizeof(message_t));
    if(rc<0){
        printf("Client failed to send\n");
        return -1;
    }
    printf("client:: wait for reply...\n");
    rc = UDP_Read(sd, &addrRcv, (char *) &message, sizeof(message_t));
    if(message.rc!=0) return -1;
    return 0;
}

// Removes the file or directory name from the directory specified by pinum. 
// 0 on success, -1 on failure. Failure modes: pinum does not exist, directory is NOT empty. 
// Note that the name not existing is NOT a failure by our definition (think about why this might be).
int MFS_Unlink(int pinum, char *name){
    fprintf(stderr,"Unlink pinum: %d name: %s\n",pinum,name);
    message_t message;
    message.mtype = MFS_UNLINK;
    message.inum = pinum;
    strcpy(message.name,name);
    int rc = UDP_Write(sd,&addrSnd,(char*)(&message),sizeof(message_t));
    if(rc<0){
        printf("Client failed to send\n");
        return -1;
    }
    fprintf(stderr,"client:: wait for reply...\n");
    rc = UDP_Read(sd, &addrRcv, (char *) &message, sizeof(message_t));
    fprintf(stderr,"Unlink return code %d %d\n",rc, message.rc);
    if(message.rc!=0) return -1;
    return 0;
}

// Just tells the server to force all of its data structures to disk and shutdown by calling exit(0). 
// This interface will mostly be used for testing purposes.
int MFS_Shutdown(){
    message_t message;
    message.mtype = MFS_SHUTDOWN;
    int rc = UDP_Write(sd,&addrSnd,(char*)(&message),sizeof(message_t));
    if(rc<0){
        printf("Client failed to send\n");
        return -1;
    }
    return 0;
}