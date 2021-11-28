#include "shell.h"

typedef struct proc {
  pid_t pid;    /* process identifier */
  int state;    /* RUNNING or STOPPED or FINISHED */
  int exitcode; /* -1 if exit status not yet received */
} proc_t;

typedef struct job {
  pid_t pgid;            /* 0 if slot is free */
  proc_t *proc;          /* array of processes running in as a job */
  struct termios tmodes; /* saved terminal modes */
  int nproc;             /* number of processes */
  int state;             /* changes when live processes have same state */
  char *command;         /* textual representation of command line */
} job_t;

static job_t *jobs = NULL;          /* array of all jobs */
static int njobmax = 1;             /* number of slots in jobs array */
static int tty_fd = -1;             /* controlling terminal file descriptor */
static struct termios shell_tmodes; /* saved shell terminal modes */

static void sigchld_handler(int sig) {
  int old_errno = errno;
  pid_t pid;
  int status;
  /* TODO: Change state (FINISHED, RUNNING, STOPPED) of processes and jobs.
   * Bury all children that finished saving their status in jobs. */
#ifdef STUDENT
  while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
    bool ready = false;
    for (int i = 0; i < njobmax; i++) {

      if (ready)
        break;
      for (int j = 0; j < jobs[i].nproc; j++) {
        if (ready)
          break;

        // We are looking for a process that returned from waitpid
        if (pid == jobs[i].proc[j].pid) {
          ready = true;
          if (WIFSTOPPED(status)) {
            jobs[i].proc[j].state = STOPPED;
            jobs[i].state = STOPPED;
          }

          else if (WIFEXITED(status)) {
            jobs[i].proc[j].state = FINISHED;
            jobs[i].proc[j].exitcode = status;
          }

          else if (WIFSIGNALED(status)) {
            jobs[i].proc[j].state = FINISHED;
            jobs[i].proc[j].exitcode = status;
          }

          else if (WIFCONTINUED(status)) {
            jobs[i].proc[j].state = RUNNING;
          }
        }
      }
    }

    // Now we want to check if there is a need for a change in a job status
    for (int i = 0; i < njobmax; i++) {
      bool is_stopped = false;
      bool is_running = false;
      for (int j = 0; j < jobs[i].nproc; j++) {
        if (jobs[i].proc[j].state == STOPPED) {
          is_stopped = true;
          break;
        } else if (jobs[i].proc[j].state == RUNNING) {
          is_running = true;
        }
      }
      if (is_stopped) {
        jobs[i].state = STOPPED;
      } else if (is_running) {
        jobs[i].state = RUNNING;
      } else {
        jobs[i].state = FINISHED;
      }
    }
  }
#endif /* !STUDENT */
  errno = old_errno;
}

/* When pipeline is done, its exitcode is fetched from the last process. */
static int exitcode(job_t *job) {
  return job->proc[job->nproc - 1].exitcode;
}

static int allocjob(void) {
  /* Find empty slot for background job. */
  for (int j = BG; j < njobmax; j++)
    if (jobs[j].pgid == 0)
      return j;

  /* If none found, allocate new one. */
  jobs = realloc(jobs, sizeof(job_t) * (njobmax + 1));
  memset(&jobs[njobmax], 0, sizeof(job_t));
  return njobmax++;
}

static int allocproc(int j) {
  job_t *job = &jobs[j];
  job->proc = realloc(job->proc, sizeof(proc_t) * (job->nproc + 1));
  return job->nproc++;
}

int addjob(pid_t pgid, int bg) {
  int j = bg ? allocjob() : FG;
  job_t *job = &jobs[j];
  /* Initial state of a job. */
  job->pgid = pgid;
  job->state = RUNNING;
  job->command = NULL;
  job->proc = NULL;
  job->nproc = 0;
  job->tmodes = shell_tmodes;
  return j;
}

static void deljob(job_t *job) {
  assert(job->state == FINISHED);
  free(job->command);
  free(job->proc);
  job->pgid = 0;
  job->command = NULL;
  job->proc = NULL;
  job->nproc = 0;
}

static void movejob(int from, int to) {
  assert(jobs[to].pgid == 0);
  memcpy(&jobs[to], &jobs[from], sizeof(job_t));
  memset(&jobs[from], 0, sizeof(job_t));
}

static void mkcommand(char **cmdp, char **argv) {
  if (*cmdp)
    strapp(cmdp, " | ");

  for (strapp(cmdp, *argv++); *argv; argv++) {
    strapp(cmdp, " ");
    strapp(cmdp, *argv);
  }
}

void addproc(int j, pid_t pid, char **argv) {
  assert(j < njobmax);
  job_t *job = &jobs[j];

  int p = allocproc(j);
  proc_t *proc = &job->proc[p];
  /* Initial state of a process. */
  proc->pid = pid;
  proc->state = RUNNING;
  proc->exitcode = -1;
  mkcommand(&job->command, argv);
}

/* Returns job's state.
 * If it's finished, delete it and return exitcode through statusp. */
static int jobstate(int j, int *statusp) {
  assert(j < njobmax);
  job_t *job = &jobs[j];
  int state = job->state;

  /* TODO: Handle case where job has finished. */
#ifdef STUDENT
  if (state == FINISHED) {
    *statusp = exitcode(job);
    deljob(job);
  }
#endif /* !STUDENT */

  return state;
}

