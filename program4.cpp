//EECE466 Intro to networking
//Program 4
// Rainier and Charles


#include <iostream>
#include <stdio.h>
#include <poll.h> 
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/types.h>
#include <string.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <vector>

//Random useful variables
#define MAX_PENDING 5
#define MAX_PEERS 5
#define MAX_FILES 10
#define MAX_FILENAME_LEN 100

struct peer_entry {
    uint32_t id;                        
    int socketFd;          
    int fileCount;                
    char files[MAX_FILES][MAX_FILENAME_LEN];
    struct sockaddr_in addr;            
    bool active;                    
};

//set up vectors for poll and peers
static peer_entry peers[MAX_PEERS];
static struct pollfd fds[MAX_PEERS + 1];
static int nfds = MAX_PEERS + 1;


//Function Prototypes
int bind_and_listen( const char *service );

int sendAll(int fd, char*buf, int len);
int recvAll(int fd, char*buf, int len);
int handleMsg(int id);
void handleJoin(int fd);
void handlePublish(int fd);
void handleSearch(int fd);

int addSocket(int fd);
int findPeerIndex(int fd);
void closeFd( int id);

int main(int argc, char *argv[]) {
    if (argc != 2) {
        std::cerr<<"Usage: " << argv[0] <<" <port>\n";
        return -1;
    }

    const char *port = argv[1];

    //initialize peer vector
    for (int i = 0; i < MAX_PEERS; ++i) {
        peers[i].id = 0;
        peers[i].socketFd = -1;
        peers[i].fileCount = 0;
        peers[i].active = false;
    }

    int listenFd = bind_and_listen(port);

    if(listenFd < 0) {
        std::cerr << "bind and listen";
        return -1;
    }


    //initialize each peer in fds
    fds[0].fd = listenFd;
    fds[0].events = POLLIN;
    fds[0].revents = 0;

    for (int i = 1; i <= MAX_PEERS; ++i) {
        fds[i].fd = -1;
        fds[i].events = POLLIN;
        fds[i].revents = 0;
    }

    while(1) {
        int ret = poll(fds, nfds, -1);

        if (ret < 0) {
            if (errno == EINTR) {
                continue; 
            }
            std::cerr << "poll";
            break;
        }

        //check listening socekt for new connections, if so add the socket
        if (fds[0].revents & POLLIN) {
            struct sockaddr_in peerAddress;
            socklen_t addrlen = sizeof(peerAddress);
            int peerFd = accept(fds[0].fd, (struct sockaddr *)&peerAddress, &addrlen);

            if (peerFd < 0){
                std::cerr << "accept";

            } else {
                if (addSocket(peerFd) < 0) {
                    close(peerFd);
                }
            }
        }


        //check each peer sockets for input / errors
        for (int i = 1; i < nfds; ++i) {
            if (fds[i].fd < 0) {
                continue;
            }

            // Peer disconnect / error
            if (fds[i].revents & (POLLHUP | POLLERR | POLLNVAL)) {
                closeFd(i);
                continue;
            }

            // Incoming data available: handle one complete command/message.
            if (fds[i].revents & POLLIN) {
                if (handleMsg(i) < 0) {
                    closeFd(i);
                }
            }
        }
    }

    //clean up, close fds 
    for (int i = 0; i < nfds; ++i) {
        if (fds[i].fd >= 0) {
            close(fds[i].fd);
        }
    }
}

//Provided by kkredo: 
int bind_and_listen( const char *service ) {
	struct addrinfo hints;
	struct addrinfo *rp, *result;
	int s;

	/* Build address data structure */
	memset( &hints, 0, sizeof( struct addrinfo ) );
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	hints.ai_protocol = 0;

	/* Get local address info */
	if ( ( s = getaddrinfo( NULL, service, &hints, &result ) ) != 0 ) {
		fprintf( stderr, "stream-talk-server: getaddrinfo: %s\n", gai_strerror( s ) );
		return -1;
	}

	/* Iterate through the address list and try to perform passive open */
	for ( rp = result; rp != NULL; rp = rp->ai_next ) {
		if ( ( s = socket( rp->ai_family, rp->ai_socktype, rp->ai_protocol ) ) == -1 ) {
			continue;
		}

		if ( !bind( s, rp->ai_addr, rp->ai_addrlen ) ) {
			break;
		}

		close( s );
	}
	if ( rp == NULL ) {
		perror( "stream-talk-server: bind" );
		return -1;
	}
	if ( listen( s, MAX_PENDING ) == -1 ) {
		perror( "stream-talk-server: listen" );
		close( s );
		return -1;
	}
	freeaddrinfo( result );

	return s;
}

//Sends data until all data is fully sent
int sendAll(int fd, char*buf, int len) {
    int tot = 0;
    int bytesLeft = len;
    int n;

    while(tot < len) {
        n = send(fd, buf+tot, bytesLeft, 0);
        if (n == -1) { 
            return -1; 
            }
        bytesLeft -= n;
        tot += n;
    }
    return tot;
}

//recvs data until all data has been collected
int recvAll(int fd, char*buf, int len) {
    int tot = 0;
    int n;

    while(tot < len) {
        n = recv(fd, buf + tot,len - tot, 0);
        if (n <= 0) {
            return -1;
        }
        tot += n;
    }
    return tot;
}

//reads first byte of data, then calls function associated with the correct int
int handleMsg(int id ) {

    int fd = fds[id].fd;
    unsigned char cmd = 255;

    int n = recv(fd, &cmd, 1, 0);
    if (n <= 0) {
        return -1;
    }

    switch (cmd) {
        case 0:
            handleJoin(fd);
            break;
        case 1:
            handlePublish(fd);
            break;
        case 2:
            handleSearch(fd);
            break;
        default:
            return -1;
    }
    
    return 0;
}

