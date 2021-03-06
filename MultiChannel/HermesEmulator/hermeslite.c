/*
      Hermes Lite 
	  
	  Radioberry implementation
	  
	  2016 Johan PA3GSB
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <math.h>
#include <semaphore.h>

#include <pigpio.h>

void runHermesLite(void);
void sendPacket(void);
void handlePacket(char* buffer);
void readPackets(void);
void fillDiscoveryReplyMessage(void);
int isValidFrame(char* data);
void fillPacketToSend(void);
void init(void);
void generateIQ(void);
void put(unsigned char  value);
unsigned char get(void);
void *spiReader(void *arg);
void *packetreader(void *arg);

void *spiWriter(void *arg);
unsigned char get_tx_buffer(void);

double timebase = 0.0;

	double amplitude;
	double noiseAmplitude;
	int vfo = 14250000;

sem_t empty;
sem_t full;
#define MAX 14400

unsigned char buffer[MAX];
int fill = 0; 
int use  = 0;

#define TX_MAX 3200 
unsigned char tx_buffer[TX_MAX];
int fill_tx = 0; 
int use_tx  = 0;
unsigned char drive_level;
unsigned char prev_drive_level;
int MOX = 0;
sem_t tx_empty;
sem_t tx_full;
sem_t mutex;

static int rx1_spi_handler;
static int rx2_spi_handler;

static const int CHANNEL = 0;
int fdspi;
unsigned char iqdata[6];
unsigned char tx_iqdata[6];
unsigned char audiooutputbuffer[4096];
int audiocounter = 0;

int nnrx =0;

#define SERVICE_PORT	1024

int hold_nrx=0;
int nrx = 4; // n Receivers

int holdfreq = 0;
int holdfreq2 = 0;
int holdtxfreq = 0;
int freq = 1008000;
int freq1 = 1008000;
int freq2 = 1395000;
int freq3 = 1395000;
int freq4 = 1395000;

int freqArray[4] = { 1008000, 1008000, 1008000, 1008000 };
int holdFreqArray[4] = { 1008000, 1008000, 1008000, 1008000 };

int txfreq = 3630000;

int att = 0;
int holdatt =128;
int holddither=128;
int dither = 0;
int rando = 0;
int sampleSpeed = 0;

unsigned char SYNC = 0x7F;
int last_sequence_number = 0;

unsigned char hpsdrdata[1032];
unsigned char broadcastReply[60];
#define TIMEOUT_MS      100     

int running = 0;
int fd;									/* our socket */

struct sockaddr_in myaddr;				/* our address */
struct sockaddr_in remaddr;				/* remote address */

socklen_t addrlen = sizeof(remaddr);	/* length of addresses */
int recvlen;							/* # bytes received */

struct timeval t0;
struct timeval t1;
struct timeval t10;
struct timeval t11;
struct timeval t20;
struct timeval t21;
float elapsed;

float timedifference_msec(struct timeval t0, struct timeval t1)
{
    return (t1.tv_sec - t0.tv_sec) * 1000.0f + (t1.tv_usec - t0.tv_usec) / 1000.0f;
}

int main(int argc, char **argv)
{
	sem_init(&empty, 0, MAX / 6); 
    sem_init(&full, 0, 0); 
	sem_init(&mutex, 0, 1);	//mutal exlusion

	if (gpioInitialise() < 0) {
		fprintf(stderr,"hpsdr_protocol (original) : gpio could not be initialized. \n");
		exit(-1);
	}
	
	gpioSetMode(13, PI_INPUT); 	//rx1_FIFOEmpty
	gpioSetMode(16, PI_INPUT);	//rx2_FIFOEmpty
	gpioSetMode(20, PI_INPUT); 
	gpioSetMode(21, PI_OUTPUT); 
	
	rx1_spi_handler = spiOpen(0, 15625000, 49155);  //channel 0
	if (rx1_spi_handler < 0) {
		fprintf(stderr,"radioberry_protocol: spi bus rx1 could not be initialized. \n");
		exit(-1);
	}
	
	printf("init done \n");
		
	pthread_t pid, pid2;
    pthread_create(&pid, NULL, spiReader, NULL);  
	pthread_create(&pid2, NULL, packetreader, NULL); 

	/* create a UDP socket */
	if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		perror("cannot create socket\n");
		return 0;
	}
	struct timeval timeout;      
    timeout.tv_sec = 0;
    timeout.tv_usec = TIMEOUT_MS;

	if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,(char*)&timeout,sizeof(timeout)) < 0)
		perror("setsockopt failed\n");
		
	/* bind the socket to any valid IP address and a specific port */
	memset((char *)&myaddr, 0, sizeof(myaddr));
	myaddr.sin_family = AF_INET;
	myaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	myaddr.sin_port = htons(SERVICE_PORT);

	if (bind(fd, (struct sockaddr *)&myaddr, sizeof(myaddr)) < 0) {
		perror("bind failed");
		return 0;
	}
	runHermesLite();
	
	if (rx1_spi_handler !=0)
		spiClose(rx1_spi_handler);
	
	gpioTerminate();
}

