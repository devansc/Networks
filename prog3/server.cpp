#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "networks.h"
#include "server.h"
#include "cpe464.h"

#define DEFAULT_TIMEOUT 10

int seq_num;
Connection connection;

int main (int argc, char **argv) {
    int connectionSocket;
    int port = 0;
    connectionSocket = udp_recv_setup(port);

    while (1) {
        if (selectCall(connectionSocket, DEFAULT_TIMEOUT) == SELECT_TIMEOUT) 
            continue;

        // HAS_DATA
        //processClient(udp_recv_setup(0));
        processClient(connectionSocket);
        printf("child exitting\n");
        exit(0);

    }

    return 0;
}

void processClient(int socket) {
    STATE curState;
    printf("talking to client on socket %d\n", socket);

    curState = FILENAME;
    seq_num = 0;
    connection.socket = socket;
    connection.addr_len = sizeof(struct sockaddr_in);


    while (curState != DONE) {
        switch (curState) {

        case FILENAME:
            curState = recieveFilename();
            break;

        case WINDOW:
            curState = recieveWindow();
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

}

STATE recieveFilename() {
    Packet packet = recievePacket(&connection);
    Packet ackPacket;
    char *filename;

    //printf("created connection socket %d, address %s\n", connection.socket, inet_ntoa(connection.address->sin_addr));

    switch (packet.flag) {
    case FLAG_FILENAME: 
        filename = strdup(packet.data);
        printf("filename recieved %s\n", filename);
        ackPacket = createPacket(seq_num++, FLAG_FILENAME_ACK, NULL, 0);  
        sendPacket(connection, ackPacket);
        return WINDOW;
    default:
        printf("Expected filename packet, recieved packet flag %d\n", packet.flag);
        break;
    }
    //printf("recieved filename %s\n", pktToString(packet));

    return DONE;
}

STATE recieveWindow() {
    Packet packet = recievePacket(&connection);
    Packet ackPacket;
    int window;

    switch (packet.flag) {
    case FLAG_WINDOW: 
        window = *(int *)packet.data;
        window = ntohl(window);
        printf("window recieved %d\n", window);
        ackPacket = createPacket(seq_num++, FLAG_WINDOW_ACK, NULL, 0); 
        sendPacket(connection, ackPacket);
        return DONE;
    default:
        printf("Expected window packet, recieved packet flag %d\n", packet.flag);
        break;
    }
    //printf("recieved window %s\n", pktToString(packet));

    return WINDOW;
}



char *pktToString(Packet packet) {
    char *returnString;
    sprintf(returnString, "Packet %d len %d, data %s", packet.seq_num, packet.size, packet.data);
    return returnString;
}

int udp_recv_setup(int port)
{
    int server_socket= 0;
    struct sockaddr_in local;      /* socket address for local side  */
    socklen_t len= sizeof(local);  /* length of local address        */

    /* create the socket  */
    server_socket= socket(AF_INET, SOCK_DGRAM, 0);
    if(server_socket < 0) {
        perror("socket call");
        exit(1);
    }

    local.sin_family= AF_INET;         //internet family
    local.sin_addr.s_addr= htonl(INADDR_ANY) ; //wild card machine address
    local.sin_port= htons(port);                 //let system choose the port

    /* bind the name (address) to a port */
    if (bindMod(server_socket, (struct sockaddr *) &local, sizeof(local)) < 0) {
        perror("bind call");
        exit(-1);
    }

    //get the port name and print it out
    if (getsockname(server_socket, (struct sockaddr*)&local, &len) < 0) {
        perror("getsockname call");
        exit(-1);
    }

    printf("socket has port %d \n", ntohs(local.sin_port));
    printf("ip %s \n", inet_ntoa(local.sin_addr));

    return server_socket;
}
