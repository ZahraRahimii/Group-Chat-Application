#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <signal.h>

#define MAX_CLIENTS 100
#define BUFFER_SZ 2048
#define MAX_CLIENTS 100
#define BUFFER_SZ 2048
#define MAX_GROUPS_AMOUNT 20

static _Atomic unsigned int cli_count = 0;
static int uid = 10;

/* Client structure */
typedef struct{
    struct sockaddr_in address;
    int sockfd;
    int uid;
    char gid [MAX_GROUPS_AMOUNT];
    int groups_num;
    char name[32];
} client_t;

client_t *clients[MAX_CLIENTS];

pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

void str_overwrite_stdout() {
    printf("\r%s", "> ");
    fflush(stdout);
}

void str_trim_lf (char* arr, int length) {
    int i;
    for (i = 0; i < length; i++) { // trim \n
        if (arr[i] == '\n') {
            arr[i] = '\0';
            break;
        }
    }
}

void print_client_addr(struct sockaddr_in addr){
    printf("%d.%d.%d.%d",
           addr.sin_addr.s_addr & 0xff,
           (addr.sin_addr.s_addr & 0xff00) >> 8,
           (addr.sin_addr.s_addr & 0xff0000) >> 16,
           (addr.sin_addr.s_addr & 0xff000000) >> 24);
}

/* Add clients to queue */
void queue_add(client_t *cl){
    pthread_mutex_lock(&clients_mutex);

    for(int i=0; i < MAX_CLIENTS; ++i){
        if(!clients[i]){
            clients[i] = cl;
            break;
        }
    }

    pthread_mutex_unlock(&clients_mutex);
}

/* Remove clients to queue */
void queue_remove(int uid){
    pthread_mutex_lock(&clients_mutex);

    for(int i=0; i < MAX_CLIENTS; ++i){
        if(clients[i]){
            if(clients[i]->uid == uid){
                clients[i] = NULL;
                break;
            }
        }
    }

    pthread_mutex_unlock(&clients_mutex);
}

/* Send message to all clients except sender */
void send_message(char *s, int uid, int gid){
    pthread_mutex_lock(&clients_mutex);

    for(int i=0; i<MAX_CLIENTS; ++i){
        if(clients[i]){
            if(clients[i]->uid != uid) {
                for (int j=0; j < (clients[i]->groups_num); j++){
                    if (clients[i]->gid[j] == gid) {
                        if (write(clients[i]->sockfd, s, strlen(s)) < 0) {
                            perror("ERROR: write to descriptor failed");
                            break;
                        }
                    }
                }
            }
        }
    }

    pthread_mutex_unlock(&clients_mutex);
}

void send_message_to_client_self(char *s, client_t * cli){
    pthread_mutex_lock(&clients_mutex);
    if (write(cli->sockfd, s, strlen(s)) < 0) {
        perror("ERROR: write to descriptor failed");
    }

    pthread_mutex_unlock(&clients_mutex);
}

