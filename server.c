#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
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
#define nThreads 10
#define MAX_USER 1024

typedef enum { false, true } boolean;

typedef struct {
  char username[256];
  char passwd[512];
  boolean online;
  FILE *log;
  int errorTimes;
  int penaltyTime;
} CLIENT, *CLIENTPTR;

FILE *server_log;
CLIENT clients[MAX_USER];
int client_num = 0;
int online_num = 0;
pthread_mutex_t client_lock = PTHREAD_MUTEX_INITIALIZER;

char *getTime() {
  time_t now;
  time(&now);
  char *time = ctime(&now);
  time[strcspn(time, "\n")] = 0;
  return time;
}

int openFifo(char *fifoName, int flag) {
  int res, fifo_fd;
  if (access(fifoName, F_OK) == -1) {
    res = mkfifo(fifoName, 0777);
    if (res != 0) {
      fprintf(server_log, "FIFO %s was not created\n", fifoName);
      fflush(server_log);
      exit(1);
    }
  }
  if (flag == 0)
    fifo_fd = open(fifoName, O_RDONLY | O_NONBLOCK);
  else if (flag == 1)
    fifo_fd = open(fifoName, O_WRONLY | O_NONBLOCK);

  if (fifo_fd == -1) {
    fprintf(server_log, "could not open %s\n", fifoName);
    fflush(server_log);
    exit(1);
  }
  return fifo_fd;
}

void print(CLIENTINFO info) {
  fprintf(server_log, "server:%d,%s,%s,%s\n", info.fifoType, info.username,
          info.content, info.receiver);
  fflush(server_log);
}

int findClientByName(char username[]) {
  for (int j = 0; j < client_num; ++j)
    if (!strcmp(clients[j].username, username)) {
      return j;
    }
  return -1;
}

void root_own(char *filename) {
  chown(filename, 0, 0);
  chmod(filename, S_IWUSR | S_IRUSR);
}

void print_log(CLIENTINFO *info, int index, boolean send) {
  char log_name[MAX_LEN];
  char log_content[MAX_LEN];
  time_t now;
  CLIENT *client = &clients[index];
  switch (info->fifoType) {
    case REO:
      sprintf(log_name, "%sclient_log/%s_log", origin, client->username);
      clients[client_num].log = fopen(log_name, "w");
      root_own(log_name);
      sprintf(log_content, "(username:%s,register,time:%s)", client->username,
              info->time);
      break;
    case LOG:
      sprintf(log_content, "(username:%s,login,time:%s)", client->username,
              info->time);
      break;
    case MSG:
      if (send == true) {
        sprintf(log_content, "(sender:%s,receiver:%s,time:%s,%s)",
                info->username, info->receiver, info->time, "true");

      } else {
        sprintf(log_content, "(sender:%s,receiver:%s,time:%s,%s)",
                info->username, info->receiver, info->time, "false");
        index = findClientByName(info->username);
        client = &clients[index];
      }
      break;
    case LOGOUT:
      sprintf(log_content, "(username:%s,logout,time:%s)", client->username,
              info->time);
      break;
  }
  fprintf(client->log, "%s\n", log_content);
  fflush(client->log);
}

void broadcast(CLIENTINFO info) {
  char content[MAX_LEN];
  if (info.fifoType == LOGOUT) {
    sprintf(content, "client %s logs out, online client number is %d",
            info.username, online_num);
  } else {
    sprintf(content, "client %s logs in, online client number is %d",
            info.username, online_num);
  }
  char username[MAX_LEN];
  info.fifoType = MSG;
  strcpy(username, info.username);
  strcpy(info.username, "server");
  strcpy(info.content, content);
  strcpy(info.time, getTime());
  int fd = openFifo(public_fifos[info.fifoType], 1);
  for (int i = 0; i < client_num; i++) {
    if (clients[i].online && strcmp(clients[i].username, username)) {
      strcpy(info.receiver, clients[i].username);
      write(fd, &info, sizeof(CLIENTINFO));
    }
  }
  close(fd);
}

