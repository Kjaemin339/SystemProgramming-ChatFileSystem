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
extern void print_chat(const char *format, ...);
extern void print_chat_msg(const char *sender, const char *text);   // 추가
extern void handle_chat_message(Message *msg);                      // 있으면 사용
extern void redraw_chat_window(void);    

int sock;
char username[MAX_NAME];
int ra;

// 다운로드 상태
volatile int g_downloading = 0;
FILE *g_download_fp = NULL;
char g_download_name[256];
long g_download_total = 0;

// UI Windows
WINDOW *win_header = NULL;
WINDOW *win_chat   = NULL;
WINDOW *win_input  = NULL;

// 리사이즈 플래그 (시그널 핸들러에서만 건드림)
volatile sig_atomic_t g_need_resize = 0;

/* ----------------------- UI 관련 함수 ----------------------- */

// 화면 크기에 맞춰 윈도우들을 다시 만든다
void create_windows(void) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    // 기존 윈도우 있으면 삭제
    if (win_header) { delwin(win_header); win_header = NULL; }
    if (win_chat)   { delwin(win_chat);   win_chat   = NULL; }
    if (win_input)  { delwin(win_input);  win_input  = NULL; }

    // 너무 작은 터미널이면 안내만 띄움
    if (rows < 10 || cols < 40) {
        erase();
        mvprintw(0, 0, "Terminal too small! (min 40 x 10)");
        refresh();
        return;
    }

    // Header (top)
    win_header = newwin(3, cols, 0, 0);
    box(win_header, 0, 0);
    mvwprintw(win_header, 1, 2, "Chat & File Transfer System v1.0");
    if (username[0] != '\0') {
        mvwprintw(win_header, 1, cols - (int)strlen(username) - 15,
                  "Logged in as %s", username);
    }
    wrefresh(win_header);

    // Chat window (middle)
    int chat_h = rows - 7;      // 3(header) + 4(input) = 7
    win_chat = newwin(chat_h, cols, 3, 0);
    box(win_chat, 0, 0);
    mvwprintw(win_chat, 1, 2, "CHAT AREA");
    // 자동 스크롤/줄바꿈 허용
    scrollok(win_chat, TRUE);
    wrefresh(win_chat);

    // Input window (bottom)
    win_input = newwin(4, cols, rows - 4, 0);
    box(win_input, 0, 0);
    mvwprintw(win_input, 1, 2, "> ");
    wrefresh(win_input);
}

// ncurses 초기화 + 윈도우 생성
void init_ui(void) {
    initscr();
    cbreak();
    noecho();
    curs_set(1);
    keypad(stdscr, TRUE);
    create_windows();
}

/* ----------------------- 시그널 핸들러 ----------------------- */

void handle_resize(int sig) {
    (void)sig;         // unused
    g_need_resize = 1; // 실제 작업은 메인 루프에서
}

/* ----------------------- 유틸 ----------------------- */

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

/* ----------------------- recv_thread ----------------------- */

void *recv_thread(void *arg) {
    (void)arg;
    Message msg;

    while (1) {
        ssize_t len = recv_all(sock, &msg, sizeof(Message));
        if (len <= 0) {
            print_chat("Server disconnected");
            endwin();
            exit(0);
        }

        // 파일 다운로드 처리
        if (g_downloading && (msg.type == MSG_FILE_DATA || msg.type == MSG_FILE_END)) {

            if (msg.type == MSG_FILE_DATA && g_download_fp) {
                fwrite(msg.data, 1, msg.data_len, g_download_fp);
                g_download_total += msg.data_len;
            }

            if (msg.type == MSG_FILE_END) {
                if (g_download_fp) fclose(g_download_fp);
                print_chat("Download Success: %s (%ld bytes)",
                           g_download_name, g_download_total);

                g_downloading    = 0;
                g_download_fp    = NULL;
                g_download_total = 0;
            }
            continue;
        }

        // 채팅 / 기타 메시지 처리
        if (msg.type == MSG_CHAT) {
            if (strcmp(msg.sender, username) != 0) {
                handle_chat_message(&msg);
            }   
        }
        else if (msg.type == MSG_LOGIN_OK) {
            print_chat("Server: Login Success");
        }
        else if (msg.type == MSG_LOGIN_FAIL) {
            print_chat("Server: Login Fail");
        }
        else if (msg.type == MSG_LIST_RESPONSE) {
            // print_chat("[User List]\n%s", msg.data);
        }
        else {
            print_chat("Server send type=%d", msg.type);
        }
    }

    return NULL;
}

