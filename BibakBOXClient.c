#include <stdio.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <sys/inotify.h>
#include <dirent.h>
#include <sys/stat.h>
#include <signal.h>

#define TYPE_DIRECTORY 4
#define TYPE_FILE 8
#define MAX_PATH_LENGTH 255
#define true 1
#define false 0
#define bool int

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-noreturn"
#define EVENT_SIZE  ( sizeof (struct inotify_event) )
#define EVENT_BUF_LEN     ( 1024 * ( EVENT_SIZE + 16 ) )
 char * clientPath;
struct listenPath{
    int wd;
    char* path ;
};

int listenPathIndex = 0;
struct listenPath* listenPaths[255];


#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCDFAInspection"
#pragma clang diagnostic ignored "-Wmissing-noreturn"
int connectToServer( char* ip,int port){
    struct sockaddr_in address;
    int sock = 0;
    struct sockaddr_in serv_addr;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        printf("\n Socket creation error \n");
        return -1;
    }
    memset(&serv_addr, '0', sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    // Convert IPv4 and IPv6 addresses from text to binary form
    if(inet_pton(AF_INET, ip, &serv_addr.sin_addr)<=0){
        printf("\nInvalid address/ Address not supported \n");
        return -1;
    }
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0){
        printf("\nConnection failed, server is down. \n");
        return -2;
    }
    return sock;
}


void addToListenPath(int wd, char* path){
    struct listenPath *lp = malloc(sizeof(struct listenPath));
    lp->wd = wd;
    lp->path=path;

    listenPaths[listenPathIndex] = lp;
    listenPathIndex++;
}

char* getPath(int wd){
    int i;
    for(i=0;i<listenPathIndex;i++){
        if(listenPaths[i]->wd==wd){
            return  listenPaths[i]->path;
        }
    }
    return NULL;
}

char * strcon ( char * str1,char * str2,char * str3){
    //Allocate memory in MAX_PATH_LENGTH size to hold result string
    char *pathBuffer = (char*) malloc(MAX_PATH_LENGTH);
    //concatenate three strings using snprintf
    snprintf(pathBuffer, MAX_PATH_LENGTH, "%s%s%s", str1, str2, str3);
    return pathBuffer;
}

int socketFd;
char sendBuffer[1024];
char receiveBuffer[1024];

//sends the string in sendBuffer to server
//receives the response into receivebuffer
int sendRequest(){
    send(socketFd , sendBuffer , strlen(sendBuffer)+1 , 0 );
    if(recv( socketFd , receiveBuffer, 1024,0)<=0){
        fprintf(stderr,"Could not load response!\n");
        return -1;
    }
    return 0;
}

int syncPaths ( char *path,int inotifyFd,int shouldListen);

int syncFile( char * path,char * filename){
    if(filename[0]=='.'){
        //fprintf(stderr,"External change detected, checking directory for changes.\n");
        syncPaths(path,0,0);
        return 0;
    }
    struct stat buf;
    char * pathname = strcon(path,"/",filename);
    stat(pathname,&buf);

    struct timespec modified = buf.st_ctim;
    //get server response for file if needs to be send
    sprintf(sendBuffer,"FILE\n%s\n%s\n%ld\n%ld",path,filename,buf.st_size,modified.tv_sec);
    sendRequest();
    //if the file needs to be send
    if(strcmp(receiveBuffer,"yes")==0){
        FILE *fp = fopen(pathname, "rb");
        if(fp == NULL){
            perror("File");
            return 2;
        }
        size_t bytesRead;
        size_t totalBytesRead = 0;
        if(buf.st_size>1024*1024){
            printf("Sending file %s of total %f MB\n",filename,buf.st_size/(1024.0*1024.0));
            fflush(stdout);
        }
        while( (bytesRead = fread(sendBuffer, 1, sizeof(sendBuffer), fp))>0 ){
            totalBytesRead += bytesRead;
            send(socketFd, sendBuffer, bytesRead, 0);
        }
        //fprintf(stderr,"totalBytesRead %ld, fileSize %ld\n",totalBytesRead,buf.st_size);

        fclose(fp);
        fprintf(stderr,"The file %s sent successfully\n",pathname);
        if(recv( socketFd , receiveBuffer, 1024,0)<=0){
            printf("Could not load response!");
            return -1;
        }
    }else if(strcmp(receiveBuffer,"empty")==0){
        fprintf(stderr,"An empty file %s sent successfully.\n",pathname);
    }else{
        //fprintf(stderr,"The file %s already up to date\n",pathname);
    }

    return 0;
}
int syncFileDelete( char * path,char * filename){
    sprintf(sendBuffer,"FILEDELETE\n%s\n%s",path,filename);
    sendRequest();
    if(strcmp(receiveBuffer,"yes")==0){
        fprintf(stderr,"File %s deleted successfully.\n",filename);
    }else{
        //fprintf(stderr,"The folder %s already exists.\n",path);
    }
    return 0;
}

