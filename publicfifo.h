#ifndef _PUBLICFIFO_H
#define _PUBLICFIFO_H

int public_fifo_num = 4;
char origin[] = "/home/lqw/SystemProgram/";
char REO_FIFO[] = "/home/lqw/SystemProgram/liuqingwu_register";
char LOGIN_FIFO[] = "/home/lqw/SystemProgram/liuqingwu_login";
char MSG_FIFO[] = "/home/lqw/SystemProgram/liuqingwu_sendmsg";
char LOGOUT_FIFO[] = "/home/lqw/SystemProgram/liuqingwu_logout";
char* public_fifos[] = {REO_FIFO, LOGIN_FIFO, MSG_FIFO, LOGOUT_FIFO};
#endif