void runHermesLite() {
	printf("runHermesLite \n");
	
	int count = 0;
	gettimeofday(&t10, 0);
	for (;;) {
		
		if (running) {
			sendPacket();
		}
	}
}
void *packetreader(void *arg) {
	while(1) {
		readPackets();
	}
}

void readPackets() {
	unsigned char buffer[2048];
	
	recvlen = recvfrom(fd, buffer, sizeof(buffer), 0, (struct sockaddr *)&remaddr, &addrlen);
	if (recvlen > 0) 
		handlePacket(buffer);
}

int att11 = 0;
int prevatt11 = 0;
int att523 = 0;
int prevatt523 = 0;

void handlePacket(char* buffer){

	if (buffer[2] == 2) {
		printf("Discovery packet received \n");
		printf("IP-address %d.%d.%d.%d  \n", 
							remaddr.sin_addr.s_addr&0xFF,
                            (remaddr.sin_addr.s_addr>>8)&0xFF,
                            (remaddr.sin_addr.s_addr>>16)&0xFF,
                            (remaddr.sin_addr.s_addr>>24)&0xFF);
		printf("Discovery Port %d \n", ntohs(remaddr.sin_port));
		
		fillDiscoveryReplyMessage();
		
		if (sendto(fd, broadcastReply, sizeof(broadcastReply), 0, (struct sockaddr *)&remaddr, addrlen) < 0)
			printf("error sendto");
		
	} else if (buffer[2] == 4) {
			if (buffer[3] == 1 || buffer[3] == 3) {
				printf("Start Port %d \n", ntohs(remaddr.sin_port));
				running = 1;
				printf("SDR Program sends Start command \n");
				return;
			} else {
				fill = 0;
				use = 0;
				running = 0;
				last_sequence_number = 0;
				printf("SDR Program sends Stop command \n");
				return;
			}
		}
	if (isValidFrame(buffer)) {
	
		 MOX = ((buffer[11] & 0x01)==0x01) ? 1:0;
	
		if ((buffer[11] & 0xFE)  == 0x14) {
			att = (buffer[11 + 4] & 0x1F);
			att11 = att;
		}
		
		if ((buffer[523] & 0xFE)  == 0x14) {
			att = (buffer[523 + 4] & 0x1F);
			att523 = att;
		}
	
		if ((buffer[11] & 0xFE)  == 0x00) {
			nrx = (((buffer[11 + 4] & 0x38) >> 3) + 1);
			
			sampleSpeed = (buffer[11 + 1] & 0x03);
			
			dither = 0;
			if ((buffer[11 + 3] & 0x08) == 0x08)
				dither = 1; 
						
			rando = 0;
			if ((buffer[11 + 3] & 0x10) == 0x10)
				rando = 1;
		}
		
		if ((buffer[523] & 0xFE)  == 0x00) {
			
			dither = 0;
			if ((buffer[523 + 3] & 0x08) == 0x08)
				dither = 1; 
					
			rando = 0;
			if ((buffer[523 + 3] & 0x10) == 0x10)
				rando = 1;
		}
		// Powersdr and Alans software are following a different patttern
		// this program does not known which package is calling 
		// by looking which value is changing... that will be the att value
		if (prevatt11 != att11) 
		{
			att = att11;
			prevatt11 = att11;
		}
		if (prevatt523 != att523) 
		{
			att = att523;
			prevatt523 = att523;
		}
			
		if ((buffer[11] & 0xFE)  == 0x00) {
			nrx = (((buffer[11 + 4] & 0x38) >> 3) + 1);
		}
		if ((buffer[523] & 0xFE)  == 0x00) {
			nrx = (((buffer[523 + 4] & 0x38) >> 3) + 1);
		}
		if (hold_nrx != nrx) {
			hold_nrx=nrx;
			printf("aantal rx %d \n", nrx);
		}
		
		// select Command
		if ((buffer[11] & 0xFE) == 0x02)
        {
            txfreq = ((buffer[11 + 1] & 0xFF) << 24) + ((buffer[11+ 2] & 0xFF) << 16)
                    + ((buffer[11 + 3] & 0xFF) << 8) + (buffer[11 + 4] & 0xFF);
        }
        if ((buffer[523] & 0xFE) == 0x02)
        {
            txfreq = ((buffer[523 + 1] & 0xFF) << 24) + ((buffer[523+ 2] & 0xFF) << 16)
                    + ((buffer[523 + 3] & 0xFF) << 8) + (buffer[523 + 4] & 0xFF);
        }
		
		if ((buffer[11] & 0xFE) == 0x04)
        {
            freq1 = ((buffer[11 + 1] & 0xFF) << 24) + ((buffer[11+ 2] & 0xFF) << 16)
                    + ((buffer[11 + 3] & 0xFF) << 8) + (buffer[11 + 4] & 0xFF);
					
			freqArray[0] = freq1;
        }
        if ((buffer[523] & 0xFE) == 0x04)
        {
            freq1 = ((buffer[523 + 1] & 0xFF) << 24) + ((buffer[523+ 2] & 0xFF) << 16)
                    + ((buffer[523 + 3] & 0xFF) << 8) + (buffer[523 + 4] & 0xFF);
					
			freqArray[0] = freq1;
        }
		
		if ((buffer[11] & 0xFE) == 0x06)
        {
            freq2 = ((buffer[11 + 1] & 0xFF) << 24) + ((buffer[11+ 2] & 0xFF) << 16)
                    + ((buffer[11 + 3] & 0xFF) << 8) + (buffer[11 + 4] & 0xFF);
			
			freqArray[1] = freq2;
        }
        if ((buffer[523] & 0xFE) == 0x06)
        {
            freq2 = ((buffer[523 + 1] & 0xFF) << 24) + ((buffer[523+ 2] & 0xFF) << 16)
                    + ((buffer[523 + 3] & 0xFF) << 8) + (buffer[523 + 4] & 0xFF);
					
			freqArray[1] = freq2;
        }
		
		if ((buffer[11] & 0xFE) == 0x08)
        {
            freq3 = ((buffer[11 + 1] & 0xFF) << 24) + ((buffer[11+ 2] & 0xFF) << 16)
                    + ((buffer[11 + 3] & 0xFF) << 8) + (buffer[11 + 4] & 0xFF);
					
			freqArray[2] = freq3;
        }
        if ((buffer[523] & 0xFE) == 0x08)
        {
            freq3 = ((buffer[523 + 1] & 0xFF) << 24) + ((buffer[523+ 2] & 0xFF) << 16)
                    + ((buffer[523 + 3] & 0xFF) << 8) + (buffer[523 + 4] & 0xFF);
					
			freqArray[2] = freq3;
        }
		
		if ((buffer[11] & 0xFE) == 0x0A)
        {
            freq4 = ((buffer[11 + 1] & 0xFF) << 24) + ((buffer[11+ 2] & 0xFF) << 16)
                    + ((buffer[11 + 3] & 0xFF) << 8) + (buffer[11 + 4] & 0xFF);
					
			freqArray[3] = freq4;
        }
        if ((buffer[523] & 0xFE) == 0x0A)
        {
            freq4 = ((buffer[523 + 1] & 0xFF) << 24) + ((buffer[523+ 2] & 0xFF) << 16)
                    + ((buffer[523 + 3] & 0xFF) << 8) + (buffer[523 + 4] & 0xFF);
					
			freqArray[3] = freq4;
        }

        // select Command
        if ((buffer[523] & 0xFE) == 0x12)
        {
            drive_level = buffer[524];  
        }
		
		if ((holdatt != att) || (holddither != dither)) {
			holdatt = att;
			holddither = dither;
			printf("att =  %d ", att);printf("dither =  %d ", dither);printf("rando =  %d ", rando);
			printf("code =  %d \n", (((rando << 6) & 0x40) | ((dither <<5) & 0x20) |  (att & 0x1F)));
			printf("att11 = %d and att523 = %d\n", att11, att523);
		}
		
		int x=0;
		for (x; x < nrx; x++) {
			if (freqArray[x] != holdFreqArray[x]) {
				printf("frequency rx%d - %d en aantal rx %d \n", x, freqArray[x] , nrx);
				holdFreqArray[x] = freqArray[x];
			}
		}
		
		//lees data en vul buffers
		int frame = 0;
		for (frame; frame < 2; frame++)
		{
			int coarse_pointer = frame * 512 + 8;

			int j = 8;
			for (j; j < 512; j += 8)
			{
				int k = coarse_pointer + j;

				// M  (MSB first) L and R channel 2 * 16 bits
				// send data to audio driver...beter latency......than using VAC.
				audiooutputbuffer[audiocounter++] = buffer[k + 0];
				audiooutputbuffer[audiocounter++] = buffer[k + 1];
				audiooutputbuffer[audiocounter++] = buffer[k + 2];
				audiooutputbuffer[audiocounter++] = buffer[k + 3];
				if (audiocounter ==  1024) {
					audiocounter = 0;
				}
	
				// TX IQ
				//MSB first according to protocol. (I and Q samples 2 * 16 bits)
				if (MOX) {
				
					//while ( gpioRead(20) == 1) {};	// wait if TX buffer is full.
					
					sem_wait(&tx_empty);
					int i = 0;
					for (i; i < 4; i++){
						//put_tx_buffer(buffer[k + 4 + i]);			
					}
					sem_post(&tx_full);
				}
			}
		}
	}
}

