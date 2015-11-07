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
}; 

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
	media_frame *frame ;
	void *data;

	frame = capture_get_frame(stream);
	data = capture_frame_data(frame);
	
	size_t size = capture_frame_size(frame); 
	syslog(LOG_INFO, "Image captured");
	
	int total_size = size;
	syslog(LOG_INFO, "Total size %d", total_size);
	total_size = htonl(total_size);
/*
	First write the total size of the image in bytes.
	htonl stands for "host to network long".
	It turns the numbers the endian-ness of the numbers from the host format to the network format
	*/
	write(connfd, &total_size, sizeof(total_size));
	syslog(LOG_INFO, "Header sent");

//	Then send the data of the image
	int row = 0;
	unsigned char rowData[size];
	for (row = 0; row < size; row++)
	{
			rowData[row] = ((unsigned char *) data)[row];
		
	}
//	encrypt the rowData
	char *keyword = "sinyata lavina pak leti napred!";
	encryptFrame(rowData, size, keyword);

	write(connfd, rowData, sizeof(rowData));
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
		uint32_t numberOfImages = readInt(connfd);

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

		syslog(LOG_INFO, "Image sent");
		close(connfd);
		exit(EXIT_SUCCESS);
	}
}
