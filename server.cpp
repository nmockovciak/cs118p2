
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
	char *filename;
	Packet inPacket, outPacket;
	
	const int SIZE_MAX_SEQ = MAX_SEQUENCE_NUM/PACKET_SIZE;
	cwnd = WINDOW_SIZE/PACKET_SIZE;

	// get arguments from command line
	if (argc != 2)
	{
		fprintf(stderr, "ERROR: Incorrect input. Use commands of the following format: server <server portnumber> <filename>\n");
		exit(1);
	}
	port_num = atoi(argv[1]);
	
	// check arguments for errors
	if (port_num < 0)
		{
		fprintf(stderr, "ERROR: portnum cannot be negative\n");
		exit(1);
	}

	// create socket
	if ((sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
	{
		fprintf(stderr, "ERROR: could not open the socket\n");
		exit(1);
	}

	memset((char *)&srv_addr, 0, clen);
	srv_addr.sin_family = AF_INET;
	srv_addr.sin_port = htons(port_num);
	srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	
	// bind socket to port
	if (bind(sockfd, (struct sockaddr *) &srv_addr, clen) == -1)
	{
		fprintf(stderr, "ERROR: could not bind to the socket\n");
		exit(1);
	}


	
	//srand(time(NULL));
	
	while (true) {
		// process file request
		//printf("\nWaiting for file request\n");

		if (recvfrom(sockfd, &inPacket, PACKET_SIZE, 0, (struct sockaddr*)&cli_addr, (socklen_t *)&clen) == -1) {
			printf("WARNING: a problem occured while receiving the file request\n");
			continue;
		}
		else if (inPacket.type != REQ) {
			printf("WARNING: Received a non-request packet. Ignore\n");
			continue;
		}
		
		filename = inPacket.data;
		//printf("Received file request for \"%s\"\n", filename);
		
		FILE *fp = fopen(filename, "r");
		if (fp == NULL) { 
		// no such file, so send a FIN packet immediately
			memset((char*)&outPacket, 0, sizeof(outPacket));
			outPacket.type = FIN;
			outPacket.seq = 0;

			if (sendto(sockfd, &outPacket, sizeof(outPacket), 0, (struct sockaddr*)&cli_addr, clen) == -1)
			{
				fprintf(stderr, "ERROR: a problem occured while sending a FIN packet\n");
				exit(1);
			}
			printf("WARNING: The file does not exist.\n");
			continue;
		}

		//fprintf(stderr, "AFTER READIN FILE IN!\n");
		
		// read file into memory buffer
		fseek(fp, 0L, SEEK_END);
		long file_size = ftell(fp);
		fseek(fp, 0L, SEEK_SET);
		char *file_buf = (char*) malloc(sizeof(char) * file_size);
		fread(file_buf, sizeof(char), file_size, fp);
		
		fclose(fp);
	
		int totPackets = file_size/DATA_SIZE + (file_size % DATA_SIZE != 0);

		int windowBase = 0;
		int totPacketsSent = 0;

		int ACKED[SIZE_MAX_SEQ]; 					// keep track of unACKed packets
		int sentPackets[SIZE_MAX_SEQ];		// keep track of packets successfully sent

		memset((char*)&ACKED, 0, sizeof(int)*SIZE_MAX_SEQ);
		memset((char*)&sentPackets, 0, sizeof(int)*SIZE_MAX_SEQ);

		bool newACK = false;
		bool recPacket = false;

		std::clock_t pktTimers[SIZE_MAX_SEQ];
		memset((char*)&pktTimers, 0, sizeof(std::clock_t)*SIZE_MAX_SEQ);

		double timePassed;

		std::clock_t generalTimer = std::clock();

		int curSeqNum = 0;
		int curSeqPos = 0;

		fd_set set;
		struct timeval timeout = {0.25, 0}; // 250 ms timeout

		//printf("\n total number of packets = %d \n window size = %d \n", totPackets, cwnd);

		int SYNsent = 0;
		int indexCSN = 0;

		while (windowBase < totPackets) {	// sending all packets AND waiting for ACKs

			//printf("IN WHILE LOOP!  current seq num = %d windowBase = %d AND SIZE_MAX_SEQ = %d\n", indexCSN, windowBase%SIZE_MAX_SEQ, SIZE_MAX_SEQ);

			newACK = false;
			recPacket = false;

			if(ACKED[windowBase%SIZE_MAX_SEQ]) {
				//fprintf(stderr, "HERE 3 \n");
				// shift windowBase until first sequence number in window is unACKed
				while( windowBase < (totPackets) &&  ACKED[windowBase%SIZE_MAX_SEQ] ) {
					windowBase++;

					ACKED[(windowBase-1) % SIZE_MAX_SEQ] = 0;
					sentPackets[(windowBase-1) % SIZE_MAX_SEQ] = 0;
					pktTimers[(windowBase-1) % SIZE_MAX_SEQ] = 0;
				}
			}
			if (windowBase >= totPackets) { break; }

			// check if general timeout since last sent packet-- must be timeout somewhere, don't bother trying to receive a packet 
			timePassed = ( std::clock() - generalTimer ) / (double) CLOCKS_PER_SEC;
				

			FD_ZERO(&set);
			FD_SET(sockfd, &set);

			if (select(sockfd+1, &set, NULL, NULL, &timeout) > 0) {
				//fprintf(stderr, "HERE 4 \n");
				if(recvfrom(sockfd, &inPacket, sizeof(inPacket), 0, (struct sockaddr *)&cli_addr, (socklen_t *)&clen) == -1) {
					fprintf(stderr, "ERROR: a problem occured while retrieving a packet from the receiver\n");
					exit(1);
				}
				printf("Receiving packet %d\n",
				(inPacket.seq*DATA_SIZE));
				recPacket = true;
			}

			if( recPacket && inPacket.type != ACK ) {
				fprintf(stderr, "ERROR: Received a non-ACK packet\n");
				exit(1);
			}
			else if (recPacket){
				//fprintf(stderr, "HERE 5 \n");

				if (inPacket.seq < (windowBase%SIZE_MAX_SEQ)) {
					fprintf(stderr, "ERROR: Received an ACK outside the current window\n");
					exit(1);
				}
				
				if (((windowBase+ cwnd)%SIZE_MAX_SEQ) < cwnd) {

					if (inPacket.seq > ((windowBase+ cwnd)%SIZE_MAX_SEQ) && inPacket.seq < (windowBase%SIZE_MAX_SEQ)) {
						fprintf(stderr, "ERROR: Received an ACK outside the current window\n");
						exit(1);
					}
				} 
				else if (inPacket.seq > ((windowBase+ cwnd)%SIZE_MAX_SEQ)){
					fprintf(stderr, "ERROR: Received an ACK outside the current window\n");
					exit(1);
				}
				newACK = true;
			}		


			int boundary = windowBase + cwnd;
			if (boundary > totPackets) {
				//fprintf(stderr, "HERE 6 \n");
				boundary = totPackets;
			}

			for (curSeqNum = windowBase; curSeqNum < boundary; curSeqNum++ ) {		// SEND or receive ACKs for each packet in window
				//fprintf(stderr, "HERE 1 \n");
				//printf("\n\n IN FOR LOOP: \n window base = %d, \ncurrent seq number = %d, \ncurrent sequence number sent yet? %d,\ncurrent sequence number ACKed yet? %d,\nreceived packet? %d \n \n", windowBase, curSeqNum, sentPackets[curSeqNum], ACKED[curSeqNum], recPacket);

				indexCSN = curSeqNum % SIZE_MAX_SEQ;

				if(! sentPackets[indexCSN])						// not yet sent
				{
					// CREATE AND SEND PACKET
					// fprintf(stderr, "HERE 2 \n");

					memset((char*)&outPacket, 0, sizeof(outPacket));

					outPacket.type = DATA;
					outPacket.seq = indexCSN;

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
						fprintf(stderr, "ERROR: A problem occured while sending a data packet\n");
						exit(1);
					}
					
					if (SYNsent) {
						printf("Sending packet %d %d\n",
							(outPacket.seq * DATA_SIZE), (windowBase* DATA_SIZE)%MAX_SEQUENCE_NUM);
					}
					else {
						printf("Sending packet %d %d SYN\n",
							(outPacket.seq* DATA_SIZE), (windowBase* DATA_SIZE)% MAX_SEQUENCE_NUM);
						SYNsent = 1;
					}
					

					sentPackets[indexCSN] = 1;
					generalTimer = std::clock();
					
					// START TIMER
					pktTimers[indexCSN] = std::clock();
				}
				else if( newACK && inPacket.seq == indexCSN) {		// ACKed
					// fprintf(stderr, "HERE 3 \n");

					ACKED[indexCSN] = 1;
				}
				else if(sentPackets[indexCSN] && !ACKED[indexCSN]) {						// unACKed
					
					// fprintf(stderr, "HERE 4 \n");
					timePassed = ( std::clock() - pktTimers[indexCSN] ) / (double) CLOCKS_PER_SEC;
					
					if (timePassed > 0.5) { 	// timed out-- resend packet
						
						// RESEND PACKET
						memset((char*)&outPacket, 0, sizeof(outPacket));

						outPacket.type = DATA;
						outPacket.seq = indexCSN;

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
						printf("Sending packet %d %d Retransmission\n",
							(outPacket.seq* DATA_SIZE), (windowBase* DATA_SIZE)% MAX_SEQUENCE_NUM);
						generalTimer = std::clock();

						// RESTART TIMER
						pktTimers[indexCSN] = std::clock();
					}

				}

			}
		}

		// all data packets sent, send FIN
		memset((char*)&outPacket, 0, sizeof(outPacket));
		outPacket.type = FIN;
		outPacket.seq = curSeqNum % SIZE_MAX_SEQ;
		outPacket.size = 0;

		if (sendto(sockfd, &outPacket, sizeof(outPacket), 0, (struct sockaddr *)&cli_addr, clen) == -1)
		{
			fprintf(stderr, "ERROR: a problem occured while sending a FIN packet\n");
			exit(1);
		}
		
		printf("Sending packet %d %d FIN\n",
				(outPacket.seq* DATA_SIZE), (windowBase* DATA_SIZE)% MAX_SEQUENCE_NUM);
		
		// wait for FIN ACK
		if (recvfrom(sockfd, &inPacket, sizeof(inPacket), 0, (struct sockaddr *)&cli_addr, (socklen_t *)&clen) == -1)
		{
			fprintf(stderr, "Error receiving ACK packet\n");
			exit(1);
		}	
		printf("Receiving packet %d\n",
				(inPacket.seq * DATA_SIZE));
				
		//printf("Connection closed\n");
		free(file_buf);
	}

	close(sockfd);
	return 0;
}
