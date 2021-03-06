#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "networks.h"
#include "server.h"
#include "cpe464.h"

#define DEFAULT_TIMEOUT 10

int seq_num;
Connection connection;
int outputFile; 
uint32_t windowSize;
uint32_t windowExpected;
Packet *buffer;
uint32_t bottomWindow;
uint32_t lowerWindow;
uint32_t upperWindow;
uint32_t highestPktSeen;
int quiet = 0;
int counter = 0;

int main (int argc, char **argv) {
    int connectionSocket;
    int port = 0;
    float errPercent;

    if (argc < 2 || argc > 3) {
        printf("usage %s [error-percent] [optional-port]\n", argv[0]);
        exit(-1);
    }
    errPercent = atof(argv[1]);
    if (errPercent < 0 || errPercent > 1) {
        fprintf(stderr, "Error error-percent must be greater than 0, less than 1");
        exit(-1);
    }

    if (argc > 2)
        port = atoi(argv[2]);

    sendErr_init(errPercent, DROP_ON, FLIP_ON, DEBUG_ON, RSEED_ON);

    connectionSocket = udp_recv_setup(port);

    while (1) {
        if (selectCall(connectionSocket, DEFAULT_TIMEOUT) == SELECT_TIMEOUT) 
            continue;
        else if (fork() == 0) {
            processClient(connectionSocket);
            printf("child exitting\n");
            exit(0);
        }
    }

    return 0;
}

void processClient(int socket) {
    STATE curState;

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
            buffer = NULL;
            break;

        case DATA:
            curState = recieveData();
            break;

        case ACK:
            break;

        case EOFCONFIRM: // EOF already seen, just waiting for 3rd handshake
            curState = eofConfirmQuit();
            break;

        case DONE:
            close(outputFile);
            break;
        }
    }
}

STATE eofConfirmQuit() {
    Packet quitPacket;

    if (selectCall(connection.socket, DEFAULT_TIMEOUT) == SELECT_TIMEOUT) {
        printf("timed out recieving eof, exitting\n");
        return DONE;
    }

    quitPacket = recievePacket(&connection);

    if (quitPacket.checksum != 0) {
        printf("crc failed for window size\n");
        return eofConfirmQuit();
    }

    switch(quitPacket.flag) {
    case FLAG_EOF:
        sendResponse(FLAG_RR, quitPacket.seq_num + 1);
        return eofConfirmQuit();
    case FLAG_QUIT:
        return DONE;
    default:
        printf("recieved flag %d instead of flag_quit.. fix this\n");
        return DONE;
    }
}

void sendResponse(int flag, int rrNum) {
    Packet ackPacket;

    if (flag == FLAG_RR)
        printf("sending ack rr %d\n", rrNum);
    else if (flag == FLAG_SREJ)
        printf("sending srej %d\n", rrNum);
    else
        printf("sending flag %d rrNum %d\n", flag, rrNum);
    rrNum = htonl(rrNum);
    ackPacket = createPacket(seq_num++, flag, (unsigned char *)&rrNum, sizeof(int));  
    sendPacket(connection, ackPacket);
}

void printBuffer() {
    printf("printing buffer\n");
    for (int i = 1; i < windowSize; i++) {
        printf("  %d  ", i);
    }
    printf("\n");
    for (int i = 1; i < windowSize; i++) {
        printf(" %d  ", buffer == NULL ? 0 : buffer[i].seq_num);
    }
    printf("\n");
}

void printToFile(Packet recvPacket) {
    write(outputFile, recvPacket.data, recvPacket.size - HDR_LEN);
    printf("writing data(len %d) %d %s\n",recvPacket.size, recvPacket.seq_num, recvPacket.data);
}

void slideWindow(int num) {
    int i;
    // could be simplified to 1 memcpy/memset
    for (i = 0; i < windowSize - num; i++) {
        memcpy(buffer+i,buffer+i+num,sizeof(Packet));
    }
    for (; i < windowSize; i++) {   
        memset(buffer+i, 0, sizeof(Packet));
    }
    printf("slid window %d\n", num);
    printBuffer();
}

STATE writeBufToFile() {
    uint32_t i = 1; // already wrote the first packet
    printf("writing buf to file\n");
    for ( ; i < windowSize && buffer != NULL && buffer[i].seq_num != 0; i++) {
        if (buffer[i].flag == FLAG_EOF) {
            sendResponse(FLAG_RR, buffer[i].seq_num + 1);
            return EOFCONFIRM;
        }
        printToFile(buffer[i]);
    }

    windowExpected = i + windowExpected;

    if (windowExpected < highestPktSeen) {
        printf("sent SREJ %d after writing buffer\n", windowExpected);
        sendResponse(FLAG_SREJ, windowExpected);
        slideWindow(i);
    } else {
        printf("sent RR %d after writing buffer\n", windowExpected);
        sendResponse(FLAG_RR, windowExpected);
        memset(buffer, 0, sizeof(Packet) * windowSize);
        free(buffer);
        buffer = NULL;
        quiet = 0;
    }
    printf("windowExpected is now %d\n", windowExpected);
    return DATA;
}

