/** @author Levente Kurusa <levex@linux.com> **/
#ifndef __TASKING_H_
#define __TASKING_H_

#include <stdint.h>

#define PROCESS_STATE_ALIVE 0
#define PROCESS_STATE_ZOMBIE 1
#define PROCESS_STATE_DEAD 2

struct _process;

typedef struct _process {
	struct _process* prev;
	char* name;
	uint32_t pid;
	uint32_t esp;
	uint32_t eip;
	uint32_t state;
	void (*notify)(int);
	struct _process* next;
} PROCESS;

extern int addProcess(PROCESS* p);
extern PROCESS* createProcess(char* name, uint32_t addr);

extern int is_pid_running(int pid);

extern int is_tasking();

extern char* p_name();
extern int p_pid();

extern void send_sig(int sig);

extern void _kill();
extern void kill();
extern void schedule();
extern void schedule_noirq();
extern void tasking_init();

#endif
