/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include "process.h"
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
static volatile sig_atomic_t stopped;
static void on_signal(int sig) { stopped=sig; }
void fw_reset_stop(void) { stopped=0; }
void fw_signals(void) {
    struct sigaction a={0}; a.sa_handler=on_signal; sigemptyset(&a.sa_mask);
    sigaction(SIGTERM,&a,NULL); sigaction(SIGINT,&a,NULL); sigaction(SIGHUP,&a,NULL);
}
void fw_child_signals(void) {
    signal(SIGTERM,SIG_DFL); signal(SIGINT,SIG_DFL); signal(SIGHUP,SIG_DFL);
}
double fw_now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec+t.tv_nsec/1e9; }
int fw_wait(pid_t pid, double timeout, int event_fd, int *screen_sleep) {
    double deadline=fw_now()+timeout, kill_at=0;
    int reason=0, status=0;
    char buf[512]={0}; size_t used=0;
    for (;;) {
        pid_t got=waitpid(pid,&status,WNOHANG);
        if (got==pid) break;
        if (got<0 && errno!=EINTR) return 125;
        if (!reason && stopped) reason=128+stopped;
        /* timeout <= 0: no deadline (the reader closes on exit or screensaver). */
        if (!reason && timeout>0 && fw_now()>=deadline) reason=124;
        if (event_fd>=0 && !reason) {
            struct pollfd p={event_fd,POLLIN,0};
            if (poll(&p,1,0)>0) {
                ssize_t n=read(event_fd,buf+used,sizeof(buf)-used-1);
                if(n<=0 && (n==0 || (errno!=EAGAIN && errno!=EINTR))) {
                    fprintf(stderr,"power event stream ended; closing session\n"); reason=125;
                } else if(n>0) {
                    used+=(size_t)n; buf[used]=0;
                    if(strstr(buf,"goingToScreenSaver")) { *screen_sleep=1; reason=0x2a; }
                    if(used>sizeof(buf)/2) { memmove(buf,buf+used-64,64); used=64; }
                }
            }
        }
        if(reason && !kill_at) { kill(pid,SIGTERM); kill_at=fw_now()+2; }
        if(kill_at && fw_now()>=kill_at) kill(pid,SIGKILL);
        struct timespec delay={0,20000000}; nanosleep(&delay,NULL);
    }
    if(reason) return reason;
    return WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);
}
int fw_command(char *const argv[], double timeout) {
    pid_t pid=fork();
    if(pid==0) { fw_child_signals(); execv(argv[0],argv); _exit(127); }
    if(pid<0) return 125;
    int sleeping=0; return fw_wait(pid,timeout,-1,&sleeping);
}
