#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "networks.h"
#include "rcopy.h"
#include "cpe464.h"

#define DEFAULT_TIMEOUT 1

int seq_num = 0;
Connection connection;   

int main(int argc, char *argv[]) {
    STATE curState;

    if (argc != 5) {
        printf("usage %s [local-file] [remote-file] [remote-machine] [remote-port]\n", argv[0]);
        exit(-1);
    }

    sendErr_init(0, DROP_OFF, FLIP_OFF, DEBUG_ON, RSEED_OFF);

    connection = udp_send_setup(argv[3], argv[4]);
    
    //printf("connection socket %d, address %s\n", connection.socket, inet_ntoa(connection.address->sin_addr));

    curState = FILENAME;
    
    while (curState != DONE) {
        switch (curState) {

        case FILENAME:
            curState = sendFilename(argv[1], argv[2]);
            break;

        case WINDOW:
            curState = sendWindow(5);
            break;

        case DATA:
            break;

        case ACK:
            break;

        case EOFCONFIRM:
            break;

        case DONE:
            break;
        }
    }

    printf("done\n");
    return 0;
}

STATE sendWindow(int window) {
    Packet packet;
    char *message;

    sprintf(message, "%d", window);

    packet = createPacket(seq_num++, FLAG_WINDOW, message, strlen(message));
    return stopAndWait(packet, 10, DONE);
}

STATE sendFilename(char *localFile, char *remoteFile) {
    Packet packet;
    FILE *transferFile;

    transferFile = fopen(localFile, "r");
    if (transferFile == NULL) {
        fprintf(stderr, "File %s does not exist, exitting", localFile);
        exit(1);
    }

    //printf("sendFilename connection socket %d, address %s\n", connection.socket, inet_ntoa(connection.address->sin_addr));
    packet = createPacket(seq_num++, FLAG_FILENAME, remoteFile, strlen(remoteFile));
    return stopAndWait(packet, 10, WINDOW);
}

STATE stopAndWait(Packet packet, int numTriesLeft, STATE nextState) {
    //printf("sending packet to connection socket %d, address %s\n", connection.socket, inet_ntoa(connection.address->sin_addr));
    if (numTriesLeft <= 0) {
        printf("Server disconnected\n");
        return DONE;
    }

    sendPacket(connection, packet);
    if (selectCall(connection.socket, DEFAULT_TIMEOUT) == SELECT_TIMEOUT) {
        printf("timed out waiting for server ack");
        stopAndWait(packet, numTriesLeft - 1, nextState);
    }
    recievePacket(&connection);
    printf("done in %d tries\n", 10 - numTriesLeft);
    return nextState;
}

Connection udp_send_setup(char *host_name, char *port) {
    Connection newConnection;
    int socket_num;
    struct sockaddr_in remote;       // socket address for remote side
    struct hostent *hp;              // address of remote host
    int sockaddrinSize = sizeof(struct sockaddr_in);

    // create the socket
    if ((socket_num = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	{
	    perror("socket call");
	    exit(-1);
	}
    

    // designate the addressing family
    remote.sin_family= AF_INET;

    // get the address of the remote host and store
    if ((hp = gethostbyname(host_name)) == NULL)
	{
        printf("Error getting hostname: %s\n", host_name);
        exit(-1);
	}
    
    memcpy((char*)&remote.sin_addr, (char*)hp->h_addr, hp->h_length);

    // get the port used on the remote side and store
    remote.sin_port= htons(atoi(port));
    
    newConnection.socket = socket_num;
    //newConnection.address = (struct sockaddr_in *) malloc(sockaddrinSize);
    //memcpy(newConnection.address, &remote, sockaddrinSize);
    newConnection.address = remote;
    newConnection.addr_len = sockaddrinSize;
    printf("created connection socket %d, address %s\n", socket_num, inet_ntoa(remote.sin_addr));
    //printf("created connection socket %d, address %s\n", socket_num, inet_ntoa(newConnection.address->sin_addr));

    return newConnection;
}
