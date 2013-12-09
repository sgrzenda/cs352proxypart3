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
#include "utlist.h"
#include "uthash.h"
#include "packets.h"
#include "helper.h"

proxy_info* my_proxy_info;
int tap_fd, linkPeriod, linkTimeout, localPort;
membership_list * mem_list;
void* TCPHandle(void *fd);
forwardingTable * fTable;
pthread_mutex_t deleterlock;

// Struct for the hash table
struct proxy_socketfd {
    char macaddr[7]; // This is used as the key
    int sock_fd;
    UT_hash_handle hh;
};

struct proxy_socketfd *socketfds = NULL;

/**
 * addsockfd is used for our forwarding table to create a hash
 * betweek MAC address and socket file descriptor.
 */
void addsockfd(char *new_macaddr, int fd) {

    //pthread_mutex_lock(&deleterlock);
    struct proxy_socketfd *s;
    
    s = malloc(sizeof(struct proxy_socketfd));
    s->macaddr[0] = new_macaddr[0];
    s->macaddr[1] = new_macaddr[1];
    s->macaddr[2] = new_macaddr[2];
    s->macaddr[3] = new_macaddr[3];
    s->macaddr[4] = new_macaddr[4];
    s->macaddr[5] = new_macaddr[5];
    s->macaddr[6] = '\0';
    s->sock_fd = fd;
    //printf("adding %s\n", s->macaddr);
    HASH_ADD_STR(socketfds,macaddr,s);
    //pthread_mutex_unlock(&deleterlock);
}

/**
 * deletesockfd is used to delete from the forwarding table up receiving
 * a leave packet or a link time out
 */
void deletesockfd(char *macaddr) {
    // pthread_mutex_lock(&deleterlock);
    struct proxy_socketfd *s;

    char *macaddr2 = malloc(7); // Null terminated because key is null terminated
    macaddr2[0] = macaddr[0];
    macaddr2[1] = macaddr[1];
    macaddr2[2] = macaddr[2];
    macaddr2[3] = macaddr[3];
    macaddr2[4] = macaddr[4];
    macaddr2[5] = macaddr[5];
    macaddr2[6] = '\0';

    HASH_FIND_STR(socketfds,macaddr2,s);
    if (s == NULL) {
        free(macaddr2);
        pthread_mutex_unlock(&deleterlock);
        return;
    }
    HASH_DEL(socketfds,s);
    //  pthread_mutex_unlock(&deleterlock);
     
    free(macaddr2);
    free(s);
}



/**
 * find_socketfd is used to look up a socket from a MAC
 * address in the forwarding table
 */
struct proxy_socketfd* find_socketfd(char *macaddr) {
    struct proxy_socketfd* s;

    char* macaddr2 = malloc(7); // Null terminated because key is null terminated
    macaddr2[0] = macaddr[0];
    macaddr2[1] = macaddr[1];
    macaddr2[2] = macaddr[2];
    macaddr2[3] = macaddr[3];
    macaddr2[4] = macaddr[4];
    macaddr2[5] = macaddr[5];
    macaddr2[6] = '\0';
    
    //printf("looking up %s\n", macaddr2);

    HASH_FIND_STR(socketfds,macaddr2,s);

    if (!s) {
        printf("Couldn't find in hashtable\n\n");
        return NULL;
    }
    /*
    printf("find_socketfd found: ");
    int j;
    for (j = 0; j < 6; j++) {
        printf("%x:",s->macaddr[j]);
    }
    printf("\n\n\n\n");
    */

    free(macaddr2);
    return s;
}


/**
 * in_member_list gives a reference to a linkstate if it exists in the membership
 * table, otherwise NULL.
 */
linkstate_node* in_member_list(proxy_info* local_recv, proxy_info* remote_recv){
    
    linkstate_node * local_list_ptr = mem_list->head;
    
    while (local_list_ptr != NULL) {
        if ((local_list_ptr->local.ip == remote_recv->ip) && (local_list_ptr->remote.ip == local_recv->ip)) {
            return local_list_ptr;
        }
        local_list_ptr = local_list_ptr->next;
    }
    return NULL;
}

