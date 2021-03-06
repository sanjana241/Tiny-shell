
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <assert.h>
 
/* Misc manifest constants */
#define MAXLINE 1024   /* max line size */
#define MAXARGS 128    /* max args on a command line */
#define MAXJOBS 16     /* max jobs at any point in time */
#define MAXJID 1 << 16 /* max job ID */
 
/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */
 
/*
* Jobs states: FG (foreground), BG (background), ST (stopped)
* Job state transitions and enabling actions:
*     FG -> ST  : ctrl-z
*     ST -> FG  : fg command
*     ST -> BG  : bg command
*     BG -> FG  : fg command
* At most 1 job can be in the FG state.
*/
 
/* Global variables */
extern char **environ;   /* defined in libc */
char prompt[] = "tsh> "; /* command line prompt (DO NOT CHANGE) */
int verbose = 0;         /* if true, print additional output */
int nextjid = 1;         /* next job ID to allocate */
char sbuf[MAXLINE];      /* for composing sprintf messages */
 
struct job_t
{                        /* The job struct */
 pid_t pid;             /* job PID */
 int jid;               /* job ID [1, 2, ...] */
 int state;             /* UNDEF, BG, FG, or ST */
 char cmdline[MAXLINE]; /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */
/* End global variables */
 
/* Function prototypes */
 
/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);
 
void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);
 
/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv);
void sigquit_handler(int sig);
 
void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int maxjid(struct job_t *jobs);
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid);
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid);
int pid2jid(pid_t pid);
void listjobs(struct job_t *jobs);
 
void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);
 
/*
* main - The shell's main routine
*/
int main(int argc, char **argv)
{
 char c;
 char cmdline[MAXLINE];
 int emit_prompt = 1; /* emit prompt (default) */
 
 /* Redirect stderr to stdout (so that driver will get all output
    * on the pipe connected to stdout) */
 dup2(1, 2);
 
 /* Parse the command line */
 while ((c = getopt(argc, argv, "hvp")) != EOF)
 {
   switch (c)
   {
   case 'h': /* print help message */
     usage();
     break;
 
   case 'v': /* emit additional diagnostic info */
     verbose = 1;
     break;
 
   case 'p':          /* don't print a prompt */
     emit_prompt = 0; /* handy for automatic testing */
     break;
 
   default:
     usage();
   }
 }
 
 /* Install the signal handlers */
 
 /* These are the ones you will need to implement */
 Signal(SIGINT, sigint_handler);   /* ctrl-c */
 Signal(SIGTSTP, sigtstp_handler); /* ctrl-z */
 Signal(SIGCHLD, sigchld_handler); /* Terminated or stopped child */
 
 /* This one provides a clean way to kill the shell */
 Signal(SIGQUIT, sigquit_handler);
 
 /* Initialize the job list */
 initjobs(jobs);
 
 /* Execute the shell's read/eval loop */
 while (1)
 {
   /* Read command line */
   if (emit_prompt)
   {
     printf("%s", prompt);
     fflush(stdout);
   }
 
   if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
     app_error("fgets error");
 
   if (feof(stdin))
   { /* End of file (ctrl-d) */
     fflush(stdout);
     exit(0);
   }
 
   /* Evaluate the command line */
   eval(cmdline);
   fflush(stdout);
   fflush(stdout);
 }
 
 exit(0); /* control never reaches here */
}
 
void getLastToken(char *source, char **target)
{
 int length;
 char string[256];
 
 strcpy(string, source);
 
 length = strlen(string);
 
 // Allocate a chunk of memory.
 *target = (char *)malloc(length);
 
 assert(*target != NULL);
 
 char delim[] = "/";
 char *token = strtok(&string[1], delim);
 while (token != NULL)
 {
   strcpy(*target, token);
   // fprintf(stderr, "token is %s\n", token);
   token = strtok(NULL, delim);
 }
}
 
