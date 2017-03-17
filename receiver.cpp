/* Nicolas Mockovciak 704337245*/

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <time.h>

#include "packet.h"

int main(int argc, char* argv[])
{
	struct sockaddr_in srv_addr;						// server address
	struct hostent *srv_ent;							// server database
	int sockfd, srv_portNum, slen = sizeof(srv_addr);		
	char *srv_hostname, *filename;
	double pl, pc;
	Packet inPacket, outPacket;

	int cwnd = WINDOW_SIZE/PACKET_SIZE;

	// read in args from command line user input-- Starting up client
	if (argc != 4)
	{
		fprintf(stderr, "Incorrect input. Use commands of the following format: client <server hostname> <server portnumber> <filename>\n");
		exit(1);
	}
	srv_hostname = argv[1];
	srv_portNum = atoi(argv[2]);
	filename = argv[3];

	// check arguments for errors
	if (srv_portNum < 0)
	{
		fprintf(stderr, "portnum cannot be negative\n");
		exit(1);
	}

	// get host from database
	srv_ent = (struct hostent*)gethostbyname(srv_hostname);	
	if (!srv_ent)
	{
		fprintf(stderr, "Invalid hostname\n");
		exit(1);
	}
	
	// initialize the socket
	if ((sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
	{
		fprintf(stderr, "Socket failed to open\n");
		exit(1);
	}

	memset((char*)&srv_addr, 0, slen);
	srv_addr.sin_family = AF_INET;
	srv_addr.sin_port = htons(srv_portNum);
	memcpy((char*) &srv_addr.sin_addr.s_addr, (char*) srv_ent->h_addr, srv_ent->h_length);
	
	// construct request message
	memset((char*)&outPacket, 0, sizeof(outPacket));
	outPacket.type = REQ;
	outPacket.seq = 0;
	outPacket.size = strlen(filename);
	memcpy(outPacket.data, filename, outPacket.size);
	
	// send request message
	if (sendto(sockfd, &outPacket, sizeof(outPacket), 0, (struct sockaddr*)&srv_addr, slen) == -1)
	{
		fprintf(stderr, "Error sending file request message\n");
		exit(1);
	}

	printf("Request sent... Awaiting file\n");
	
	// open file for writing
	char n_filename[DATA_SIZE];
	strcpy(n_filename, "n_");
	strcat(n_filename, filename);
	//.strcat(n_filename, filename);
	FILE *fp = fopen(n_filename, "w+");
	if (fp == NULL)
	{
		fprintf(stderr, "Error opening file for writing\n");
		exit(1);
	}
	
	memset((char*)&outPacket, 0, sizeof(outPacket));
	outPacket.type = ACK;
	outPacket.seq = 0;
	outPacket.size = 0;

	int windowBase = 0;
	int shift = 0;

	int seqInWindow = 0;
	int pcktsReceived[cwnd];
	memset((char*)&pcktsReceived, 0, cwnd*sizeof(int));
	char windowBuffer[cwnd * DATA_SIZE];
	memset((char*)&windowBuffer, 0, cwnd*DATA_SIZE);

	char tempDataBuffer[DATA_SIZE];
	memset((char*)&tempDataBuffer, 0, DATA_SIZE);	
	
	
	while (true) {

		fprintf(stderr, "IN WHILE LOOP!\n");

		memset((char*)&inPacket, 0, sizeof(inPacket));
		if (recvfrom(sockfd, &inPacket, sizeof(inPacket), 0, (struct sockaddr *)&srv_addr, (socklen_t *)&slen) == -1) {
			fclose(fp);
			fprintf(stderr, "Error receiving file\n");
			exit(1);
		}
		else if (inPacket.seq == 0 && inPacket.type == FIN) { // file does not exist
			fprintf(stderr, "RECEIVED PACKET <3!\n");

			fclose(fp);	
			fprintf(stderr, "No such file\n");
			exit(1);
		}
		
		else if (inPacket.type == DATA){
			printf("Recvd (type: %c, seq: %d, size: %d)\n",
				inPacket.type, inPacket.seq, inPacket.size);

			outPacket.seq = inPacket.seq;
			seqInWindow = inPacket.seq - windowBase;

			if (inPacket.seq < windowBase && inPacket.seq > (windowBase - cwnd) ) {
				// just send ACK, below
			}
			else if ( inPacket.seq >= windowBase && inPacket.seq < (windowBase + cwnd)) {

				fprintf(stderr, "\nHERE I AM!\n inPacket.seq = %d\n windowBase = %d\n", inPacket.seq, windowBase);

				if (inPacket.seq == windowBase) {
					fwrite(inPacket.data, sizeof(char), inPacket.size, fp);
					// go through buffer and deliver any buffered packets, in order
					printf("WROTE TO FILE: packet number %d\n\n", windowBase);

					shift = 1;

					int i = 1;
					int j = 0;

					for (; i < cwnd; i++) {
						if (pcktsReceived[i] == 0) {
							break;
						}
						else {
							fwrite(windowBuffer + (DATA_SIZE*i), sizeof(char), DATA_SIZE, fp);
							shift++;
							printf("WROTE TO FILE: packet number %d\n\n", windowBase+i);
						}
					} 

					// advance windowBase to next not-yet-recvd packet
					for (; j < (cwnd-shift); j++) {
						pcktsReceived[j] = pcktsReceived[shift+j];

						memcpy(tempDataBuffer, windowBuffer+(DATA_SIZE*(j+shift)), DATA_SIZE);
						memcpy(windowBuffer + (DATA_SIZE*j), tempDataBuffer, DATA_SIZE);
					}
					// fill in rest of buffers as empty
					for (; j < cwnd; j++) {
						pcktsReceived[j] = 0;
						memset(windowBuffer + (DATA_SIZE*j), 0, DATA_SIZE);
					}
					windowBase = windowBase + shift;
				}
				else {
					// add packet to buffer, in order
					memcpy(windowBuffer + (DATA_SIZE*seqInWindow), inPacket.data, inPacket.size);
					pcktsReceived[seqInWindow] = 1;
				}
			}
			else {
				continue;
			}
			
		}
		else {
			fprintf(stderr, "RECEIVED PACKET <3!\n");
			if (inPacket.type == FIN) {
				printf("Recvd (type: %c, seq: %d, size: %d)\n",
					inPacket.type, inPacket.seq, inPacket.size);
				break;
			}
			else {
				printf("Received non-data packet. Ignored\n");
				continue;
			}
		}
		fprintf(stderr, "RECEIVED PACKET!\n");
		
		if (sendto(sockfd, &outPacket, sizeof(outPacket), 0, (struct sockaddr *)&srv_addr, slen) == -1)
		{
			fprintf(stderr, "Error sending ACK packet\n");
			exit(1);
		}
		printf("Sent (type: %c, seq: %d, size: %d)\n",
				outPacket.type, outPacket.seq, outPacket.size);
	}

	// send FIN ACK
	outPacket.type = FIN;
	outPacket.seq = inPacket.seq;
	outPacket.size = 0;
	if (sendto(sockfd, &outPacket, sizeof(outPacket), 0, (struct sockaddr *)&srv_addr, slen) == -1)
	{
		fprintf(stderr, "Error sending FIN ACK packet\n");
		exit(1);
	}
	printf("Sent (type: %c, seq: %d, size: %d)\n",
				outPacket.type, outPacket.seq, outPacket.size);
	
	printf("Connection closed\n");
	
	fclose(fp);
	close(sockfd);
	return 0;
}
