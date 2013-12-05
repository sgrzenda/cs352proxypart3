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
#include <utlist.h>

#define BUFSIZE 1500
#define HEADERLEN 4
#define MSGLEN (BUFSIZE-HEADERLEN)
#define PK_TYPE 0xABCD

int tap_fd, TCPSocketfd;

/****************************
 * allocate_tunnel:
 * open a tun or tap device and returns the file
 * descriptor to read/write back to the caller
 * **************************/
 int allocate_tunnel(char *dev, int flags) {

		int fd, error;
		struct ifreq ifr;
		char *device_name = "/dev/net/tun";

		if ( (fd = open(device_name , O_RDWR)) < 0) {
				perror("error opening /dev/net/tun");
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
		return fd;
 }

/* TAPHandle reads data from the tap device, encapsulates the data
 * and writes it to the TCP Socket. TAPHandle assumes that packet
 * length is 1500 Bytes.
 */
void *TAPHandle () {
    int n = 0;
    char buf[MSGLEN];
	for (;;) {
		 uint16_t type, length;

        	// Read data from the tap device.
        	n = read(tap_fd, buf, MSGLEN);
        	if (n < 0)
            		perror("ERROR reading from TCP Socket");
        	printf("Read %d bytes from tap\n", n);

        	type   = htons(PK_TYPE);
        	length = htons(n);

        	// Encapsulate the data to send
        	write(TCPSocketfd, &type, 2);
        	write(TCPSocketfd, &length, 2);
        	write(TCPSocketfd, buf, n);

        	printf("Wrote %d bytes the TCP socket\n", n);
	}

	return NULL;
}

/* TCPHandle handles income TCP Packets by deencapulating the Packet.
 * It takes the length and type and make sure that the type is Big Endian.
 * After verifying the packet type it will grab the data and write it to the tap.
 */
void *TCPHandle () {

	char buf[MSGLEN];
	int n = 0;

	for (;;) {
        	uint16_t type, length;

		// Read the Packet type and length
        	read(TCPSocketfd, &type, 2);
        	read(TCPSocketfd, &length, 2);

        	type   = ntohs(type);
        	length = ntohs(length);

        	if (type != PK_TYPE)
            		perror("ERROR wrong packet type");

        	bzero(buf, length);
        	n = read(TCPSocketfd, buf, BUFSIZE);
        	if (n < 0) {
            		perror("ERROR reading from socket.");
        	}
        	printf("server received %d bytes\n", n);
        	n = write(tap_fd, buf, length);

        	printf("Wrote %d bytes to the tap device\n", n);

        	if (n < 0)
            		perror("ERROR writing to socket");
	}

	return NULL;
}

int createServer(int sd, int port) {
	struct sockaddr_in stSockAddr;
	struct sockaddr_in cli_addr;
	int newsockfd;

	memset(&stSockAddr, 0, sizeof(stSockAddr));
	stSockAddr.sin_family = AF_INET;
	stSockAddr.sin_port = htons(port);
	stSockAddr.sin_addr.s_addr = INADDR_ANY;
	if (bind(sd, (struct sockaddr *)&stSockAddr, sizeof(stSockAddr)) < 0) {
		perror("bind failed");
		close(sd);
		exit(1);
	}

	if(listen(sd, 5) < 0) {
		perror("error on listening");
		close(sd);
		exit(1);
	}
	unsigned int cli_addr_size = sizeof(cli_addr);

	newsockfd = accept(sd, (struct sockaddr *)&cli_addr, &cli_addr_size);
	if (newsockfd < 0) {
		perror("error accepting");
		exit(1);
	} else {
		printf("Got a connection from %s on port %d\n", inet_ntoa(cli_addr.sin_addr), htons(cli_addr.sin_port));
		return newsockfd;
	}

}

int createClient(int socketfd, int remote_port, char* remote_host) {
	struct sockaddr_in to;
	struct hostent *hp;

	to.sin_family = AF_INET;
	to.sin_port = htons(remote_port);
	to.sin_addr.s_addr = inet_addr(remote_host);
	if (to.sin_addr.s_addr == -1) {
		hp = gethostbyname(remote_host);
		if (hp == NULL) {
			fprintf(stderr, "Host name %s not foune\n", remote_host);
			exit(1);
		}
		bcopy(hp->h_addr, &to.sin_addr, hp->h_length);
	}
	if (connect(socketfd, (struct sockaddr *) &to, sizeof(to)) < 0) {
		perror("connect");
		exit(-1);
	}
	return socketfd;
}

int main(int argc, char* argv[]) {
	int local_port;
	char* local_interface;
	char* remote_host;
	int remote_port;
	int isClient = 0;

	if (argc < 3 || argc > 4) {
		perror("Error: improper number of args entered\nProper format: ./cs352proxy <port> <local_interface> or ./cs352proxy <remote_host> <remote_port> <local_interface>\n");
		return 0;
	} else if (argc == 3) {
		local_port = atoi(argv[1]);
		if (local_port < 1024 || local_port > 65536) {
			printf("port out of range\n");
			return 1;
		}
		local_interface = argv[2];
		isClient = 0;
	} else {
		remote_host = argv[1];
		remote_port = atoi(argv[2]);
		if (remote_port < 1024 || remote_port > 65536) {
			printf("port out of range\n");
			return 1;
		}
		local_interface = argv[3];
		isClient = 1;
	}

	if ( (tap_fd = allocate_tunnel(local_interface, IFF_TAP | IFF_NO_PI)) < 0) {
		perror("Opening tap interface failed! \n");
		exit(1);
	}

	int socketfd = socket(AF_INET, SOCK_STREAM, 0);
	if (socketfd < 0) {
		perror("Socket failed");
		exit(1);
	}

	if (!isClient) {
		TCPSocketfd = createServer(socketfd, local_port);
	} else {
		TCPSocketfd = createClient(socketfd, remote_port, remote_host);
	}

	pthread_t TAPthread, TCPthread;

	pthread_create(&TAPthread, NULL, TAPHandle, NULL);
	pthread_create(&TCPthread, NULL, TCPHandle, NULL);

 	pthread_join(TAPthread, NULL);
	pthread_join(TCPthread, NULL);

	return 0;
}
