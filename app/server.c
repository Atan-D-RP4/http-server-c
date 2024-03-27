#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>

char *dir = "./";

int simpleServer() {
	// Disable output buffering
	setbuf(stdout, NULL);

	// You can use print statements as follows for debugging, they'll be visible when running tests.
	printf("Logs from your program will appear here!\n");
	
	int server_fd;
	
	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd == -1) {
		printf("Socket creation failed: %s...\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	// Since the tester restarts your program quite often, setting REUSE_PORT
	// ensures that we don't run into 'Address already in use' errors
	int reuse = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) < 0) {
		printf("SO_REUSEPORT failed: %s \n", strerror(errno));
		exit(EXIT_FAILURE);
	}
	
	// Set up the server address struct
	struct sockaddr_in serv_addr = { 
		 .sin_family = AF_INET ,
		 .sin_port = htons(4221),
		 .sin_addr = { htonl(INADDR_ANY) },
	};

	// Bind server to port 4221
	if (bind(server_fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) != 0) {
		printf("Bind failed: %s \n", strerror(errno));
		exit(EXIT_FAILURE);
	}
	
	// Listen for incoming connections
	int connection_backlog = 5;
	if (listen(server_fd, connection_backlog) != 0) {
		printf("Listen failed: %s \n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	return server_fd;
}

void serve(int client_fd) {
	char readbuf[2048];
	int bytesread = recv(client_fd, readbuf, sizeof(readbuf), 0);
	if (bytesread < 0) {
		printf("Receive failed: %s \n", strerror(errno));
		return;
	}

	char method[16], reqtype[16], path[256];
	sscanf(readbuf, "%s %s %[^\r]", method, path, reqtype);
	printf("Method: %s\n", method);
	printf("Path: %s\n", path);
	printf("Request Type: %s\n", reqtype);
	printf("\n");

	// Extract the path from the request
	char *reqPath = path;

	int bytessent;
	char response[512];
	int contentLength;

	if (strcmp(reqPath, "/") == 0) {
		sprintf(response, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 11\r\n\r\nHello World");
	 } else if (strncmp(reqPath, "/echo/", 6) == 0) {

		// parse the content from the request
		reqPath = strtok(reqPath, "/");
		reqPath = strtok(NULL, "");
		contentLength = strlen(reqPath);
		sprintf(response, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %d\r\n\r\n%s", contentLength, reqPath);
		printf("Sending Response: %s\n", response);
	} else if (strcmp(reqPath, "/user-agent") == 0) {

		// parse the user-agent from the request
		char *userAgent = strtok(readbuf, "\r\n");
		userAgent = strtok(NULL, "\r\n");
		userAgent = strtok(NULL, " ");
		userAgent = strtok(NULL, " ");
		userAgent = strtok(userAgent, "\r\n");
		printf("User-Agent: %s\n", userAgent);
	
		// parse the body from the request
		contentLength = strlen(userAgent);								
		sprintf(response, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %d\r\n\r\n%s", contentLength, userAgent);
	} else if (strncmp(reqPath, "/files/", 7) == 0 && strcmp(method, "GET") == 0) {
	
		// parse the file path
		reqPath = strtok(reqPath, "/");
		printf("ReqPath: %s\n", reqPath);
		reqPath = strtok(NULL, "/");
		printf("ReqPath: %s\n", reqPath);
		
		char filename[256];
		sprintf(filename, "%s%s", dir, reqPath);
		printf("Filename: %s\n", filename);

		FILE *fp = fopen(filename, "rb");
		if (!fp) {
			printf("File not found: %s\n", filename);
			sprintf(response, "HTTP/1.1 404 NOT FOUND\r\n\r\n");
			bytessent = send(client_fd, response, strlen(response), 0);
		} else if (strcmp(reqPath, "/redirect") == 0) {
			sprintf(response, "HTTP/1.1 301 Moved Permanently\r\nLocation: http://www.google.com\r\n\r\n");
		} else if (strcmp(reqPath, "/error") == 0) {
			sprintf(response, "HTTP/1.1 500 INTERNAL SERVER ERROR\r\n\r\n");
		}  else {
			printf("Opening file %s\n", filename);
		}

		if (fseek(fp, 0, SEEK_END) < 0) {
			printf("Seek failed: %s\n", strerror(errno));
			sprintf(response, "HTTP/1.1 500 INTERNAL SERVER ERROR\r\n\r\n");
		}
		
		// Get the size of the file
		size_t data_size = ftell(fp);

		// Reset the file pointer to the beginning of the file
		rewind(fp);

		// Allocate memory to store the file
		void *data = malloc(data_size);

		// Fill the data buffer 
		if (fread(data, 1, data_size, fp) != data_size) {
			printf("Read failed: %s\n", strerror(errno));
			sprintf(response, "HTTP/1.1 500 INTERNAL SERVER ERROR\r\n\r\n");
		}

		// Close the file
		fclose(fp);

		// Send the response
		sprintf(response, "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: %ld\r\n\r\n%s", data_size, (char *) data);
	} else if (strncmp(reqPath, "/files/", 7) == 0 && strcmp(method, "POST") == 0) {
		
		// parse the file path
		reqPath = strtok(reqPath, "/");
		printf("ReqPath: %s\n", reqPath);
		reqPath = strtok(NULL, "/");
		printf("ReqPath: %s\n", reqPath);
	
		char filename[256];
		sprintf(filename, "%s%s", dir, reqPath);
		printf("Filename: %s\n", filename);

		FILE *fp = fopen(filename, "wb");
		if (!fp) {
			printf("Failed to open file: %s\n", filename);
			sprintf(response, "HTTP/1.1 404 NOT FOUND\r\n\r\n");
			bytessent = send(client_fd, response, strlen(response), 0);
		} else if (strcmp(reqPath, "/redirect") == 0) {
			sprintf(response, "HTTP/1.1 301 Moved Permanently\r\nLocation: http://www.google.com\r\n\r\n");
		} else if (strcmp(reqPath, "/error") == 0) {
			sprintf(response, "HTTP/1.1 500 INTERNAL SERVER ERROR\r\n\r\n");
		} else {
			printf("Creating file %s\n", filename);
		}

		printf("Received: %s\n\n", readbuf);
		char *parser = strtok(readbuf, "\r\n");
		parser = strtok(NULL, "\r\n");
		parser = strtok(NULL, "\r\n");
		parser = strtok(NULL, "\r\n");
		parser = strtok(NULL, "\r\n");


		char *contentLengthStr = strtok(parser, " ");
		contentLengthStr = strtok(NULL, " ");
		printf("Content Length Str: %s\n", contentLengthStr);
		int contentLength = atoi(contentLengthStr);

		printf("Content Length: %d\n", contentLength);

		parser = strtok(parser, "\r\n");
		parser = strtok(parser, " ");
		parser = strtok(parser, " ");
		printf("Parser: %s\n", parser);

		char content[contentLength + 1];
		strncpy(content, parser, contentLength);
		content[contentLength] = '\0';
		printf("Content to Write: %s\n", content);

		fwrite(content, sizeof(char), contentLength, fp);
		fclose(fp);

		sprintf(response, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 0\r\n\r\n");

	} else if (strcmp(reqPath, "/redirect") == 0) {
		sprintf(response, "HTTP/1.1 301 Moved Permanently\r\nLocation: http://www.google.com\r\n\r\n");
	} else if (strcmp(reqPath, "/error") == 0) {
		sprintf(response, "HTTP/1.1 500 INTERNAL SERVER ERROR\r\n\r\n");
	} else {
		sprintf(response, "HTTP/1.1 404 NOT FOUND\r\n\r\n");
	}
	
	printf("Sending Response: %s\n", response);
	bytessent = send(client_fd, response, strlen(response), 0);
	if (bytessent < 0) {
		printf("Send failed: %s \n", strerror(errno));
		return;
	}
}

int main(int argc, char *argv[]) {
	if (argc == 3) 
		dir = argv[2];

	int server_fd = simpleServer();
	
	int client_addr_len;
	struct sockaddr_in client_addr;
	
	while(1) {
		printf("Waiting for a client to connect...\n");
		client_addr_len = sizeof(client_addr);
		int client_fd = accept(server_fd, (struct sockaddr *) &client_addr, (socklen_t*) &client_addr_len);	
		if (client_fd < 0) {
			printf("Accept failed: %s \n", strerror(errno));
			continue;
		}
		printf("Client connected\n");
		
		pid_t pid = fork();
        if (pid == -1) {
            printf("Fork failed: %s\n", strerror(errno));
            close(client_fd);
            continue;
        } else if (pid == 0) {
            // Child process
            close(server_fd);
            serve(client_fd);
            exit(0);
        } else {
            // Parent process
            close(client_fd);
        }
	}
	close(server_fd);

	return 0;
}
