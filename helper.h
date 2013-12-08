//
//  membership.h
//  Practice
//
//  Created by Steve Grzenda on 11/17/13.
//  Copyright (c) 2013 Steve Grzenda. All rights reserved.
//

#ifndef Practice_membership_h
#define Practice_membership_h

#include <stdint.h>

unsigned long gettimeid(void);

int allocate_tunnel(char*,int, char*);
unsigned long long htonll(unsigned long long);
void delete_members(linkstate_node*);
int compare_pinfo(proxy_info*, proxy_info*);
linkstate_node* in_member_list(proxy_info* local, proxy_info* remote);
int connecttohost(int port, char* hostname);
int connecttohost32(uint16_t port, uint32_t ip_addr);
void add_member_connect(linkstate_node* pkt, int gateway_sockID);
int comp_mac_addrs(uint8_t* mac1, uint8_t* mac2);
int comp_mac_zero(uint8_t* mac1);

#endif