/*
* eval - Evaluate the command line that the user has just typed in
*
* If the user has requested a built-in command (quit, jobs, bg or fg)
* then execute it immediately. Otherwise, fork a child process and
* run the job in the context of the child. If the job is running in
* the foreground, wait for it to terminate and then return.  Note:
* each child process must have a unique process group ID so that our
* background children don't receive SIGINT (SIGTSTP) from the kernel
* when we type ctrl-c (ctrl-z) at the keyboard. 
*/
void eval(char *cmdline)
{
 int retval,bg;                      //for storing return value for builtin_cmd and parseline
    if(!strcmp(cmdline,"\n")) return;   //simply return if there is no cmd/argument
    char* argv[MAXLINE];                //argument list
    sigset_t mask;                       //signal set
    
    bg=parseline(cmdline,argv);
    retval=builtin_cmd(argv);
    
    if(retval==0){                      //if not builtin_cmd
	    pid_t pid;

        sigemptyset(&mask);                      //initialize signal set
        sigaddset(&mask, SIGCHLD);               //add SIGCHLD to the set
        sigprocmask(SIG_BLOCK, &mask, NULL);     //block the signal

        //child
	    if((pid=fork())==0){
            sigprocmask(SIG_UNBLOCK, &mask, NULL);   //unblock signal SIGCHILD
            setpgrp();  //making the calling process as a process group leader i.e. making child a group leader
            execvp(argv[0],argv);                       //executes user command if successful
            printf("%s: Command not found\n",argv[0]);  //Throw error if execution unsuccessful
            exit(1);
	    }
        //parent
        else{
            if(bg)  //Background
                addjob(jobs,pid,BG,cmdline);        //add process to job list
            else    //Foreground
                addjob(jobs,pid,FG,cmdline);        //add process to job list
            sigprocmask(SIG_UNBLOCK, &mask, NULL);   //unblock signal SIGCHILD
            if(bg)
                printf("[%d] (%d) %s", pid2jid(pid), pid, cmdline);     //print background job info
            else
                waitfg(pid);        //parent waits for foreground job to terminate
        }
    }

    return;
    
}
 
/*
* parseline - Parse the command line and build the argv array.
*
* Characters enclosed in single quotes are treated as a single
* argument.  Return true if the user has requested a BG job, false if
* the user has requested a FG job. 
*/
int parseline(const char *cmdline, char **argv)
{
 static char array[MAXLINE]; /* holds local copy of command line */
 char *buf = array;          /* ptr that traverses command line */
 char *delim;                /* points to first space delimiter */
 int argc;                   /* number of args */
 int bg;                     /* background job? */
 
 strcpy(buf, cmdline);
 buf[strlen(buf) - 1] = ' ';   /* replace trailing '\n' with space */
 while (*buf && (*buf == ' ')) /* ignore leading spaces */
   buf++;
 
 /* Build the argv list */
 argc = 0;
 if (*buf == '\'')
 {
   buf++;
   delim = strchr(buf, '\'');
 }
 else
 {
   delim = strchr(buf, ' ');
 }
 
 while (delim)
 {
   argv[argc++] = buf;
   *delim = '\0';
   buf = delim + 1;
   while (*buf && (*buf == ' ')) /* ignore spaces */
     buf++;
 
   if (*buf == '\'')
   {
     buf++;
     delim = strchr(buf, '\'');
   }
   else
   {
     delim = strchr(buf, ' ');
   }
 }
 argv[argc] = NULL;
 
 if (argc == 0) /* ignore blank line */
   return 1;
 
 /* should the job run in the background? */
 if ((bg = (*argv[argc - 1] == '&')) != 0)
 {
   argv[--argc] = NULL;
 }
 return bg;
}
 