int get_lowest_saved_distance(uint8_t* mac) {
    linkstate_node * local_list_ptr = mem_list->head;
    int dist = -1;
    while (local_list_ptr != NULL) {
        if (comp_mac_addrs(local_list_ptr->remote.macaddr, mac) == 0) {
            if (dist > local_list_ptr->linkweight) {
                dist = local_list_ptr->linkweight;
            }
        }
        local_list_ptr = local_list_ptr->next;
    }
    return dist;
}


/** 
 * delete_members deletes a member form the membership list if the 
 * link times out or if a leave packet is received.
 */
void delete_members(linkstate_node* link){
    
    struct linkstate_node* curr = mem_list->head;
    struct linkstate_node* prev = NULL;

    while(curr != NULL)        {

        if(comp_mac_addrs(curr->remote.macaddr, link->remote.macaddr) == 0) { //Found linkstate
            printf("Before deletesockfd\n");
            deletesockfd(curr->remote.macaddr);
            printf("After deletesockfd\n");
            if (prev == NULL) { // Delete Head
                mem_list->head = curr->next;
            } else {
                prev->next = curr->next;
                // free(curr);
            }
            mem_list->size--;
            break;
        } else {
            prev = curr;
            curr = curr->next;
        }
    }
}

void add_members(linkstate_node* list, int gateway_sockID) {
    linkstate_node * ptr = list;
    printf("Got to add_members\n");
    while (ptr != NULL) {
        add_member_connect(ptr, gateway_sockID);
        ptr = ptr->next;
    }
}

void add_member_connect(linkstate_node* recv_node, int gateway_sockID) {
    pthread_mutex_lock(&deleterlock);
    printf("Got to add_member_connect part2\n");
    
    struct proxy_socketfd* pkt_sockfd = find_socketfd(recv_node->remote.macaddr);
    
    if (pkt_sockfd == NULL) {
        printf("Their Macaddr wasn't there\n");
        printf("The recieved remote macaddr is: \n");
        int j;
        for (j = 0; j < 6; j++) {
            printf("%x:",recv_node->remote.macaddr[j]);
        }
        printf("\n");
        printf("My macaddr is: \n");
        for (j = 0; j < 6; j++) {
            printf("%x:",my_proxy_info->macaddr[j]);
        }
        printf("\n");
        if (comp_mac_zero(recv_node->remote.macaddr) != 0) {
            printf("But it was me\n");
            pthread_mutex_unlock(&deleterlock);
            return;
        }
        printf("Got past comp_macs\n");
        addsockfd(recv_node->remote.macaddr, gateway_sockID);
        printf("Got past addsock\n");

        linkstate_node* temp = mem_list->head;
        linkstate_node* ptr = recv_node;
        
        ptr->next = temp;
        ptr->linkweight++;
        mem_list->head = ptr;
        mem_list->head->ID = gettimeid();
        mem_list->size++;
        pthread_mutex_unlock(&deleterlock);
        printf("Got past unlock\n");
        return;
        
    } else {
        printf("Mac was there\n");
        
        linkstate_node * finder = in_member_list(&(recv_node->local), &(recv_node->remote));
        int compVal = comp_mac_addrs(recv_node->remote.macaddr, my_proxy_info->macaddr);
        
        if (finder != NULL && compVal == 0) {
            printf("Member in list\n");
            //if (recv_node->ID > finder->ID) {
            finder->ID = gettimeid();
            printf("Updated time\n");
            pthread_mutex_unlock(&deleterlock);
            return;
            
        } else {
            if (finder == NULL) {
                int dist = get_lowest_saved_distance(recv_node->local.macaddr);
                int saveDist = get_lowest_saved_distance(recv_node->remote.macaddr);
                linkstate_node* temp = mem_list->head;
                linkstate_node* ptr = recv_node;
                
                ptr->next = temp;
                ptr->linkweight = ptr->linkweight + dist;
                mem_list->head = ptr;
                mem_list->head->ID = gettimeid();
                mem_list->size++;

                if (ptr->linkweight < saveDist) {
                    pkt_sockfd->sock_fd = gateway_sockID;
                }
            } else {
                pthread_mutex_unlock(&deleterlock);
                return;
            }
        }
        
        pthread_mutex_unlock(&deleterlock);
        return;
        
    }
    
    pthread_mutex_unlock(&deleterlock);
    return;
}

