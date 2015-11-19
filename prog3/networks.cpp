#include <stdio.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>
#include "networks.h"
#include "cpe464.h"

/*   PACKET HEADER
 *  |    4    |  2  | 1 |  2  |   DATA
 *  |  seqN   | cksm|flg| size|   DATA
 */
void initHeader(Packet *pkt) {
    char *data = pkt->payload;
    uint32_t seq_num = htonl(pkt->seq_num);
    uint16_t size = htons(pkt->size);

    memcpy(data, &seq_num, 4);
    memcpy(data + 4, &(pkt->checksum), 2);
    memcpy(data + 6, &(pkt->flag), 1);
    memcpy(data + 7, &size, 2);
}

Packet createPacket(uint32_t seq_num, int flag, char *payload, int size_payload) {
    Packet packet;

    packet.seq_num = seq_num;
    packet.flag = (int8_t) flag;
    packet.size = size_payload + HDR_LEN + 1;
    packet.payload = (char *) (malloc(packet.size));
    memset(packet.payload, 0, packet.size);
    memcpy(packet.payload + HDR_LEN, payload, size_payload);
    packet.data = packet.payload + HDR_LEN;
    
    printf("created packet %d, size %d, data %s\n", seq_num, packet.size, packet.data);
    initHeader(&packet);
    return packet;
}

void print_packet(void * start, int bytes) {
    int i;
    uint8_t *byt1 = (uint8_t*) start;
    for (i = 0; i < bytes; i += 2) {
        printf("%.2X%.2X ", *byt1, *(byt1 + 1));
        byt1 += 2;
    }
    printf("\n");
}

void sendPacket(Connection connection, Packet packet) {
    //printf("sending packet %s\n", packet.data);
    printf("sending packet to %s\n", inet_ntoa(connection.address.sin_addr));
    print_packet(packet.payload, packet.size);
    if (sendtoErr(connection.socket, packet.payload, packet.size, 0, (struct sockaddr*) &connection.address, connection.addr_len) < 0) {
        perror("send call failed");
        exit(-1);
    }
    //printf("sent\n");
}


SELECTVAL selectCall(int socket, int timeoutSec) {
    fd_set readFds;
    struct timeval timeout;
    int selectReturn;

    timeout.tv_sec = timeoutSec;
    timeout.tv_usec = 0;
    FD_ZERO(&readFds);
    FD_SET(socket, &readFds);

    printf("recieving data\n");
    if ((selectReturn = selectMod(socket + 1, &readFds, 0, 0, &timeout)) < 0) {
        perror("select call");
        exit(-1);
    }
    if (selectReturn == 0) {
        printf("timed out\n");
        return SELECT_TIMEOUT;
    }
    return SELECT_HAS_DATA;

}

Packet fromPayload(char *payload) {
    Packet pkt;

    memcpy(&(pkt.seq_num), payload, 4);
    memcpy(&(pkt.checksum), payload + 4, 2);
    memcpy(&(pkt.flag), payload + 6, 1);
    memcpy(&(pkt.size), payload + 7, 2);

    pkt.payload = payload;
    pkt.data = pkt.payload + HDR_LEN;
    pkt.size = ntohs(pkt.size);
    pkt.seq_num = ntohl(pkt.seq_num);
    return pkt;
}

Packet recievePacket(Connection *connection) {
    char payload[MAX_LEN_PKT];
    int message_len;

    if ((message_len = recvfromErr(connection->socket, payload, MAX_LEN_PKT, 0, (struct sockaddr *) &connection->address, &connection->addr_len)) < 0) {
        perror("recvFrom call");
        exit(-1);
    } else if (message_len == 0) {
        exit(0);   // client exitted
    }
    printf("established connection %s\n", inet_ntoa(connection->address.sin_addr));
    return fromPayload(payload);
}