#ifdef READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

#define DEBUG 0
#include "shell.h"

sigset_t sigchld_mask;

static sigjmp_buf loop_env;

static void sigint_handler(int sig) {
  siglongjmp(loop_env, sig);
}

/* Rewrite closed file descriptors to -1,
 * to make sure we don't attempt do close them twice. */
static void MaybeClose(int *fdp) {
  if (*fdp < 0)
    return;
  Close(*fdp);
  *fdp = -1;
}

/* Consume all tokens related to redirection operators.
 * Put opened file descriptors into inputp & output respectively. */
static int do_redir(token_t *token, int ntokens, int *inputp, int *outputp) {
  token_t mode = NULL; /* T_INPUT, T_OUTPUT or NULL */
  int n = 0;           /* number of tokens after redirections are removed */

  for (int i = 0; i < ntokens; i++) {
    /* TODO: Handle tokens and open files as requested. */
#ifdef STUDENT
    if (token[i] == T_INPUT) {
      mode = T_INPUT;
      MaybeClose(inputp);

      // Read only
      *inputp = open(token[i + 1], O_RDONLY, 0);

      // Remove redirect and subsequent token
      token[i] = T_NULL;
      token[i + 1] = T_NULL;
    } else if (token[i] == T_OUTPUT) {
      mode = T_OUTPUT;
      MaybeClose(outputp);

      // Write only. Can create file. Permissions for new file: rw-, rw-, r--
      *outputp = open(token[i + 1], O_WRONLY | O_CREAT,
                      S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);

      // Remove redirect and subsequent token
      token[i] = T_NULL;
      token[i + 1] = T_NULL;
    }

    else if (mode == NULL) {
      n++;
    }

    else {
      mode = NULL;
    }

#endif /* !STUDENT */
  }

  token[n] = NULL;
  return n;
}

/* Execute internal command within shell's process or execute external command
 * in a subprocess. External command can be run in the background. */
static int do_job(token_t *token, int ntokens, bool bg) {
  int input = -1, output = -1;
  int exitcode = 0;

  ntokens = do_redir(token, ntokens, &input, &output);

  if (!bg) {
    if ((exitcode = builtin_command(token)) >= 0)
      return exitcode;
  }

  sigset_t mask;
  Sigprocmask(SIG_BLOCK, &sigchld_mask, &mask);

  /* TODO: Start a subprocess, create a job and monitor it. */
#ifdef STUDENT
  pid_t pid = fork();

  if (pid) { // Parent
    setpgid(pid, pid);

    MaybeClose(&input);
    MaybeClose(&output);

    int new_job = addjob(pid, bg);
    addproc(new_job, pid, token);

    if (bg == 0) {
      exitcode = monitorjob(&mask);
    } else {
      msg("[%d] running '%s'\n", new_job, jobcmd(new_job));
    }
  } else { // Child
    setpgid(0, 0);

    Sigprocmask(SIG_SETMASK, &mask, NULL);

    Signal(SIGINT, SIG_DFL);
    Signal(SIGTSTP, SIG_DFL);
    Signal(SIGTTIN, SIG_DFL);
    Signal(SIGTTOU, SIG_DFL);

    if (input != -1)
      dup2(input, STDIN_FILENO);
    if (output != -1)
      dup2(output, STDOUT_FILENO);

    MaybeClose(&input);
    MaybeClose(&output);

    external_command(token);
  }

#endif /* !STUDENT */

  Sigprocmask(SIG_SETMASK, &mask, NULL);
  return exitcode;
}

/* Start internal or external command in a subprocess that belongs to pipeline.
 * All subprocesses in pipeline must belong to the same process group. */
static pid_t do_stage(pid_t pgid, sigset_t *mask, int input, int output,
                      token_t *token, int ntokens, bool bg) {
  ntokens = do_redir(token, ntokens, &input, &output);

  if (ntokens == 0)
    app_error("ERROR: Command line is not well formed!");

  /* TODO: Start a subprocess and make sure it's moved to a process group. */
  pid_t pid = Fork();
#ifdef STUDENT

  if (pid == 0) {
    setpgid(getpid(), pgid);

    Sigprocmask(SIG_SETMASK, mask, NULL);

    Signal(SIGINT, SIG_DFL);
    Signal(SIGTSTP, SIG_DFL);
    Signal(SIGTTIN, SIG_DFL);
    Signal(SIGTTOU, SIG_DFL);

    if (input != -1)
      dup2(input, STDIN_FILENO);
    if (output != -1)
      dup2(output, STDOUT_FILENO);

    MaybeClose(&input);
    MaybeClose(&output);

    int exitcode = 0;
    if ((exitcode = builtin_command(token)) >= 0)
      exit(exitcode);

    external_command(token);
  }

  setpgid(pid, pgid);

#endif /* !STUDENT */

  return pid;
}