/* ----------------------- main ----------------------- */

int main() {
    setlocale(LC_ALL, "");

    // SIGWINCH 핸들러 등록 (터미널 리사이즈)
    signal(SIGWINCH, handle_resize);

    struct sockaddr_in server_addr;
    Message msg;
    pthread_t recv_tid;

    // UI 초기화
    init_ui();

    // 소켓 생성 및 서버 연결
    sock = socket(AF_INET, SOCK_STREAM, 0);
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        endwin();
        perror("connect failed");
        return 1;
    }

    print_chat("Server Connect Success");
    client_log("Server Connect Success");

    /* ---------------- 로그인 과정 ---------------- */

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

    // 로그인 요청 전송
    msg.type = MSG_LOGIN;
    sprintf(msg.data, "%s %s", id, pw);
    send(sock, &msg, sizeof(msg), 0);
    ra = read(sock, &msg, sizeof(msg));
    if (ra < 0) perror("read");

    if (strcmp(msg.data, "LOGIN_FAIL") == 0) {
        print_chat("Login Failed");
        client_log("Login Failed (%s)", id);
        sleep(1);
        endwin();
        return 0;
    }

    strcpy(username, id);
    print_chat("Login Success! Command: /upload, /download, /exit, /kick, /root, /list");
    client_log("Login Success (%s)", username);

    // 헤더 갱신 (로그인 후)
    if (win_header) {
        int rows, cols;
        getmaxyx(win_header, rows, cols);
        mvwprintw(win_header, 1, cols - (int)strlen(username) - 15,
                  "Logged in as %s", username);
        wrefresh(win_header);
    }

    // 수신 스레드 시작
    pthread_create(&recv_tid, NULL, recv_thread, NULL);

    /* ---------------- 메인 입력 루프 ---------------- */

    char buf[MAX_BUF];

    while (1) {
        if (g_need_resize) {
            g_need_resize = 0;

            endwin();
            refresh();
            clear();
            init_ui();               // 새 크기로 윈도우 재생성

            // ✅ 채팅 히스토리를 이용해 채팅창 다시 그리기
            redraw_chat_window();

            // (선택) 헤더 다시 찍기
            if (username[0] != '\0' && win_header) {
                int rows, cols;
                getmaxyx(win_header, rows, cols);
                mvwprintw(win_header, 1, cols - (int)strlen(username) - 15,
                        "Logged in as %s", username);
                wrefresh(win_header);
            }
        }
        // 입력창 초기화
        werase(win_input);
        box(win_input, 0, 0);
        mvwprintw(win_input, 1, 2, "> ");
        wrefresh(win_input);

        echo();
        wgetnstr(win_input, buf, MAX_BUF - 1);
        noecho();

        /* ---------- Upload ---------- */
        if (strncmp(buf, "/upload ", 8) == 0) {
            char filename[256];
            int ttl_minutes = 0;
            int count;

            count = sscanf(buf + 8, "%255s %d", filename, &ttl_minutes);
            if (count < 1) {
                print_chat("Usage: /upload <filename> [ttl_minutes]");
                continue;
            }
            if (count == 1) ttl_minutes = 0;

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

        /* ---------- Download ---------- */
        else if (strncmp(buf, "/download ", 10) == 0) {
            if (g_downloading) {
                print_chat("Already downloading another file!");
                continue;
            }
            download_file(sock, buf + 10);
            client_log("Download request: %s", buf + 10);
        }

        /* ---------- List ---------- */
        else if (strcmp(buf, "/list") == 0) {
            Message req;
            memset(&req, 0, sizeof(req));
            req.type = MSG_LIST_REQEUST;
            strcpy(req.sender, username);
            send(sock, &req, sizeof(req), 0);
        }

        /* ---------- Exit ---------- */
        else if (strcmp(buf, "/exit") == 0) {
            msg.type = MSG_EXIT;
            strcpy(msg.sender, username);
            send(sock, &msg, sizeof(msg), 0);

            print_chat("Client exit");
            client_log("Client exit");
            endwin();
            break;
        }

        /* ---------- 기본 Chat ---------- */
        else {
        msg.type = MSG_CHAT;
        strcpy(msg.sender, username);
        strcpy(msg.data, buf);
        send(sock, &msg, sizeof(msg), 0);
        client_log("Chat: %s", buf);

        // ✅ 내가 보낸 메시지도 바로 채팅창에 표시 (오른쪽 정렬)
        handle_chat_message(&msg);
        }

    }

    endwin();
    close(sock);
    return 0;
}