/*
* builtin_cmd - If the user has typed a built-in command then execute
* it immediately. 
*  1. The quit command terminates the shell.
*  2. The jobs command lists all background jobs.
*  3. The bg <job> command restarts <job> by sending it a SIGCONT signal, and then runs it in the background. The <job> argument can either be a PID or a JID.
*  4. The fg <job> command restarts <job> by sending it a SIGCONT signal, and then runs it in the foreground.  The <job> argument can be either a PID or a JID.
*/
int builtin_cmd(char **argv)
{
 assert(argv != NULL);
 
 if (strcmp(argv[0], "quit") == 0)
 {
   //fprintf(stderr, "Quit the shell now!\n");
   exit(0);
   return 1;
 }
 else if (strcmp(argv[0], "jobs") == 0)
 {
   // List all background jobs
   //     fprintf(stderr, "Run the built-in %s command!\n", argv[0]);
   listjobs(jobs);
   return 1;
 }
 else if ((strcmp(argv[0], "bg") == 0) || (strcmp(argv[0], "fg") == 0))
 {
   //   fprintf(stderr, "do_bgfg\n");
   do_bgfg(argv);
   return 1;
 }
 else
 {
   // fprintf(stderr, "we are in the else branch\n");
   return 0; /* not a builtin command */
 }
}
 
 int numbers_only(const char *s)
{
    while (*s) {
        if (isdigit(*s++) == 0) return 0;
    }

    return 1;
}
/*
* do_bgfg - Execute the builtin bg and fg commands
*  It should identify the process PID with the specified job id or process id befoore sending a SIGCONT to the specified process. In the end, it should handle the background and foreground process differently. 
*/
void do_bgfg(char **argv)
{
    int pid,jid;
    struct job_t *job;

    if(argv[1]==NULL){          //to checks if it has second argument
	   printf("%s command requires PID or %%jobid argument\n", argv[0]);
	   return;
    }

    else if(argv[1][0]=='%'){   //to check if second argument is valid(in case of jid)
	   char temp1,temp2[10];
	   sscanf(argv[1], "%c %s",&temp1,temp2);
	   if(!numbers_only(temp2)){
            printf("%s: argument must be a PID or %%jobid\n", argv[0]);
		    return;
	   }
    }

    else if(!numbers_only(argv[1])){    //to check if second argument is valid(in case of pid)
	printf("%s: argument must be a PID or %%jobid\n", argv[0]);
	return;
    }

    {
	if(numbers_only(argv[1])){
        pid=atoi(argv[1]);
		if((job=getjobpid(jobs, pid))==NULL){     //get PID and check if it is in job table
			printf("(%d): No such process\n",atoi(argv[1]));
			return;
		}
	}
	else{
		char first,rest[10];
		sscanf(argv[1], "%c %s",&first,rest);
        jid=atoi(rest);
		if((job=getjobjid(jobs,jid))==NULL){      //get JID and check if it is in job table
			printf("%s (%s): No such job\n",argv[0],argv[1]);
			return;
		}
	}

    }
    if(!strcmp(argv[0],"bg") && job->state==ST){
        printf("[%d] (%d) %s", job->jid, job->pid, job->cmdline);
        job->state = BG;            //change state FG-or-ST to BG
        kill(-(job->pid), SIGCONT); //send signal SIGCONT to whole group of given job
    }
    else if(!strcmp(argv[0],"fg")){
        if(job->state==ST){
            job->state = FG;        //change state ST to FG
            kill(-(job->pid), SIGCONT); //send signal SIGCONT to whole group of given job
        }
        else if(job->state==BG)     //change state BG to FG
            job->state = FG;        
        waitfg(job->pid);           //wait for fg to finish
    }

    return;
}


 
/*
* waitfg - Block until process pid is no longer the foreground process
*/
void waitfg(pid_t pid)
{
 sigset_t mask;
 
 //if (sigemptyset(&mask_all) < 0) //empty the mask set
 // perror("sigemptyset");
 
 while (1)
 {
   if (fgpid(jobs) != pid) //return the pid of the jobs
     return;
   sigsuspend(&mask); //block the signals in mask set
 }
 
 /*while(!pid){
 sigsuspend(&mask); --> can you try this?
 }*/
 
 return;
}
 
/*****************
* Signal handlers
*****************/
 
/*
* sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
*     a child job terminates (becomes a zombie), or stops because it
*     received a SIGSTOP or SIGTSTP signal. The handler reaps all
*     available zombie children, but doesn't wait for any other
*     currently running children to terminate. 
*/
void sigchld_handler(int sig)
{
 pid_t pid;
 int status;
 
 while (1)
 {
   pid = waitpid(fgpid(jobs), &status, WNOHANG | WUNTRACED);
   if (pid <= 0)
     break;
   //returns true if the child terminated normally, that is, by calling exit(3) or _exit(2), or by returning from main()
   if (WIFEXITED(status))
   {
     deletejob(jobs, pid);
   }
   else if (WIFSTOPPED(status))
   {
     printf("Job [%d] (%d) stopped by signal %d\n", pid2jid(pid), pid, WSTOPSIG(status));
     struct job_t *job = getjobpid(jobs, pid);
     job->state = ST;
   }
   //returns true if the child process was terminated by a signal
   else if (WIFSIGNALED(status))
   {
     printf("Job (%d) terminated by signal %d\n", pid, WTERMSIG(status)); //check the outptu again
     deletejob(jobs, pid);
   }
 }
 return;
}
 
/*
* sigint_handler - The kernel sends a SIGINT to the shell whenver the
*    user types ctrl-c at the keyboard.  Catch it and send it along
*    to the foreground job. 
*/
void sigint_handler(int sig)
{
 // Put your code here.
 int pid = fgpid(jobs);
 
 if (pid > 0)
 
 //If pid is -1, sig shall be sent to all processes (excluding an unspecified set of system processes) for which the process has permission to send that signal.
 {
   //kill(-pid, sig); // modified
   kill(-pid, SIGINT); //send SIGINT signal to the entire foreground process group
 }
 return;
}
 
/*
* sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
*     the user types ctrl-z at the keyboard. Catch it and suspend the
*     foreground job by sending it a SIGTSTP. 
*/
void sigtstp_handler(int sig)
{
 int pid = fgpid(jobs);
 if (pid == 0)
   return;
 if (pid > 0)
 {
   struct job_t *temp;
   temp = getjobpid(jobs, pid);
   temp->state = ST;
   kill(-pid, SIGTSTP);
   printf("Job [%d] (%d) stopped by signal %d\n", pid2jid(pid), pid, sig);
 }
 return;
}
 