void clearBuff(int index) {
  CLIENT client = clients[index];
  char buff_loc[MAX_LEN];
  sprintf(buff_loc, "%sclient_buff/%s_buff", origin, client.username);
  if (access(buff_loc, F_OK) == -1) {
    fprintf(server_log, "file %s no exist\n", buff_loc);
    fflush(server_log);
    return;
  }
  FILE *buffer = fopen(buff_loc, "r");
  char file_content[MAX_LEN];
  CLIENTINFO info;
  int i = 0;
  char userFifo[MAX_LEN];
  strcpy(userFifo, origin);
  strcat(userFifo, client.username);
  int user_fd = open(userFifo, O_WRONLY);
  fprintf(server_log, "user fd is %d\n", user_fd);
  fflush(server_log);
  while (fgets(file_content, MAX_LEN, buffer) != NULL) {
    fprintf(server_log, "%d\n", i);
    fflush(server_log);
    file_content[strcspn(file_content, "\n")] = 0;
    if (i % 4 == 0)
      strcpy(info.username, file_content);
    else if (i % 4 == 1)
      strcpy(info.receiver, file_content);
    else if (i % 4 == 2)
      strcpy(info.content, file_content);
    else if (i % 4 == 3) {
      strcpy(info.time, file_content);
      print_log(&info, index, true);
      write(user_fd, &info, sizeof(CLIENTINFO));
    }
    i++;
  }
  fclose(buffer);
  close(user_fd);
  unlink(buff_loc);
}

void *fun(void *args) {
  CLIENTINFO info = *((CLIENTINFO *)args);
  char sender[MAX_LEN];
  strcpy(sender, info.username);
  int tmp_fd;
  char tmp_fifo[MAX_LEN];
  strcpy(tmp_fifo, origin);
  if (info.fifoType == REO || info.fifoType == LOG) {
    strcat(tmp_fifo, info.username);
    strcat(tmp_fifo, "|");
    strcat(tmp_fifo, info.content);
    tmp_fd = openFifo(tmp_fifo, 1);
  }
  boolean judge;
  int index;
  int user_fd;
  char userFifo[MAX_LEN];

  switch (info.fifoType) {
    case REO:
      judge = true;
      if (client_num >= MAX_USER) {
        judge = false;
        strcpy(info.username, "failure");
        strcpy(info.content, "The number of registered users is full");
      }

      if (findClientByName(info.username) != -1) {
        judge = false;
        strcpy(info.username, "failure");
        strcpy(info.content, "This username already exists");
      }
      if (judge == true) {
        pthread_mutex_lock(&client_lock);
        strcpy(clients[client_num].username, info.username);

        strcpy(clients[client_num].passwd, info.content);
        clients[client_num].online = true;
        clients[client_num].errorTimes = 0;
        clients[client_num].penaltyTime = 0;
        print_log(&info, client_num, 0);
        client_num++;
        online_num++;
        broadcast(info);
        strcpy(info.username, "success");
        pthread_mutex_unlock(&client_lock);
      }
      write(tmp_fd, &info, sizeof(CLIENTINFO));
      close(tmp_fd);
      unlink(tmp_fifo);
      break;

    case LOG:
      index = findClientByName(info.username);
      boolean login_right = false;
      int diffTime;
      if (index == -1) {
        strcpy(info.username, "failure");
        strcpy(info.content, "This user does not exist");
      } else {
        // have this user
        if (clients[index].errorTimes >= 4) {
          diffTime = time(NULL) - clients[index].penaltyTime;
          if (diffTime < 600) {
            strcpy(info.username, "failure");
            sprintf(info.content,
                    "You need to wait another %d seconds to try again",
                    (600 - diffTime));
          } else {
            // Reset penalty
            login_right = true;
            clients[index].errorTimes = 0;
            clients[index].penaltyTime = 0;
          }
        } else {
          login_right = true;
        }
        if (login_right) {
          // have login right
          if (strcmp(clients[index].passwd, info.content)) {
            strcpy(info.username, "failure");
            strcpy(info.content, "Password wrong");
            clients[index].errorTimes++;
            if (clients[index].errorTimes >= 4) {
              clients[index].penaltyTime = time(NULL);
            }
          } else {
            pthread_mutex_lock(&client_lock);
            online_num++;
            print_log(&info, index, 0);
            clients[index].online = true;
            clients[index].errorTimes = 0;
            clients[index].penaltyTime = 0;
            pthread_mutex_unlock(&client_lock);
            broadcast(info);
            strcpy(info.username, "success");
          }
        }
      }
      write(tmp_fd, &info, sizeof(CLIENTINFO));
      close(tmp_fd);
      unlink(tmp_fifo);

      if (!strcmp(info.username, "success")) clearBuff(index);

      break;

    case MSG:
      index = findClientByName(info.receiver);
      strcpy(userFifo, origin);
      if (index == -1) {
        char content[MAX_LEN];
        sprintf(content, "The user %s does not exist", info.receiver);
        strcpy(info.content, content);
        strcat(userFifo, info.username);
        user_fd = open(userFifo, O_WRONLY);
      } else {
        if (clients[index].online == false) {
          char buff_loc[MAX_LEN];
          sprintf(buff_loc, "%sclient_buff/%s_buff", origin,
                  clients[index].username);
          FILE *buffer = fopen(buff_loc, "a");
          fprintf(buffer, "%s\n", info.username);
          fprintf(buffer, "%s\n", info.receiver);
          fprintf(buffer, "%s\n", info.content);
          fprintf(buffer, "%s\n", info.time);
          fflush(buffer);
          fclose(buffer);
          print_log(&info, index, false);
        } else {
          // client is online
          strcat(userFifo, info.receiver);
          user_fd = open(userFifo, O_WRONLY);
          if (strcmp(info.username, "server")) {
            print_log(&info, index, true);
          }
        }
      }
      write(user_fd, &info, sizeof(CLIENTINFO));
      close(user_fd);
      break;

    case LOGOUT:
      index = findClientByName(info.username);
      strcpy(userFifo, origin);
      strcat(userFifo, info.username);
      user_fd = openFifo(userFifo, 1);
      print_log(&info, index, 0);
      write(user_fd, &info, sizeof(CLIENTINFO));
      close(user_fd);
      pthread_mutex_lock(&client_lock);
      clients[index].online = false;
      pthread_mutex_unlock(&client_lock);
      online_num--;
      broadcast(info);
      break;
  }
  return NULL;
}

