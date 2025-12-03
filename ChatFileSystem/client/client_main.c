#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <ncurses.h>
#include <locale.h>
#include <signal.h>
#include "protocol.h"

void upload_file(int sock, const char *filename, const char *username, int ttl_seconds);
void download_file(int sock, const char *filename);
void client_log(const char *fmt, ...);

int sock;
char username[MAX_NAME];
int ra;

// client_main.c
volatile int g_downloading = 0;
FILE *g_download_fp = NULL;
char g_download_name[256];
long g_download_total = 0;


// UI Windows
WINDOW *win_header;
WINDOW *win_chat;
WINDOW *win_input;

void init_ui() {
    initscr();
    cbreak();
    noecho();
    curs_set(1);

    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    // Header (top)
    win_header = newwin(3, cols, 0, 0);
    box(win_header, 0, 0);
    mvwprintw(win_header, 1, 2, "Chat & File Transfer System v1.0");
    wrefresh(win_header);

    // Chat window (middle)
    win_chat = newwin(rows - 7, cols, 3, 0);
    box(win_chat, 0, 0);
    mvwprintw(win_chat, 1, 2, "CHAT AREA");
    wrefresh(win_chat);   // ← 여기까지 OK, print_chat() 호출 금지

    // Input window (bottom)
    win_input = newwin(4, cols, rows - 4, 0);
    box(win_input, 0, 0);
    mvwprintw(win_input, 1, 2, "> ");
    wrefresh(win_input);
}


void handle_resize(int sig) {
    endwin();
    refresh();
    clear();
    init_ui();
}

extern void print_chat(const char *format, ...);


ssize_t recv_all(int sock, void *buf, size_t size) {
    size_t total = 0;

    while (total < size) {
        ssize_t len = recv(sock, (char*)buf + total, size - total, 0);
        if (len <= 0) {
            return len;
        }
        total += len;
    }
    return total;
}


void *recv_thread(void *arg) {
    Message msg;

    while (1) {
        ssize_t len = recv_all(sock, &msg, sizeof(Message));
        if (len <= 0) {
            print_chat("Server disconnected");
            endwin();
            exit(0);
        }

        // ✅ 파일 다운로드 중일 때 파일 관련 메시지 처리
        if (g_downloading && (msg.type == MSG_FILE_DATA || msg.type == MSG_FILE_END)) {

            if (msg.type == MSG_FILE_DATA && g_download_fp) {
                fwrite(msg.data, 1, msg.data_len, g_download_fp);
                g_download_total += msg.data_len;
                // print_chat("...progress...");
            }

            if (msg.type == MSG_FILE_END) {
                if (g_download_fp) fclose(g_download_fp);
                print_chat("Download Success: %s (%ld bytes)", 
                           g_download_name, g_download_total);

                g_downloading = 0;
                g_download_fp = NULL;
                g_download_total = 0;
            }

            continue;   // 이 메시지는 여기서 끝
        }

        // 🔥 채팅/로그인 등 나머지 메시지 처리
        if (msg.type == MSG_CHAT) {
            print_chat("[%s] %s", msg.sender, msg.data);
        }
        else if (msg.type == MSG_LOGIN_OK) {
            print_chat("Server: Login Success");
        }
        else if (msg.type == MSG_LOGIN_FAIL) {
            print_chat("Server: Login Fail");
        }else if (msg.type == MSG_LIST_RESPONSE) {
            //print_chat("[User List]\n%s", msg.data);
        }else {
            print_chat("Server send type=%d", msg.type);
        }
    }

    return NULL;
}

