#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "protocol.h"

extern int client_sockets[];
extern char usernames[][MAX_NAME];
extern void server_log(const char *fmt, ...);

#define MAX_CLIENTS 10


/**
 *  전체 사용자에게 메시지 전송 (sender 제외)
 */
void broadcast(int sender_fd, Message *msg, int max_clients) {
    for (int i = 0; i < max_clients; i++) {
        int sd = client_sockets[i];

        if (sd > 0) {
            int sent = send(sd, msg, sizeof(Message), 0);

            if (sent < 0) {
                server_log("Fail Send: socket %d", sd);
                close(sd);
                client_sockets[i] = 0;
            }
        }
    }
}


/**
 *  클라이언트에게 문자열 메시지를 보내는 단축 함수
 */
void send_text(int client_fd, const char *sender, const char *text) {
    Message msg;
    memset(&msg, 0, sizeof(msg));
    
    msg.type = MSG_CHAT;
    strcpy(msg.sender, sender);
    strcpy(msg.data, text);

    send(client_fd, &msg, sizeof(msg), 0);
}

