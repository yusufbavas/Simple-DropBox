#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdlib.h>
#include <signal.h>
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/file.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 300

#define PERM (S_IRUSR | S_IWUSR)

void client(int fd,char* directory);
int client_to_server(int fd, char* directory);
void server_to_client(int fd,char* directory,int flag);
int callSocket(char* ip,int port);
void sendfile(int fd, int file_fd);
void recvfile(int fd,int fd_write);
int intlenght(int x);
void handler();
char* pathname(char* path);

int fd;

int main(int argc, char *argv[]){

	if(argc != 4){

		fprintf(stderr, "Usage: ./BibakBOXClient [dirName] [ip address] [portnumber]\n");
		return 1;	
	}
	
	if((fd = callSocket(argv[2],atoi(argv[3]))) == -1)
		return 0;
	
	signal(SIGINT,handler);
	signal(SIGPIPE, SIG_IGN);
	
	char buffer[BUFFER_SIZE];
	char buf[BUFFER_SIZE];
	memset(buffer,'\0',BUFFER_SIZE);
	
	read(fd,buffer,BUFFER_SIZE);
	if (strcmp(buffer,"5") == 0){

		printf("Server Full!\n");
		exit(0);
	}

	memset(buffer,'\0',strlen(argv[1]));
	strcpy(buffer,argv[1]);
	
	if (buffer[strlen(buffer) -1 ] == '/')
		buffer[strlen(buffer) -1 ] = '\0';
	
	sprintf(buf,"%s",pathname(buffer));
	
	gethostname(buffer,HOST_NAME_MAX);
	strcat(buffer,"-");
	strcat(buffer,buf);

	write(fd,buffer,BUFFER_SIZE);

	client(fd,argv[1]);
}
// integer bir degerin uzunlugu bulunur.
int intlenght(int x){
	if (x == 0)
		return 0;
	return 1 + intlenght(x/10);
}
// dosya icerigi buffer size a gore soketten okunur.
void recvfile(int fd,int fd_write){

	char buffer[BUFFER_SIZE];
	char buf[BUFFER_SIZE];
	
	memset(buffer,'\0',BUFFER_SIZE);
	memset(buf,'\0',BUFFER_SIZE);

	if(read(fd,buffer,BUFFER_SIZE) <= 0){
			close(fd);
			printf("Server closed\n");
			exit(1);
		}
	
	while(strcmp(buffer,"-1") !=0 ){

		memset(buf,'\0',BUFFER_SIZE);
		if(read(fd,buf,BUFFER_SIZE) <= 0){
			close(fd);
			printf("Server closed\n");
			exit(1);
		}
		
		if (strcmp(buf,"-1") == 0){
			int count;
			memset(buf,'\0',BUFFER_SIZE);
			sscanf(buffer,"%d",&count);
			
			memcpy(buf,&buffer[intlenght(count)+1],count);
			
			write(fd_write,buf,count);
			break;
		}
		
		else
			write(fd_write,buffer,BUFFER_SIZE);

		memset(buffer,'\0',BUFFER_SIZE);
		memcpy(buffer,buf,BUFFER_SIZE);
		memset(buf,'\0',BUFFER_SIZE);
	}

	close(fd_write);
}
// dosya icerikleri buffer size a gore sokete yazilir.
void sendfile(int fd,int file_fd){
	
	char buffer[BUFFER_SIZE];
	char buf[BUFFER_SIZE];
	memset(buffer,'\0',BUFFER_SIZE);
	int count;
	int total_count = 0;
	do{
		
		memset(buffer,'\0',BUFFER_SIZE);
		count = read(file_fd ,buffer ,BUFFER_SIZE);
		
		if (count != BUFFER_SIZE){

			memset(buf,'\0',BUFFER_SIZE);
			sprintf(buf,"%d %s",count,buffer);
			
			write(fd,buf,BUFFER_SIZE);
		}
		else
			write(fd,buffer,BUFFER_SIZE);
		total_count += count;
	}while(count == BUFFER_SIZE);

	strcpy(buffer,"-1");
	write(fd,buffer,BUFFER_SIZE);

	close(file_fd);
}
// soket baglantisi yapilir.
int callSocket(char *ip, int port){

	struct sockaddr_in sa;
	int s;
	
	memset(&sa,0,sizeof(sa));
	
	if ((s= socket(AF_INET ,SOCK_STREAM,0)) < 0){
		printf("Socket Failed %s\n",strerror(errno));
		return(-1);
	}
	
	sa.sin_family= AF_INET;
	sa.sin_port= htons(port);
	sa.sin_addr.s_addr = inet_addr(ip);

	if (connect(s,(struct sockaddr *)&sa,sizeof(sa) ) < 0) {
		close(s);
		printf("%s\n",strerror(errno) );
		return(-1);
	}
	return(s);
}
/*
serverdan clienta veri akisi saglanir.
Eger dosya clientta yok ise dosya serverdan alinir veya serverdan silinmesi saglanir.
*/
void server_to_client(int fd, char* directory,int flag){

	char buffer[BUFFER_SIZE];
	char filename[BUFFER_SIZE];
	
	struct stat statbuf;

	while(1){
		int size;
		memset(buffer,'\0',BUFFER_SIZE);
		if(read(fd,buffer,BUFFER_SIZE) <= 0){
			close(fd);
			printf("Server closed\n");
			exit(1);
		}
		if (strcmp(buffer,"-1") == 0)
			break;
		
		memset(filename,'\0',BUFFER_SIZE);
		sscanf(buffer,"%s %d",filename,&size);

		if (size == -2){
			
			strcpy(buffer,directory);
			if( buffer[strlen(buffer)-1] != '/')
				strcat(buffer,"/");
			strcat(buffer,filename);
			
			if (stat(buffer,&statbuf) == -1){

				if (flag == 0){
					memset(filename,'\0',BUFFER_SIZE);
					strcpy(filename,"-1");
					write(fd,filename,BUFFER_SIZE);
					mkdir(buffer,0777);
					server_to_client(fd,buffer,flag);
				}
				else{
					memset(filename,'\0',BUFFER_SIZE);
					strcpy(filename,"-2");
					write(fd,filename,BUFFER_SIZE);
				}
			}
			else{
				memset(filename,'\0',BUFFER_SIZE);
				strcpy(filename,"-1");
				write(fd,filename,BUFFER_SIZE);
				mkdir(buffer,0777);
				server_to_client(fd,buffer,flag);
				
			}

			continue;
		}

		char *path = (char*) malloc(strlen(directory) + strlen(filename) + 2);

		strcpy(path,directory);
		if( path[strlen(path)-1] != '/')
			strcat(path,"/");
		
		strcat(path,filename);

		if(stat(path,&statbuf) == -1){
			
			strcpy(buffer,"1");
			write(fd,buffer,BUFFER_SIZE);
			if(flag == 0){
				int fd_write = open(path, O_WRONLY | O_CREAT | O_TRUNC,PERM);
				recvfile(fd,fd_write);
			}	
		}
		else{
			strcpy(buffer,"0");
			write(fd,buffer,BUFFER_SIZE);
		}
		free(path);
	}
}
/*
clienttan servera veri akisi saglanir.
client directory gezilir ve dosyalarin isimleri ve degistirilme tarihleri sokete yailir.
eger dosya serverda yoksa veya degistirilme tarihi daha eskiyse dosya servera kopyalanir.
*/
int client_to_server(int fd, char* directory){
	
	char buffer[BUFFER_SIZE];
	struct stat statbuf;
	struct dirent *direntptr;
	DIR *dirptr;
	
	if((dirptr = opendir(directory)) == NULL){
		printf("Failed to open directory\n");
		exit(0);
	}
	
	while (dirptr != NULL && (direntptr = readdir(dirptr)) != NULL){

		if(strcmp(direntptr->d_name,".") == 0 || strcmp(direntptr->d_name,"..") == 0)
			continue;

		char *file = (char*) malloc(strlen(directory) + strlen(direntptr->d_name) + 2);

		strcpy(file,directory);
		if( file[strlen(file)-1] != '/')
			strcat(file,"/");
		
		strcat(file,direntptr->d_name);

		if(lstat(file,&statbuf) < 0){
			free(file);
			continue;
		}
		if(S_ISDIR(statbuf.st_mode)){

			memset(buffer,'\0',BUFFER_SIZE);
			sprintf(buffer,"%s -2",direntptr->d_name);
			write(fd,buffer,BUFFER_SIZE);
			if(client_to_server(fd,file) == -1){
				free(file);
				closedir(dirptr);
				return -1;
			}
			free(file);
			continue;
		}

		if(!S_ISREG(statbuf.st_mode)){
			free(file);
			continue;
		}
		memset(buffer,'\0',BUFFER_SIZE);
		
		sprintf(buffer,"%s %ld",direntptr->d_name,statbuf.st_mtime);
		
		if(write(fd,buffer,BUFFER_SIZE) <= 0){
			close(fd);
			free(file);
			closedir(dirptr);
			printf("Server closed\n");
			return -1;
		}

		if(read(fd,buffer,BUFFER_SIZE) <= 0){
			close(fd);
			free(file);
			closedir(dirptr);
			printf("Server closed\n");
			return -1;
		}

		if (strcmp(buffer,"1") == 0){

			int file_fd = open(file, O_RDONLY,PERM);
			sendfile(fd,file_fd);
		}
		free(file);
	}

	closedir(dirptr);
	memset(buffer,'\0',BUFFER_SIZE);
	strcpy(buffer,"-1");
	write(fd,buffer,BUFFER_SIZE);

	return 0;
}


void client(int fd, char* directory){

	int flag = 0;
	while(1){
		server_to_client(fd,directory,flag);
		if(flag == 1)
			sleep(2);
		flag = 1;
		if(client_to_server(fd,directory) == -1)
			break;		
	}
}
void handler(){

	char buffer[BUFFER_SIZE];
	memset(buffer,'\0',BUFFER_SIZE);
	strcpy(buffer,"-2");
	write(fd,buffer,BUFFER_SIZE);
	close(fd);
	exit(0);
}
// path parse edilecerek dosya adi bulunur.
char* pathname(char * path){

	char* result;
	if((result = strstr(path,"/")) != 0)
		return pathname(&result[1]);
	return path;
	
}