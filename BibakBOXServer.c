#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
#include <dirent.h>
#include <signal.h>

#define TYPE_DIRECTORY 4
#define TYPE_FILE 8
#define MAX_PATH_LENGTH 255
#define MAX_CLIENTS_ALLOWED 128
#define true 1
#define false 0
#define bool int


#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCDFAInspection"
#pragma clang diagnostic ignored "-Wmissing-noreturn"

 char * serverPath;
struct sockaddr_in address;
char * connectedPath[MAX_CLIENTS_ALLOWED] = {0};
int connectedClients[MAX_CLIENTS_ALLOWED] = {0};
void removeDirectory(char * path);
void sendMissingFiles(int fd,char * path,FILE *logFile);

int totalClientsConnected=0;
int connectionIdAI=0;
int threadPoolSize = 4;

char * strcon (char * str1,char * str2,char * str3){
    //Allocate memory in MAX_PATH_LENGTH size to hold result string
    char *pathBuffer = (char*) malloc(MAX_PATH_LENGTH);
    //concatenate three strings using snprintf
    snprintf(pathBuffer, 255, "%s%s%s", str1, str2, str3);
    return pathBuffer;
}


static void createClientDirectory( char *path) {
    char tmp[256];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp),"%s",path);
    len = strlen(tmp);
    if(tmp[len - 1] == '/')
        tmp[len - 1] = 0;
    for(p = tmp + 1; *p; p++)
        if(*p == '/') {
            *p = 0;
            mkdir(tmp, S_IRWXU);
            *p = '/';
        }
    mkdir(tmp, S_IRWXU);
}