void sendPacket() {
	fillPacketToSend();
	
	if (sendto(fd, hpsdrdata, sizeof(hpsdrdata), 0, (struct sockaddr *)&remaddr, addrlen) < 0)
			printf("error sendto");
}


int isValidFrame(char* data) {
	return (data[8] == SYNC && data[9] == SYNC && data[10] == SYNC && data[520] == SYNC && data[521] == SYNC && data[522] == SYNC);
}

static int started=0;
void fillPacketToSend() {
		
		memset(&hpsdrdata[0], 0, sizeof(hpsdrdata));
		hpsdrdata[0] = 0xEF;
		hpsdrdata[1] = 0xFE;
		hpsdrdata[2] = 0x01;
		hpsdrdata[3] = 0x06;
		hpsdrdata[4] = ((last_sequence_number >> 24) & 0xFF);
		hpsdrdata[5] = ((last_sequence_number >> 16) & 0xFF);
		hpsdrdata[6] = ((last_sequence_number >> 8) & 0xFF);
		hpsdrdata[7] = (last_sequence_number & 0xFF);
		last_sequence_number++;

		int factor = (nrx - 1) * 6;
		int index;
		int frame = 0;
		for (frame; frame < 2; frame++) {
			int coarse_pointer = frame * 512; // 512 bytes total in each frame
			hpsdrdata[8 + coarse_pointer] = SYNC;
			hpsdrdata[9 + coarse_pointer] = SYNC;
			hpsdrdata[10 + coarse_pointer] = SYNC;
			hpsdrdata[11 + coarse_pointer] = 0x00; // c0
			hpsdrdata[12 + coarse_pointer] = 0x00; // c1
			hpsdrdata[13 + coarse_pointer] = 0x00; // c2
			hpsdrdata[14 + coarse_pointer] = 0x00; // c3
			hpsdrdata[15 + coarse_pointer] = 0x1D; // c4 //v2.9

			int j = 0;
			for (j; j < (504 / (8 + factor)); j++) {
				index = 16 + coarse_pointer + (j * (8 + factor));

				if (!MOX) {
				
					int nrrx=0;
					for (nrrx ; nrrx < nrx; nrrx++) {
						
						sem_wait(&full);   
						
						int i =0;
						for (i; i< 6; i++) {			
							hpsdrdata[index + i + (6 * nrrx)] = get(); // MSB comes first!!!!
						}
						
						sem_post(&empty);
					}						
				} else {
					//required to fill data ?...mic data ... maybe in future reading data from audio usb....or adding TLV codec??
					
					//Modulation LF
                     //hpsdrdata[index + 7 + factor] = tx_Audio_buffer.Read();// LSB; comes first!!!!
                     //hpsdrdata[index + 6 + factor] = tx_Audio_buffer.Read();
					int i =0;
					for (i; i< 6; i++){
						hpsdrdata[index + i] = 0x00;
					}
					hpsdrdata[index + 7] = 0x00;	// LSB
					hpsdrdata[index + 6] = 0x00;
					
					//usleep(1); // sleep required????
					
				}
			}
			if (MOX){
				if (sampleSpeed ==0)
					usleep(620);  // use pin...to indicate status...//usleep(620);
				if (sampleSpeed == 1)
					usleep(260); 
			}
		}
}