/** 
 * remove_expired_members deletes a member from the membership list if the link is expired
 */
void removeExpiredMembers(int link_timeout) {
    pthread_mutex_lock(&deleterlock);
    linkstate_node* tmp;
    linkstate_node* ptr = mem_list->head;

    double time = gettimeid();
    int micro_timeout = link_timeout*1000;

    while (ptr != NULL) {
        if ((time - ptr->ID) > micro_timeout) {
            tmp = ptr;
            ptr = ptr->next;
            printf("Auto deleted something\n");
            delete_members(tmp);
        } else {
            ptr = ptr->next;
        }
    }
    pthread_mutex_unlock(&deleterlock);
}



/******************************
 *create_link_packet
 *
 * takes the membership list and makes it into a linkstate_pkt.
 *****************************/

linkstate_pkt* create_link_packet() {
    if (mem_list->head == NULL) {
        return NULL;
    }
    linkstate_pkt* lstate = malloc(sizeof(pkt_header) + sizeof(uint16_t) + sizeof(proxy_info) + ((sizeof(linkstate_node)-sizeof(linkstate_node*))*mem_list->size));
    lstate->head.type = PKT_LINKSTATE;
    lstate->head.length = sizeof(uint16_t) + sizeof(proxy_info) + ((sizeof(linkstate_node)-sizeof(linkstate_node*))*mem_list->size);
    printf("length of lstate pkt is: %d\n",lstate->head.length); 
    lstate->numNeighbors = mem_list->size;   
    lstate->source = *my_proxy_info;
    lstate->list = mem_list->head;
    return lstate;
}


/****************************
 * sendPackets
 * thread sends linkstate packets every link period seconds
 * a little confused now cuz I think his write up says 10 seconds so now Idk what time to use
 ****************************/

void * sendPackets() {
    while(1) {
        sleep(linkPeriod);
        printf("Done sleep, transmitting membership list linkstate pkt\n");
        linkstate_pkt* lstate = create_link_packet();

        if (lstate == NULL) { // Don't transmit anything, nothing in mem_list
            printf("Continuing\n\n");
            continue;
        }

        int length = lstate->head.length;

        struct proxy_socketfd* s;
        for (s = socketfds; s != NULL; s = s->hh.next) {

            uint8_t* sock_macaddr = (uint8_t*)s->macaddr;

            printf("Sending linkstate pkt to machine with macaddr ");
            int j;
            for (j = 0; j < 6; j++) {
                printf("%x:",sock_macaddr[j]);
            }
            printf("\n");
            
            char buf[MAX_PKT_LEN];
            
            htonLinkstate(lstate,(char*)&buf);
            send(s->sock_fd,buf,length+sizeof(pkt_header),MSG_NOSIGNAL);
        }

        free(lstate);
    }
}

/****************************
 *checkLinkTimeOut
 *
 *thread to check the list for timeouts
 ****************************/

void * checkLinkTimeOut () {
    while(1) {
        sleep(linkTimeout);
        printf("Searching for expired members\n");
        removeExpiredMembers(linkTimeout);
    }
}

/*
 *connecttohost - connects a client to a server. the "connect" call is here
 */
