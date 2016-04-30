// 
// tsh - A tiny shell program with job control
//
// <Brennon Lee brle1617 >

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

//
// Needed global variable definitions
//

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

void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

//
// main - The shell's main routine 
//
int main(int argc, char **argv) 
{
  int emit_prompt = 1; // emit prompt (default)

  //
  // Redirect stderr to stdout (so that driver will get all output
  // on the pipe connected to stdout)
  //
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

  //
  // Install the signal handlers
  //

  //
  // These are the ones you will need to implement
  //
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
    //
    // End of file? (did user type ctrl-d?)
    //
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
  //
  // The 'argv' vector is filled in by the parseline
  // routine below. It provides the arguments needed
  // for the execve() routine, which you'll need to
  // use below to launch a process.
  //
  char *argv[MAXARGS]; // holds the arg for execve() function
  
  pid_t pid; //create pid variable
  sigset_t set; //declare a signal set
  
  sigemptyset(&set); 	//initializes set to be empty
  sigaddset(&set, SIGCHLD);		//add the sigchild to the set
  //SIGCHLD sent when child is stopped


  // The 'bg' variable is TRUE if the job should run
  // in background mode or FALSE if it should run in FG
  //
  int bg = parseline(cmdline, argv); //read from CL and returns 0 if Foreground and 1 if background
  
  if (argv[0] == NULL)  
    return;   /* ignore empty lines */
    
 if (builtin_cmd(argv)==0){       // if the first argument is not a builtin commmand, then fork a child 
	  
	  sigprocmask(SIG_BLOCK, &set, NULL);   // Parent blocks SIGCHILD signal temporarily
	  pid = fork();
	  if (pid < 0 ){
		  printf("fork() : forking error\n");
		  return;
	  }
	  if(pid ==0) {
		  setpgid(0,0);      // 1st argument, if zero, sets pid to the parent
							 // 2nd argument, if zero, set pgid to the same as 1st argument
							 // group id is used to refer to all the children and parent
	
	 if (execve(argv[0],argv,environ)< 0) { 
		 printf("%s : Command not found. \n",argv[0]);
		 exit(0);
		 }
	 }
     else{								//The parent process
     if(bg == 0)					//Foreground process
		 {
			 if(!addjob(jobs,pid,FG,cmdline)){
				 return;}  //add pid to job list with state of Foreground
		
		sigprocmask(SIG_UNBLOCK, &set, NULL);		//unblock all signals in the set
													//have to unblock so children of children dont a set block
		waitfg(pid);
		}
		else{							//Background process
			if(!addjob(jobs,pid,BG,cmdline)){return;} //add pid to job list with state of backround
			sigprocmask(SIG_UNBLOCK, &set, NULL);
			printf("[%d] (%d) %s\n", pid2jid(pid), pid, cmdline);
		}
	 }

  return;
}
}


/////////////////////////////////////////////////////////////////////////////
//
// builtin_cmd - If the user has typed a built-in command then execute
// it immediately. The command name would be in argv[0] and
// is a C string. We've cast this to a C++ string type to simplify
// string comparisons; however, the do_bgfg routine will need 
// to use the argv array as well to look for a job number.
// 
int builtin_cmd(char **argv) 			//returns 1 if we have a built in argument
{										// returns 0 if we do not have a built in command
  if (strcmp(argv[0], "quit")==0) {
	    exit(0);
    }
	
    if (strcmp(argv[0], "jobs")==0) {		// if first argument is jobs we list them and return 1
		listjobs(jobs);
		return 1;
    }
    
    if((strcmp(argv[0], "fg")==0)){ // if we have a fg program we call do_bgfg where we will handle that
	  do_bgfg(argv);
	  return 1;
	}

	if((strcmp(argv[0], "bg")==0)){ // if we have a bg program we call do_bgfg where we will handle that
	  do_bgfg(argv);
	  return 1;
	 }
	
	
	return 0;     /* not a builtin command */
}

/////////////////////////////////////////////////////////////////////////////
//
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
  pid_t  pid;  // need the pid of the job
  pid = jobp->pid;  // get the job pid 
  if (jobp->state == ST){    // work only with stopped programs
	  
	 if(!strcmp(argv[0],"fg")){  // if first argument is fg
		  jobp->state = FG;      // change the state of the process
		  kill(-pid,SIGCONT);    // send a signal to continue to all the process
		  waitfg(pid);           // since it's foreground now we have to wait for it to terminate before 
								 // returning
		  }
	  
	  
	  if(!strcmp(argv[0],"bg")){ // if first argument is equal to bg we change state of the processe
		  jobp->state = BG;
		  printf("[%d] (%d) %s", jobp->jid, pid, jobp->cmdline);
		  kill(-pid,SIGCONT);  // send signal to continue to all the signals
		 
		  }
		  		  
					    }
					    
	if(jobp->state == BG){   //now if we have any processes in the background that we need
		if(!strcmp(argv[0],"fg")){  // to move foregroung we do that
			jobp->state = FG;
			waitfg(jobp->pid);
			}
		
		}
  
  return;
}


/////////////////////////////////////////////////////////////////////////////
//
// waitfg - Block until process pid is no longer the foreground process
//
void waitfg(pid_t pid)
{
  while(fgpid(jobs)== pid){   // until the process is out of foreground, we stay in this infinite loop
    sleep(1);
  } 
  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// Signal handlers
//


/////////////////////////////////////////////////////////////////////////////
//
// sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
//     a child job terminates (becomes a zombie), or stops because it
//     received a SIGSTOP or SIGTSTP signal. The handler reaps all
//     available zombie children, but doesn't wait for any other
//     currently running children to terminate.  
//
void sigchld_handler(int sig) 
{
	pid_t pid;
	int process_state; 
	// Return imediately if no child has exited
	while ((pid = waitpid(-1, &process_state, WNOHANG|WUNTRACED )) > 0) {   // waitpid function will return a value > 0 which is the PID of the terminated child.
	  
	  if (WIFEXITED(process_state))           // checks if children exited properly
	{
		deletejob(jobs,pid);                              // either terminated or stopped
	}
	if (WIFSIGNALED(process_state)) {			//WIFSIGNALED: True if child terminated by signal
		printf("Job [%d] (%d) terminated by signal %d\n", pid2jid(pid), pid, WTERMSIG(process_state)); // WTERMSIG returns the number of the signal that
	    deletejob(jobs,pid);					// caused a child process to terminate
		
	   }   
	if (WIFSTOPPED(process_state)) // if a process is stopped we change his state and print to the stdout
	
	{
		getjobpid(jobs,pid)->state = ST;
		printf("Job [%d] (%d) stopped by signal %d\n", pid2jid(pid),pid,WSTOPSIG(process_state));// WSTOPSIG returns the number of the signal that caused the	
																								  // child to stop
	}
}	


	return;
}								 // the option -1 blocks the call to waitpid until any arbitrary child have been terminated


/////////////////////////////////////////////////////////////////////////////
//
// sigint_handler - The kernel sends a SIGINT to the shell whenver the
//    user types ctrl-c at the keyboard.  Catch it and send it along
//    to the foreground job.  
//
void sigint_handler(int sig) 
{
	pid_t pid = fgpid(jobs);

  if (pid != 0)			// if the pid of the foreground process is different than zero
    kill(-pid, SIGINT);	// interrupt all the processes in the process group

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

  if (pid != 0)			// if the pid of the foreground process is different than zero
    kill(-pid, SIGTSTP);	// interrupt all the processes in the process group

  return;

}

/*********************
 * End signal handlers
 *********************/