void fillDiscoveryReplyMessage() {
	int i = 0;
	for (i; i < 60; i++) {
		broadcastReply[i] = 0x00;
	}
	i = 0;
	broadcastReply[i++] = 0xEF;
	broadcastReply[i++] = 0xFE;
	broadcastReply[i++] = 0x02;

	broadcastReply[i++] =  0x00; // MAC
	broadcastReply[i++] =  0x01;
	broadcastReply[i++] =  0x02;
	broadcastReply[i++] =  0x03;
	broadcastReply[i++] =  0x04;
	broadcastReply[i++] =  0x05;
	broadcastReply[i++] =  31;
	broadcastReply[i++] =  6; //0x10; // Hermes boardtype public static final
									// int DEVICE_HERMES_LITE = 6;
}

void *spiReader(void *arg) {
	
	long slack = 0;
	
	long long value = 0;
	long long prevalue = 0;
		nnrx = 0;
	int count =0;
	gettimeofday(&t0, 0);
	while(1) {
	
		
		if (!MOX) {
			sem_wait(&mutex); 
			
			nnrx = nnrx + 1; 
			if ((nnrx % nrx) == 0)	nnrx = 0;			
			
			freq = freqArray[nnrx];

			while ( gpioRead(13) == 1) {slack++;}; // wait till rxFIFO buffer is filled with at least one element
			//00 0000 ---  01 0100  --- 10 1000  --- 11  1100
			iqdata[0] = (((nnrx << 2) & 0x0C)  | (sampleSpeed & 0x03));
			iqdata[1] = (((rando << 6) & 0x40) | ((dither <<5) & 0x20) |  (att & 0x1F));
			iqdata[2] = ((freq >> 24) & 0xFF);
			iqdata[3] = ((freq >> 16) & 0xFF);
			iqdata[4] = ((freq >> 8) & 0xFF);
			iqdata[5] = (freq & 0xFF);
					
			spiXfer(rx1_spi_handler, iqdata, iqdata, 6);
			//firmware: tdata(56'h00010203040506) -> 0-1-2-3-4-5-6 (element 0 contains 0; second element contains 1)
			//sem_wait(&empty);
			
			int i =0;
			value = 0;
			for (i; i< 6; i++){
					put(iqdata[i]);
					//printf("%x ", iqdata[i]);
					//value = (((iqdata[i] & 0xFF) << (i * 8)) ) | value;
			}
			//printf("value = %d \n", value);
						
			//sem_post(&full);
		
			count ++;
			if (count == 48000) {
				count = 0;
				gettimeofday(&t1, 0);
				elapsed = timedifference_msec(t0, t1);
				printf("Code rx mode spi executed in %f milliseconds.\n", elapsed);
				printf("number of empt reads %d \n", slack);
				slack = 0;
				gettimeofday(&t0, 0);
			}
			
			sem_post(&mutex);
		}
	}
	
}

void put(unsigned char  value) {
    buffer[fill] = value;    
    fill = (fill + 1) % MAX; 
}

unsigned char get() {
    int tmp = buffer[use];   
    use = (use + 1) % MAX;   
    return tmp;
}