// function for threadPool
static void *work_routine(void *args) {
  tpool_t *pool = (tpool_t *)args;
  tpool_work_t *work = NULL;

  while (1) {
    pthread_mutex_lock(&pool->queue_lock);
    while (!pool->tpool_head &&
           !pool->shutdown) {  // if there is no works and pool is not shutdown,
                               // it should be suspended for being awake
      pthread_cond_wait(&pool->queue_ready, &pool->queue_lock);
    }

    if (pool->shutdown) {
      pthread_mutex_unlock(
          &pool->queue_lock);  // pool shutdown,release the mutex and exit
      pthread_exit(NULL);
    }

    /* tweak a work*/
    work = pool->tpool_head;
    pool->tpool_head = (tpool_work_t *)pool->tpool_head->next;
    pthread_mutex_unlock(&pool->queue_lock);

    work->work_routine(work->args);

    free(work);
  }
  return NULL;
}

int create_tpool(tpool_t **pool, size_t max_thread_num) {
  (*pool) = (tpool_t *)malloc(sizeof(tpool_t));
  if (NULL == *pool) {
    fprintf(server_log, "in %s,malloc tpool_t failed!,errno = %d,explain:%s\n",
            __func__, errno, strerror(errno));
    fflush(server_log);
    exit(-1);
  }
  (*pool)->shutdown = 0;
  (*pool)->maxnum_thread = max_thread_num;
  (*pool)->thread_id = (pthread_t *)malloc(sizeof(pthread_t) * max_thread_num);
  if ((*pool)->thread_id == NULL) {
    fprintf(server_log, "in %s,init thread id failed,errno = %d,explain:%s",
            __func__, errno, strerror(errno));
    fflush(server_log);
    exit(-1);
  }
  (*pool)->tpool_head = NULL;
  if (pthread_mutex_init(&((*pool)->queue_lock), NULL) != 0) {
    fprintf(server_log, "in %s,initial mutex failed,errno = %d,explain:%s",
            __func__, errno, strerror(errno));
    fflush(server_log);
    exit(-1);
  }

  if (pthread_cond_init(&((*pool)->queue_ready), NULL) != 0) {
    fprintf(server_log,
            "in %s,initial condition variable failed,errno = %d,explain:%s",
            __func__, errno, strerror(errno));
    fflush(server_log);
    exit(-1);
  }

  for (int i = 0; i < max_thread_num; i++) {
    if (pthread_create(&((*pool)->thread_id[i]), NULL, work_routine,
                       (void *)(*pool)) != 0) {
      fprintf(server_log, "pthread_create failed!\n");
      fflush(server_log);
      exit(-1);
    }
  }
  return 0;
}

