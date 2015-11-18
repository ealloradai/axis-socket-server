#define _GNU_SOURCE
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <time.h> 
#include <syslog.h>

#include <sys/time.h>
#include <capture.h>

#include <glib.h>
#include <gio/gio.h>

#define APP_NAME "axisSocketServer"

char * generateRandomString(int length) 
{
	const char *alphabet = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRTSUVWXYZ0123456789";
	int alphabetLength = strlen(alphabet);
	char* stringbuffer = malloc((length + 1) * sizeof(char));
	
	int i;
	for(i = 0; i < length; ++i)
	{
		int random = rand() % alphabetLength;
		char ch = alphabet[random];
		stringbuffer[i] = ch;
	}
	stringbuffer[length] = '\0';
	return stringbuffer;	
}

void encryptFrame(unsigned char rowData[], size_t size, const char *keyword) 
{
	syslog(LOG_INFO, "Encrypting with keyword %s", keyword);
	int i = 0;
	int keywordLength = strlen(keyword);
	for(i = 0; i < size; ++i) 
	{
		char keywordChar = keyword[i % keywordLength];
		rowData[i] = rowData[i] ^ keywordChar;
	}
}

void sendImageFromStream(media_stream *stream, int connfd, char* encKey, uint32_t maxDelay, int imageNumber)
{
	media_frame *frame;
	void *data;

	frame = capture_get_frame(stream);
	data = capture_frame_data(frame);
	
	size_t size = capture_frame_size(frame); 
	syslog(LOG_INFO, "Image captured");
	
	int total_size = size;
	syslog(LOG_INFO, "Total size %d", total_size);
	total_size = htonl(total_size);

//	Then send the data of the image
	int row = 0;
	unsigned char rowData[size];
	for (row = 0; row < size; row++)
	{
			rowData[row] = ((unsigned char *) data)[row];
	}
	
	if(encKey) {
		encryptFrame(rowData, size, encKey);
	}
	
	int randDelay = rand()%maxDelay;
	syslog(LOG_INFO, "Sleeping with random delay %d", randDelay);
	sleep(randDelay);
	
	imageNumber = htonl(imageNumber);
	
//	g_mutex_lock(mutex);
	write(connfd, &imageNumber, sizeof(imageNumber));
	write(connfd, &total_size, sizeof(total_size));
	write(connfd, rowData, sizeof(rowData));
//	g_mutex_unlock(mutex);
}


/*
 *	Reads an integer from the socket.
 *	Calls the read() function in a loop to make sure that all 4 bytes of the int are read.
 */
uint32_t readInt(int connfd)
{
	int bytesToRead = sizeof(uint32_t);
	unsigned char buff[bytesToRead];
	int bytesRead = 0, r = 0;
	while(bytesToRead > 0)
	{
		r = read(connfd, buff + bytesRead, bytesToRead);
		if(r < 0)
		{
			break;
		}
		bytesRead += r;
		bytesToRead -= r;
	}

	uint32_t *resultP = (uint32_t* ) buff;
	// ntohl stands for "network to host long"
	// The opposite of htonl
	uint32_t result = ntohl(*resultP);
	return result;
}

int main(int argc, char *argv[])
{
	openlog(APP_NAME, LOG_PID, LOG_LOCAL4);

	srand ( time(NULL) );
	int listenfd = 0, connfd = 0;
	struct sockaddr_in serv_addr;

	char sendBuff[1025];

//	Create the socket
	listenfd = socket(AF_INET, SOCK_STREAM, 0);
	memset(&serv_addr, '0', sizeof(serv_addr));
	memset(sendBuff, '0', sizeof(sendBuff));

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(5000);

//	Bind the socket to its settings
	bind(listenfd, (struct sockaddr*) &serv_addr, sizeof(serv_addr));

	listen(listenfd, 10);

	while (1)
	{
/*
 * 		Wait for client connection.
 * 		connfd stands for Connection File Descriptor.
 * 		This is because in Linux everything you can write in, incl. sockets, is considered a file
 */
		connfd = accept(listenfd, (struct sockaddr*) NULL, NULL);
		syslog(LOG_INFO, "Client accepted");

		
		/*
		Read the four parameters: timeout, width, height and number of images
		Check readInt()
		*/
		uint32_t timeout = readInt(connfd);
		uint32_t width = readInt(connfd);
		uint32_t height = readInt(connfd);
		uint32_t maxDelay = readInt(connfd);
		uint32_t encrypt = readInt(connfd);
		
		char* encKey = NULL;
		if(encrypt != NULL && encrypt != 0) 
		{
			// generate key and send it to the client
			encKey = generateRandomString(25);
			int keyLen = strlen(encKey);
			keyLen = htonl(keyLen);
			write(connfd, &keyLen, sizeof(int));
			write(connfd, encKey, strlen(encKey)*sizeof(char));
		}
		
		syslog(LOG_INFO, "Fixed parameters received: %d, %d, %d, %d", timeout, width, height, maxDelay);		
		char streamParamsBuffer[100];

//		Build the stream parameters with the given width and height
		sprintf(streamParamsBuffer, "resolution=%dx%d&fps=15",  width, height);
		syslog(LOG_INFO, "%s", streamParamsBuffer);
		media_stream *stream;
//		Open the stream
		stream = capture_open_stream(IMAGE_JPEG, streamParamsBuffer);
		syslog(LOG_INFO, "Open stream captured 1");
		pid_t childPID;
		childPID = fork();
		if(childPID >= 0) 
		{
			srand(time(NULL) ^ (getpid()<<16));
			syslog(LOG_INFO, "forked");
			if(childPID == 0) 
			{
//				GMutex *mutex;
//				g_mutex_init(mutex);
				int i = 0;
				while(1)
				{
					int imagePid = fork();
					srand(time(NULL) ^ (getpid()<<16));
					if(imagePid == 0) 
					{
						sendImageFromStream(stream, connfd, encKey, maxDelay, i);
						exit(0);
					} 
					else 
					{
						sleep(timeout);
					}
				}

				syslog(LOG_INFO, "Images sent");
				close(connfd);
			}
		} 
		else 
		{
			syslog(LOG_INFO, "Fork failed");
		} 
	}
}
