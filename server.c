#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <zlib.h>

#define BUFFER_LEN 255
#define MAX_CONNECTIONS 300
#define CHUNK 16384

const char *resp1 = "HTTP/1.1 200 OK\r\n"; // resp = resposne
const char *resp2 = "HTTP/1.1 404 Not Found\r\n\r\n";
const char *resp3 = "HTTP/1.1 201 Created\r\n\r\n";

char buffer[BUFFER_LEN];

struct str_manip{
	char *buff;
	char *port; // path 
	int fd; // file descriptor
	int argc;
	char **argv;
	char *cont; // file contents
};

pthread_mutex_t rp_mutex;

//Help from claude:
void to_hex(const unsigned char *data, size_t len, char *hex) {
    const char hex_chars[] = "0123456789ABCDEF";
    for (size_t i = 0; i < len; i++) {
        hex[i * 2] = hex_chars[data[i] >> 4];
        hex[i * 2 + 1] = hex_chars[data[i] & 0x0F];
    }
    hex[len * 2] = '\0';
}
//

void* response_parse(void* arg){

	struct str_manip *sm = (struct str_manip*)arg;
		
	int received = recv(sm->fd, buffer, BUFFER_LEN, 0);
	if(received == -1){
		printf("Recv failed: %s \n", strerror(errno));
		return NULL;
	}

	sm->buff = malloc(received + 1);
	if(!sm->buff){
		printf("Malloc failed\n");
		return NULL;
	}
	strcpy(sm->buff, buffer);

	pthread_mutex_lock(&rp_mutex);
	if (!strstr(sm->buff, "GET /user-agent ") && !strstr(sm->buff, "GET /files") && !strstr(sm->buff, "POST") && !strstr(sm->buff, "gzip") ){
		
		char *buff_copy = strdup(sm->buff);

		sm->port = strtok(buff_copy, " ");
		sm->port = strtok(NULL, " ");

		char *port_copy = strdup(sm->port);

		sm->port = strtok(port_copy, "/");
		sm->port = strtok(NULL, "/");

		if(strcmp(port_copy, "/") == 0){
			if(send(sm->fd, resp1, strlen(resp1), 0) == -1){
				printf("Send failed: %s \n", strerror(errno));
				return NULL;
			}
		}
		else if (strncmp(port_copy, "/echo", 5) == 0){

			int len = strlen(sm->port);
			char *concStr = malloc(BUFFER_LEN*sizeof(char));

			sprintf(concStr, 	
				"HTTP/1.1 200 OK\r\n" 
				"Content-Type: text/plain\r\n"
				"Content-Length: %d\r\n\r\n"
				"%s",
				len, sm->port);

			if(send(sm->fd, concStr, strlen(concStr), 0) == -1){
				printf("Send failed: %s \n", strerror(errno));
				return NULL;
			}
		}
		else{
			if(send(sm->fd, resp2, strlen(resp2), 0) == -1){
				printf("Send failed: %s \n", strerror(errno));
				return NULL;
			}
		}

		free(buff_copy);
		free(port_copy);
	}
	else if (strstr(sm->buff, "GET /user-agent ")){

		sm->port = strtok(sm->buff, "\r\n");
		sm->port = strtok(NULL, "\r\n");
		sm->port = strtok(NULL, " ");
		sm->port = strtok(NULL, "\r\n");

		int len = strlen(sm->port);
		char *length = malloc(5*sizeof(char));

		char *concStr = malloc(255*sizeof(char));

		sprintf(concStr, 
				"HTTP/1.1 200 OK\r\n"
				"Content-Type: text/plain\r\n"
				"Content-Length: %d\r\n\r\n"
				"%s", len, sm->port);

		if (send(sm->fd, concStr, strlen(concStr), 0) == -1){
			printf("Send failed: %s \n", strerror(errno));
			return NULL;
		} 
		free(length);
		free(concStr);
	}
	else if (strstr(sm->buff, "GET /files") && sm->argc >= 2){

		sm->port = strtok(sm->buff, " ");
		sm->port = strtok(NULL, " ");
		sm->port = strtok(sm->port, "/");
		sm->port = strtok(NULL, "/");

		FILE *fptr;

		char *dir_path = NULL;

		if(strcmp(sm->argv[1], "--directory") == 0 && strlen(sm->argv[2]) >= 1){
			dir_path = sm->argv[2];
		}
		else{
			if(send(sm->fd, resp2, strlen(resp2), 0) == -1){
            	printf("Send failed: %s \n", strerror(errno));
        	}
        	return NULL;
		}

		char full_path[255];
		snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, sm->port);

		if(access(strcat(dir_path, sm->port), F_OK) != 0){
			if(send(sm->fd, resp2, strlen(resp2), 0) == -1) {
            	printf("Send failed: %s \n", strerror(errno));
    		 }
			return NULL;		
		}

		fptr = fopen(dir_path, "r");

		if (!fptr){ 
			if(send(sm->fd, resp2, strlen(resp2), 0) == -1){
				printf("Send failed: %s \n", strerror(errno));
				return NULL;
			}
			return NULL;
		}

		fseek(fptr, 0L, SEEK_END);
		int sz = ftell(fptr);

		fseek(fptr, 0L, SEEK_SET);
		
		char *concStr = malloc(BUFFER_LEN*sizeof(char));

		sprintf(concStr, 
				"HTTP/1.1 200 OK\r\n"
				"Content-Type: application/octet-stream\r\n"
				"Content-Length: %d\r\n\r\n",
				sz);

		if (send(sm->fd, concStr, strlen(concStr), 0) == -1){
			printf("Send failed: %s \n", strerror(errno));
			return NULL;
		}

		//Help from claude:
		char file_content[4096];
		ssize_t bytes_read;
		while ((bytes_read = fread(file_content, 1, sizeof(file_content), fptr)) > 0){
			if (send(sm->fd, file_content, bytes_read, 0) == -1){
				printf("Send failed: %s \n", strerror(errno));
				break;
			}
		}
		//
		free(concStr);
		fclose(fptr);
	}
	else if(strstr(sm->buff, "POST /files") && sm->argc >= 2){
		
		printf("This is the POST request:\n%s\n", sm->buff);

		char *buff_copy1 = strdup(sm->buff);
		
		sm->port = strtok(buff_copy1, " ");
		sm->port = strtok(NULL, " ");
		sm->port = strtok(sm->port, "/"); 
		sm->port = strtok(NULL, "/"); 

		char *port_copy = strdup(sm->port);
		char *buff_copy2 = strdup(sm->buff);

		sm->cont = strtok(buff_copy2, "\r\n");
		sm->cont = strtok(NULL, "\r\n");
		sm->cont = strtok(NULL, "\r\n");
		sm->cont = strtok(NULL, "\r\n");
		sm->cont = strtok(NULL, "\r\n");

		char *cont_copy = strdup(sm->cont);

		char *dir_path = NULL;

		if(strcmp(sm->argv[1], "--directory") == 0 && strlen(sm->argv[2]) >= 1){
			dir_path = sm->argv[2];
		}
		else
		{
			if(send(sm->fd, resp2, strlen(resp2), 0) == -1){
            	printf("Send failed: %s \n", strerror(errno));
        	}
        	return NULL;
		}

		strcat(dir_path, port_copy);

		FILE *fptr = fopen(dir_path, "w");

		if(!fptr){
			printf("Fopen failed: %s \n", strerror(errno));
		}

		fprintf(fptr, cont_copy);

		if(send(sm->fd, resp3, strlen(resp3), 0) == -1) {
        	printf("Send failed: %s \n", strerror(errno));
			return NULL;
		}

		free(buff_copy1);
		free(buff_copy2);
		free(port_copy);
		free(cont_copy);

		fclose(fptr);
	}
	else if(strstr(sm->buff, "gzip")){

		char hex[CHUNK*2+1];
		unsigned char out[CHUNK*2+1];

		sm->port = strtok(sm->buff, "/");
		sm->port = strtok(NULL, "/");
		sm->port = strtok(NULL, " ");
		
		z_stream z;
		z.zalloc = NULL;
		z.zfree = NULL;
		z.opaque = NULL;

		z.avail_in = strlen(sm->port);
		z.next_in = (char*)sm->port;
		z.avail_out = CHUNK;
		z.next_out = out;

		deflateInit2(&z, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 | 16, 8, Z_DEFAULT_STRATEGY);

		deflate(&z, Z_FINISH);
		deflateEnd(&z);	

		int compressed_size = CHUNK - z.avail_out;

		to_hex(out, compressed_size, hex);

		//Help from claude:
  		char formatted_hex[compressed_size * 3 + (compressed_size / 16) + 1]; // Space for hex, spaces, newlines, and null terminator 
		int formatted_index = 0;

		for (int i = 0; i < compressed_size; i++) {
    	
    		sprintf(&formatted_hex[formatted_index], "%02X", out[i]);
    		formatted_index += 2;

	    	if (i < compressed_size - 1) {
    	    	formatted_hex[formatted_index++] = ' ';
    		}

    		if ((i + 1) % 8 == 0 && i < compressed_size - 1) {
        		formatted_hex[formatted_index++] = '\n';
    		}
		//
	}

	formatted_hex[formatted_index] = '\0';

		printf("This is the formatted hex: %s\n", formatted_hex);

		char response[BUFFER_LEN]; 
		sprintf(response, 
				"HTTP/1.1 200 OK\r\n"
				"Content-Type: text/plain\r\n"
				"Content-Encoding: gzip\r\n"
				"Content-Length: %d\r\n\r\n"
				"%s",
				compressed_size, formatted_hex);

		if(send(sm->fd, response, strlen(response), 0) == - 1){
			printf("Send failed: %s", strerror(errno));
			return NULL;
		}
			
	}
	else if(!strstr(sm->buff, "gzip")){
		
	}
	else{
			if(send(sm->fd, resp2, strlen(resp2), 0) == -1){
			printf("Send failed: %s \n", strerror(errno));
				return NULL;
			}
	}
		pthread_mutex_unlock(&rp_mutex);
	
	return NULL;
}

