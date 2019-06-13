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
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 300
#define PERM (S_IRUSR | S_IWUSR)
#define logfile "log"
#define server_active "server_active"

void *server();
int server_to_client(char *directory, int fd,int flag, int fd_log);
int client_to_server(char *directory, int fd, int fd_log);
void sendfile(int fd, int file_fd);
void recvfile(int fd, int fd_write);
int intlenght(int x);
void handler();

sem_t empty,full,m,sem_print;
int buf_size;
int push,pop;
char** thread_buf;
int active_thread_count;
pthread_t *tids;
int thread_count;
char* active_directory;
int s;

int main(int argc, char *argv[]){

	if(argc != 4){

		fprintf(stderr, "Usage: ./BibakBOXServer [directory] [threadPoolSize] [portnumber]\n");
		return 1;	
	}

	signal(SIGINT,handler);
	signal(SIGPIPE, SIG_IGN);
	struct stat statbuf;
	
	// server acık oldugu surece calistigi klasore bir dosya olusturur.
	// Eger o dosya varsa orada baska bir serverin calistigin anlar ve sonlanir.
	char buffer[BUFFER_SIZE];
	memset(buffer,'\0',BUFFER_SIZE);
	
	strcpy(buffer,argv[1]);
	if( buffer[strlen(buffer)-1] != '/')
		strcat(buffer,"/");
	strcat(buffer,server_active);

	if (stat(buffer,&statbuf) < 0){
		int fd_write = open(buffer, O_WRONLY | O_CREAT | O_TRUNC,PERM);
		close(fd_write);
	}
	else{
		printf("Another server active in this directory\n");
		exit(0);
	}
	active_directory = (char *) malloc(strlen(buffer)+1);
	memset(active_directory,'\0',strlen(buffer));
	strcpy(active_directory,buffer);
	
	struct sockaddr_in socket_addr;
	memset(&socket_addr, 0, sizeof(struct sockaddr_in));

	if ((s = socket(AF_INET, SOCK_STREAM, 0)) < 0){
		printf("Socket Failed! : %s\n",strerror(errno));
		exit(0);
	}
	
	if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int)) < 0)
    	perror("setsockopt(SO_REUSEADDR) failed");

	socket_addr.sin_family = AF_INET;
    socket_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    socket_addr.sin_port= htons(atoi(argv[3]));

	if (bind(s ,(struct sockaddr *)&socket_addr,sizeof(struct sockaddr_in)) < 0) {
		close(s);
		printf("Bind Failed! : %s\n",strerror(errno));
		exit(0);
	}

	listen(s, atoi(argv[2]));

	sem_init(&empty,0,atoi(argv[2]));
    sem_init(&full,0,0);
    sem_init(&m,0,1);

    buf_size = atoi(argv[2]);
    thread_buf = (char **) malloc(sizeof(char *) * buf_size);


    for (int i = 0; i < buf_size; ++i)
    	thread_buf[i] = (char *) malloc(BUFFER_SIZE);
    push = 0;
    pop = 0;
    // Thread pool sayisi kadar thread olusturulur
    thread_count = atoi(argv[2]);
    tids = (pthread_t *) malloc(sizeof(pthread_t) * atoi(argv[2]));
    for (int i = 0; i < atoi(argv[2]); ++i)
    	pthread_create(&tids[i], NULL, server, NULL);
		
    active_thread_count = 0;
    int fd;
	while(1){
		if ((fd = accept(s,NULL,NULL)) < 0){
			if (errno == EINTR)
				continue;
			perror("accept");
			exit(1);
		}
		// eger threadler doluysa clienta bilgi verilip devam edilir.
		memset(buffer,'\0',BUFFER_SIZE);
		sem_wait(&m);
		if (active_thread_count == atoi(argv[2])){
			strcpy(buffer,"5");
			write(fd,buffer,BUFFER_SIZE);
			sem_post(&m);
			continue;
		}
		strcpy(buffer,"6");
		write(fd,buffer,BUFFER_SIZE);
		sem_post(&m);

		struct sockaddr_in m_addr;
		memset(&m_addr, 0, sizeof(struct sockaddr));
		socklen_t len = sizeof(m_addr);
		getsockname(fd, (struct sockaddr*)&m_addr, &len);
		printf("IP address: %s\n", inet_ntoa(m_addr.sin_addr));

		sem_wait(&empty);
        sem_wait(&m);
        
        active_thread_count++;
        
        sprintf(thread_buf[push],"%s %d",argv[1],fd);
		
		push == buf_size-1 ? push = 0 : push++;

		sem_post(&m);
        sem_post(&full);		
	}
	return 0;
}
// klasorler icersindeki dosyalar ile beraber silinir.
void removedir(char* directory){

	struct dirent *direntptr;
	struct stat statbuf;
	DIR *dirptr;
	
	if((dirptr = opendir(directory)) == NULL){
		printf("Failed to open directory\n");
		exit(0);
	}
	
	while (dirptr != NULL && (direntptr = readdir(dirptr)) != NULL){

		if(strcmp(direntptr->d_name,".") == 0 || strcmp(direntptr->d_name,"..") == 0)
			continue;

		char *file = (char*) malloc(strlen(directory) + strlen(direntptr->d_name) + 3);

		strcpy(file,directory);
		if( file[strlen(file)-1] != '/')
			strcat(file,"/");
		
		strcat(file,direntptr->d_name);

		lstat(file,&statbuf);
		
		if(S_ISDIR(statbuf.st_mode)){
			
			if(rmdir(file) == -1)
				removedir(file);}
		else
			remove(file);	
	}
	rmdir(directory);
	closedir(dirptr);
	
}
/* 
serverdan clienta dosya akisi saglanir.
server kendi directory sini gezerek dosyalarin clienta olup olmadiga bakar.
Eger dosya clientta yok ise dosya serverdan silinir veya clienta aktarilir.
*/
int server_to_client(char *directory, int fd,int flag, int fd_log){

	struct dirent *direntptr;
	struct stat statbuf;
	DIR *dirptr;
	char buffer[BUFFER_SIZE];
	
	if((dirptr = opendir(directory)) == NULL){
		printf("Failed to open directory\n");
		exit(0);
	}
	
	while (dirptr != NULL && (direntptr = readdir(dirptr)) != NULL){

		if(strcmp(direntptr->d_name,".") == 0 || strcmp(direntptr->d_name,"..") == 0)
			continue;
		
		if (strcmp(direntptr->d_name,"log") == 0)
			continue;
		
		char *file = (char*) malloc(strlen(directory) + strlen(direntptr->d_name) + 2);

		strcpy(file,directory);
		if( file[strlen(file)-1] != '/')
			strcat(file,"/");
		
		strcat(file,direntptr->d_name);

		lstat(file,&statbuf);
		
		if(S_ISDIR(statbuf.st_mode)){
			memset(buffer,'\0',BUFFER_SIZE);
			sprintf(buffer,"%s -2",direntptr->d_name);
			write(fd,buffer,BUFFER_SIZE);
			read(fd,buffer,BUFFER_SIZE);
			if (strcmp(buffer,"-1") == 0)		
				server_to_client(file,fd,flag,fd_log);
			
			else{
				memset(buffer,'\0',BUFFER_SIZE);
				sprintf(buffer,"Directory removed : %s\n",direntptr->d_name);
				write(fd_log,buffer,strlen(buffer));
				if(rmdir(file) == -1)
					removedir(file);
			}
			
			free(file);
			continue;
			
		}
		memset(buffer,'\0',BUFFER_SIZE);
		sprintf(buffer,"%s %ld",direntptr->d_name,statbuf.st_size);
		
		write(fd,buffer,BUFFER_SIZE);

		read(fd,buffer,BUFFER_SIZE);
		if (strcmp(buffer,"-2") == 0){

			closedir(dirptr);
			free(file);
			return -1;
		}
		if (strcmp(buffer,"1") == 0){
			
			if(flag == 0){
				int file_fd = open(file, O_RDONLY,PERM);
				sendfile(fd,file_fd);	
			}
			else{
				memset(buffer,'\0',BUFFER_SIZE);
				sprintf(buffer,"File removed : %s\n",direntptr->d_name);
				write(fd_log,buffer,strlen(buffer));
				
				remove(file);
			}
		}
		free(file);
	}
	closedir(dirptr);
	memset(buffer,'\0',BUFFER_SIZE);
	strcpy(buffer,"-1");
	write(fd,buffer,BUFFER_SIZE);
	return 0;
}
/*
clienttan servera veri akisi saglanir. clienttaki dosyalarin server'da olup olamadigina bakilir.
Eger yok ise olusturulup icerigi alinir.
*/
int client_to_server(char *directory, int fd,int fd_log){
	
	struct stat statbuf;
	char buffer[BUFFER_SIZE];
	char filename[BUFFER_SIZE];
	time_t time;
		
	while(1){

		if(read(fd,buffer,BUFFER_SIZE) <= 0){
			printf("Read Failed : %s\n",strerror(errno) );
			return -1;
			break;
		}
		if (strcmp(buffer,"-1") == 0)
			break;
		
		else if(strcmp(buffer,"-2") == 0)
			return -1;
		
		sscanf(buffer,"%s %ld",filename,&time);
		
		if (time == -2){
			
			strcpy(buffer,directory);
			if( buffer[strlen(buffer)-1] != '/')
				strcat(buffer,"/");
			strcat(buffer,filename);
			if(mkdir(buffer,0777) != -1){
				write(fd_log,"Directory created : ",strlen("Directory created : "));
				write(fd_log,filename,strlen(filename));
				write(fd_log,"\n",1);
			}
			
			if(client_to_server(buffer,fd, fd_log) == -1)
			 	return -1;
			continue;
		}
		
		char *path = (char*) malloc(strlen(directory) + strlen(filename) + 2);

		strcpy(path,directory);
		if( path[strlen(path)-1] != '/')
			strcat(path,"/");
		
		strcat(path,filename);
		int st = stat(path,&statbuf); 
		
		if(st == -1 || statbuf.st_mtime < time){
			if (st == -1){

				memset(buffer,'\0',BUFFER_SIZE);
				sprintf(buffer,"File created : %s\n",filename);
				write(fd_log,buffer,strlen(buffer));
			}
			else{
				memset(buffer,'\0',BUFFER_SIZE);
				sprintf(buffer,"File modified : %s\n",filename);
				write(fd_log,buffer,strlen(buffer));
			}
			
			strcpy(buffer,"1");
			write(fd,buffer,BUFFER_SIZE);
			
			int fd_write = open(path, O_WRONLY | O_CREAT | O_TRUNC,PERM);
			recvfile(fd,fd_write);

		}
		else{
			
			strcpy(buffer,"0");
			write(fd,buffer,BUFFER_SIZE);
		}
		free(path);
	}
	return 0;
}

