/* Nicolas Mockovciak 704337245*/

#ifndef PACKET_H
#define PACKET_H

#define DATA_SIZE 1015
#define PACKET_SIZE 1024
#define WINDOW_SIZE 5120
#define MAX_SEQUENCE_NUM 30720

#define REQ '1'
#define DATA '2'
#define ACK '3'
#define FIN '4'
#define FIN_ACK '5'


typedef struct {
	char type;
	unsigned int seq;	
	unsigned int size; 
	char data[DATA_SIZE];
} Packet;

#endif 