void * threadMain(void * vargp){
    int clientId = 0;
    char readBuffer[1024];
    char sendBuffer[1024];

    int serverFd = *((int*)vargp);
    int socketFd;
    clientId = connectionIdAI++;
    char command[32];
    char path[128];
    char filename[128];
    char filesize[32];
    char modified[32];

    while(true){
        int addrlen = sizeof(address);
        if ((socketFd = accept(serverFd, (struct sockaddr *)&address,
                               (socklen_t*)&addrlen))<0){
            printf("accept");fflush(stdout);
            exit(EXIT_FAILURE);
        }
        if(totalClientsConnected==threadPoolSize){
            send(socketFd , "max" , 4 , 0 );
            printf("FULL Exit socketThread with %d \n",socketFd);
            continue;
        }
        send(socketFd , "yes" , 4 , 0 );
        totalClientsConnected++;

        fflush(stdout);


        recv(socketFd,path,128,0);

        int i;
        for(i=0;i<threadPoolSize+1;i++){
            if(strcmp(connectedPath[i],path)==0){
                send(socketFd , "no" , 3 , 0 );
                continue;
            }
        }
        send(socketFd , "yes" , 4 , 0 );
        strcpy(connectedPath[clientId],path);
        connectedClients[clientId] = socketFd;

        char * serverClientPath = strcon(serverPath,"/",path);
        createClientDirectory(serverClientPath);
        char * logPath = strcon(serverClientPath,"/","server.log");
        free(serverClientPath);
        FILE* logFile = fopen( logPath, "w+");
        fprintf(logFile,"Client was connected to server with path %s\n",path);
        fflush(logFile);
        free(logPath);
        printf("Connection %d accepted with path %s\n",clientId,connectedPath[clientId]);
        while (read( socketFd , readBuffer, 1024)>0){

            sscanf(readBuffer,"%[^\t\n]\n%[^\t\n]\n%[^\t\n]\n%[^\t\n]\n%[^\t\n]",command,path,filename,filesize,modified);
            serverClientPath = strcon(serverPath,"/",path);
            if(path[0]=='/'){
                free(serverClientPath);
                serverClientPath = strcon(serverPath,"",path);
            }
            createClientDirectory(serverClientPath);
            struct stat st = {0};

            if(strcmp(command,"FILE")==0){
                int totalFileSize = atoi(filesize);
                //fprintf(stderr,"total file size is %d\n",totalFileSize);

                if(strcmp(filename,"server.log")==0){
                    send(socketFd , "no" , 3 , 0 );
                    continue;
                }
                char * pathall = strcon(serverClientPath,"/",filename);

                char * pathallHidden = strcon(serverClientPath,"/.last_mod_",filename);

                FILE* checkFile = fopen( pathallHidden, "r");
                if(checkFile!=NULL){
                    fread(readBuffer,1, 10,checkFile);
                    readBuffer[10]=0;

                    if(strcmp(modified,readBuffer)==0){ //check modified date
                        stat(pathall, &st);
                        if(st.st_size==totalFileSize){ //check file size
                            send(socketFd , "no" , 3 , 0 );
                            continue;
                        }
                        //printf("the file %s is synced.\n",pathall);
                       // fflush(stdout);

                    }
                    fclose(checkFile);
                }
                FILE* fp = fopen( pathall, "wb");

                FILE* fp2 = fopen( pathallHidden, "w+");
                fwrite(modified,1,strlen(modified),fp2);
                fclose(fp2);
                free(pathallHidden);

                if(totalFileSize==0){
                    send(socketFd , "empty" , 6 , 0 );
                }else{
                    send(socketFd , "yes" , 4 , 0 );
                    int tot=0,b;
                    if(fp != NULL) {
                        while ((b = recv(socketFd, readBuffer, 1024, 0)) > 0) {
                            tot += b;
                            fwrite(readBuffer, 1, b, fp);
                            if(tot==totalFileSize){
                                break;
                            }
                        }
                        sprintf(sendBuffer,"i have received");
                        fprintf(stderr,"I have updated the file %s\n",pathall);
                        send(socketFd , sendBuffer , strlen(sendBuffer)+1 , 0 );
                        if (b < 0)
                            perror("Receiving");

                        fclose(fp);
                    }
                }
                fprintf(logFile,"File %s was updated\n",pathall);
                fflush(logFile);
                free(pathall);
            }else if(strcmp(command,"FILEDELETE")==0) {
                char * pathall2 = strcon(serverClientPath,"/.last_mod_",filename);
                char * pathall = strcon(serverClientPath,"/",filename);
                unlink(pathall2);
                if(unlink(pathall)<0){
                    send(socketFd , "no" , 3 , 0 );
                }else{
                    send(socketFd , "yes" , 4 , 0 );
                    fprintf(stderr,"I have deleted the file %s\n",pathall);
                }

                fprintf(logFile,"File %s was deleted\n",pathall);
                fflush(logFile);
                free(pathall);
                free(pathall2);
            }else if(strcmp(command,"FOLDER")==0) {
                if (stat(serverClientPath, &st) == -1) {
                    mkdir(serverClientPath, 0700);
                    fprintf(stderr,"creating folder %s\n",serverClientPath);
                    fprintf(logFile,"Folder %s was created\n",serverClientPath);
                    fflush(logFile);
                    send(socketFd , "yes" , 4 , 0 );
                }else{
                    send(socketFd , "no" , 3 , 0 );
                }
            }else if(strcmp(command,"FOLDERDELETE")==0) {
                sprintf(sendBuffer,"I am deleting folder");
                removeDirectory(serverClientPath);
                fprintf(stderr,"Folder deleted %s\n",serverClientPath);
                fprintf(logFile,"Folder %s was deleted\n",serverClientPath);
                fflush(logFile);
                send(socketFd , sendBuffer , strlen(sendBuffer)+1 , 0 );
            }else if(strcmp(command,"GETMISSINGFILES")==0) {
                sprintf(sendBuffer,"I am deleting folder");
                sendMissingFiles(socketFd,path,logFile);
                sprintf(sendBuffer,"EXIT\n%s",path);
                send(socketFd,sendBuffer, strlen(sendBuffer)+1,0);
            }else{
                send(socketFd , "OK NOT" , strlen("OK NOT")+1 , 0 );
            }
            free(serverClientPath);
        }
        connectedPath[clientId][0] = 0;
        printf("Connection %d was closed\n",clientId);
        close(socketFd);
        fclose(logFile);
        connectedClients[clientId] = 0;

        totalClientsConnected--;
    }
    
    pthread_exit(NULL);
    return NULL;
}


void removeDirectory(char * path){
    DIR *dir = opendir(path);
    struct dirent *ent;

    while ((ent = readdir(dir)) != NULL) {
        if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
            continue;

        char *pathall = strcon(path, "/",ent->d_name);

        if(ent->d_type==TYPE_DIRECTORY){
            removeDirectory(pathall);
            continue;
        }

        unlink(pathall);
        free(pathall);
    }
    rmdir(path);
    closedir(dir);
}