//method to handle join 
void handleJoin(int fd) {

    int peerIndex = findPeerIndex(fd);

    if (peerIndex < 0) {
        return;
    }

    uint32_t netPeerId;

    if (recvAll(fd, reinterpret_cast<char *>(&netPeerId), sizeof(netPeerId)) == -1) {
        return;
    }

    uint32_t peerId = ntohl(netPeerId);
    peers[peerIndex].id = peerId;

    std::cout << "TEST] JOIN " << peerId << std::endl;
}

//method to handle publishing
void handlePublish(int fd) {
    int peerIndex = findPeerIndex(fd);

    if (peerIndex < 0) {
        return;
    }


    uint32_t netCount;
    if (recvAll(fd, reinterpret_cast<char *>(&netCount), sizeof(netCount)) == -1) {
        return;
    }

    uint32_t count = ntohl(netCount);
    std::vector<std::string> fileNames;
    fileNames.reserve(count);


    for (uint32_t i = 0; i < count; ++i) {
        char nameBuf[MAX_FILENAME_LEN];
        int  pos = 0;

        while(1) {
            char c;
            int n = recv(fd, &c, 1, 0);
            if (n <= 0) {
                return;
            }

            if (c == '\0') {
                break;
            }

            if (pos < MAX_FILENAME_LEN - 1) {
                nameBuf[pos++] = c;
            }
        }

        nameBuf[pos] = '\0';
        fileNames.push_back(std::string(nameBuf));

        if (peers[peerIndex].fileCount < MAX_FILES) {
            std::strncpy(peers[peerIndex].files[peers[peerIndex].fileCount], nameBuf, MAX_FILENAME_LEN - 1);
            peers[peerIndex].files[peers[peerIndex].fileCount][MAX_FILENAME_LEN - 1] = '\0';
            peers[peerIndex].fileCount++;
        }
    }

    std::cout << "TEST] PUBLISH " << count;
    for (auto &name : fileNames) {
        std::cout << " " << name;
    }
    std::cout << std::endl;
}

//method to handle searching
void handleSearch(int fd) {

    int peerIndex = findPeerIndex(fd);
    if (peerIndex < 0) {
        return;
    }

    char filename[MAX_FILENAME_LEN];
    int pos = 0;

    while(1) {
        char c;
        int n = recv(fd, &c, 1, 0);
        if (n <= 0) return;

        if (c == '\0') break;

        if (pos < MAX_FILENAME_LEN - 1)
            filename[pos++] = c;
    }

    filename[pos] = '\0';

    uint32_t foundId = 0;
    uint32_t foundIp = htonl(INADDR_ANY);
    uint16_t foundPort = htons(0);

    bool fileFound = false;

    for (int i = 0; i < MAX_PEERS && !fileFound; i++) {
        if (!peers[i].active) continue;

        for (int j = 0; j < peers[i].fileCount; j++) {
            if (strncmp(peers[i].files[j], filename, MAX_FILENAME_LEN) == 0) {

                foundId = htonl(peers[i].id);
                foundIp = peers[i].addr.sin_addr.s_addr;
                foundPort = peers[i].addr.sin_port;
                fileFound = true;
                break;
            }
        }
    }

    unsigned char response[10];
    memcpy(response, &foundId, 4);
    memcpy(response + 4, &foundIp, 4);
    memcpy(response + 8, &foundPort, 2);

    if (sendAll(fd, (char*)response, 10) == -1) {
        return;
    }

    uint32_t hostId = ntohl(foundId);
    uint16_t hostPort = ntohs(foundPort);

    char ipStr[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &foundIp, ipStr, sizeof(ipStr));

    // REQUIRED output format
    std::cout << "TEST] SEARCH " << filename << " " << hostId << " " << ipStr << ":" << hostPort << std::endl;
}

//takes fd and adds to peer
int addSocket(int fd) {
    int peerIndex = -1;
    for (int i = 0; i < MAX_PEERS; ++i) {
        if (!peers[i].active) {
            peerIndex = i;
            break;
        }
    }

    if (peerIndex < 0) {
        return -1;
    }

    peers[peerIndex].id = 0;      
    peers[peerIndex].socketFd = fd;
    peers[peerIndex].fileCount = 0;
    peers[peerIndex].active = true;

    socklen_t len = sizeof(peers[peerIndex].addr);
    if (getpeername(fd, reinterpret_cast<struct sockaddr*>(&peers[peerIndex].addr),&len) < 0) {
        std::cerr << "get peer name";
    }

    //if open slot for soxcket, place socket into fds 
    for (int i = 1; i <= MAX_PEERS; ++i) {
        if (fds[i].fd < 0) {
            fds[i].fd = fd;
            fds[i].events = POLLIN;
            fds[i].revents = 0;
            return 0;
        }
    }
    //otherwise, new fd cannot be stored, return error
    peers[peerIndex].active = false;
    peers[peerIndex].socketFd = -1;
    return -1;
}

//takes an fd and returns the index within the Peers vector
int findPeerIndex(int fd) {
    for (int i = 0; i < MAX_PEERS; ++i) {
        if (peers[i].active && peers[i].socketFd == fd) {
            return i;
        }
    }
    return -1;
}

//takes the index of peer in fds and closes it
void closeFd( int id) {
    int fd = fds[id].fd;

    if (fd >= 0) {
        close(fd);
    }

    fds[id].fd = -1;
    fds[id].events = POLLIN;
    fds[id].revents = 0;

    int peerId =findPeerIndex(fd);
    if (peerId >= 0) {
        peers[peerId].active = false;
        peers[peerId].id = 0;
        peers[peerId].socketFd = -1;
        peers[peerId].fileCount = 0;
    }
}