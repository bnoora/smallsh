#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <signal.h>
#include <unistd.h> 
#include <fcntl.h>
#include <stdbool.h>
#include <sys/wait.h>
#include <setjmp.h>


#ifndef MAX_WORDS
#define MAX_WORDS 512
#endif

int bg_process_c = 0; // Number of background processes
int last_fg_stat = 0; // Last foreground process status
int last_fg_pid = 0; // Last foreground process pid
bool is_background = false;
pid_t last_bg_pid = 0;
int last_bg_stat = 0;
char *input_file;
char *output_file;
bool is_stopped = false;

jmp_buf buf;
char *words[MAX_WORDS];
size_t wordsplit(char const *line);
char * expand(char const *word);
void handle_SIGINT(int sig);
void sigtstp_handler(int sig_num);
void set_signal_handlers();
void check_bg_processes();
void exithandle(char *args[]);
void cdhandle(char *args[]);
void handle_sigint(int sig);



bool parse_command(char *words[], size_t nwords, char *args[], char **input_file, char **output_file, bool *is_append) {

    int args_index = 0;
    bool is_background = false;
    *input_file = NULL;
    *output_file = NULL;
    *is_append = false;
    for (size_t i = 0; i < nwords; ++i) {
        if (strcmp(words[i], "<") == 0) {
            // inp redirection
            *input_file = words[++i];
        } else if (strcmp(words[i], ">") == 0) {
            // outp redirection
            *is_append = false;
            *output_file = words[++i];
        } else if (strcmp(words[i], ">>") == 0) {
            // outp redirection w/ append
            *is_append = true;
            *output_file = words[++i];
        } else if (strcmp(words[i], "&") == 0) {
            // Bg process
            is_background = true;
        } else {
            args[args_index++] = words[i];
        }
    }
    args[args_index] = NULL; 
    return is_background;
}

void execute_command(char *args[], bool is_background, char *input_file, char *output_file, bool is_append) {

    pid_t pid = fork();
    if (pid < 0) {
        // err check for fork
        perror("fork err");
        exit(1);
    } else if (pid == 0) {
        // child proc
        // TODO multiple redirections, idk where to start
        if (input_file) {
            int fd = open(input_file, O_RDONLY);
            if (fd < 0) err(1, "%s", input_file);
            if (dup2(fd, STDIN_FILENO) < 0) err(1, "dup2");
            close(fd);
        }
        if (output_file) {
            int flags = O_WRONLY | O_CREAT;
            if (is_append) {
                flags |= O_APPEND;
            } else {
                flags |= O_TRUNC;
            }
            int fd = open(output_file, flags, 0777);
            if (fd < 0) err(1, "%s", output_file);
            if (dup2(fd, STDOUT_FILENO) < 0) err(1, "dup2");
            close(fd);
        }
        struct sigaction SIGINT_dfl= {0};
        SIGINT_dfl.sa_handler = SIG_DFL;
        sigaction(SIGINT, &SIGINT_dfl, NULL);
        struct sigaction SIGTSTP_dfl = {0};
        SIGTSTP_dfl.sa_handler = SIG_DFL;
        sigaction(SIGTSTP, &SIGTSTP_dfl, NULL);
        
        if (execvp(args[0], args) < 0) {
            exit(1);
        }
    } 


    // parent proc
    else {

        int status;
        if (is_background) {
            last_bg_pid = pid;
        } else {
            last_bg_pid = 0;
            last_fg_pid = pid;
            waitpid(pid, &status, WUNTRACED);
            if (WIFEXITED(status)) {
                    last_fg_stat = WEXITSTATUS(status);
            }
            else if (WIFSIGNALED(status)) {
                    last_fg_stat = 128 + WTERMSIG(status);
            }
            else if(WIFSTOPPED(status)) {
                is_stopped = true;
                last_fg_stat = WSTOPSIG(status);
                kill(pid, SIGCONT); // Sends the continue signal
                fprintf(stderr, "Child process %d stopped. Continuing.\n", pid);
                last_bg_pid = pid;
            }
        }
    }       
}