void sendMissingFiles(int fd,char * path,FILE * logFile) {
    char * serverClientPath = strcon(serverPath,"/",path);
    char readBuffer[1024];
    char sendBuffer[1024];




    DIR * dirr = opendir(serverClientPath);

    if(dirr==NULL)return ;

    struct dirent *ent;

    while ((ent=readdir(dirr)) != NULL){

        //Skip files with . and ..
        if(ent->d_name[0] == '.')
            continue;
        if(strcmp(ent->d_name,"server.log")==0){
            continue;
        }
        //get relative path to file or directory
        char * pathall = strcon(path,"/",ent->d_name);
        char * pathall2 = strcon(serverClientPath,"/",ent->d_name);
        if(ent->d_type==TYPE_FILE){
            struct stat st = {0};
            stat(pathall2, &st);
            sprintf(sendBuffer,"FILE\n%s\n%ld",pathall,st.st_size);
            send(fd,sendBuffer, strlen(sendBuffer)+1,0);
            recv(fd,readBuffer, 1024,0);
            readBuffer[3]=0;

            if(strcmp(readBuffer,"yes")==0){

                FILE *fp = fopen(pathall2, "rb");

                size_t bytesRead;
                size_t totalBytesRead = 0;
                while( (bytesRead = fread(sendBuffer, 1, sizeof(sendBuffer), fp))>0 ){
                    totalBytesRead += bytesRead;
                    send(fd, sendBuffer, bytesRead, 0);
                }
                fclose(fp);
                fprintf(stderr,"The file %s sent to client successfully\n",pathall);
                fprintf(logFile,"The file %s was sent to client\n",pathall);
                fflush(logFile);
                if(recv( fd , readBuffer, 1024,0)>0){
                    char * pathallMod = strcon(serverClientPath,"/.last_mod_",ent->d_name);
                    FILE* fp2 = fopen( pathallMod, "w+");
                    fwrite(readBuffer,1,strlen(readBuffer),fp2);
                    fclose(fp2);
                }
            }
        }else if(ent->d_type==TYPE_DIRECTORY){
            sprintf(sendBuffer,"DIR\n%s",pathall);
            send(fd,sendBuffer, strlen(sendBuffer)+1,0);
            recv(fd,readBuffer, 1024,0);
            sendMissingFiles(fd,pathall,logFile);
        }else{
            //printf("%-16d  Special file %s\n",getpid(),ent->d_name);
        }
        free(pathall);
    }

    free(serverClientPath);
    closedir(dirr);//free dirr allocated in opendir

}

pthread_t * threads;

int server_fd;
void signalHandler(int sig){
    close(server_fd);
    int i;
    for(i=0;i<threadPoolSize+1;i++){
        if(connectedPath[i]!=NULL){
            free(connectedPath[i]);
        }
    }

    for(i=0;i<threadPoolSize+1;i++){
        if(connectedClients[i]!=0){
            close(connectedClients[i]);
        }
    }
free(serverPath);
    printf("\nExiting BibakBOXServer...\n\n");
    exit(0);
}


int main(int argc, char const *argv[]){
    int port;
    if(argc<4){
        printf("BibakBOXServer [directory] [threadPoolSize] [portnumber] \n");
        return 0;
    }else{
        port = atoi(argv[3]);
        threadPoolSize = atoi(argv[2]);
        serverPath = malloc(128);
        strcpy(serverPath,argv[1]);
    }
    int i;
    for(i=0;i<threadPoolSize+1;i++){
        connectedPath[i] = calloc(255, sizeof(char));
    }


    if (signal(SIGINT, signalHandler) == SIG_ERR)
        printf("\nCan't catch SIGINT\n");

    if (signal(SIGTERM, signalHandler) == SIG_ERR)
        printf("\nCan't catch SIGTERM\n");

    if (signal(SIGQUIT, signalHandler) == SIG_ERR)
        printf("\nCan't catch SIGQUIT\n");


    int opt = 1;
    struct stat st = {0};
    if (stat(serverPath, &st) == -1) {
        mkdir(serverPath, 0700);
    }

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0){
        printf("socket failed");
        exit(EXIT_FAILURE);
    }
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT,
                   &opt, sizeof(opt))){
        printf("setsockopt");
        exit(EXIT_FAILURE);
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons( port );
    if (bind(server_fd, (struct sockaddr *)&address,
             sizeof(address))<0){
        printf("bind failed");fflush(stdout);
        exit(EXIT_FAILURE);
    }
    if (listen(server_fd, 0) < 0){
        printf("listen");fflush(stdout);
        exit(EXIT_FAILURE);
    }
    printf("Server listening on %d\n",port);
    fflush(stdout);
    threads = malloc(threadPoolSize* sizeof(pthread_t));

    for(i=0;i<threadPoolSize+1;i++){
        pthread_create(&(threads[i]), NULL, threadMain, &server_fd);
    }

    for(i=0;i<threadPoolSize+1;i++){
        pthread_join(threads[i],NULL);
    }


    return 0;
}


#pragma clang diagnostic pop