#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <sched.h>
#include <assert.h>
#include <sys/wait.h>
#include <errno.h>


void handle_err(int ret, char *func)
{
 perror(func);
 exit(EXIT_FAILURE);
}


void fork_processes (int process_count) {
    unsigned long i, iter, iter1 = 0;
    int ret = -1;
    pid_t pid;
    struct sched_param sp;
    unsigned long MAX_ITER = 1000000000;
    //while(iter++ < MAX_ITER);
    
    iter = 0;
    for (i = 1; i <= process_count; i++) {
        pid = fork();
        if (pid == -1) {
            handle_err(ret, "fork error");
            return;
        }
        if (pid == 0) {
            printf("I am a child: %lu PID: %d\n",i, getpid());
	                
	    if (process_count)
	    	fork_processes(process_count - 1);
	
	    while(iter++ < MAX_ITER)
		while(iter1++ < MAX_ITER / (MAX_ITER/10000));
  
            return;
        } else {
            /**/
           // int ret_status;
            //waitpid(pid, &ret_status, 0);
            //if (ret_status == -1) 
             //   handle_err(ret, "child return");
        }
     }
}

void long_running(void) {
	int MAX = 10000000;
	int i=0;
	for (; i < MAX; i++) {
		if (i % (MAX/1000000))
			sleep(1);
	}
}

void sig_handler(int signal) {
	if (signal == SIGINT) {
		printf("Process %d handled signal %d\n", getpid(), signal );
	}
	return;
}


int main(int argc, char *argv[]) {

    int i;
    printf("Long Running Process' PID: %d\n", getpid());
    if (signal(SIGINT, sig_handler) == SIG_ERR)
	printf("Can't catch the signal\n");
    long_running();
}

