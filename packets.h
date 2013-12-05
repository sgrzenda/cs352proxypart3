#ifndef _PACKETS_H
#define _PACKETS_H

#include <stdint.h>

#define PKT_DATA 0xABCD
#define PKT_LEAVE 0xAB01
#define PKT_QUIT 0xAB12
#define PKT_LINKSTATE 0xABAC
#define PKT_PROBEREQ 0xAB34
#define PKT_PROBERES 0xAB35

#define MAX_DATA_LEN 2048
#define MAX_PKT_LEN 2052
#define HEADER_LEN 4

typedef struct pkt_header {
    uint16_t type;
    uint16_t length;
} pkt_header;

typedef struct proxy_info {
    uint32_t ip;
    uint16_t portno;
    uint8_t macaddr[6];
} proxy_info;

typedef struct linkstate_node {
    proxy_info local;
    proxy_info remote;
    uint32_t linkweight;
    uint64_t ID;
    struct linkstate_node* next; // Used for membership list
} linkstate_node;

typedef struct data_pkt {
    pkt_header head;
    void* data;
} data_pkt;

typedef struct leave_pkt {
    pkt_header head;
    proxy_info local;
    uint64_t ID;
} leave_pkt;

typedef struct quit_pkt {
    pkt_header head;
    proxy_info local;
    uint64_t ID;
} quit_pkt;

typedef struct linkstate_pkt {
    pkt_header head;
    uint16_t numNeighbors;
    proxy_info source;
    linkstate_node* list;
} linkstate_pkt;

typedef struct probereq_pkt {
    pkt_header head;
    uint64_t ID;
} probereq_pkt;

typedef struct proberes_pkt {
    pkt_header head;
    uint64_t ID;
} proberes_pkt;

typedef struct membership_list {
    linkstate_node * head;
    int size;
}membership_list;

typedef struct tableEntry {
    int sockID;
    linkstate_node * info;
    struct tableEntry * next;
}tableEntry;

typedef struct forwardingTable {
    struct tableEntry * head;
}forwardingTable;

void htonHeader(pkt_header, char*);
pkt_header* ntohHeader(char*);
void htonProxyInfo(proxy_info*, char*);
proxy_info* ntohProxyInfo(char*);
void htonLinkstate(linkstate_pkt*, char*);
linkstate_pkt* ntohLinkstate(char*);
void htonData(data_pkt*, char*);
data_pkt* ntohData(char*);
void htonLeave(leave_pkt*, char*);
leave_pkt* ntohLeave(char*);
void htonQuit(quit_pkt*, char*);
quit_pkt* ntohQuit(char*);
#endif
