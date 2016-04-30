// tsh - A tiny shell program with job control
// <Nicholas Clement Nicl7004>

using namespace std;

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <string>

#include "globals.h"
#include "jobs.h"
#include "helper-routines.h"

static char prompt[] = "tsh> ";
int verbose = 0;

//
// You need to implement the functions eval, builtin_cmd, do_bgfg,
// waitfg, sigchld_handler, sigstp_handler, sigint_handler
//
// The code below provides the "prototypes" for those functions
// so that earlier code can refer to them. You need to fill in the
// function bodies below.
//
void do_bgfg(char **argv);
void waitfg(pid_t pid);
void eval(char *cmdline);
int builtin_cmd(char **argv);

//sigs
void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

//
// main - The shell's main routine
//
int main(int argc, char **argv)
{
  int emit_prompt = 1;

  // Redirect stderr to stdout (so that driver will get all output
  // on the pipe connected to stdout)
  dup2(1, 2);

  /* Parse the command line */
  char c;
  while ((c = getopt(argc, argv, "hvp")) != EOF) {
    switch (c) {
    case 'h':             // print help message
      usage();
      break;
    case 'v':             // emit additional diagnostic info
      verbose = 1;
      break;
    case 'p':             // don't print a prompt
      emit_prompt = 0;  // handy for automatic testing
      break;
    default:
      usage();
    }
  }


  Signal(SIGINT,  sigint_handler);   // ctrl-c
  Signal(SIGTSTP, sigtstp_handler);  // ctrl-z
  Signal(SIGCHLD, sigchld_handler);  // Terminated or stopped child

  //
  // This one provides a clean way to kill the shell
  //
  Signal(SIGQUIT, sigquit_handler);

  //
  // Initialize the job list
  //
  initjobs(jobs);

  //
  // Execute the shell's read/eval loop
  //
  for(;;) {
    //
    // Read command line
    //
    if (emit_prompt) {
      printf("%s", prompt);
      fflush(stdout);
    }

    char cmdline[MAXLINE];

    if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin)) {
      app_error("fgets error");
    }
    // End of file? (did user type ctrl-d?)
    if (feof(stdin)) {
      fflush(stdout);
      exit(0);
    }

    //
    // Evaluate command line
    //
    eval(cmdline);
    fflush(stdout);
    fflush(stdout);
  }

  exit(0); //control never reaches here
}

/////////////////////////////////////////////////////////////////////////////
//
// eval - Evaluate the command line that the user has just typed in
//
// If the user has requested a built-in command (quit, jobs, bg or fg)
// then execute it immediately. Otherwise, fork a child process and
// run the job in the context of the child. If the job is running in
// the foreground, wait for it to terminate and then return.  Note:
// each child process must have a unique process group ID so that our
// background children don't receive SIGINT (SIGTSTP) from the kernel
// when we type ctrl-c (ctrl-z) at the keyboard.
//
void eval(char *cmdline)
{
  /* Parse command line */
  char *argv[MAXARGS];
  pid_t pid; //init process id
  sigset_t set; //init signal

  sigemptyset(&set); //initialize set to be empty
  sigaddset(&set, SIGCHLD); //add sigchild to set -SIGCHLD is sent when child terminates
  // The 'bg' variable is 1 (true) if the job should run

  int bg = parseline(cmdline, argv); // reads in from command line and returns 0 if foreground and 1 if background, just like above
  if (argv[0] == NULL)
    return;   //to prevent against empty lines

    if (builtin_cmd(argv)==0){ //if parameter is not a built in function fork a child and cont.

			sigprocmask(SIG_BLOCK, &set, NULL); //parent blocks SIGCHILD signal temporarily so child can run
			pid = fork();
			if(pid < 0){
					printf("fork() : forking error\n");
					return;
				}
			if(pid == 0){
					setpgid(0,0); //if first parameter is zero, sets pid to the parent.
          //second parameter is zero, sets pgid to above parent.
									//group id refers to all the children and parent.
			if (execve(argv[0], argv, environ) < 0){ //helps prevent against recursively calling child processes
					printf("%s : Command not found. \n", argv[0]);
					exit(0);
				}
				}
	else{						//parent process
		if(bg == 0){			//Fg
				if(!addjob(jobs, pid, FG, cmdline)){
						return; //add pid to job list in the current state of fg
					}
				sigprocmask(SIG_UNBLOCK, &set, NULL);  //unblock sigs

				waitfg(pid); //unblock so that grandchildren dont block and wait
			}
		else{
				if(!addjob(jobs, pid, BG, cmdline)){
						return; //add pid to joblist in current state of bg
					}
				sigprocmask(SIG_UNBLOCK, &set, NULL);
				printf("[%d] (%d) %s\n", pid2jid(pid), pid, cmdline); //dispaly it all to user
			}
		}
		  return;

		}
	}



