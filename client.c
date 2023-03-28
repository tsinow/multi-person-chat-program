#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "clientinfo.h"
#include "publicfifo.h"
#include "threadPool.h"

#define MAX_LEN 512

char* getTime() {
  time_t now;
  time(&now);
  char* time = ctime(&now);
  time[strcspn(time, "\n")] = 0;
  return time;
}

int openFifo(const char* fifoName, int flag) {
  int res, fifo_fd;
  if (access(fifoName, F_OK) == -1) {
    res = mkfifo(fifoName, 0777);
    if (res != 0) {
      printf("FIFO %s was not created\n", fifoName);
      exit(1);
    }
  }
  if (flag == 0)
    fifo_fd = open(fifoName, O_RDONLY | O_NONBLOCK);
  else if (flag == 1)
    fifo_fd = open(fifoName, O_WRONLY | O_NONBLOCK);

  if (fifo_fd == -1) {
    printf("could not open %s\n", fifoName);
    exit(1);
  }
  return fifo_fd;
}

// void print(CLIENTINFO info) {
//   printf("client:%d,%s,%s,%s\n", info.fifoType, info.username, info.content,
//          info.receiver);
// }

int main() {
  //    login or register user
  int res, fd;
  char username[MAX_LEN];
  char userFifo[MAX_LEN];
  char input[MAX_LEN];
  CLIENTINFO info;
  printf("Please register or login first\n");
  printf("I want to ");
  fgets(input, MAX_LEN, stdin);
  input[strcspn(input, "\n")] = 0;
  if (!strcmp(input, "login")) {
    info.fifoType = LOG;
  } else if (!strcmp(input, "register")) {
    info.fifoType = REO;
  } else {
    printf("wrong operator\n");
    exit(1);
  }
  printf("your name is ");
  fgets(input, MAX_LEN, stdin);
  input[strcspn(input, "\n")] = 0;
  strcpy(info.username, input);
  strcpy(username, input);
  printf("your passwd is ");
  fgets(input, MAX_LEN, stdin);
  input[strcspn(input, "\n")] = 0;
  strcpy(info.content, input);

  char tmp_fifo[MAX_LEN];
  strcpy(tmp_fifo, info.username);
  strcat(tmp_fifo, "|");
  strcat(tmp_fifo, info.content);
  int tmp_fd = openFifo(tmp_fifo, 0);

  fd = openFifo(public_fifos[info.fifoType], 1);
  strcpy(info.time, getTime());
  write(fd, &info, sizeof(CLIENTINFO));
  close(fd);

  //    confirm login success from server
  int t = 0;
  while (1) {
    t = read(tmp_fd, &info, sizeof(CLIENTINFO));
    if (!strcmp(info.username, "failure")) {
      printf("%s\n", info.content);
      exit(1);
    } else if (!strcmp(info.username, "success")) {
      printf("login successfully\n");
      break;
    }
  }
  close(tmp_fd);
  unlink(tmp_fifo);
  //    send or receive message
  strcpy(userFifo, username);
  int user_fd = openFifo(userFifo, 0);
  if (user_fd == -1) {
    printf("error\n");
  }
  struct pollfd myFds[2];
  myFds[0].events = POLLIN;
  myFds[0].fd = STDIN_FILENO;
  myFds[1].fd = user_fd;
  myFds[1].events = POLLIN;
  char* str_res;
  poll(myFds, 2, -1);
  while (1) {
    for (int i = 0; i < 2; i++) {
      if (myFds[i].revents & POLLIN) {
        switch (i) {
          case 0:
            // get input from terminal
            str_res = fgets(input, MAX_LEN, stdin);
            if (str_res == NULL) {
              printf("read from stdin error\n");
              continue;
            }
            input[strcspn(input, "\n")] = 0;
            strcpy(info.content, input);
            printf("msg num is ");
            int msg_num;
            fgets(input, MAX_LEN, stdin);
            input[strcspn(input, "\n")] = 0;
            msg_num = atoi(input);
            while (msg_num--) {
              printf("you want to send to ");
              str_res = fgets(input, MAX_LEN, stdin);
              input[strcspn(input, "\n")] = 0;
              strcpy(info.receiver, input);
              strcpy(info.username, username);
              info.fifoType = MSG;

              if (!strcmp(info.receiver, "server")) {
                if (!strcmp(info.content, "bye")) {
                  info.fifoType = LOGOUT;
                }
              }
              fd = openFifo(public_fifos[info.fifoType], 1);
              strcpy(info.time, getTime());
              write(fd, &info, sizeof(CLIENTINFO));
              close(fd);
            }
            break;
          case 1:
            // get input from server
            res = read(user_fd, &info, sizeof(CLIENTINFO));
            if (res == -1) {
              printf("read information from server error\n");
            }
            if (!strcmp(info.receiver, "server")) {
              if (!strcmp(info.content, "bye")) {
                close(user_fd);
                exit(0);
              }
            }
            char res[MAX_LEN];
            strcpy(res, info.username);
            strcat(res, ":");
            strcat(res, info.content);
            strcat(res, "  ");
            strcat(res, info.time);
            printf("%s\n", res);
            break;
        }
        
      }
    }
  }
}