int main(int argc, char** argv) {
	// starter code from codecrafters

	setbuf(stdout, NULL);
 	setbuf(stderr, NULL);

	printf("Logs from your program will appear here!\n");

	
	int server_fd, client_addr_len;
	struct sockaddr_in client_addr;
	
	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd == -1) {
		printf("Socket creation failed: %s...\n", strerror(errno));
		return 1;
	}
	
	const int reuse = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
		printf("SO_REUSEADDR failed: %s \n", strerror(errno));
		return 1;
	}
	
	struct sockaddr_in serv_addr = { .sin_family = AF_INET ,
									 .sin_port = htons(4221),
									 .sin_addr = { htonl(INADDR_ANY) },
									};
	
	if (bind(server_fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) != 0) {
		printf("Bind failed: %s \n", strerror(errno));
		return 1;
	}
	
	int connection_backlog = 5;
	if (listen(server_fd, connection_backlog) != 0) {
		printf("Listen failed: %s \n", strerror(errno));
		return 1;
	}
	
	printf("Waiting for a client to connect...\n");
	client_addr_len = sizeof(client_addr);
	
	int connections_thread = 0;
	int newfd;
	
	while(1){
	
		newfd = accept(server_fd, (struct sockaddr *) &client_addr, &client_addr_len);
		if (newfd == -1){
			printf("Accept failed: %s \n", strerror(errno));
			return 1;
		}

		if (connections_thread >= MAX_CONNECTIONS) {
            printf("Max connections reached. Rejecting new connection.\n");
            close(newfd);
            continue;
        }

		struct str_manip *sm = malloc(sizeof(struct str_manip));
		if(sm == NULL){
			printf("Failed memory allocation for str_manip\n");
			close(newfd);
		}

		sm->fd = newfd;
		sm->argc = argc;
		sm->argv = argv;

		pthread_t thread[connections_thread];
		pthread_mutex_init(&rp_mutex, NULL);

		if (pthread_create(&thread[connections_thread], NULL, &response_parse, (void*) sm) != 0){
			perror("Failed to create thread\n");
			free(sm);
			close(newfd);
			continue;
		}

		//Help from claude:
		for (int i = 0; i < connections_thread; i++) {
            if (pthread_join(thread[i], NULL) == 0) {
                
                for (int j = i; j < connections_thread - 1; j++) {
                    thread[j] = thread[j + 1];
                }
                connections_thread--;
                i--;  
            }
        }
		//
	}
	pthread_mutex_destroy(&rp_mutex);
	close(server_fd);

	return 0;
}
