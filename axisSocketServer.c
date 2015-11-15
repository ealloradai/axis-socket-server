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

struct ImageThreadParam {
   media_stream *stream;
   int connfd;
   char* encKey;
   uint32_t maxDelay;
   uint32_t encrypt;
   int imageNumber;
}; 

GMutex *mutex;
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
	syslog(LOG_INFO, "KEY %s", stringbuffer);
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


void sendImageFromStream(void *paramPtr)
{
	
	struct ImageThreadParam *param = (struct ImageThreadParam *) paramPtr;
	media_stream *stream = param->stream;
	int connfd = param->connfd; 
	char *keyword = param->encKey;	
	uint32_t maxDelay = param->maxDelay;
	uint32_t encrypt = param->encrypt;
	int imageNumber = param -> imageNumber;
	
	media_frame *frame ;
	void *data;

	frame = capture_get_frame(stream);
	data = capture_frame_data(frame);
	
	size_t size = capture_frame_size(frame); 
	syslog(LOG_INFO, "Image captured");
	
	//random delay
	int randDelay = rand()%maxDelay;
	syslog(LOG_INFO, "RANDOM DELAY IS %d", randDelay);
	sleep(randDelay);

	
//	Get the image bytes into rowData
	int row = 0;
	unsigned char rowData[size];
	for (row = 0; row < size; row++)
	{
			rowData[row] = ((unsigned char *) data)[row];
		
	}
//	encrypt the rowData if necessary
	if(encrypt != NULL && encrypt != 0) {
		encryptFrame(rowData, size, keyword);		
	}
	
	
	//lock the mutex for thread safety
	g_mutex_lock(mutex);
	
	/*
	 * First we send the image number
	 */
	syslog(LOG_INFO, "Image number %d", imageNumber);
	imageNumber = htonl(imageNumber);
	write(connfd, &imageNumber, sizeof(imageNumber));
	
/*
	Then write the total size of the image in bytes.
	htonl stands for "host to network long".
	It turns the numbers the endian-ness of the numbers from the host format to the network format
	*/
	int total_size = size;
	syslog(LOG_INFO, "Total size %d", total_size);
	total_size = htonl(total_size);
	write(connfd, &total_size, sizeof(total_size));
	write(connfd, rowData, sizeof(rowData));
	
//	unlock the mutex
	g_mutex_unlock(mutex);
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
	time_t t;
	srand((unsigned) time(&t));
	
	g_mutex_init(mutex);

	openlog(APP_NAME, LOG_PID, LOG_LOCAL4);
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
		
		uint32_t encrypt = readInt(connfd);
		char* encKey = NULL;
		if(encrypt != NULL && encrypt != 0) 
		{
			// generate key and send it to the client
			encKey = generateRandomString(25);
			int keyLen = strlen(encKey);
			syslog(LOG_INFO, "keylen %d", keyLen);
			syslog(LOG_INFO, "key %s", encKey);
			keyLen = htonl(keyLen);
			write(connfd, &keyLen, sizeof(int));
			write(connfd, encKey, strlen(encKey)*sizeof(char));
			syslog(LOG_INFO, "Encryption key sent");			
		}

		/*
		Read the four parameters: timeout, width, height and number of images
		Check readInt()
		*/
		uint32_t timeout = readInt(connfd);
		uint32_t width = readInt(connfd);
		uint32_t height = readInt(connfd);
		uint32_t numberOfImages = readInt(connfd);
		uint32_t maxDelay = readInt(connfd);

		syslog(LOG_INFO, "Fixed parameters received: %d, %d, %d, %d", timeout, width, height, numberOfImages);		
		char streamParamsBuffer[100];

//		Build the stream parameters with the given width and height
		sprintf(streamParamsBuffer, "resolution=%dx%d&fps=15",  width, height);
		syslog(LOG_INFO, "%s", streamParamsBuffer);


		media_stream *stream;
//		Open the stream
		stream = capture_open_stream(IMAGE_JPEG, streamParamsBuffer);
		syslog(LOG_INFO, "Open stream captured 1");
		int i = 0;
		for(i = 0; i < numberOfImages; ++i)
		{
			struct ImageThreadParam param;
			param.stream = stream;
			param.connfd = connfd;
			param.encKey = encKey;
			param.maxDelay = maxDelay;
			param.encrypt = encrypt;
			param.imageNumber = i;
			
			//every time open new thread - synchronization
			GThread * threadResult;
			threadResult = g_thread_new( "lala", sendImageFromStream, &param);
			if(!threadResult) 
			{
				syslog(LOG_INFO, "Error creating thread");
				exit(EXIT_FAILURE);
			} 
			syslog(LOG_INFO, "thread started");
			g_thread_join(threadResult);
			//sendImageFromStream(param);
			sleep(timeout);
		}
		//release the memory for malloc in random string generator
		if(encKey) {
			free(encKey);
		}
		syslog(LOG_INFO, "Images sent. Closing.");
		close(connfd);
		exit(EXIT_SUCCESS);
	}
}
