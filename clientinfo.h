#ifndef _CLIENTINFO_H
#define _CLIENTINFO_H
enum PUBLIC_FIFO { REO, LOG, MSG, LOGOUT };
typedef struct {
  enum PUBLIC_FIFO fifoType;
  char username[128];
  char receiver[128];
  char content[512];
  char time[256];
} CLIENTINFO, *CLIENTINFOPTR;
#endif