int main() {
    signal(SIGWINCH,handle_resize);
    setlocale(LC_ALL, "");
    struct sockaddr_in server_addr;
    Message msg;
    pthread_t recv_tid;

    // Initialize UI
    init_ui();

    // Create socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        endwin();
        perror("connect failed");
        return 1;
    }

    print_chat("Server Connect Success");
    client_log("Server Connect Success");

    // -------------------------
    // Login 과정
    // -------------------------
    char id[32], pw[32];

    echo();
    mvwprintw(win_input, 1, 2, "ID: ");
    wrefresh(win_input);
    wgetnstr(win_input, id, 31);

    werase(win_input); box(win_input, 0, 0);
    mvwprintw(win_input, 1, 2, "PW: ");
    wrefresh(win_input);
    wgetnstr(win_input, pw, 31);
    noecho();

    werase(win_input); box(win_input, 0, 0);
    mvwprintw(win_input, 1, 2, "> ");
    wrefresh(win_input);

    // Send login
    msg.type = MSG_LOGIN;
    sprintf(msg.data, "%s %s", id, pw);
    send(sock, &msg, sizeof(msg), 0);
    ra = read(sock, &msg, sizeof(msg));

    if(ra < 0){
        perror("write");
    }

    if (strcmp(msg.data, "LOGIN_FAIL") == 0) {
        print_chat("Login Failed");
        client_log("Login Failed (%s)", id);
        sleep(1);
        endwin();
        return 0;
    }

    strcpy(username, id);
    print_chat("Login Success! Command: /upload, /download, /exit");
    client_log("Login Success (%s)", username);

    // UI header update
    mvwprintw(win_header, 1, 40, "Logged in as %s", username);
    wrefresh(win_header);

    // -------------------------
    // recv_thread 시작
    // -------------------------
    pthread_create(&recv_tid, NULL, recv_thread, NULL);

    // -------------------------
    // 메인 입력 루프
    // -------------------------
    char buf[MAX_BUF];

    while (1) {
        werase(win_input);
        box(win_input, 0, 0);
        mvwprintw(win_input, 1, 2, "> ");
        wrefresh(win_input);

        echo();
        wgetnstr(win_input, buf, MAX_BUF - 1);
        noecho();

        // -------------------------
        // Upload
        // -------------------------
        if (strncmp(buf, "/upload ", 8) == 0) {
        char filename[256];
        int ttl_minutes = 0;    // 기본값: 자동 삭제 없음
        int count;

        // buf + 8 이후: "<filename> [ttl_minutes]"
        // 예)
        //   "/upload test.txt"      -> filename="test.txt", ttl_minutes=0
        //   "/upload test.txt 5"    -> filename="test.txt", ttl_minutes=5
        count = sscanf(buf + 8, "%255s %d", filename, &ttl_minutes);

        if (count < 1) {
            print_chat("Usage: /upload <filename> [ttl_minutes]");
            continue;
        }

        if (count == 1) {
            ttl_minutes = 0; // TTL 입력 안 하면 일반 업로드
        }

        int ttl_seconds = ttl_minutes * 60;

        upload_file(sock, filename, username, ttl_seconds);

        if (ttl_minutes > 0) {
            print_chat("Upload request: %s (auto-delete in %d min)",
                       filename, ttl_minutes);
            client_log("Upload: %s (ttl=%d min)", filename, ttl_minutes);
        } else {
            print_chat("Upload request: %s", filename);
            client_log("Upload: %s", filename);
        }
    }
    

        // -------------------------
        // Download (비동기)
        // -------------------------
        else if (strncmp(buf, "/download ", 10) == 0) {

            if (g_downloading) {
                print_chat("Already downloading another file!");
                continue;
            }

            download_file(sock, buf + 10);

            // 이제 파일 데이터는 recv_thread가 모두 처리한다
            client_log("Download request: %s", buf + 10);
        }

        else if(strcmp(buf,"/list") == 0){
            Message req;
            memset(&req,0,sizeof(req));
            req.type = MSG_LIST_REQEUST;
            strcpy(req.sender,username);
            send(sock,&req,sizeof(req),0);
        }

        // -------------------------
        // Exit
        // -------------------------
        else if (strcmp(buf, "/exit") == 0) {
            msg.type = MSG_EXIT;
            strcpy(msg.sender, username);
            send(sock, &msg, sizeof(msg), 0);

            print_chat("Client exit");
            client_log("Client exit");
            endwin();

            break;
        }

        // -------------------------
        // Chat 기본 메시지
        // -------------------------
        else {
            msg.type = MSG_CHAT;
            strcpy(msg.sender, username);
            strcpy(msg.data, buf);
            send(sock, &msg, sizeof(msg), 0);
            client_log("Chat: %s", buf);
        }
    }

    endwin();
    close(sock);
    return 0;
}