static void mkpipe(int *readp, int *writep) {
  int fds[2];
  Pipe(fds);
  fcntl(fds[0], F_SETFD, FD_CLOEXEC);
  fcntl(fds[1], F_SETFD, FD_CLOEXEC);
  *readp = fds[0];
  *writep = fds[1];
}

/* Pipeline execution creates a multiprocess job. Both internal and external
 * commands are executed in subprocesses. */
static int do_pipeline(token_t *token, int ntokens, bool bg) {
  pid_t pid, pgid = 0;
  int job = -1;
  int exitcode = 0;

  int input = -1, output = -1, next_input = -1;

  mkpipe(&next_input, &output);

  sigset_t mask;
  Sigprocmask(SIG_BLOCK, &sigchld_mask, &mask);

  /* TODO: Start pipeline subprocesses, create a job and monitor it.
   * Remember to close unused pipe ends! */
#ifdef STUDENT

  int cmd_start = 0;
  int cmd_length = 0;
  bool is_job_created = false;

  token_t *token_aux = token;

  for (int i = 0; i < ntokens; i++) {
    if (token[i] == T_PIPE) {
      token_aux = &token[cmd_start];

      pid = do_stage(pgid, &mask, input, output, token_aux, cmd_length, bg);

      if (is_job_created) {
        MaybeClose(&input);
      }

      MaybeClose(&output);

      input = next_input;
      mkpipe(&next_input, &output);

      if (!is_job_created) {
        is_job_created = true;
        pgid = pid;
        job = addjob(pgid, bg);
      }

      addproc(job, pid, token);

      cmd_start = i + 1;
      cmd_length = 0;
    } else {
      cmd_length++;
    }
  }

  MaybeClose(&next_input);
  MaybeClose(&output);
  token_aux = &token[cmd_start];
  pid =
    do_stage(pgid, &mask, input, output, token_aux, ntokens - cmd_start, bg);

  MaybeClose(&input);
  addproc(job, pid, token);

  if (bg == 0) {
    exitcode = monitorjob(&mask);
  } else {
    msg("[%d] running '%s'\n", job, jobcmd(job));
  }

#endif /* !STUDENT */

  Sigprocmask(SIG_SETMASK, &mask, NULL);
  return exitcode;
}

static bool is_pipeline(token_t *token, int ntokens) {
  for (int i = 0; i < ntokens; i++)
    if (token[i] == T_PIPE)
      return true;
  return false;
}

static void eval(char *cmdline) {
  bool bg = false;
  int ntokens;
  token_t *token = tokenize(cmdline, &ntokens);

  if (ntokens > 0 && token[ntokens - 1] == T_BGJOB) {
    token[--ntokens] = NULL;
    bg = true;
  }

  if (ntokens > 0) {
    if (is_pipeline(token, ntokens)) {
      do_pipeline(token, ntokens, bg);
    } else {
      do_job(token, ntokens, bg);
    }
  }

  free(token);
}

#ifndef READLINE
static char *readline(const char *prompt) {
  char *line = Malloc(MAXLINE);
  int len, res;

  write(STDOUT_FILENO, prompt, strlen(prompt));

  for (len = 0; len < MAXLINE; len++) {
    if (!(res = Read(STDIN_FILENO, line + len, 1)))
      break;

    if (line[len] == '\n') {
      line[len] = '\0';
      return line;
    }
  }

  if (len == 0) {
    free(line);
    return NULL;
  }

  return line;
}
#endif

int main(int argc, char *argv[]) {
#ifdef READLINE
  rl_initialize();
#endif

  sigemptyset(&sigchld_mask);
  sigaddset(&sigchld_mask, SIGCHLD);

  if (getsid(0) != getpgid(0))
    Setpgid(0, 0);

  initjobs();

  Signal(SIGINT, sigint_handler);
  Signal(SIGTSTP, SIG_IGN);
  Signal(SIGTTIN, SIG_IGN);
  Signal(SIGTTOU, SIG_IGN);

  char *line;
  while (true) {
    if (!sigsetjmp(loop_env, 1)) {
      line = readline("# ");
    } else {
      msg("\n");
      continue;
    }

    if (line == NULL)
      break;

    if (strlen(line)) {
#ifdef READLINE
      add_history(line);
#endif
      eval(line);
    }
    free(line);
    watchjobs(FINISHED);
  }

  msg("\n");
  shutdownjobs();

  return 0;
}
