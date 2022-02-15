#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <pthread.h>
#include <poll.h>
#include <ctype.h>
#include <sys/stat.h>
#include <semaphore.h>

#define MAX_WORKING_THREADS 20
#define MAX_DATA_SIZE 10000000
#define OK_RESPONSE "HTTP/1.1 200 OK\r\n\r\n"
#define FILE_NOT_FOUND_RESPONSE "HTTP/1.1 404 Not Found\r\n\r\n"
#define METHOD_NOT_FOUND_RESPONSE "HTTP/1.1 405 Method Not Allowed\r\n\r\n"
#define SERVER_BOX "./server_box"
#define MAX_TIME_OUT 15 //in seconds

sem_t ThreadsSemaphore; 
pthread_mutex_t ConnectionMutex = PTHREAD_MUTEX_INITIALIZER; 
int numOfConnections = 0; //currently active connections
double Timeout = MAX_TIME_OUT; 

void editTimeOut(){
    if(numOfConnections) Timeout = MAX_TIME_OUT/numOfConnections;
    else Timeout = MAX_TIME_OUT;
    printf("[+] handeling %d connections\n[+]Timeout is %f seconds\n",numOfConnections,Timeout);
}
void incr_Connections(){
    pthread_mutex_lock(&ConnectionMutex);
    numOfConnections++;
    editTimeOut();
    pthread_mutex_unlock(&ConnectionMutex);
}
void decre_Connections(){
    pthread_mutex_lock(&ConnectionMutex);
    numOfConnections--;
    editTimeOut();
    pthread_mutex_unlock(&ConnectionMutex);
}

int creatSocket(){
     char *ip = "127.0.0.1";
  int port = 7788;

  int server_sock, client_sock;
  struct sockaddr_in server_addr, client_addr;
  socklen_t addr_size;
  int opt =1;
  int n;

  server_sock = socket(AF_INET, SOCK_STREAM, 0);
  if (server_sock < 0){
    perror("[-]Socket error");
    exit(1);
  }
  printf("[+]TCP server socket created.\n");
  if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT,
												&opt, sizeof(opt)))
	{
		perror("setsockopt");
		exit(EXIT_FAILURE);
	}

  memset(&server_addr, '\0', sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = port;
  server_addr.sin_addr.s_addr = inet_addr(ip);

  n = bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr));
  if (n < 0){
    perror("[-]Bind error");
    exit(1);
  }
  printf("[+]Bind to the port number: %d\n", port);

  listen(server_sock, 10);
  printf("Listening...\n");
    return server_sock;
}


int acceptConnections(int socketDescriptor){
    struct sockaddr_storage clientAddress; 
    socklen_t addressSize = sizeof clientAddress;
    int connectionDescriptor = accept(socketDescriptor, (struct sockaddr *)&clientAddress, &addressSize);
    if(connectionDescriptor == -1){
        printf("Can't accept connection\n");
        exit(1);
    }

    
    return connectionDescriptor;
}

void sendStringToClient(char* response,int connection){
    int len = strlen(response);
    int sent = 0;
    int left = len;
    while (sent<len){
        int n = send(connection,response+sent,left,0);
        if (n == -1) break;
        sent += n;
        left -= n;
    }
}


void sendFileToClient(char* filePath,int connection){
    FILE *fPtr = fopen(filePath, "rb");
    if(fPtr == NULL) {
        sendStringToClient(FILE_NOT_FOUND_RESPONSE,connection);
        return;
    }
    sendStringToClient(OK_RESPONSE,connection);
    fseek(fPtr, 0, SEEK_END);
    int fileLen = ftell(fPtr);
    rewind(fPtr);
    fseek(fPtr, 0, SEEK_SET);
    int written = 0;
    char ch;
    while(written < fileLen){
        fread(&ch,1,1,fPtr);
        written += (send(connection,&ch,sizeof ch,0) != 0);
    }
    fclose(fPtr);
}