int connecttohost(int remote_port, char* remote_host) {

    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
	perror("Socket failed");
	return -1;
    }

    struct sockaddr_in serv_addr;
    struct hostent *hp;

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(remote_port);
    serv_addr.sin_addr.s_addr = inet_addr(remote_host);
    if (serv_addr.sin_addr.s_addr == -1) {
        hp = gethostbyname(remote_host);
        if (hp == NULL) {
            fprintf(stderr, "Host name %s not found\n", remote_host);
            return -1;
        }
        bcopy(hp->h_addr, &serv_addr.sin_addr, hp->h_length);
    }
    if (connect(socket_fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        return -1;
    }

    // TODO ClientPath - Add the host to the membership list
    
    uint64_t timeID = gettimeid();

    proxy_info * remoteInfo = malloc(sizeof(proxy_info));
    remoteInfo->ip = serv_addr.sin_addr.s_addr;
    remoteInfo->portno = remote_port;

    linkstate_node* client_node = malloc(sizeof(linkstate_node));

    client_node->local = *my_proxy_info;
    client_node->remote = *remoteInfo;
    client_node->linkweight = 1;
    client_node->ID = timeID;
    client_node->next = NULL;

    pkt_header* head = malloc(sizeof(pkt_header));
    head->type = PKT_LINKSTATE;
    head->length = 50;

    char buf[54];

    linkstate_pkt* lspkt = malloc(sizeof(linkstate_pkt));

    lspkt->head = *head;
    lspkt->numNeighbors = 1;
    lspkt->source = *my_proxy_info;
    lspkt->list = client_node;

    //TODO send packet the right way
    htonLinkstate(lspkt,(char*)&buf);

    printf("\n\n\nSeding Inital Connect Linkstate Packet\n\n\n");
    
    send(socket_fd, buf,54,0);

    //shutdown(socket_fd,1);

    free(client_node);
    free(head);
    free(lspkt);
    
    return socket_fd;
}

/*
 *acceptconn - socket, bind, listen, accept. accept is in an infinite loop that
 * spawns threads to listen
 */
void *acceptconn(void* listenfd) {
    struct sockaddr_in serv_addr;
    struct sockaddr_in cli_addr;
    int connfd;
    int server_fd = *(int*)listenfd;

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(localPort);
    serv_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("bind failed");
        close(server_fd);
        exit(1);
    }

    if(listen(server_fd, 5) < 0) {
        perror("error on listening");
        close(server_fd);
        exit(1);
    }

    unsigned int cli_addr_size = sizeof(cli_addr);

    while (1) {
        connfd = accept(server_fd, (struct sockaddr *)&cli_addr, &cli_addr_size);
        if (connfd < 0) {
            perror("error accepting");
        } else {
            printf("Got a connection from %s on port %d\n", inet_ntoa(cli_addr.sin_addr), htons(cli_addr.sin_port));
            // TODO ServerPath - Accept many connections and add to membership list

    	    pthread_t readskt;
    	    pthread_create(&readskt,NULL,TCPHandle,&connfd);
        }
    }
    return NULL;
}

/* TAPHandle reads data from the tap device, encapsulates the data
 * and writes it to the TCP Socket. TAPHandle assumes that packet
 * length is 1500 Bytes.
 */
void *TAPHandle () {
    long n = 0;
    char buf[MAX_PKT_LEN];
    for (;;) {
        bzero(buf, MAX_PKT_LEN);

        // Read data from the tap device.
        n = read(tap_fd, buf+4, MAX_DATA_LEN);
        if (n < 0) {
            perror("ERROR reading from TCP Socket");
        }
        printf("Read %ld bytes from tap\n", n);

        uint16_t type = htons(PKT_DATA);
        uint16_t length = htons(n);

        memcpy(buf,&type,2);
        memcpy(buf+2,&length,2);

        // TODO Be able to send to any Node on the network

        struct proxy_socketfd* s;
        for (s = socketfds; s != NULL; s = s->hh.next) {
            write(s->sock_fd, buf, n+4);
        }

    }


    return NULL;
}