char *jobcmd(int j) {
  assert(j < njobmax);
  job_t *job = &jobs[j];
  return job->command;
}

/* Continues a job that has been stopped. If move to foreground was requested,
 * then move the job to foreground and start monitoring it. */
bool resumejob(int j, int bg, sigset_t *mask) {
  if (j < 0) {
    for (j = njobmax - 1; j > 0 && jobs[j].state == FINISHED; j--)
      continue;
  }

  if (j >= njobmax || jobs[j].state == FINISHED)
    return false;

    /* TODO: Continue stopped job. Possibly move job to foreground slot. */
#ifdef STUDENT

  if (bg == 1 && jobs[j].state == STOPPED) {
    kill(-jobs[j].pgid, SIGCONT);
    msg("[%d] continue '%s'\n", j, jobcmd(j));
  } else if (bg == 0) {
    int current_job = 0;
    movejob(j, current_job);
    tcsetpgrp(tty_fd, jobs[current_job].pgid);
    tcsetattr(tty_fd, current_job, &jobs[current_job].tmodes);

    if (jobs[current_job].state == STOPPED) {
      kill(-jobs[current_job].pgid, SIGCONT);
      while (jobs[current_job].state == STOPPED) {
        sigsuspend(mask);
      }
    }

    msg("[%d] continue '%s'\n", current_job, jobcmd(current_job));
    monitorjob(mask);
  }
#endif /* !STUDENT */

  return true;
}

/* Kill the job by sending it a SIGTERM. */
bool killjob(int j) {
  if (j >= njobmax || jobs[j].state == FINISHED)
    return false;
  debug("[%d] killing '%s'\n", j, jobs[j].command);

  /* TODO: I love the smell of napalm in the morning. */
#ifdef STUDENT
  kill(-jobs[j].pgid, SIGTERM);
  kill(-jobs[j].pgid, SIGCONT);
#endif /* !STUDENT */

  return true;
}

/* Report state of requested background jobs. Clean up finished jobs. */
void watchjobs(int which) {
  for (int j = BG; j < njobmax; j++) {
    if (jobs[j].pgid == 0)
      continue;

      /* TODO: Report job number, state, command and exit code or signal. */
#ifdef STUDENT
    int status;
    char *job_command = strdup(jobcmd(j));
    int job_state = jobstate(j, &status);

    if (which == ALL || which == FINISHED) {
      if (job_state == FINISHED) {
        if (WIFEXITED(status))
          printf("[%d] exited '%s', status=%d\n", j, job_command,
                 WEXITSTATUS(status));
        else
          printf("[%d] killed '%s' by signal %d\n", j, job_command,
                 WTERMSIG(status));
      }
    }
    if (which == ALL) {
      if (job_state == STOPPED)
        msg("[%d] suspended '%s'\n", j, job_command);
      else if (job_state == RUNNING)
        msg("[%d] running '%s'\n", j, job_command);
    }

    free(job_command);

#endif /* !STUDENT */
  }
}

/* Monitor job execution. If it gets stopped move it to background.
 * When a job has finished or has been stopped move shell to foreground. */
int monitorjob(sigset_t *mask) {
  int exitcode = 0, state;

  /* TODO: Following code requires use of Tcsetpgrp of tty_fd. */
#ifdef STUDENT

  tcsetpgrp(tty_fd, jobs[0].pgid);

  state = jobstate(0, &exitcode);

  while (state == RUNNING) {
    sigsuspend(mask);
    state = jobstate(0, &exitcode);
  }
  if (state == STOPPED) {
    tcgetattr(tty_fd, &jobs[0].tmodes);
    int new_job = allocjob();
    movejob(0, new_job);
  }
  if (state == STOPPED || state == FINISHED) {
    tcsetattr(tty_fd, 0, &shell_tmodes);
    tcsetpgrp(tty_fd, getpgrp());
  }

#endif /* !STUDENT */

  return exitcode;
}

/* Called just at the beginning of shell's life. */
void initjobs(void) {
  Signal(SIGCHLD, sigchld_handler);
  jobs = calloc(sizeof(job_t), 1);

  /* Assume we're running in interactive mode, so move us to foreground.
   * Duplicate terminal fd, but do not leak it to subprocesses that execve. */
  assert(isatty(STDIN_FILENO));
  tty_fd = Dup(STDIN_FILENO);
  fcntl(tty_fd, F_SETFD, FD_CLOEXEC);

  /* Take control of the terminal. */
  Tcsetpgrp(tty_fd, getpgrp());

  /* Save default terminal attributes for the shell. */
  Tcgetattr(tty_fd, &shell_tmodes);
}

/* Called just before the shell finishes. */
void shutdownjobs(void) {
  sigset_t mask;
  Sigprocmask(SIG_BLOCK, &sigchld_mask, &mask);

  /* TODO: Kill remaining jobs and wait for them to finish. */
#ifdef STUDENT

  for (int i = 0; i < njobmax; i++) {
    if (jobs[i].state != FINISHED) {
      killjob(i);
      while (jobs[i].state != FINISHED)
        sigsuspend(&mask);
    }
  }

#endif /* !STUDENT */

  watchjobs(FINISHED);

  Sigprocmask(SIG_SETMASK, &mask, NULL);

  Close(tty_fd);
}

/* Sets foreground process group to `pgid`. */
void setfgpgrp(pid_t pgid) {
  Tcsetpgrp(tty_fd, pgid);
}
