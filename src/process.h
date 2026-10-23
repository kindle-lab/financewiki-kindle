/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef FW_PROCESS_H
#define FW_PROCESS_H
#include <sys/types.h>
double fw_now(void);
/* timeout <= 0 waits without a deadline. */
int fw_wait(pid_t pid, double timeout, int event_fd, int *screen_sleep);
int fw_command(char *const argv[], double timeout);
void fw_signals(void);
void fw_child_signals(void);
void fw_reset_stop(void);
#endif