int syncFolder( char * path){
    sprintf(sendBuffer,"FOLDER\n%s",path);
    sendRequest();
    if(strcmp(receiveBuffer,"yes")==0){
        fprintf(stderr,"The folder %s created successfully.\n",path);
    }else{
        //fprintf(stderr,"The folder %s already exists.\n",path);
    }
    return 0;
}

int syncFolderDelete( char * path,char * foldername){
    char * pathname = strcon(path,"/",foldername);
    struct stat buf;
    stat(pathname,&buf);

    sprintf(sendBuffer,"FOLDERDELETE\n%s",pathname);
    sendRequest();

    fprintf(stderr,"Folder %s deleted from remote successfully\n",receiveBuffer);
    return 0;
}

int syncPaths ( char *path,int inotifyFd,int shouldListen){
    if(shouldListen){
        int watchFd = inotify_add_watch( inotifyFd, path, IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE | IN_ATTRIB);
        addToListenPath(watchFd,path);
        syncFolder(path);
    }
    DIR * dirr = opendir(path);
    if(dirr==NULL)return -1;

    int totalSize = 0;
    struct dirent *ent;
    while ((ent=readdir(dirr)) != NULL){
        //Skip files with .
        if(ent->d_name[0] == '.') continue;

        //get relative path to file or directory
        char * pathall = strcon(path,"/",ent->d_name);

        if(ent->d_type==TYPE_FILE){
            syncFile(path,ent->d_name);
        }else if(ent->d_type==TYPE_DIRECTORY){
            syncPaths(pathall,inotifyFd,shouldListen);
        }else{

        }
    }

    closedir(dirr);

    return totalSize;
}

void syncMissingFiles( char * path){
    sprintf(sendBuffer,"GETMISSINGFILES\n%s",path);
    send(socketFd , sendBuffer , strlen(sendBuffer)+1 , 0 );
    char readCommand[15];
    char fileSize[35];
    char readPath[255];
    while(recv( socketFd , receiveBuffer, 1024,0)>0){
        sscanf(receiveBuffer,"%[^\t\n]\n%[^\t\n]\n%[^\t\n]",readCommand,readPath,fileSize);
        if(strcmp(readCommand,"DIR")==0){
            struct stat st = {0};
            if (stat(readPath, &st) == -1) {
                mkdir(readPath, 0700);
            }
            send(socketFd,"no",3,0);//if dir does not exist it is creted now no need to send files
        }else if(strcmp(readCommand,"FILE")==0){
            long totalFileSize = atol(fileSize);
            struct stat st = {0};
            if (stat(readPath, &st) == -1) {
                fprintf(stderr,"%s is missing.\n",readPath);

                FILE* fp = fopen( readPath, "wb");
                if(totalFileSize==0){
                    fclose(fp);
                    send(socketFd,"no",3,0);//file exists no need to receive
                    continue;
                }
                send(socketFd,"yes",4,0);//file is missing from client, start receiving file
                ssize_t tot=0,b;
                if(fp != NULL) {
                    while ((b = recv(socketFd, receiveBuffer, 1024, 0)) > 0) {
                        tot += b;
                        fwrite(receiveBuffer, 1, b, fp);
                        if(tot==totalFileSize){
                            break;
                        }
                    }

                    fprintf(stderr,"Received missing file %s from server\n",readPath);
                    stat(readPath, &st);

                    struct timespec modified = st.st_ctim;
                    sprintf(sendBuffer,"%ld\n",modified.tv_sec);

                    send(socketFd , sendBuffer , strlen(sendBuffer)+1 , 0 );

                    fclose(fp);
                }
            }else{
                send(socketFd,"no",3,0);//file exists no need to receive
            }

        }else if(strcmp(readCommand,"EXIT")==0){
            break;
        }

    }

}

int inotifyFd;
void signalHandler(int sig){
    if(sig==SIGALRM){
        syncPaths(clientPath,inotifyFd,1);
        alarm(4);
    }else{
        if(sig==SIGPIPE){
            fprintf(stderr,"Connection was closed...\n");
        }
        close(inotifyFd);
        close(socketFd);
        printf("\nExiting client...\n\n");
        exit(0);
    }


}