/* TCPHandle handles incoming TCP Packets by deencapulating the Packet.
 * It takes the length and type and make sure that the type is Big Endian.
 * After verifying the packet type it will grab the data and write it to the tap.
 */
void *TCPHandle (void* fd) {

    int tcp_fd = *(int*)fd;
    ssize_t n = 0;

    char buf[MAX_PKT_LEN];

    // Read the Packet type and length
    // printf("Waiting to read.\n");
    while (1) {
        bzero(buf, MAX_PKT_LEN);
        
        
        
	    // printf("Waiting to read\n");
        n = recv(tcp_fd, &buf, MAX_PKT_LEN, 0);
        if (n <= 0) { // Lost connection


            continue; 
        }
	    if (n != 0) {
       		printf("Read %d Bytes from the socket.\n",n);
	    }

        uint16_t pkt_type = 0;
        // Get the pkt type and do appropriate operation
        pkt_type = ntohs(*(uint16_t*)buf);

        //if data packet check destination if if it is us then send to tap, else I'm not sure since we aren't forwarding, I guess we only would be the dest
        if (pkt_type == PKT_DATA) {
	        printf("I am data pkt!\n");
    		// Write the data to the tap device.

    		write(tap_fd,buf+4,n-4);
        } else if (pkt_type == PKT_LEAVE) {
           	printf("I am leave pkt!\n");
            //leave_pkt* pkt = ntohLeave(buf);

    		// proxy_info leavingNode = pkt->local;
    		// uint64_t ID = pkt->ID;
    		
    		// TODO remove node from membership list

        } else if (pkt_type == PKT_QUIT) {
            
	        //quit_pkt* pkt = ntohQuit(buf);
		    
    		// TODO remove node from membership list
            /*
             * send packet to all
             */

	        // TODO Exit gracefully and close all connections.
            exit(1);
        } else if (pkt_type == PKT_LINKSTATE) {
	    
            printf("I am a link state pkt!\n");
	        linkstate_pkt* pkt = ntohLinkstate(buf);

		    printf("Below is where I came from and who I am connected to.\n");
		    printf("This is the length of the pkt: %d\n",pkt->head.length);
	        printf("This is the numNeighbors: %d\n", pkt->numNeighbors);
            printf("This is the ip that connected to me: %s\n", inet_ntoa(*(struct in_addr*)&pkt->source.ip));
            printf("this is my ip: %s\nAlso my ip is: %s\n", inet_ntoa(*(struct in_addr*)&pkt->list->remote.ip), inet_ntoa(*(struct in_addr*)&my_proxy_info->ip));    	       
            printf("This is the port they connected from: %d\n",pkt->source.portno);    
	        printf("This is their mac address: ");    
	        int j;
	        for (j = 0; j < 6; j++) {
		        printf("%x:",pkt->source.macaddr[j]);
	        }
	        printf("\n\n");

            // Check if incoming node is in hash table
            
            struct proxy_socketfd* pkt_sockfd = find_socketfd(pkt->source.macaddr);
            if (pkt_sockfd == NULL) {   // The initial receive, not in hashtable yet
                
                if (comp_mac_zero(pkt->list->remote.macaddr) != 0) { //recieved packet after inital send
                    
                    printf("\n\n\nGot the packet back\n\n\n");
                    
                    addsockfd(pkt->source.macaddr, tcp_fd);
                    uint64_t timeID2 = gettimeid();
                    
                    
                    proxy_info * remoteInfo2 = malloc(sizeof(proxy_info));
                    remoteInfo2->ip = pkt->source.ip;
                    remoteInfo2->portno = pkt->source.portno;
                    remoteInfo2->macaddr[0] = pkt->source.macaddr[0];
                    remoteInfo2->macaddr[1] = pkt->source.macaddr[1];
                    remoteInfo2->macaddr[2] = pkt->source.macaddr[2];
                    remoteInfo2->macaddr[3] = pkt->source.macaddr[3];
                    remoteInfo2->macaddr[4] = pkt->source.macaddr[4];
                    remoteInfo2->macaddr[5] = pkt->source.macaddr[5];
                    
                    
                    linkstate_node* client_node2 = malloc(sizeof(linkstate_node));
                    
                    client_node2->local = *my_proxy_info;
                    client_node2->remote = *remoteInfo2;
                    client_node2->linkweight = 1;
                    client_node2->ID = timeID2;
                    client_node2->next = NULL;
                    
                    pthread_mutex_lock(&deleterlock);
                    
                    linkstate_node* temp = mem_list->head;
                    linkstate_node* ptr = client_node2;
                    ptr->next = temp;
                    mem_list->head = ptr;
                    mem_list->size++;
                    
                    temp = mem_list->head;
                    while (temp != NULL) {
                        if (comp_mac_zero(temp->remote.macaddr) == 0) {
                            printf("Get into remote macaddr changing\n");
                            temp->remote.macaddr[0] = pkt->source.macaddr[0];
                            temp->remote.macaddr[1] = pkt->source.macaddr[1];
                            temp->remote.macaddr[2] = pkt->source.macaddr[2];
                            temp->remote.macaddr[3] = pkt->source.macaddr[3];
                            temp->remote.macaddr[4] = pkt->source.macaddr[4];
                            temp->remote.macaddr[5] = pkt->source.macaddr[5];
                        }
                        temp = temp->next;
                    }
                    
                    pthread_mutex_unlock(&deleterlock);
                    
                    printf("\n\n\nAttempting to add recieved list to my list\n\n\n");
                    
                    add_members(pkt->list, tcp_fd);
                    
                    printf("\n\n\nGot out of add_members\n\n\n");
                    
                }
                
                
                if (comp_mac_zero(pkt->list->remote.macaddr)==0 && pkt->numNeighbors == 1) { // pkt came from client initially connecting to network.
                    
                    printf("\n\n\nGot the original packet sent from connectToHost\n\n\n");
                    
                    addsockfd(pkt->source.macaddr, tcp_fd);
                    // On initial connect the remote macaddr will be 0's
                    pkt->list->remote.macaddr[0] = my_proxy_info->macaddr[0];
                    pkt->list->remote.macaddr[1] = my_proxy_info->macaddr[1];
                    pkt->list->remote.macaddr[2] = my_proxy_info->macaddr[2];
                    pkt->list->remote.macaddr[3] = my_proxy_info->macaddr[3];
                    pkt->list->remote.macaddr[4] = my_proxy_info->macaddr[4];
                    pkt->list->remote.macaddr[5] = my_proxy_info->macaddr[5];

                    uint64_t timeID2 = gettimeid();

                    proxy_info * remoteInfo2 = malloc(sizeof(proxy_info));
                    remoteInfo2->ip = pkt->source.ip;
                    remoteInfo2->portno = pkt->source.portno;
                    remoteInfo2->macaddr[0] = pkt->list->local.macaddr[0];
                    remoteInfo2->macaddr[1] = pkt->list->local.macaddr[1];
                    remoteInfo2->macaddr[2] = pkt->list->local.macaddr[2];
                    remoteInfo2->macaddr[3] = pkt->list->local.macaddr[3];
                    remoteInfo2->macaddr[4] = pkt->list->local.macaddr[4];
                    remoteInfo2->macaddr[5] = pkt->list->local.macaddr[5];

                    linkstate_node* client_node2 = malloc(sizeof(linkstate_node));

                    client_node2->local = *my_proxy_info;
                    client_node2->remote = *remoteInfo2;
                    client_node2->linkweight = 1;
                    client_node2->ID = timeID2;
                    client_node2->next = NULL;

                    printf("This MY IP %s\n", inet_ntoa(*(struct in_addr*)&client_node2->local.ip));
	                printf("This is the remote IP %s\n", inet_ntoa(*(struct in_addr*)&client_node2->remote.ip));       
                    
                    pthread_mutex_lock(&deleterlock);
                    
                    linkstate_node* temp = mem_list->head;
                    linkstate_node* ptr = client_node2;
                    ptr->next = temp;
                    mem_list->head = ptr;
                    mem_list->size++;
                    
                    printf("List is as follows:\n");
                    printf("Local IP: %s\n", inet_ntoa(*(struct in_addr*)&mem_list->head->local.ip));
                    printf("Remote IP: %s\n", inet_ntoa(*(struct in_addr*)&mem_list->head->remote.ip));
                    
                    
                    pthread_mutex_unlock(&deleterlock);
                    
                    
                    
                    pkt_header* head2 = malloc(sizeof(pkt_header));
                    head2->type = PKT_LINKSTATE;
                    head2->length = 50; // Change this based on the membership list.

                    char buf2[54];
                    //bzero(buf2, 54);

                    linkstate_pkt* lspkt2 = malloc(sizeof(linkstate_pkt));

                    lspkt2->head = *head2;
                    lspkt2->numNeighbors = mem_list->size; // Hardcoded for now to avoid infinite loop.
                    lspkt2->source = *my_proxy_info;
                    lspkt2->list = mem_list->head;
                    lspkt2->list->next = NULL;



                    htonLinkstate(lspkt2,(char*)&buf2);
                    // write(tcp_fd, buf2, 54);
                    send(tcp_fd, buf2,54,0);
                } else {
                    //close(tcp_fd); // Mentioned in description, hopefully will make sense later
                }
                continue;
            } else {
            
                printf("Received auto link state pkt\n");
                continue;
                
            }
        } else if (pkt_type == PKT_PROBEREQ) {
            //do nothing for now I think
            printf("That's part 3\n");
        } else if (pkt_type == PKT_PROBERES) {
            //do nothing for now I think
            printf("That's part 3\n");
        } else {
            printf("Didn't recognize the pkt type.\n");
        }
    }

    return NULL;
}