void insertIntoBuffer(int bufferPos, Packet recvPacket) {
    memcpy(buffer + bufferPos, &recvPacket, sizeof(Packet));
    buffer[bufferPos].data = buffer[bufferPos].payload + HDR_LEN;
    //buffer[bufferPos].data = (char *) malloc(recvPacket.size - HDR_LEN);
    //memcpy(buffer[bufferPos].data, recvPacket.data, recvPacket.size - HDR_LEN);
    printf("buffered data %s\n", buffer[bufferPos].data);
}

STATE recieveData() {
    Packet recvPacket;

    if (selectCall(connection.socket, DEFAULT_TIMEOUT) == SELECT_TIMEOUT) {
        printf("timed out recieving data, exitting\n");
        return DONE;
    }

    printf("recieving packet\n");
    counter++;
    recvPacket = recievePacket(&connection); 

    if (recvPacket.checksum != 0) {
        printf("crc failed for data\n");
        return DATA;
    }

    if (recvPacket.seq_num > highestPktSeen) 
        highestPktSeen = recvPacket.seq_num;
    printf("highest packet seen is %d, quiet %s\n", highestPktSeen, quiet?"true":"false");

    if (recvPacket.flag == FLAG_DATA && recvPacket.seq_num == windowExpected) {
        printToFile(recvPacket);
        if (buffer == NULL) {
            sendResponse(FLAG_RR, recvPacket.seq_num + 1);
            windowExpected++;
        } else return writeBufToFile();
        counter = 0;
        return DATA;
    } else if (recvPacket.seq_num > windowExpected) {
        int bufferPos = (recvPacket.seq_num - windowExpected) % windowSize;
        if (buffer == NULL) 
            buffer = (Packet *) malloc(sizeof(Packet) * windowSize);
        printf("buffering data %d to bufferPos %d -- %s\n",recvPacket.seq_num, bufferPos, recvPacket.data);
        if (!quiet) sendResponse(FLAG_SREJ, windowExpected);
        insertIntoBuffer(bufferPos, recvPacket);
        printBuffer();
        quiet = 1;
        return DATA;
    } else if (recvPacket.flag == FLAG_DATA && recvPacket.seq_num < windowExpected) {
        printf("recieved %d when expected %d\n", recvPacket.seq_num, windowExpected);
        /*
        if (!quiet) sendResponse(FLAG_RR, windowExpected);   
        else if (counter > 10) sendResponse(FLAG_RR, windowExpected); // this is a fix... not perfect
        */
        printf("running %d < %d\n", windowExpected, highestPktSeen + 1);
        sendResponse(FLAG_RR, windowExpected);

        quiet = 0;
        return DATA;
    } else if (recvPacket.flag != FLAG_EOF) {
        sendResponse(FLAG_RR, recvPacket.seq_num + 1);
        return DATA;
    } else {
        printf("EOF flag recieved\n");
        if (!quiet) sendResponse(FLAG_RR, recvPacket.seq_num + 1);
        return EOFCONFIRM;
    }
}

STATE recieveFilename() {
    Packet packet = recievePacket(&connection);
    Packet errPacket;
    char *filename;
    int rrNum;
    int newSocket = udp_recv_setup(0);
    connection.socket = newSocket;

    if (packet.checksum != 0) {
        return recieveFilename();
    }

    switch (packet.flag) {
    case FLAG_FILENAME: 
        filename = strdup(packet.data);
        if((outputFile = open(filename, O_CREAT | O_RDWR | O_TRUNC, 0777)) < 0) {
            errPacket = createPacket(seq_num++, FLAG_ERR_REMOTE, NULL, 0);  
            sendPacket(connection, errPacket);
            fprintf(stderr, "Couldn't open %s for writing\n", filename);
            return DONE;
        }
        printf("opened file %s for writing\n", filename);
        rrNum = packet.seq_num + 1;
        sendResponse(FLAG_RR, rrNum);
        return WINDOW;
    default:
        printf("Expected filename packet, recieved packet flag %d\n", packet.flag);
        break;
    }

    return DONE;
}

STATE recieveWindow() {
    Packet packet = recievePacket(&connection);
    int rrNum;

    if (packet.checksum != 0) {
        printf("crc failed for window size\n");
        return recieveWindow();
    }

    switch (packet.flag) {
    case FLAG_WINDOW: 
        windowSize = *(uint32_t *)packet.data;
        windowSize = ntohl(windowSize);
        rrNum = packet.seq_num + 1;
        sendResponse(FLAG_RR, rrNum);
        windowExpected = rrNum;
        return DATA;
    default:
        printf("Expected window packet, recieved packet flag %d\n", packet.flag);
        break;
    }

    return WINDOW;
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

    return server_socket;
}