/* Handle all communication with the client */
void *handle_client(void *arg){
    char buff_out[BUFFER_SZ];
    char response[BUFFER_SZ];
    char name[32];
    int leave_flag = 0;

    cli_count++;
    client_t *cli = (client_t *)arg;

    // Name
    if(recv(cli->sockfd, name, 32, 0) <= 0 || strlen(name) <  2 || strlen(name) >= 32-1){
        printf("Didn't enter the name.\n");
        leave_flag = 1;
    } else{
        strcpy(cli->name, name);
        sprintf(buff_out, "%s has joined\n", cli->name);
        printf("%s", buff_out);
//        send_message(buff_out, cli->uid, );
    }

    bzero(buff_out, BUFFER_SZ);

    while(leave_flag != 1){
        
        buff_out[0] = '\0';
        int receive = recv(cli->sockfd, buff_out, BUFFER_SZ, 0);
        if (receive > 0){
            if (strstr(buff_out, "join")){//join gid
                cli->gid[cli->groups_num] = buff_out[5] -'0';

                sprintf(response, "%s has joined the group\n", cli->name);
                send_message(response, cli->uid, (buff_out[5] - '0'));
                printf("%s joined group with id %d\n", cli->name, (buff_out[5] - '0'));
                cli->groups_num += 1;

            } else if (strstr(buff_out, "send")){
                int i, joined_flag = 0;
                int group_id = buff_out[5] - '0';
                int l = 0;
		char name_sent[40];
		for ( i = 0; i < cli->groups_num; i++){
		    if (cli -> gid[i] == group_id) {
			joined_flag = 1;
		    }
		}
		
		if (joined_flag == 1) {
		    int l = 0;
			sprintf(name_sent, "%s : ", cli->name); 
		    for (i = 7; i < sizeof(buff_out); i++) {
			if (buff_out[i] != '\0' || buff_out[i] != ' ') {
			    response[l++] = buff_out[i];
			}
		    }
		    send_message(name_sent, cli->uid, group_id);
		    send_message(response, cli->uid, group_id);
		    printf("%s sent from %s to group %d\n", response, cli->name, group_id);
		} else {
		    sprintf(response, "You are not allowed to send message to group that haven't joined yet\n please join and then send\n");
		    send_message_to_client_self(response, cli);
		}
                
            } else if (strstr(buff_out, "leave")) {
                int joined_flag = 0;
                int group_id = buff_out[6] - '0';
                for (int i = 0; i < cli->groups_num; i++){
                    if (cli -> gid[i] == group_id) {
                        joined_flag = 1;
                    }
                }
                if (cli->groups_num >= 1) {
                    if (joined_flag == 0) {
                        printf("You have not joined to this group\n");

                    } else {
                        sprintf(response, "%s has left the group\n", cli->name);
                        send_message(response, cli->uid, group_id);
                        cli->gid[group_id] = -1;
                        cli->groups_num -= 1;
                    }
                } else {
                    printf("You have not joined to any group\n");
                }
            } else if (strstr(buff_out, "quit")) {
                sprintf(response, "%s has left\n", cli->name);
                for (int i = 0; i < cli->groups_num; i++){
                    send_message(response, cli->uid, cli->gid[i]);
                }
		leave_flag = 1;

                //return EXIT_SUCCESS;
            }

        } else if (receive == 0){
            //sprintf(response, "%s has left\n", cli->name);
//for (int i = 0; i < cli->groups_num; i++){
// send_message(response, cli->uid, cli->gid[i]);
//}

            leave_flag = 1;
        } else {
            printf("ERROR: -1\n");
            leave_flag = 1;
        }

        bzero(buff_out, BUFFER_SZ);
        bzero(response, BUFFER_SZ);
    }

    /* Delete client from queue and yield thread */

    sprintf(response, "Thank you for your support\n Please come back soon\n");
    send_message_to_client_self(response, cli);
    close(cli->sockfd);
    queue_remove(cli->uid);
    free(cli);
    cli_count--;
    pthread_detach(pthread_self());

    return NULL;
}

int main(int argc, char **argv){
    if(argc != 2){
        printf("Usage: %s <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    char *ip = "127.0.0.1";
    int port = atoi(argv[1]);
    int option = 1;
    int listenfd = 0, connfd = 0;
    struct sockaddr_in serv_addr;
    struct sockaddr_in cli_addr;
    pthread_t tid;

    /* Socket settings */
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(ip);
    serv_addr.sin_port = htons(port);

    /* Ignore pipe signals */
    signal(SIGPIPE, SIG_IGN);

    if(setsockopt(listenfd, SOL_SOCKET,(SO_REUSEADDR),(char*)&option,sizeof(option)) < 0){
        perror("ERROR: setsockopt failed");
        return EXIT_FAILURE;
    }

    /* Bind */
    if(bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("ERROR: Socket binding failed");
        return EXIT_FAILURE;
    }

    /* Listen */
    if (listen(listenfd, 10) < 0) {
        perror("ERROR: Socket listening failed");
        return EXIT_FAILURE;
    }

    printf("=== WELCOME TO THE CHATROOM ===\n");

    while(1){
        socklen_t clilen = sizeof(cli_addr);
        connfd = accept(listenfd, (struct sockaddr*)&cli_addr, &clilen);

        /* Check if max clients is reached */
        if((cli_count + 1) == MAX_CLIENTS){
            printf("Max clients reached. Rejected: ");
            print_client_addr(cli_addr);
            printf(":%d\n", cli_addr.sin_port);
            close(connfd);
            continue;
        }

        /* Client settings */
        client_t *cli = (client_t *)malloc(sizeof(client_t));
        cli->address = cli_addr;
        cli->sockfd = connfd;
        cli->uid = uid++;

        /* Add client to the queue and fork thread */
        queue_add(cli);
        pthread_create(&tid, NULL, &handle_client, (void*)cli);

        /* Reduce CPU usage */
        sleep(1);
    }

    return EXIT_SUCCESS;
}

	
	
	