int main(int argc, char *argv[])
{ 
    /*---------------*/
    FILE *input = stdin;
    char *input_fn = "(stdin)";
    if (argc == 2) {
    input_fn = argv[1];
    input = fopen(input_fn, "re");
    if (!input) err(1, "%s", input_fn);
    } 
    else if (argc > 2) {
    errx(1, "too many arguments");
    }
    char *line = NULL;
    size_t n = 0;

    /*---------------*/

    char *ps1 = getenv("PS1");
    if (!ps1) {
    setenv("PS1", "", 1);
    ps1 = getenv("PS1");
    }


    for (;;) 
    {
        check_bg_processes(); // checks for bg procs
        
        // signal handler p1
        struct sigaction SIGINT_ign = {0};
        sigfillset(&SIGINT_ign.sa_mask);
        SIGINT_ign.sa_flags = 0;
        SIGINT_ign.sa_handler = SIG_IGN;
        sigaction(SIGINT, &SIGINT_ign, NULL);
        struct sigaction SIGTSTP_ign = {0};
        sigfillset(&SIGTSTP_ign .sa_mask);
        SIGTSTP_ign.sa_flags = 0;
        SIGTSTP_ign.sa_handler = SIG_IGN;
        sigaction(SIGTSTP, &SIGTSTP_ign, NULL);
        /*-----------------------*/

        // prompt
        struct sigaction action4;
        action4.sa_handler = handle_SIGINT;
        sigemptyset(&action4.sa_mask);
        action4.sa_flags = 0;
        sigaction(SIGINT, &action4, NULL);

        if (input == stdin) {
            fprintf(stderr, "%s", ps1);
            }  
            ssize_t line_len = getline(&line, &n, input);
            if (line_len < 0) {
                // eof check
                if (feof(input)) {
                    break;
                } else {
                    err(1, "%s", input_fn);
                }
            }

            // correctly ignores SIGINT when NOT reading input
            struct sigaction noinput;
            noinput.sa_handler = SIG_IGN;
            sigemptyset(&noinput.sa_mask);
            noinput.sa_flags = 0;
            sigaction(SIGINT, &noinput, NULL);
            

            size_t nwords = wordsplit(line);
            char *args[MAX_WORDS] = {0};
            char *input_file;
            char *output_file;
            bool is_append;
            bool is_background = parse_command(words, nwords, args, &input_file, &output_file, &is_append);        for (size_t i = 0; i < MAX_WORDS; ++i) {
                args[i] = NULL; // free mem
            }


            for (size_t i = 0; i < nwords; ++i) 
            {
                // fprintf(stderr, "Word %zu: %s  -->  ", i, words[i]); why were these here? in skeleton code if it makes testscript not work
                if(strcmp(words[i], "<") == 0 || strcmp(words[i], ">") == 0 || strcmp(words[i], ">>") == 0){
                i++;
                } else {
                char *exp_word = expand(words[i]);
                free(words[i]);
                words[i] = exp_word;
                args[i] = exp_word;
                // fprintf(stderr, "%s\n", words[i]); same as above 
                }
            }
            //execute command

            if (args[0] == NULL) {
                continue;
            }
            if (strcmp(args[0], "exit") == 0) 
            {
                exithandle(args);
            }
            else if (strcmp(args[0], "cd") == 0) 
            {
                cdhandle(args);
            }
            else 
            {
                execute_command(args, is_background, input_file, output_file, is_append);
            }
            sigaction(SIGINT, &SIGINT_ign, NULL);
            sigaction(SIGTSTP, &SIGTSTP_ign, NULL);
            continue;
    }
}

char *words[MAX_WORDS] = {0};

/* Splits a string into words delimited by whitespace. Recognizes
 * comments as '#' at the beginning of a word, and backslash escapes.
 *
 * Returns number of words parsed, and updates the words[] array
 * with pointers to the words, each as an allocated string.
 */
size_t wordsplit(char const *line) {
  size_t wlen = 0;
  size_t wind = 0;

  char const *c = line;
  for (;*c && isspace(*c); ++c); /* discard leading space */

  for (; *c;) {
    if (wind == MAX_WORDS) break;
    /* read a word */
    if (*c == '#') break;
    for (;*c && !isspace(*c); ++c) {
      if (*c == '\\') ++c;
      void *tmp = realloc(words[wind], sizeof **words * (wlen + 2));
      if (!tmp) err(1, "realloc");
      words[wind] = tmp;
      words[wind][wlen++] = *c; 
      words[wind][wlen] = '\0';
    }
    ++wind;
    wlen = 0;
    for (;*c && isspace(*c); ++c);
  }
  return wind;
}

/* Find next instance of a parameter within a word. Sets
 * start and end pointers to the start and end of the parameter
 * token.
 */
char
param_scan(char const *word, char const **start, char const **end)
{
  static char const *prev;
  if (!word) word = prev;
  
  char ret = 0;
  *start = 0;
  *end = 0;
  for (char const *s = word; *s && !ret; ++s) {
    s = strchr(s, '$');
    if (!s) break;
    switch (s[1]) {
    case '$':
    case '!':
    case '?':
      ret = s[1];
      *start = s;
      *end = s + 2;
      break;
    case '{':;
      char *e = strchr(s + 2, '}');
      if (e) {
        ret = '{';  // Assign '{' to differentiate this case
        *start = s;
        *end = e + 1;
      }
      break;
    }
  }
  prev = *end;
  return ret;
}



/* Simple string-builder function. Builds up a base
 * string by appending supplied strings/character ranges
 * to it.
 */
