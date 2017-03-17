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
#include <stdlib.h>
#include <cstdio>
#include <ctime>

#include "packet.h"

int main(int argc, char* argv[])
{
	struct sockaddr_in srv_addr, cli_addr;
	int sockfd, port_num, cwnd;
	int clen = sizeof(cli_addr);
	double pl, pc;
	char *filename;
	Packet inPacket, outPacket;
	
	cwnd = WINDOW_SIZE/PACKET_SIZE;

	// get arguments from command line
	if (argc != 2)
	{
		fprintf(stderr, "Incorrect input. Use commands of the following format: server <server portnumber> <filename>\n");
		exit(1);
	}
	port_num = atoi(argv[1]);
	
	// check arguments for errors
	if (port_num < 0)
		{
		fprintf(stderr, "portnum must be non-negative\n");
		exit(1);
	}

	// create socket
	if ((sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
	{
		fprintf(stderr, "Error opening socket\n");
		exit(1);
	}

	memset((char *)&srv_addr, 0, clen);
	srv_addr.sin_family = AF_INET;
	srv_addr.sin_port = htons(port_num);
	srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	
	// bind socket to port
	if (bind(sockfd, (struct sockaddr *) &srv_addr, clen) == -1)
	{
		fprintf(stderr, "Error binding socket\n");
		exit(1);
	}
	
	//srand(time(NULL));
	
	while (true) {
		// process file request
		printf("\nWaiting for file request\n");

		if (recvfrom(sockfd, &inPacket, PACKET_SIZE, 0, (struct sockaddr*)&cli_addr, (socklen_t *)&clen) == -1) {
			printf("Error receiving file request\n");
			continue;
		}
		else if (inPacket.type != REQ) {
			printf("Received non-request packet. Ignored\n");
			continue;
		}
		
		filename = inPacket.data;
		printf("Received file request for \"%s\"\n", filename);
		
		FILE *fp = fopen(filename, "r");
		if (fp == NULL) { 
		// no such file, so send a FIN packet immediately
			memset((char*)&outPacket, 0, sizeof(outPacket));
			outPacket.type = FIN;
			outPacket.seq = 0;

			if (sendto(sockfd, &outPacket, sizeof(outPacket), 0, (struct sockaddr*)&cli_addr, clen) == -1)
			{
				fprintf(stderr, "Error sending FIN packet\n");
				exit(1);
			}
			printf("No such file. Sent FIN\n");
			continue;
		}

		fprintf(stderr, "AFTER READIN FILE IN!\n");
		
		// read file into memory buffer
		fseek(fp, 0L, SEEK_END);
		long file_size = ftell(fp);
		fseek(fp, 0L, SEEK_SET);
		char *file_buf = (char*) malloc(sizeof(char) * file_size);
		fread(file_buf, sizeof(char), file_size, fp);
		
		fclose(fp);



		// we can have up to N (cwnd) unACKED packets in pipeline
		// we have a timer for each unACKed packet
			// when timer expires, retransmit only unACKed packet

		// if we received smallest ACK we are waiting for in window, advance window base to next unACKed seq number

	
		int totPackets = file_size/DATA_SIZE + (file_size % DATA_SIZE != 0);

		int windowBase = 0;	// most recent value? idk?
		int totPacketsSent = 0;

		int ACKED[totPackets]; 					// keep track of unACKed packets
		int sentPackets[totPackets];		// keep track of packets successfully sent

		memset((char*)&ACKED, 0, sizeof(int)*totPackets);
		memset((char*)&ACKED, 0, sizeof(int)*totPackets);

		bool newACK = false;
		bool recPacket = false;

		std::clock_t pktTimers[totPackets];
		memset((char*)&pktTimers, 0, sizeof(std::clock_t)*totPackets);

		double timePassed;

		std::clock_t generalTimer = std::clock();

		int curSeqNum = 0;
		int curSeqPos = 0;

		fd_set set;
		struct timeval timeout = {0.25, 0}; // 250 ms timeout

		printf("\n total number of packets = %d \n window size = %d \n", totPackets, cwnd);

		while (windowBase < totPackets) {	// sending all packets AND waiting for ACKs

			fprintf(stderr, "IN WHILE LOOP FOR SENDING PACKETS AND RECEIVING ACKS!\n");

			newACK = false;
			recPacket = false;

			if(ACKED[windowBase]) {
				// shift windowBase until first sequence number in window is unACKed
				while( windowBase < (totPackets) &&  ACKED[windowBase] ) {
					windowBase++;
				}
			}
			if (windowBase >= totPackets) { break; }

			// check if general timeout since last sent packet-- must be timeout somewhere, don't bother trying to receive a packet 
			timePassed = ( std::clock() - generalTimer ) / (double) CLOCKS_PER_SEC;
				

			FD_ZERO(&set);
			FD_SET(sockfd, &set);

			if (select(sockfd+1, &set, NULL, NULL, &timeout) > 0) {
				if(recvfrom(sockfd, &inPacket, sizeof(inPacket), 0, (struct sockaddr *)&cli_addr, (socklen_t *)&clen) == -1) {
					fprintf(stderr, "Error retrieving packet from receiver\n");
					exit(1);
				}
				printf("Recvd (type: %c, seq: %d, size: %d)\n",
				inPacket.type, inPacket.seq, inPacket.size);
				recPacket = true;
			}

			if( recPacket && inPacket.type != ACK ) {
				fprintf(stderr, "Expected an ACK, but received a different type of packet\n");
				exit(1);
			}
			else if (recPacket){
				if (inPacket.seq < windowBase || inPacket.seq > (windowBase	+ cwnd)) {
					fprintf(stderr, "Received an ACK which is not within current window\n");
					exit(1);
				}
				newACK = true;
			}		


			int boundary = windowBase + cwnd;
			if (boundary > totPackets) {
				boundary = totPackets;
			}

			for (curSeqNum = windowBase; curSeqNum < boundary; curSeqNum++ ) {		// SEND or receive ACKs for each packet in window
				
				printf("\n\n IN FOR LOOP: \n window base = %d, \ncurrent seq number = %d, \ncurrent sequence number sent yet? %d,\ncurrent sequence number ACKed yet? %d,\nreceived packet? %d \n \n", windowBase, curSeqNum, sentPackets[curSeqNum], ACKED[curSeqNum], recPacket);

				if(! sentPackets[curSeqNum])						// not yet sent
				{
					// CREATE AND SEND PACKET

					memset((char*)&outPacket, 0, sizeof(outPacket));

					outPacket.type = DATA;
					outPacket.seq = curSeqNum;

					curSeqPos = curSeqNum * DATA_SIZE;

					if (file_size - curSeqPos >= DATA_SIZE) 
					{ 
						outPacket.size = DATA_SIZE; 
					}
					else { 
						outPacket.size = file_size - curSeqPos; 
					}

					memcpy(outPacket.data, file_buf + curSeqPos, outPacket.size);
		
					if (sendto(sockfd, &outPacket, sizeof(outPacket), 0, (struct sockaddr *)&cli_addr, clen) == -1)
					{
						fprintf(stderr, "Error sending data packet\n");
						exit(1);
					}
					printf("Sent  (type: %c, seq: %d, size: %d)\n",
							outPacket.type, outPacket.seq, outPacket.size);

					sentPackets[curSeqNum] = 1;
					generalTimer = std::clock();
					
					// START TIMER
					pktTimers[curSeqNum] = std::clock();
				}
				else if( newACK && inPacket.seq == curSeqNum) {		// ACKed
					ACKED[curSeqNum] = 1;
				}
				else if(!ACKED[curSeqNum]) {						// unACKed
					timePassed = ( std::clock() - pktTimers[curSeqNum] ) / (double) CLOCKS_PER_SEC;
					
					if (timePassed > 0.5) { 	// timed out-- resend packet
						
						// RESEND PACKET
						memset((char*)&outPacket, 0, sizeof(outPacket));

						outPacket.type = DATA;
						outPacket.seq = curSeqNum;

						curSeqPos = curSeqNum * DATA_SIZE;

						if (file_size - curSeqPos >= DATA_SIZE) 
						{ 
							outPacket.size = DATA_SIZE; 
						}
						else { 
							outPacket.size = file_size - curSeqPos; 
						}

						memcpy(outPacket.data, file_buf + curSeqPos, outPacket.size);
			
						if (sendto(sockfd, &outPacket, sizeof(outPacket), 0, (struct sockaddr *)&cli_addr, clen) == -1)
						{
							fprintf(stderr, "Error sending data packet\n");
							exit(1);
						}	
						printf("Resent  (type: %c, seq: %d, size: %d)\n",
							outPacket.type, outPacket.seq, outPacket.size);
						generalTimer = std::clock();

						// RESTART TIMER
						pktTimers[curSeqNum] = std::clock();
					}

				}

			}
		}

		// all data packets sent, send FIN
		memset((char*)&outPacket, 0, sizeof(outPacket));
		outPacket.type = FIN;
		outPacket.seq = curSeqNum;
		outPacket.size = 0;

		if (sendto(sockfd, &outPacket, sizeof(outPacket), 0, (struct sockaddr *)&cli_addr, clen) == -1)
		{
			fprintf(stderr, "Error sending FIN packet\n");
			exit(1);
		}
		
		printf("Sent  (type: %c, seq: %d, size: %d \n",
				outPacket.type, outPacket.seq, outPacket.size);
		
		// wait for FIN ACK
		if (recvfrom(sockfd, &inPacket, sizeof(inPacket), 0, (struct sockaddr *)&cli_addr, (socklen_t *)&clen) == -1)
		{
			fprintf(stderr, "Error receiving ACK packet\n");
			exit(1);
		}	
		printf("Recvd (type: %c, seq: %d, size: %d)\n",
				inPacket.type, inPacket.seq, inPacket.size);
				
		printf("Connection closed\n");
		free(file_buf);
	}

	close(sockfd);
	return 0;
}
