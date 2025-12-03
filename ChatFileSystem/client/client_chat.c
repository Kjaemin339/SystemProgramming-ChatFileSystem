// client_chat.c
// 채팅 로직: send_chat_message / handle_chat_message / 출력 포맷

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <ncurses.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "protocol.h"

// 외부 UI Window 선언 (client_main.c에서 생성)
extern WINDOW *win_chat;

// 외부 변수 (client_main.c에서 정의)
extern int  sock;
extern char username[MAX_NAME];

// =====================
//   채팅 히스토리
// =====================

#define MAX_HISTORY 1000

typedef struct {
    char text[1024];
    int  right_align;   // 0: 왼쪽, 1: 오른쪽
} ChatLine;

static ChatLine chat_history[MAX_HISTORY];
static int      chat_history_count = 0;

// 채팅창 현재 줄 (테두리 안쪽 기준)
static int chat_cur_line = 1;

// 히스토리에 한 줄 추가
static void push_history(const char *text, int right_align) {
    if (chat_history_count >= MAX_HISTORY) {
        // 큐처럼 앞을 하나 밀어내기
        memmove(&chat_history[0],
                &chat_history[1],
                sizeof(ChatLine) * (MAX_HISTORY - 1));
        chat_history_count = MAX_HISTORY - 1;
    }

    strncpy(chat_history[chat_history_count].text,
            text,
            sizeof(chat_history[chat_history_count].text) - 1);
    chat_history[chat_history_count].text
        [sizeof(chat_history[chat_history_count].text) - 1] = '\0';

    chat_history[chat_history_count].right_align = right_align;
    chat_history_count++;
}

// =====================
//   내부 출력 헬퍼
// =====================

/**
 * 한 줄을 채팅창에 추가
 * right_align = 0 → 왼쪽 정렬
 * right_align = 1 → 오른쪽 정렬
 */
static void add_chat_line(const char *text, int right_align) {
    if (!win_chat) return;

    int maxy, maxx;
    getmaxyx(win_chat, maxy, maxx);

    int inner_height = maxy - 2;   // 테두리 제외 높이
    int inner_width  = maxx - 2;   // 테두리 제외 폭

    // 맨 아래까지 내려갔으면 스크롤
    if (chat_cur_line > inner_height) {
        wscrl(win_chat, 1);        // 한 줄 위로 밀기
        chat_cur_line = inner_height;
    }

    int len = (int)strlen(text);
    if (len > inner_width) len = inner_width;   // 너무 길면 잘라서 표시

    int start_col = 1;   // 왼쪽 정렬 기본
    if (right_align && len < inner_width) {
        start_col = 1 + (inner_width - len);    // 오른쪽 정렬 시작 위치
    }

    // 해당 줄 전체를 공백으로 지우고
    mvwhline(win_chat, chat_cur_line, 1, ' ', inner_width);
    // 원하는 위치에 텍스트 출력
    mvwprintw(win_chat, chat_cur_line, start_col, "%.*s", len, text);

    chat_cur_line++;

    box(win_chat, 0, 0);        // 테두리 다시 그리기
    wrefresh(win_chat);
}

// =====================
//   외부에서 사용하는 함수들
// =====================

/**
 * 시스템/안내 메시지용 (항상 왼쪽 정렬)
 * 예: "Server Connect Success", "Login Failed" 등
 */
void print_chat(const char *fmt, ...) {
    char buf[1024];

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    push_history(buf, 0);       // 히스토리에 저장 (왼쪽)
    add_chat_line(buf, 0);      // 화면에 출력
}

/**
 * 채팅 메시지 출력용
 *  - 내가 보낸 메시지: 오른쪽
 *  - 남이 보낸 메시지: 왼쪽
 */
void print_chat_msg(const char *sender, const char *text) {
    char line[1024];
    snprintf(line, sizeof(line), "[%s] %s", sender, text);

    int is_self = (strcmp(sender, username) == 0);

    push_history(line, is_self);      // 히스토리에 저장
    add_chat_line(line, is_self);     // 화면에 출력
}

/**
 * 리사이즈 후 채팅창을 다시 그릴 때 사용
 * (client_main.c에서 리사이즈 처리 후 호출)
 */
void redraw_chat_window(void) {
    if (!win_chat) return;

    int maxy, maxx;
    getmaxyx(win_chat, maxy, maxx);
    int inner_height = maxy - 2;

    werase(win_chat);
    box(win_chat, 0, 0);
    mvwprintw(win_chat, 1, 2, "CHAT AREA");

    // 제목 아래줄부터 시작
    chat_cur_line = 2;

    // 화면에 다 못 올리면 마지막 N줄만 보여줌
    int start = 0;
    int available_lines = inner_height - 1; // 1줄은 "CHAT AREA" 아래줄

    if (chat_history_count > available_lines) {
        start = chat_history_count - available_lines;
    }

    for (int i = start; i < chat_history_count; i++) {
        add_chat_line(chat_history[i].text,
                      chat_history[i].right_align);
    }
}

/**
 * 서버로 채팅 메시지를 보내는 함수
 */
void send_chat_message(const char *msg_text) {
    Message msg;
    memset(&msg, 0, sizeof(msg));

    msg.type = MSG_CHAT;
    strcpy(msg.sender, username);
    strcpy(msg.data, msg_text);

    send(sock, &msg, sizeof(msg), 0);
}

/**
 * 서버에서 받은 채팅 메시지 처리
 */
void handle_chat_message(Message *msg) {
    // sender가 나인지에 따라 print_chat_msg가 알아서 좌/우 정렬
    print_chat_msg(msg->sender, msg->data);
}