/////////////////////////////////////////////////////////////////////////////
//
// builtin_cmd
int builtin_cmd(char **argv)
{

  if (strcmp(argv[0], "quit") == 0) {    //if first arguement is quit, then we exit
		exit(0);
	}
  if (strcmp(argv[0], "jobs") == 0) {   //if first parameter is jobs show jobs
		listjobs(jobs);
		return 1;
	  }
  if (strcmp(argv[0], "fg") == 0) {   //if parameter is fg call do_bgfg
		do_bgfg(argv);
		return 1;
	  }
  if (strcmp(argv[0], "bg") == 0) {   //if parameter is bg call do_bgfg
		do_bgfg(argv);
		return 1;
	  }


  return 0;   //must not be a built in command if it makes it to here
}

// do_bgfg - Execute the builtin bg and fg commands
//
void do_bgfg(char **argv)
{
  struct job_t *jobp=NULL;

  /* Ignore command if no argument */
  if (argv[1] == NULL) {
    printf("%s command requires PID or %%jobid argument\n", argv[0]);
    return;
  }

  /* Parse the required PID or %JID arg */
  if (isdigit(argv[1][0])) {
    pid_t pid = atoi(argv[1]);
    if (!(jobp = getjobpid(jobs, pid))) {
      printf("(%d): No such process\n", pid);
      return;
    }
  }

  else if (argv[1][0] == '%') {
    int jid = atoi(&argv[1][1]);
    if (!(jobp = getjobjid(jobs, jid))) {
      printf("%s: No such job\n", argv[1]);
      return;
    }
  }

  else {
    printf("%s: argument must be a PID or %%jobid\n", argv[0]);
    return;
}

  //
  // You need to complete rest. At this point,
  // the variable 'jobp' is the job pointer
  // for the job ID specified as an argument.
  //
  // Your actions will depend on the specified command
  // so we've converted argv[0] to a string (cmd) for
  // your benefit.
  //
  pid_t pid;
  pid = jobp->pid; //get the pid from the jobp pointer
  if (jobp->state == ST){  //if state = stopped
		if (!strcmp(argv[0], "fg")){ //if first parameter is bg
				jobp->state = FG;  //chang the state of the process from bg to fg
				kill(-pid,SIGCONT); //send a signal to continue
				waitfg(pid); //wait for fg to terminate before next step
			}
		if (!strcmp(argv[0], "bg")){ // if first paramter is fg
				jobp->state = BG; //change state from fg to bg
				printf("[%d] (%d) %s", jobp->jid, pid, jobp->cmdline);
				kill(-pid, SIGCONT); //send signal to continue
			}
		if(jobp->state == BG){
				if(!strcmp(argv[0], "fg")){ //move any processes in the background to foreground
						jobp->state = FG;
						waitfg(jobp->pid);
					}
			}
	  }

  return;
}


// waitfg - Block until process pid is no longer the foreground process
//
void waitfg(pid_t pid)
{
	while (fgpid(jobs) == pid){  //stay in infinite loop while process is in fg
		sleep(1);
		}
  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// Signal handlers

void sigchld_handler(int sig) //sig handler for child
{
		pid_t pid;
	int process_state;
	// Return imediately if no child has exited
	while ((pid = waitpid(-1, &process_state, WNOHANG|WUNTRACED )) > 0) {   // waitpid function will return a value > 0 which is the PID of the terminated child.

	  if (WIFEXITED(process_state)) //used to determine if childrin exit
	{
		deletejob(jobs,pid);  // either terminated or stopped
	}


	if (WIFSIGNALED(process_state)) {			//WIFSIGNALED: True if child terminated by signal
		printf("Job [%d] (%d) terminated by signal %d\n", pid2jid(pid), pid, WTERMSIG(process_state)); // WTERMSIG returns the number of the signal to term.
	    deletejob(jobs,pid);					// child process to terminate

	   }


	if (WIFSTOPPED(process_state)) // if a process stops, display it
	{
		getjobpid(jobs,pid)->state = ST;
		printf("Job [%d] (%d) stopped by signal %d\n", pid2jid(pid),pid,WSTOPSIG(process_state));// WSTOPSIG returns the number of the signal that caused the child process to stop

	}
}


	return;
}

/////////////////////////////////////////////////////////////////////////////
//
// sigint_handler - The kernel sends a SIGINT to the shell whenver the
//    user types ctrl-c at the keyboard.  Catch it and send it along
//    to the foreground job.
//
void sigint_handler(int sig)
{
	pid_t pid = fgpid(jobs);

	if(pid != 0){
		kill(-pid, SIGINT);
	} //interrupt all the processes in the process group
  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
//     the user types ctrl-z at the keyboard. Catch it and suspend the
//     foreground job by sending it a SIGTSTP.
//
void sigtstp_handler(int sig)
{
	pid_t pid = fgpid(jobs);

	if (pid != 0)	//if the pid of the foreground process is different that zero
		kill(-pid, SIGTSTP); //stp all the processes in the process group
  return;
}