void destroy_tpool(tpool_t *pool) {
  tpool_work_t *tmp_work;

  if (pool->shutdown) {
    return;
  }
  pool->shutdown = 1;

  pthread_mutex_lock(&pool->queue_lock);
  pthread_cond_broadcast(&pool->queue_ready);
  pthread_mutex_unlock(&pool->queue_lock);

  for (int i = 0; i < pool->maxnum_thread; i++) {
    pthread_join(pool->thread_id[i], NULL);
  }
  free(pool->thread_id);
  while (pool->tpool_head) {
    tmp_work = pool->tpool_head;
    pool->tpool_head = (tpool_work_t *)pool->tpool_head->next;
    free(tmp_work);
  }

  pthread_mutex_destroy(&pool->queue_lock);
  pthread_cond_destroy(&pool->queue_ready);
  free(pool);
}

int add_task_2_tpool(tpool_t *pool, void *(*routine)(void *), void *args) {
  tpool_work_t *work, *member;

  if (!routine) {
    printf("rontine is null!\n");
    return -1;
  }

  work = (tpool_work_t *)malloc(sizeof(tpool_work_t));
  if (!work) {
    fprintf(server_log, "in %s,malloc work error!,errno = %d,explain:%s\n",
            __func__, errno, strerror(errno));
    fflush(server_log);
    return -1;
  }

  work->work_routine = routine;
  work->args = args;
  work->next = NULL;

  pthread_mutex_lock(&pool->queue_lock);
  member = pool->tpool_head;
  if (!member) {
    pool->tpool_head = work;
  } else {
    while (member->next) {
      member = (tpool_work_t *)member->next;
    }
    member->next = work;
  }

  // notify the pool that new task arrived!
  pthread_cond_signal(&pool->queue_ready);
  pthread_mutex_unlock(&pool->queue_lock);
  return 0;
}

int main(void) {
  signal(SIGCHLD, SIG_IGN);
  signal(SIGTERM, SIG_IGN);

  /* Our process ID and Session ID */
  pid_t pid, sid;

  /* Fork off the parent process */
  pid = fork();
  if (pid < 0) {
    exit(EXIT_FAILURE);
  }
  /* If we got a good PID, then
     we can exit the parent process. */
  if (pid > 0) {
    exit(EXIT_SUCCESS);
  }

  /* Change the file mode mask */
  umask(0);

  /* Open any logs here */
  char server_log_loc[MAX_LEN];
  strcpy(server_log_loc, origin);
  strcat(server_log_loc, "server_log");
  server_log = fopen(server_log_loc, "w");
  root_own(server_log_loc);
  char tmp_log[MAX_LEN];
  sprintf(tmp_log, "server is started at %s", getTime());
  fprintf(server_log, "%s\n", tmp_log);
  fflush(server_log);
  /* Create a new SID for the child process */
  sid = setsid();
  if (sid < 0) {
    /* Log the failure */
    exit(EXIT_FAILURE);
  }

  /* Change the current working directory */
  if ((chdir("/")) < 0) {
    /* Log the failure */
    exit(EXIT_FAILURE);
  }
  /* Close out the standard file descriptors */
  close(STDIN_FILENO);
  close(STDOUT_FILENO);
  close(STDERR_FILENO);

  /* Daemon-specific initialization goes here */
  struct pollfd myFds[public_fifo_num];
  int public_fd[public_fifo_num];
  for (int i = 0; i < public_fifo_num; ++i) {
    public_fd[i] = openFifo(public_fifos[i], 0);
    myFds[i].events = POLLIN;
    myFds[i].fd = public_fd[i];
  }
  CLIENTINFO info;
  int event_count;
  enum PUBLIC_FIFO public_fifo;
  tpool_t *pool = NULL;
  if (create_tpool(&pool, nThreads) != 0) {
    exit(1);
  }

  /* The Big Loop */
  while (1) {
    poll(myFds, public_fifo_num, -1);
    for (int i = 0; i < public_fifo_num; ++i) {
      if (myFds[i].revents & POLLIN) {
        int res = read(myFds[i].fd, &info, sizeof(CLIENTINFO));
        add_task_2_tpool(pool, fun, (void *)&info);
      }
    }
  }
  exit(EXIT_SUCCESS);
}