char *
build_str(char const *start, char const *end)
{
  static size_t base_len = 0;
  static char *base = 0;

  if (!start) {
    /* Reset; new base string, return old one */
    char *ret = base;
    base = NULL;
    base_len = 0;
    return ret;
  }
  /* Append [start, end) to base string 
   * If end is NULL, append whole start string to base string.
   * Returns a newly allocated string that the caller must free.
   */
  size_t n = end ? end - start : strlen(start);
  size_t newsize = sizeof *base *(base_len + n + 1);
  void *tmp = realloc(base, newsize);
  if (!tmp) err(1, "realloc");
  base = tmp;
  memcpy(base + base_len, start, n);
  base_len += n;
  base[base_len] = '\0';

  return base;
}

/* Expands all instances of $! $$ $? and ${param} in a string 
 * Returns a newly allocated string that the caller must free
 */
char *expand(char const *word)
{
    char const *pos = word;
    char buffer[32];
    pid_t pid = getpid();
    int status = last_fg_stat;
    const char *start, *end;
    char c;

    build_str(NULL, NULL);
    
    do {
        c = param_scan(pos, &start, &end);
        build_str(pos, start);

        if (c == '!')
        {
            pid_t bgpid = last_bg_pid;
            if(bgpid > 0) {
                sprintf(buffer, "%d", bgpid);
            } else {
                strcpy(buffer, "");
            }
            build_str(buffer, NULL);
        }
        else if (c == '$')
        {
            sprintf(buffer, "%d", pid);
            build_str(buffer, NULL);
        }
        else if (c == '?')
        {
            if(status >= 0) {
                sprintf(buffer, "%d", status);
            } else {
                strcpy(buffer, "0");
            }
            build_str(buffer, NULL);
        }
        else if (c == '{')
        {
            char *e = strchr(start + 2, '}');
            if (e)
            {
                size_t len = e - (start + 2);
                char *param = malloc(sizeof(char) * (len + 1));
                strncpy(param, start + 2, len);
                param[len] = '\0'; // null terminate
                char *val = getenv(param);
                if (val)
                    build_str(val, NULL);
                free(param);
                pos = e + 1;
            }
        }

        if (c) {
            pos = end;
        }

    } while(c);

    return build_str(start, NULL);
}

/*Checks the status of bg prcos */
void check_bg_processes(){       
    int status;
    pid_t pid = waitpid(-1, &status, WNOHANG | WUNTRACED);
    if(pid > 0) {
        if(WIFEXITED(status)) {
            last_bg_stat = WEXITSTATUS(status);
            fprintf(stderr, "Child process %d done. Exit status %d.\n", pid,last_bg_stat);
        } else if(WIFSIGNALED(status)) {
            last_bg_stat = WTERMSIG(status);
            fprintf(stderr, "Child process %d done. Signaled %d.\n", pid, last_bg_stat);
        } else if(WIFSTOPPED(status)) {
            last_bg_stat = WSTOPSIG(status);
            fprintf(stderr, "Child process %d stopped. Continuing.\n", pid);
        }
    } else {
        // nothing
    }
}

// sets the signal handlers
void set_signal_handlers()
{
    // ignore SIGTSTP CtrlZ
    struct sigaction sigtstp_action = {0};
    sigtstp_action.sa_handler = SIG_IGN; // Ignore the signal
    if (sigaction(SIGTSTP, &sigtstp_action, NULL) < 0)
    {
        perror("sigaction: SIGTSTP");
        exit(EXIT_FAILURE);
    }
    // do nothing for SIGINT Ctrl C
    struct sigaction sigint_action = {0};
    sigint_action.sa_handler = SIG_IGN; // Default action
    sigint_action.sa_flags = 0; // No flags
    if (sigaction(SIGINT, &sigint_action, NULL) < 0)
    {
        perror("sigaction: SIGINT");
        exit(EXIT_FAILURE);
    }
}

// handles cd conmmand, worked first time
void cdhandle(char **words)
{
    // handle when theres a cd command
    if (strcmp(words[0], "cd") == 0)
    {
        if (words[1] == NULL)
        {
            chdir(getenv("HOME"));
        }
        else if (words[2] != NULL)
        {
            fprintf(stderr, "Too many arguments\n");
        }
        else if (words[1] != NULL)
        {
            chdir(words[1]);
        }
        else
        {
            fprintf(stderr, "No such directory\n");
        }
    }
}

// handles exit command, had to change to make it work with different exit numbers
void exithandle(char *args[])
{
    // handle when theres an exit command
    int exit_status = 0;
    if (args[1] != NULL) {
        exit_status = atoi(args[1]);
    }
    exit(exit_status);
}   

void handle_SIGINT(int sig)
{
    fprintf(stderr, "\n");
    // longjmp(buf, 1);
}