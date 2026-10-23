/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include "process.h"
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
int main(void) {
    fw_signals();
    char *good[]={"/bin/sh","-c","exit 0",NULL};
    char *bad[]={"/bin/sh","-c","exit 7",NULL};
    assert(fw_command(good,1)==0); assert(fw_command(bad,1)==7);
    int dummy=0; pid_t pid=fork();
    if(pid==0) { fw_child_signals(); raise(SIGKILL); _exit(0); }
    assert(fw_wait(pid,1,-1,&dummy)==128+SIGKILL);
    pid=fork();
    if(pid==0) { fw_child_signals(); for(;;) pause(); }
    double start=fw_now();
    assert(fw_wait(pid,0.1,-1,&dummy)==124);
    assert(fw_now()-start<3);
    /* No deadline: a reader that runs past the old 120 s limit is not killed. */
    pid=fork();
    if(pid==0) { fw_child_signals(); usleep(300000); _exit(5); }
    assert(fw_wait(pid,0,-1,&dummy)==5);
    int p[2]; assert(pipe(p)==0);
    pid=fork();
    if(pid==0) { close(p[0]); close(p[1]); fw_child_signals(); for(;;) pause(); }
    const char message[]="goingToScreenSaver";
    assert(write(p[1],message,sizeof(message)-1)==sizeof(message)-1);
    assert(fw_wait(pid,1,p[0],&dummy)==42 && dummy==1);
    close(p[0]); close(p[1]);
    pid=fork();
    if(pid==0) { fw_child_signals(); signal(SIGTERM,SIG_IGN); for(;;) pause(); }
    /* Give the child time to install its handler before timing out. */
    assert(fw_wait(pid,0.2,-1,&dummy)==124);
    pid=fork();
    if(pid==0) { fw_child_signals(); for(;;) pause(); }
    raise(SIGTERM);
    assert(fw_wait(pid,1,-1,&dummy)==128+SIGTERM);
    fw_reset_stop();
    assert(fw_command(good,1)==0);
    puts("process: normal/failure exit, crash, timeout, no-deadline and screensaver termination passed");
}