/*********************
* End signal handlers
*********************/
 
/***********************************************
* Helper routines that manipulate the job list
**********************************************/
 
/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job)
{
 job->pid = 0;
 job->jid = 0;
 job->state = UNDEF;
 job->cmdline[0] = '\0';
}
 
/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs)
{
 int i;
 
 for (i = 0; i < MAXJOBS; i++)
   clearjob(&jobs[i]);
}
 
/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs)
{
 int i, max = 0;
 
 for (i = 0; i < MAXJOBS; i++)
   if (jobs[i].jid > max)
     max = jobs[i].jid;
 return max;
}
 
/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline)
{
 int i;
 
 if (pid < 1)
   return 0;
 
 for (i = 0; i < MAXJOBS; i++)
 {
   if (jobs[i].pid == 0)
   {
     jobs[i].pid = pid;
     jobs[i].state = state;
     jobs[i].jid = nextjid++;
     if (nextjid > MAXJOBS)
       nextjid = 1;
 
     strcpy(jobs[i].cmdline, cmdline);
     if (verbose)
     {
       printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
     }
     return 1;
   }
 }
 printf("Tried to create too many jobs\n");
 return 0;
}
 
/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid)
{
 int i;
 
 if (pid < 1)
   return 0;
 
 for (i = 0; i < MAXJOBS; i++)
 {
   if (jobs[i].pid == pid)
   {
     clearjob(&jobs[i]);
     nextjid = maxjid(jobs) + 1;
     return 1;
   }
 }
 return 0;
}
 
/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs)
{
 int i;
 
 for (i = 0; i < MAXJOBS; i++)
   if (jobs[i].state == FG)
     return jobs[i].pid;
 return 0;
}
 
/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid)
{
 int i;
 
 if (pid < 1)
   return NULL;
 for (i = 0; i < MAXJOBS; i++)
   if (jobs[i].pid == pid)
     return &jobs[i];
 return NULL;
}
 
/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid)
{
 int i;
 
 if (jid < 1)
   return NULL;
 for (i = 0; i < MAXJOBS; i++)
   if (jobs[i].jid == jid)
     return &jobs[i];
 return NULL;
}
 
/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid)
{
 int i;
 
 if (pid < 1)
   return 0;
 for (i = 0; i < MAXJOBS; i++)
   if (jobs[i].pid == pid)
   {
     return jobs[i].jid;
   }
 
 return 0;
}
 
/* listjobs - Print the job list */
void listjobs(struct job_t *jobs)
{
 int i;
 
 for (i = 0; i < MAXJOBS; i++)
 {
   if (jobs[i].pid != 0)
   {
     printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
 
     switch (jobs[i].state)
     {
     case BG:
       printf("Running background ");
       break;
 
     case FG:
       printf("Foreground ");
       break;
 
     case ST:
       printf("Stopped ");
       break;
 
     default:
       printf("listjobs: Internal error: job[%d].state=%d ", i, jobs[i].state);
     }
 
     printf("%s", jobs[i].cmdline);
   }
 }
}
/******************************
* end job list helper routines
******************************/
 
/***********************
* Other helper routines
***********************/
 
/*
* usage - print a help message
*/
void usage(void)
{
 printf("Usage: shell [-hvp]\n");
 printf("   -h   print this message\n");
 printf("   -v   print additional diagnostic information\n");
 printf("   -p   do not emit a command prompt\n");
 exit(1);
}
 
/*
* unix_error - unix-style error routine
*/
void unix_error(char *msg)
{
 fprintf(stdout, "%s: %s\n", msg, strerror(errno));
 exit(1);
}
 
/*
* app_error - application-style error routine
*/
void app_error(char *msg)
{
 fprintf(stdout, "%s\n", msg);
 exit(1);
}
 
/*
* Signal - wrapper for the sigaction function
*/
handler_t *Signal(int signum, handler_t *handler)
{
 struct sigaction action, old_action;
 
 action.sa_handler = handler;
 sigemptyset(&action.sa_mask); /* block sigs of type being handled */
 action.sa_flags = SA_RESTART; /* restart syscalls if possible */
 
 if (sigaction(signum, &action, &old_action) < 0)
   unix_error("Signal error");
 return (old_action.sa_handler);
}
 
/*
* sigquit_handler - The driver program can gracefully terminate the
*    child shell by sending it a SIGQUIT signal.
*/
void sigquit_handler(int sig)
{
 printf("Terminating after receipt of SIGQUIT signal\n");
 exit(1);
}
 
 