// create directory
void _mkdir(char *dir) {
   
    char tmp[256];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp),"%s",dir);
    len = strlen(tmp);
    if (tmp[len - 1] == '/') tmp[len - 1] = 0;
    for (p = tmp + 1; *p; p++)
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, S_IRWXU);
            *p = '/';
        }
}

void writeToFile(char* filePath,char* content,int len){
    printf("[+]file length is %d\n",len);
    _mkdir(filePath);
    FILE *fPtr = fopen(filePath, "wb");
    //eliminate the header
    char* begin = strstr(content,"\r\n\r\n");
    int headerLen = begin-content;
    for(int i=headerLen+4;i<len;i++) putc(content[i],fPtr);
    fclose(fPtr);
}

void handleRequest(char* request,int len,int connection){
    int leadingSpaces = 0;
    while(isspace(request[leadingSpaces])) {
        leadingSpaces++;
        if(leadingSpaces == len) return;
    }
    char *duplication = (char*) malloc(sizeof(char)*len);
    printf("[+]request is \"");
    for(int i=0;i<len-leadingSpaces;i++) {
        printf("%c",request[i+leadingSpaces]);
        duplication[i] = request[i+leadingSpaces];
    }
    printf("\"\n");
    char *method,*uri,*version;
    method = strtok(duplication," ");
    uri = strtok(NULL," ");
    version = strtok(NULL,"\n");

    if(method == NULL || uri==NULL || version == NULL) {
        sendStringToClient(METHOD_NOT_FOUND_RESPONSE,connection);
        return;
    }

    
    char server_box[200] = SERVER_BOX;

    strcat(server_box,uri);

    if (strcmp(method,"GET") == 0 ) sendFileToClient(server_box,connection);
    else if (strcmp(method,"POST") == 0) {
        sendStringToClient(OK_RESPONSE,connection);
        writeToFile(server_box,request+leadingSpaces,len-leadingSpaces);
    }
    else sendStringToClient (METHOD_NOT_FOUND_RESPONSE,connection);
    free(duplication);
}

int isEmptyString(char* line,int len){
    for(int i=0;i<len;i++) if(!isspace(line[i])) return 0;
    return 1;
}
void* handleConnection(void* connection){
    incr_Connections();
    int connectionDescriptor = *(int*)connection;
    char* buffer = (char*) malloc(sizeof(char) * MAX_DATA_SIZE);
    int emptyLines = 0;
    //list of sockets to monitor events [only one socket in our case] 
    struct pollfd socketMonitor[1];
    socketMonitor[0].fd = connectionDescriptor;
    socketMonitor[0].events = POLLIN;
    int len = 0;
    while(1){
        // poll if the socket had new event to handle or not.
        int numOfEvents = poll(socketMonitor,1, Timeout*1000);
        if(numOfEvents == 0) break; // no more IN events happend during the timeout interval
        int receivedBytes = recv(connectionDescriptor,buffer+len,MAX_DATA_SIZE-len,0);
        if(receivedBytes == -1){
            printf("Error when receiving from the client\n");
            free(buffer);
            close(connectionDescriptor);
            decre_Connections();
            sem_post(&ThreadsSemaphore);
            return NULL;
        }
        if(receivedBytes == 0) break; // the client closed the connection
        len += receivedBytes;
        if(!isEmptyString(buffer,len) && len >= 4 && buffer[len-1] == '\n' && buffer[len-2] == '\r' && buffer[len-3] == '\n' && buffer[len-4] == '\r') {
            handleRequest(buffer,len,connectionDescriptor);
            len=0;
        }
    }
    if(len != 0) handleRequest(buffer,len,connectionDescriptor);
    free(buffer);
    close(connectionDescriptor);
    decre_Connections();
    sem_post(&ThreadsSemaphore);
}

int main(){
    
    sem_init(&ThreadsSemaphore,0,MAX_WORKING_THREADS);

    int server_socket=creatSocket();
    while (1){
        //block until the num of working threads < MAX_THREADS
        sem_wait(&ThreadsSemaphore);
        int connection = acceptConnections(server_socket);
        pthread_t thread;
        pthread_create(&thread, NULL, handleConnection, (void *)(&connection));
    }
    
    return 0;
}