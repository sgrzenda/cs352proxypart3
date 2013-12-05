//
//  membership.c
//  Practice
//
//  Created by Steve Grzenda on 11/17/13.
//  Copyright (c) 2013 Steve Grzenda. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <linux/if_tun.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netdb.h>
#include <limits.h>
#include <sys/time.h>

#define MAX_DEV_LINE 256

#define TYP_INIT 0 
#define TYP_SMLE 1 
#define TYP_BIGE 2 

/****************************
 *gettimeid
 *
 *converts Epoch to miliseconds and returns an unsigned long
 ****************************/
unsigned long gettimeid(void) {
    struct timeval tim;
    gettimeofday(&tim, NULL);
    return (tim.tv_sec) * 1000 + (tim.tv_usec)/1000;
}

/****************************
 * allocate_tunnel:
 * open a tun or tap device and returns the file
 * descriptor to read/write back to the caller
 * **************************/
int allocate_tunnel(char *dev, int flags, char *local_mac) {
    int fd, error;
    struct ifreq ifr;
    char *device_name = "/dev/net/tun";
    char buffer[MAX_DEV_LINE];
    
    if ((fd = open(device_name, O_RDWR)) < 0) {
    	fprintf(stderr,"error opening /dev/net/tun\n%d:%s\n",errno,strerror(errno));
        return fd;
    }
    
    memset(&ifr, 0, sizeof(ifr));
    
    ifr.ifr_flags = flags;
    
    if (*dev) {
        strncpy(ifr.ifr_name, dev, IFNAMSIZ);
    }
    
    if ( (error = ioctl(fd, TUNSETIFF, (void *)&ifr)) < 0) {
        perror("ioctl on tap failed");
        close(fd);
        return error;
    }
    
    strcpy(dev, ifr.ifr_name);
    
    // Get device MAC address //
    
    sprintf(buffer,"/sys/class/net/%s/address",dev);
    FILE* f = fopen(buffer,"r");
    fread(buffer,1,MAX_DEV_LINE,f);
    sscanf(buffer,"%hhX:%hhX:%hhX:%hhX:%hhX:%hhX",local_mac,local_mac+1,local_mac+2,local_mac+3,local_mac+4,local_mac+5);
    fclose(f);
    return fd;
}
 
unsigned long long htonll(unsigned long long src) { 
  static int typ = TYP_INIT; 
  unsigned char c; 
  union { 
    unsigned long long ull; 
    unsigned char c[8]; 
  } x; 
  if (typ == TYP_INIT) { 
    x.ull = 0x01; 
    typ = (x.c[7] == 0x01ULL) ? TYP_BIGE : TYP_SMLE; 
  } 
  if (typ == TYP_BIGE) 
    return src; 
  x.ull = src; 
  c = x.c[0]; x.c[0] = x.c[7]; x.c[7] = c; 
  c = x.c[1]; x.c[1] = x.c[6]; x.c[6] = c; 
  c = x.c[2]; x.c[2] = x.c[5]; x.c[5] = c; 
  c = x.c[3]; x.c[3] = x.c[4]; x.c[4] = c; 
  return x.ull; 
}

int comp_mac_addrs(uint8_t* mac1, uint8_t* mac2) {
    int i = 0;
    for (i = 0; i < 6; i++) {
        if (mac1[i] != mac2[i]) {
            return -1;
        }
    }
    return 0;
}

int comp_mac_zero(uint8_t* mac1) {
    int i = 0;
    for (i = 0; i < 6; i++) {
        if (mac1[i] != 0) {
            return 0;
        }
    }
    return -1;
}
 
