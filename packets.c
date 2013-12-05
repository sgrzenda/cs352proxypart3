#include <stdlib.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>
#include "packets.h"
#include "helper.h"


/*****************************
*
*These following functions perform the 
*serialization/deserialization of a packet
*starts with hton = serialize
*starts with ntoh = deserialize
*
******************************/


void htonHeader(pkt_header head, char* buffer) {
    uint16_t type, len;
    type = htons(head.type);
    memcpy(buffer+0,&type,2);
    len = htons(head.length);
    memcpy(buffer+2,&len,2);
}

// Note that these all malloc, so my free after called
pkt_header* ntohHeader(char* buffer) {
    pkt_header* head = malloc(sizeof(pkt_header));
    uint16_t type = ntohs(*(uint16_t*)buffer);
    uint16_t length = ntohs(*(uint16_t*)(buffer+2));
    head->type = type;
    head->length = length;
    return head;
}

void htonProxyInfo(proxy_info* proxy, char* buffer) {
    uint32_t ip = htonl(proxy->ip);
    memcpy(buffer,&ip,4);
    uint16_t portno = htons(proxy->portno);
    memcpy(buffer+4,&portno,2);
    memcpy(buffer+6,&proxy->macaddr,6);
}

proxy_info* ntohProxyInfo(char* buffer) {
    proxy_info* proxy = malloc(sizeof(proxy_info));
    uint32_t ip = ntohl(*(uint32_t*)buffer);
    uint16_t portno = ntohs(*(uint16_t*)(buffer+4));
    char* macaddr = buffer+6;
    proxy->ip = ip;
    proxy->portno = portno;
    int j;
    for (j = 0; j < 6; j++) {
	   proxy->macaddr[j] = macaddr[j];
    }
    return proxy;
}

void htonLinkstate(linkstate_pkt* pkt, char *buffer) { // can be variable length
    htonHeader(pkt->head, buffer);
    uint16_t numNeighbors;
    numNeighbors = htons(pkt->numNeighbors);
    memcpy(buffer+4,&numNeighbors,2);
    htonProxyInfo(&pkt->source,buffer+6);
    linkstate_node* list = pkt->list;
    int i = 0;
    while (list != NULL) {
        htonProxyInfo(&list->local,buffer+18+36*i);
        htonProxyInfo(&list->remote,buffer+30+36*i);
        uint32_t linkweight;
        linkweight = htonl(list->linkweight);
        memcpy(buffer+42+36*i,&linkweight,4);
        uint64_t ID;
        ID = htonll(list->ID);
        memcpy(buffer+46+36*i,&ID,8);
        list = list->next;
        i++;
    }
}

linkstate_pkt* ntohLinkstate(char *buffer) {
    // Length is the number of bytes in the packets minus the header
    uint16_t length = ntohs(*(uint16_t*)(buffer+2));
    uint16_t numNodes = (length-14)/36; // Test this!!!
    linkstate_pkt* pkt = malloc(length+4+8*numNodes);
    pkt_header* head = ntohHeader(buffer);
    pkt->head = *head;
    pkt->numNeighbors = ntohs(*(uint16_t*)(buffer+4));
    proxy_info* source = ntohProxyInfo(buffer+6);
    pkt->source = *source;
    int i;
    linkstate_node* lsptr = NULL;
    printf("numNodes: %d\n", numNodes);
    for (i = 0; i < numNodes; i++) {
        linkstate_node* curr = malloc(sizeof(linkstate_node));
        curr->local = *ntohProxyInfo(buffer+18+36*i);
        curr->remote = *ntohProxyInfo(buffer+30+36*i);
        curr->linkweight = ntohl(*(uint32_t*)(buffer+42+36*i));
        curr->ID = htonll(*(uint64_t*)(buffer+46+36*i));
        if (i == 0) {
	    curr->next = NULL;
            pkt->list = curr;
            lsptr = curr;
        } else {
            lsptr->next = curr;
            lsptr = lsptr->next;
        }
	// free(curr);
    }
    free(head);
    free(source);
    return pkt;
}

void htonData(data_pkt* pkt, char *buffer) {
    htonHeader(pkt->head, buffer);
    // Need to test this, might just want t read the length of the data.
    memcpy(buffer+4,&pkt->data,MAX_DATA_LEN);
}

data_pkt* ntohData(char *buffer) {
    data_pkt* pkt = malloc(MAX_PKT_LEN);
    pkt_header* head = ntohHeader(buffer);
    pkt->head = *head;
    pkt->data = buffer+4;
    free(head);
    return pkt;
}

void htonLeave(leave_pkt* pkt, char *buffer) {
    htonHeader(pkt->head, buffer);
    htonProxyInfo(&pkt->local,buffer+4);
    uint64_t ID;
    ID = htonll(pkt->ID);
    memcpy(buffer+16,&ID,8);
}

leave_pkt* ntohLeave(char *buffer) {
    leave_pkt* pkt = malloc(sizeof(leave_pkt));
    pkt_header* head = ntohHeader(buffer);
    pkt->head = *head;
    proxy_info* local = ntohProxyInfo(buffer+4);
    pkt->local = *local;
    uint64_t ID;
    // The conversion should work both ways, need to test and change name of function.
    ID = htonll(*(uint64_t*)(buffer+16));
    pkt->ID = ID;
    free(head);
    free(local);
    return pkt;
}

void htonQuit(quit_pkt* pkt, char *buffer) {
    htonHeader(pkt->head, buffer);
    htonProxyInfo(&pkt->local,buffer+4);
    uint64_t ID;
    ID = htonll(pkt->ID);
    memcpy(buffer+16,&ID,8);
}

quit_pkt* ntohQuit(char *buffer) {
    quit_pkt* pkt = malloc(sizeof(quit_pkt));
    pkt_header* head = ntohHeader(buffer);
    pkt->head = *head;
    proxy_info* local = ntohProxyInfo(buffer+4);
    pkt->local = *local;
    uint64_t ID;
    // The conversion should work both ways, need to test and change name of function.
    ID = htonll(*(uint64_t*)(buffer+16));
    pkt->ID = ID;
    free(head);
    free(local);
    return pkt;
}

