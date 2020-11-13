//Client side

#include <stdio.h>		
#include <sys/socket.h>		
#include <arpa/inet.h>	
#include <stdlib.h>	
#include <string.h>		
#include <unistd.h>	
#include <errno.h>	
#include <signal.h>	
#include "gbnpacket.c" //include go-back-n packet structre

#define TIMEOUT_SECS    3	  // Seconds between retransmits 
#define MAXTRIES        10	// max try

int tries = 0;			// menghitung try saat mengirim packet
int base = 0;
int windowSize = 0;
int sendflag = 1;

void DieWithError (char *errorMessage);	
void CatchAlarm (int ignored);	
int max (int a, int b);		
int min(int a, int b);		

int main (int argc, char *argv[])
{
  int sock;		
  struct sockaddr_in gbnServAddr;	
  struct sockaddr_in fromAddr;	
  unsigned short gbnServPort;
  unsigned int fromSize;	
  struct sigaction myAction;	
  char *servIP;		
   int respLen;			
  int packet_received = -1;	
  int packet_sent = -1;		

  char buffer[8192] =	"Test, ini data yang dikirim.";	// data yang dikirim
  const int datasize = 8192;	//ukuran total data buffer 
  int chunkSize;		// ukuran 1 packet
  int nPackets = 0;		// jumlah packet yang dikirim
  
  if (argc != 5)		// check jumlah argumen
  {
      fprintf (stderr,"Usage: %s <Server IP> <Server Port No> <Chunk size> <Window Size>\n",argv[0]);
      exit (1);
  }

  servIP = argv[1];		//argumen pertama IP Adress local
  chunkSize = atoi (argv[3]);	//argumen ketiga chunksize
  gbnServPort = atoi (argv[2]);	//argumen kedua port
  windowSize = atoi (argv[4]); //argumen keempat window size (n)
  if(chunkSize >= 512)
  {
    fprintf(stderr, "chunk size must be less than 512\n");
    exit(1);
  }
  nPackets = datasize / chunkSize; //menentukan jumlah packet dari datasize/chunkSize

  if (datasize % chunkSize) nPackets++;			
  //nPackets=8;
  
  printf("total packet : %d\n",nPackets);

  //create socket
  if ((sock = socket (PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
    DieWithError ("socket() failed");
  printf ("created socket\n");

  myAction.sa_handler = CatchAlarm;
  if (sigfillset (&myAction.sa_mask) < 0)	
    DieWithError ("sigfillset() failed");
  myAction.sa_flags = 0;

  if (sigaction (SIGALRM, &myAction, 0) < 0)
    DieWithError ("sigaction() failed for SIGALRM");

  //local addres structure
  memset (&gbnServAddr, 0, sizeof (gbnServAddr));	
  gbnServAddr.sin_family = AF_INET;
  gbnServAddr.sin_addr.s_addr = inet_addr (servIP);	// Server IP address local
  gbnServAddr.sin_port = htons (gbnServPort);

  // Send Data to the server 
  while ((packet_received < nPackets-1) && (tries < MAXTRIES))
  {
      if (sendflag > 0)
	    {
	      sendflag = 0;
	      int ctr; //window size counter
	      for (ctr = 0; ctr < windowSize; ctr++)
	      {
	        packet_sent = min(max (base + ctr, packet_sent),nPackets-1);
	        struct gbnpacket currpacket; 
	        if ((base + ctr) < nPackets)
		      {
		        memset(&currpacket,0,sizeof(currpacket));
 		        printf ("sending packet %d packet_sent %d packet_received %d\n",base+ctr, packet_sent, packet_received+1);

            currpacket.type = htonl (1);
            currpacket.seq_no = htonl (base + ctr);
            int currlength;
            if ((datasize - ((base + ctr) * chunkSize)) >= chunkSize) 
              currlength = chunkSize;
            else
              currlength = datasize % chunkSize;
            currpacket.length = htonl (currlength);
		        memcpy (currpacket.data,buffer + ((base + ctr) * chunkSize), currlength);
		        if (sendto(sock, &currpacket, (sizeof (int) * 3) + currlength, 0, (struct sockaddr *) &gbnServAddr,
		        sizeof (gbnServAddr)) !=	((sizeof (int) * 3) + currlength))
		          DieWithError("sendto() sent a different number of bytes than expected");
		      }
	      }
	    }
      // Get a response

      fromSize = sizeof (fromAddr);
      alarm (TIMEOUT_SECS);	
      struct gbnpacket currAck;
      while ((respLen = (recvfrom (sock, &currAck, sizeof (int) * 3, 0,(struct sockaddr *) &fromAddr,&fromSize))) < 0)

	      if (errno == EINTR)	
	      {
	        if (tries < MAXTRIES)	
	        {
		         printf ("timed out, %d more tries...\n", MAXTRIES - tries);
		        break;
	        }
	        else DieWithError ("No Response");
	      }
	      else DieWithError ("recvfrom() failed");

      if (respLen)
	    {
	      int acktype = ntohl (currAck.type); /* convert to host byte order */
	      int ackno = ntohl (currAck.seq_no); 
	      if (ackno > packet_received && acktype == 2)
	      {
	        packet_received++;
	        printf ("received ack %d\n", packet_received); /* receive/handle ack */
	        base = packet_received+1; /* handle new ack */
	        if (packet_received == packet_sent) /* all sent packets acked */
		      {
		        alarm (0); /* clear alarm */
		        tries = 0;
		        sendflag = 1;
		      }
	        else /* not all sent packets acked */
		      {
		        tries = 0; /* reset retry counter */
		        sendflag = 0;
		        alarm(TIMEOUT_SECS); /* reset alarm */

		      }
	      }
	    }
  }
  int ctr;
  for (ctr = 0; ctr < 10; ctr++) //data terkirim
  {
      struct gbnpacket teardown;
      teardown.type = htonl (4);
      teardown.seq_no = htonl (0);
      teardown.length = htonl (0);
      sendto (sock, &teardown, (sizeof (int) * 3), 0,(struct sockaddr *) &gbnServAddr, sizeof (gbnServAddr));
  }
  close (sock); //close socket
  exit (0);
}

void CatchAlarm (int ignored)	/* Handler for SIGALRM */
{
  tries += 1;
  sendflag = 1;
}

void DieWithError (char *errorMessage)
{
  perror (errorMessage);
  exit (1);
}

int max (int a, int b)
{
  if (b > a)
    return b;
  return a;
}

int min(int a, int b)
{
  if(b>a)
	return a;
  return b;
}