/*
 *Struct that allows us to make a linked list for peers from the config file
 */
 
typedef struct PeerList {
    int port;
    char* IP;
    struct PeerList * next;
} plist;

plist * head = NULL;

int main(int argc, char *argv[]) {

    if (pthread_mutex_init(&deleterlock, NULL) != 0) {
        printf("\n mutex init failed\n");
        return 1;
    }

    if (argc != 2) {
        perror("Improper Input Format\n");
        exit(1);
    }

    FILE * fp;
    char linep[LINE_MAX];
    fp = fopen(argv[1], "r");
    if (fp == NULL) {
        perror("File path does not exist\n");
        return 0;
    }
    int listenPort, remotePort, quitAfter;
    char* addr;
    char* tapDevice;
    char* globalSecretKey;
    char* macaddr;
    char* peerKey;
    
    fTable = malloc(sizeof(forwardingTable));
    mem_list = malloc(sizeof(membership_list));
    mem_list->head = NULL;
    mem_list->size = 0;
    my_proxy_info = malloc(sizeof(proxy_info));

    while (fgets(linep, LINE_MAX, fp) != NULL) {
        char* pch;
        plist * curr = malloc(sizeof(plist));
        pch = strtok(linep," ");

        if (strcmp(pch, "listenPort") == 0) {
            pch = strtok(NULL," \n");
            listenPort = atoi(pch);
            localPort = listenPort;
            my_proxy_info->portno = listenPort;
            printf("ListenPort is: %d\n", listenPort);
        } else if (strcmp(pch, "linkPeriod") == 0) {
            pch = strtok(NULL, " \n");
            linkPeriod = atoi(pch);
            printf("linkPeriod is: %d\n", linkPeriod);
        } else if (strcmp(pch, "linkTimeout") == 0) {
            pch = strtok(NULL, " \n");
            linkTimeout = atoi(pch);
            printf("linkTimeout is: %d\n", linkTimeout);
        } else if (strcmp(pch, "peer") == 0) {
            pch = strtok(NULL, " \n");
            addr = malloc(strlen(pch) + 1);
            strcpy(addr,pch);
            pch = strtok(NULL, " \n");
            remotePort = atoi(pch);
            printf("Address is: %s\nRemotePort is: %d\n", addr, remotePort);
            curr->IP = malloc(strlen(addr) + 1);
            strcpy(curr->IP, addr);
            curr->port = remotePort;
            LL_APPEND(head, curr);
        } else if (strcmp(pch, "quitAfter")==0) {
            pch = strtok(NULL, " \n");
            quitAfter = atoi(pch);
            printf("QuitAter: %d\n", quitAfter);
        } else if (strcmp(pch, "tapDevice") == 0) {
            pch = strtok(NULL, " \n");
            tapDevice = (char*) malloc(strlen(pch) + 1);
            strcpy(tapDevice,pch);
            printf("tapDevice is: %s\n", tapDevice);
        } else if (strcmp(pch, "globalSecretKey") == 0) {
            globalSecretKey = malloc(strlen(pch) + 1);
            strcpy(globalSecretKey, pch);
            printf("GlobalSecretKey is: %s\n", globalSecretKey);
        } else if (strcmp(pch, "peerKey") == 0) {
            pch = strtok(NULL, " \n");
            macaddr = malloc(strlen(pch) + 1);
            strcpy(macaddr, pch);
            pch = strtok(NULL, " \n");
            peerKey = malloc(strlen(pch) + 1);
            strcpy(peerKey, pch);
            printf("macaddr is %s\nPeerKey is: %s\n", macaddr, peerKey);
        } else {
            perror("file is not formatted correctly\n");
            return 0;
        }
    }

    // free(linep);

    if ((tap_fd = allocate_tunnel(tapDevice, IFF_TAP | IFF_NO_PI, (char*)&my_proxy_info->macaddr)) < 0) {
    	perror("Opening tap interface failed! \n");
    	return -1;
    }

    struct hostent* he;
    char ip[4];
    char hostname[128];


    gethostname(hostname,sizeof(hostname));
    printf("My hostname is: %s\n",hostname);

    if ((he = gethostbyname(hostname)) == NULL) {
    	herror("gethostbyname");
	    return -1;
    }

    struct in_addr **addr_list;

    addr_list = (struct in_addr **) he->h_addr_list;
    strcpy(ip,inet_ntoa(*addr_list[0]));

    inet_aton(ip, (struct in_addr *)&my_proxy_info->ip);

    printf("This is my ip: %s\n", inet_ntoa(*(struct in_addr*)&my_proxy_info->ip));

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
    	perror("Socket Failed");
	    return -1;
    }

    pthread_t acceptconnthread;
    pthread_create(&acceptconnthread,NULL,acceptconn,&server_fd);


    plist* curr = head;
    while (curr != NULL) {
       	char* ipaddr = curr->IP;
    	int portno = curr->port;

    	// TODO Connect to host and update membership list
    	int socket_fd = connecttohost(portno,ipaddr);

        if (socket_fd < 0) {
            printf("Failed to connect to host with ipaddr %s\n", inet_ntoa(*(struct in_addr*)&ipaddr));
            curr = curr->next;
            continue;
        }
        
        pthread_t listen_thread;
        pthread_create(&listen_thread,NULL,TCPHandle,&socket_fd);
	    curr = curr->next;
    }

    pthread_t TAPthread;
    pthread_t sendThread;
    pthread_t timeoutThread;

    pthread_create(&TAPthread,NULL,TAPHandle,NULL);
    pthread_create(&sendThread, NULL, sendPackets, NULL);
    //pthread_create(&timeoutThread, NULL, checkLinkTimeOut, NULL);

    pthread_join(TAPthread, NULL);
    pthread_join(acceptconnthread, NULL);
    pthread_join(sendThread, NULL);
    //pthread_join(timeoutThread, NULL);

    return 0;
}