int main(int argc, char  *argv[]){

    signal(SIGALRM,signalHandler);
    if (signal(SIGINT, signalHandler) == SIG_ERR)
        printf("\nCan't catch SIGINT\n");

    if (signal(SIGTERM, signalHandler) == SIG_ERR)
        printf("\nCan't catch SIGTERM\n");

    if (signal(SIGQUIT, signalHandler) == SIG_ERR)
        printf("\nCan't catch SIGQUIT\n");
    if (signal(SIGPIPE, signalHandler) == SIG_ERR)
        printf("\nCan't catch SIGPIPE\n");
    alarm(4);

     char * ip;
    int port;

    if(argc<4){
        printf("BibakBOXClient [dirName] [ip address] [portnumber] \n");
        return 0;
    }else{
        port = atoi(argv[3]);
        ip = argv[2];
        clientPath = argv[1];
    }
    struct stat st = {0};
    if (stat(clientPath, &st) == -1) {
        printf("The folder %s does not exist!\n",clientPath);
        return -1;
    }


    socketFd = connectToServer(ip,port);

    if(recv( socketFd , receiveBuffer, 1024,0)<=0){
        fprintf(stderr,"Connection was closed!\n");
        return 0;
    }
    if(strcmp(receiveBuffer,"yes")==0){
        send(socketFd,clientPath,strlen(clientPath)+1,0);
        recv( socketFd , receiveBuffer, 1024,0);
        if(strcmp(receiveBuffer,"yes")==0){
            printf("Successfully connected to server !\n");
        }else{
            printf("The path %s already used by another client !\n",clientPath);
            return 0;
        }

        fflush(stdout);
    }else if(strcmp(receiveBuffer,"max")==0){
        fprintf(stderr,"Maximum number of clients already connected !\n");
        close(socketFd);
        return 1;
    }else{
        fprintf(stderr,"Could not connect properly !\n");
        close(socketFd);
        return 1;
    }

    inotifyFd = inotify_init();
    char buffer2[EVENT_BUF_LEN];
    char sendBuffer[255];

    printf("Syncing files !\n");
    syncPaths(clientPath,inotifyFd,1);
    printf("Checking for missing files !\n");
    syncMissingFiles(clientPath);

    printf("Everything is up to date !\n");
    fflush(stdout);
    while(1){
        if ( read( inotifyFd, buffer2, EVENT_BUF_LEN) <= 0 ) {
            fprintf(stderr,"An error occured reading inotify events.");
        }
        struct inotify_event *event = ( struct inotify_event * ) buffer2;
        if ( event->len ) {
            if ( event->mask & IN_CREATE ) {
                if ( event->mask & IN_ISDIR ) {
                    sprintf(sendBuffer, "New directory %s  from %s\n", event->name,getPath(event->wd) );
                    char * pathall = strcon(getPath(event->wd),"/",event->name);
                    int watchFd = inotify_add_watch( inotifyFd, pathall, IN_CLOSE_WRITE | IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO);
                    addToListenPath(watchFd,pathall);
                    syncFolder(pathall);

                }
                else {
                    syncFile(getPath(event->wd),event->name);
                }
            }
            else if ( event->mask & IN_DELETE ) {
                if ( event->mask & IN_ISDIR ) {
                    syncFolderDelete(getPath(event->wd),event->name);
                }
                else {
                    syncFileDelete(getPath(event->wd),event->name);
                }
            }
            else if ( event->mask & IN_MODIFY ) {
                syncFile(getPath(event->wd),event->name);
            }
            else if ( event->mask & IN_CLOSE_WRITE ) {
                syncFile(getPath(event->wd),event->name);
            }
                /*else if ( event->mask & IN_ATTRIB ) {
                    syncFile(getPath(event->wd),event->name);
                }*/
            else if ( event->mask & IN_MOVED_FROM ) {
                if ( event->mask & IN_ISDIR ) {
                    syncFolderDelete(getPath(event->wd),event->name);
                }
                else {
                    syncFileDelete(getPath(event->wd),event->name);
                }
            }
            else if ( event->mask & IN_MOVED_TO ) {
                if ( event->mask & IN_ISDIR ) {
                    char * pathall = strcon(getPath(event->wd),"/",event->name);
                    syncFolder(pathall);
                }
                else {
                    syncFile(getPath(event->wd),event->name);
                }
            }

        }
    }

    return 0;
}

