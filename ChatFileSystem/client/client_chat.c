//채팅 로직
//send_chat_message()
//recv_chat_message()
//메시지 파싱
//출력 포맷 처리 ->UI에 맞게 출력

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include "protocol.h"
#include <ncurses.h>
#include <sys/types.h>
#include <sys/socket.h>

// 외부 UI Window 선언 (client_main.c에서 만들어짐)
extern WINDOW *win_chat;

// 외부 변수
extern int sock;
extern char username[MAX_NAME];

// 채팅창에 출력할 현재 라인 번호
int chat_line = 1;

/**
 * 채팅 메시지를 UI에 출력하는 함수
 */
void print_chat(const char *fmt, ...) {
    int maxy;
    getmaxyx(win_chat, maxy, maxy);

    // 안전 범위: 1 ~ maxy-2
    if (chat_line >= maxy - 2) {
        // 스크롤 처리
        wscrl(win_chat, 1);
        chat_line = maxy - 3;
    }

    va_list args;
    va_start(args, fmt);

    // 다음 줄로 이동
    chat_line++;
    if (chat_line >= maxy - 1)
        chat_line = maxy - 2;

    wmove(win_chat, chat_line, 2);       // safe move
    vw_printw(win_chat, fmt, args);
    va_end(args);

    wrefresh(win_chat);
}


/**
 * 서버로 채팅 메시지를 보내는 함수
 */
void send_chat_message(const char *msg_text) {
    Message msg;
    memset(&msg,0,sizeof(msg));

    msg.type = MSG_CHAT;
    strcpy(msg.sender, username);
    strcpy(msg.data, msg_text);

    send(sock, &msg, sizeof(msg), 0);
}

/**
 * 서버에서 받은 채팅 메시지 처리
 */
void handle_chat_message(Message *msg) {
    print_chat("[%s] %s", msg->sender, msg->data);
}