void *server(){

	while(1){

		int fd;
		char buffer[BUFFER_SIZE];
		char buf[BUFFER_SIZE];

		memset(buf,'\0',BUFFER_SIZE);
		memset(buffer,'\0',BUFFER_SIZE);
		
		sem_wait(&full);
	    
	    sem_wait(&m);
	    
	    sscanf(thread_buf[pop],"%s %d",buffer,&fd);
	    pop == buf_size-1 ? pop = 0 : pop++;
	    
	    sem_post(&m);
	    
		int flag = 0;
		
		if( buffer[strlen(buffer)-1] != '/')
				strcat(buffer,"/");

		read(fd,buf,BUFFER_SIZE);
		strcat(buffer,buf);
		mkdir(buffer,0777);
		errno = 0;
		
		strcpy(buf,buffer);
		strcat(buf,"/");
		strcat(buf,logfile);
		int fd_log = open(buf, O_WRONLY | O_CREAT | O_APPEND,PERM);
		write(fd_log,"Client Connected\n",strlen("Client Connected\n"));

		while(1){
			if(server_to_client(buffer,fd,flag,fd_log) == -1)
				break;
			if(client_to_server(buffer,fd,fd_log) == -1)
				break;
			flag = 1;
		}
		close(fd);	
		sem_post(&empty);
		sem_wait(&m);
		active_thread_count--;
		sem_post(&m);
		printf("Client disconnect\n");
		memset(buffer,'\0',BUFFER_SIZE);
		strcpy(buffer,"Client disconnect\n");
		write(fd_log,buffer,strlen(buffer));
				
	}
		
	return NULL;
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

void handler(){
	printf("server closed!\n");
	
	for (int i = 0; i < buf_size; ++i)
    	free(thread_buf[i]);
    free(thread_buf);

    for (int i = 0; i < thread_count; ++i){
    	pthread_cancel(tids[i]);
    	pthread_join(tids[i],NULL);
    }
    free(tids);
	close(s);
    remove(active_directory);
    free(active_directory);
	exit(0);
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

	read(fd,buffer,BUFFER_SIZE);
	
	while(strcmp(buffer,"-1") !=0 ){
		
		memset(buf,'\0',BUFFER_SIZE);
		read(fd,buf,BUFFER_SIZE);
		
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