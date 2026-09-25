#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/stat.h>
#include <syslog.h>
#include <pthread.h>
#include <pwd.h>
#include <limits.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <time.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <spawn.h>
#include <poll.h>

extern char **environ;


char *spinner[] = {"⣾","⣽","⣻","⢿","⡿","⣟","⣯","⣷"};
// fields the command's thread updates and the main loop reads are atomic
typedef struct {
  _Atomic int spinner;
  char *command;
  char *name;
  char *out;
  char *previousOut;
  char *diffOut;  // For storing difference-highlighted output
  size_t outPos;
  size_t outCap;
  _Atomic int status;
  int change;
  int warned127;
  int pid;
  pthread_t thread;
  long start_time;
  _Atomic long last_duration_ms;
  _Atomic long prev_duration_ms;
  _Atomic int runs;      // completed runs
  _Atomic int changes;   // runs whose stdout differed from the previous run
  int seenChanges;       // changes already acted on by the main loop
  // guards out/outPos/outCap/previousOut/diffOut: the command's thread
  // writes them while the main loop and other commands read them
  pthread_mutex_t lock;
} COMMAND;

COMMAND *c;

typedef struct {
  int expectedStatus;
  int interval;
  int timeout;
  int cmd_timeout;
  long start_time;
  int any;
  int change;
  int silent;
  int forever;
  int daemonize;
  int fail;
  int stdout;
  int diff;
  char *exec;
  char* service;
  char* args;
  int nCommands;
  int no_stderr;
  int retry;
  int json;
  int lap;
} ARGS;

ARGS args = {.interval=200, .expectedStatus = 0, .silent=0, .change=0, .nCommands=0, .args="", .timeout=0, .cmd_timeout=0, .retry=0};

int const BUF_SIZE = 1024;
int const CHUNK_SIZE = BUF_SIZE * 100;

char* replace(const char* oldW, const char* newW, const char* s) {
    char* result;
    int i, cnt = 0;
    int newWlen = strlen(newW);
    int oldWlen = strlen(oldW);

    // Counting the number of times old word
    // occur in the string
    for (i = 0; s[i] != '\0'; i++) {
        if (strstr(&s[i], oldW) == &s[i]) {
            cnt++;

            // Jumping to index after the old word.
            i += oldWlen - 1;
        }
    }

    // Making new string of enough length
    result = (char*)malloc(i + cnt * (newWlen - oldWlen) + 1);

    i = 0;
    while (*s) {
        // compare the substring with the result
        if (strstr(s, oldW) == s) {
            strcpy(&result[i], newW);
            i += newWlen;
            s += oldWlen;
        }
        else
            result[i++] = *s++;
    }

    result[i] = '\0';
    return result;
  if (args.daemonize) closelog();
  return 0;
}

void print_autocomplete_fish() {
  printf("complete -c await -l version -s v -d 'Print the version of await'\n"
         "complete -c await -l help -d 'Print this help'\n"
         "complete -c await -l stdout -s o -d 'Print stdout of commands'\n"
         "complete -c await -l silent -s V -d 'Do not print spinners and commands'\n"
         "complete -c await -l fail -s f -d 'Waiting commands to fail'\n"
         "complete -c await -l status -s s -d 'Expected status [default: 0]' -r\n"
         "complete -c await -l any -s a -d 'Terminate if any of command return expected status'\n"
         "complete -c await -l change -s c -d 'Waiting for stdout to change and ignore status codes'\n"
         "complete -c await -l diff -s d -d 'Highlight differences between previous and current output'\n"
         "complete -c await -l exec -s e -d 'Run some shell command on success' -r\n"
         "complete -c await -l interval -s i -d 'Seconds between one round of commands [default: 0.2]' -r\n"
         "complete -c await -l timeout -s T -d 'Seconds to wait before giving up [default: 0]' -r\n"
         "complete -c await -l cmd-timeout -s t -d 'Seconds per command before killing it' -r\n"
         "complete -c await -l retry -s r -d 'Max number of attempts before giving up [default: 0 (unlimited)]' -r\n"
         "complete -c await -l forever -s F -d 'Do not exit ever'\n"
         "complete -c await -l name -s n -d 'Label for the next command (usable as \\\\name in --exec)' -r\n"
         "complete -c await -l json -s j -d 'Output results as JSON on exit'\n"
         "complete -c await -l lap -s l -d 'Show last run duration per command in spinner'\n"
         "complete -c await -l service -s S -d 'Create systemd user service with same parameters and activate it'\n"
         "complete -c await -l no-stderr -s E -d 'Surpress stderr of commands by adding 2>/dev/null to commands'\n"
         "complete -c await -l watch -s w -d 'Equivalent to -fVodE (fail, silent, stdout, diff, no-stderr)'\n"
         "\n"
         "# For command completion\n"
         "complete -c await -f -a '(__fish_complete_command)'\n");
}

void print_autocomplete_bash() {
  printf("_await() {\n"
         "    local cur prev opts\n"
         "    COMPREPLY=()\n"
         "    cur=\"${COMP_WORDS[COMP_CWORD]}\"\n"
         "    prev=\"${COMP_WORDS[COMP_CWORD-1]}\"\n"
         "\n"
         "    opts=\"--help --stdout --silent --fail --status --any --change --diff --exec --interval --timeout --cmd-timeout --retry --forever --service --version --no-stderr --watch --name --json --lap\"\n"
         "\n"
         "    case \"${prev}\" in\n"
         "        --exec)\n"
         "            COMPREPLY=($(compgen -c -- \"${cur}\"))\n"
         "            return 0\n"
         "            ;;\n"
         "        --status|--interval|--timeout|--cmd-timeout|--retry|--name|--service)\n"
         "            return 0\n"
         "            ;;\n"
         "    esac\n"
         "\n"
         "    if [[ ${cur} == -* ]]; then\n"
         "        COMPREPLY=($(compgen -W \"${opts}\" -- \"${cur}\"))\n"
         "        return 0\n"
         "    fi\n"
         "\n"
         "    COMPREPLY=($(compgen -c -- \"${cur}\"))\n"
         "    return 0\n"
         "}\n"
         "\n"
         "complete -F _await await\n");
}

void print_autocomplete_zsh() {
  printf("# Ensure compinit is loaded\n"
         "autoload -Uz compinit\n"
         "compinit\n"
         "\n"
         "# Define the completion function for 'await'\n"
         "_await() {\n"
         "  _arguments -s -S \\\n"
         "    '--help[Print this help]' \\\n"
         "    '--version[Display version]' \\\n"
         "    '--stdout[Print stdout of commands]' \\\n"
         "    '--no-stderr[Surpress stderr of commands by adding 2>/dev/null to commands]' \\\n"
         "    '--silent[Do not print spinners and commands]' \\\n"
         "    '--fail[Wait for commands to fail]' \\\n"
         "    '--status[Expected status (default: 0)]:status:' \\\n"
         "    '--any[Terminate if any command returns expected status]' \\\n"
         "    '--change[Wait for stdout to change and ignore status codes]' \\\n"
         "    '--diff[Highlight differences between previous and current output]' \\\n"
         "    '--exec[Run some shell command on success]:command:_command_names' \\\n"
         "    '--interval[Seconds between rounds of commands (default: 0.2)]:interval:' \\\n"
         "    '--timeout[Seconds to wait before giving up (default: 0)]:timeout:' \\\n"
         "    '--cmd-timeout[Seconds per command before killing it]:cmd-timeout:' \\\n"
         "    '--retry[Max number of attempts before giving up (default: 0 = unlimited)]:retry:' \\\n"
         "    '--forever[Do not exit ever]' \\\n"
         "    '--name[Label for the next command]:name:' \\\n"
         "    '--json[Output results as JSON on exit]' \\\n"
         "    '--lap[Show last run duration per command in spinner]' \\\n"
         "    '--watch[Equivalent to -fVodE (fail, silent, stdout, diff, no-stderr)]' \\\n"
         "    '--service[Create systemd user service with same parameters and activate it]:service name:'\n"
         "}\n"
         "\n"
         "# Register the completion function\n"
         "compdef _await await\n");
}

void install_autocompletions() {
  const char *home;
  if ((home = getenv("HOME")) == NULL) {
    home = getpwuid(getuid())->pw_dir;
  }

  // Get the path to the current binary
  char binary_path[PATH_MAX];
  ssize_t len = readlink("/proc/self/exe", binary_path, sizeof(binary_path) - 1);
  if (len == -1) {
    // Fallback to "await" if we can't get the binary path
    strcpy(binary_path, "await");
  } else {
    binary_path[len] = '\0';
  }

  printf("Detecting installed shells and installing completions...\n\n");

  // Check bash
  if (access("/bin/bash", F_OK) == 0 || access("/usr/bin/bash", F_OK) == 0) {
    printf("✓ bash found\n");
    char bashrc[PATH_MAX];
    snprintf(bashrc, sizeof(bashrc), "%s/.bashrc", home);
    char cmd[PATH_MAX * 4];
    // Check if completions already exist, only append if not present
    snprintf(cmd, sizeof(cmd), "grep -q '_await' '%s' 2>/dev/null || '%s' --autocomplete-bash >> '%s' 2>/dev/null", bashrc, binary_path, bashrc);
    int ret = system(cmd);
    if (ret == 0) {
      printf("  → completions installed to ~/.bashrc\n");
    } else {
      printf("  → failed to install completions\n");
    }
  } else {
    printf("✗ bash not found\n");
  }

  // Check zsh
  if (access("/bin/zsh", F_OK) == 0 || access("/usr/bin/zsh", F_OK) == 0) {
    printf("✓ zsh found\n");
    char zshrc[PATH_MAX];
    snprintf(zshrc, sizeof(zshrc), "%s/.zshrc", home);
    char cmd[PATH_MAX * 4];
    // Check if completions already exist, only append if not present
    snprintf(cmd, sizeof(cmd), "grep -q '_await' '%s' 2>/dev/null || '%s' --autocomplete-zsh >> '%s' 2>/dev/null", zshrc, binary_path, zshrc);
    int ret = system(cmd);
    if (ret == 0) {
      printf("  → completions installed to ~/.zshrc\n");
    } else {
      printf("  → failed to install completions\n");
    }
  } else {
    printf("✗ zsh not found\n");
  }

  // Check fish
  if (access("/bin/fish", F_OK) == 0 || access("/usr/bin/fish", F_OK) == 0) {
    printf("✓ fish found\n");
    char fish_dir[PATH_MAX];
    snprintf(fish_dir, sizeof(fish_dir), "%s/.config/fish/completions", home);
    char fish_file[PATH_MAX];
    snprintf(fish_file, sizeof(fish_file), "%s/await.fish", fish_dir);
    char cmd[PATH_MAX * 4];
    // Check if completions already exist, only append if not present
    // await.fish belongs to us, so (re)write it instead of appending
    snprintf(cmd, sizeof(cmd), "mkdir -p '%s' 2>/dev/null && '%s' --autocomplete-fish > '%s' 2>/dev/null", fish_dir, binary_path, fish_file);
    int ret = system(cmd);
    if (ret == 0) {
      printf("  → completions installed to ~/.config/fish/completions/await.fish\n");
    } else {
      printf("  → failed to install completions\n");
    }
  } else {
    printf("✗ fish not found\n");
  }

  printf("\nAutocompletions installation complete!\n");
  exit(0);
}

static void daemonize() {
    pid_t pid;
    pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);

    /* Success: Let the parent terminate */
    if (pid > 0) exit(EXIT_SUCCESS);

    /* On success: The child process becomes session leader */
    if (setsid() < 0) exit(EXIT_FAILURE);

    /* Catch, ignore and handle signals */
    //TODO: Implement a working signal handler */
    // signal(SIGCHLD, SIG_IGN);
    // signal(SIGHUP, SIG_IGN);
    //
    // /* Fork off for the second time*/
    // pid = fork();
    //
    // if (pid < 0) exit(EXIT_FAILURE);
    //
    // /* Success: Let the parent terminate */
    // if (pid > 0) exit(EXIT_SUCCESS);

    /* Set new file permissions */
    umask(0);

    setpgid(0, 0);
    /* Close all open file descriptors */
    // int x;
    // for (x = sysconf(_SC_OPEN_MAX); x>=0; x--) {
    //     close (x);
    // }

    // /* Open the log file */
    openlog("await", LOG_PID, LOG_DAEMON);
}

volatile sig_atomic_t stop = 0;

int msleep(long msec)
{
    struct timespec ts;
    int res;

    if (msec < 0)
    {
        errno = EINVAL;
        return -1;
    }

    ts.tv_sec = msec / 1000;
    ts.tv_nsec = (msec % 1000) * 1000000;

    do {
        res = nanosleep(&ts, &ts);
    } while (res && errno == EINTR);

    return res;
}

long current_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
}

// command output without trailing newlines, like shell $(...)
char * substitution(COMMAND *cmd) {
  pthread_mutex_lock(&cmd->lock);
  char *out = strdup(cmd->previousOut);
  pthread_mutex_unlock(&cmd->lock);
  size_t len = strlen(out);
  while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r')) out[--len] = '\0';
  return out;
}

static void buf_add(char **buf, size_t *len, size_t *cap, const char *s, size_t n) {
  if (*len + n + 1 > *cap) {
    *cap = (*len + n + 1) * 2;
    *buf = realloc(*buf, *cap);
  }
  memcpy(*buf + *len, s, n);
  *len += n;
  (*buf)[*len] = '\0';
}

// Placeholders become references to $AWAIT_<n>, whose value is passed in
// the environment (command_env): the shell never parses a variable's value
// as code, however the placeholder is nested ('...', "...", $(...)).
static void buf_add_reference(char **buf, size_t *len, size_t *cap, int n, char quote) {
  char ref[64];
  if (quote == '"') snprintf(ref, sizeof(ref), "${AWAIT_%d}", n);
  else if (quote == '\'') snprintf(ref, sizeof(ref), "'\"${AWAIT_%d}\"'", n);
  else snprintf(ref, sizeof(ref), "\"${AWAIT_%d}\"", n);
  buf_add(buf, len, cap, ref, strlen(ref));
}

// placeholder at p (\1, \2 ... or \name) -> its command, and its length
static COMMAND *placeholder(const char *p, size_t *plen) {
  COMMAND *found = NULL;
  *plen = 0;
  int n = 0;
  for (size_t j = 1; p[j] >= '0' && p[j] <= '9' && j < 9; j++) {
    n = n * 10 + (p[j] - '0');
    if (n >= 1 && n <= args.nCommands) { found = &c[n]; *plen = j + 1; }
  }
  for (int i = 1; i <= args.nCommands; i++) {
    if (!c[i].name || c[i].name == c[i].command) continue;
    size_t l = strlen(c[i].name);
    if (l + 1 > *plen && strncmp(p + 1, c[i].name, l) == 0) { found = &c[i]; *plen = l + 1; }
  }
  return found;
}

// new string with \1, \2 ... and \name replaced by the commands' last output
char * replace_placeholders(const char *string) {
  char *out = NULL;
  size_t len = 0, cap = 0;
  char quote = 0;
  buf_add(&out, &len, &cap, "", 0);
  for (const char *p = string; *p; ) {
    if (*p == '\\') {
      size_t plen;
      // \\1 means \1: a backslash left in front of the reference would escape its $
      if (p[1] == '\\' && quote != '\'' && placeholder(p + 1, &plen)) p++;
      COMMAND *src = placeholder(p, &plen);
      if (src && src->runs) {
        buf_add_reference(&out, &len, &cap, (int)(src - c), quote);
        p += plen;
        continue;
      }
      // not a placeholder: outside '...' an escaped quote doesn't open or close one
      size_t n = (quote != '\'' && p[1] && strchr("'\"`$", p[1])) ? 2 : 1;
      buf_add(&out, &len, &cap, p, n);
      p += n;
      continue;
    }
    if (*p == '\'' && quote != '"') quote = quote ? 0 : '\'';
    else if (*p == '"' && quote != '\'') quote = quote ? 0 : '"';
    buf_add(&out, &len, &cap, p, 1);
    p++;
  }
  return out;
}

// environment for a command: ours plus AWAIT_<n>=<output of command n>
char ** command_env() {
  size_t n = 0;
  while (environ[n]) n++;
  char **env = malloc((n + args.nCommands + 1) * sizeof(char *));
  size_t k = 0;
  for (size_t i = 0; i < n; i++)
    if (strncmp(environ[i], "AWAIT_", 6) != 0) env[k++] = environ[i];
  for (int i = 1; i <= args.nCommands; i++) {
    if (!c[i].runs) continue;
    char *value = substitution(&c[i]);
    env[k] = malloc(strlen(value) + 32);
    sprintf(env[k++], "AWAIT_%d=%s", i, value);
    free(value);
  }
  env[k] = NULL;
  return env;
}

void free_command_env(char **env) {
  for (char **e = env; *e; e++)
    if (strncmp(*e, "AWAIT_", 6) == 0) free(*e);
  free(env);
}

// does string contain placeholder \n (and not e.g. \n0)?
int references(const char *string, int n) {
  char C[16];
  sprintf(C, "\\%d", n);
  for (const char *p = strstr(string, C); p; p = strstr(p + 1, C))
    if (p[strlen(C)] < '0' || p[strlen(C)] > '9') return 1;
  return 0;
}

// commands referencing earlier commands (\1, \2...) wait for their first output
void wait_for_dependencies(COMMAND *cmd) {
  for (int k = 1; &c[k] < cmd; k++)
  {
    char named[256] = "";
    if (c[k].name && c[k].name != c[k].command) snprintf(named, sizeof(named), "\\%s", c[k].name);
    if (references(cmd->command, k) || (*named && strstr(cmd->command, named)))
      while (!c[k].runs && !stop) msleep(10);
  }
}

pid_t exec_pid = 0;  // running --exec, if any

pid_t start_exec() {
  // build the command before fork: malloc in the child of a threaded process can deadlock
  char *cmd = replace_placeholders(args.exec);
  char **env = command_env();
  fflush(stdout);
  fflush(stderr);
  pid_t pid = fork();
  if (pid == 0) {
    // keep stdout a single JSON document
    if (args.json) dup2(STDERR_FILENO, STDOUT_FILENO);
    execle("/bin/sh", "sh", "-c", cmd, NULL, env);
    _exit(127);
  }
  free(cmd);
  free_command_env(env);
  if (pid < 0) perror("await: cannot run --exec");
  return pid;
}

int wait_exec(pid_t pid) {
  if (pid < 0) return 1;
  int status;
  while (waitpid(pid, &status, 0) < 0)
    if (errno != EINTR) return 1;
  return WIFSIGNALED(status) ? 128 + WTERMSIG(status) : WEXITSTATUS(status);
}

int not_done_cmd(int i) {
  if (args.change) return c[i].changes == c[i].seenChanges;
  return c[i].status==-1 || (args.fail && c[i].status == 0) || (!args.fail && c[i].status != args.expectedStatus);
}

void print_json_string(const char *s) {
  putchar('"');
  for (const unsigned char *p = (const unsigned char *)(s ? s : ""); *p; p++) {
    if (*p == '"') printf("\\\"");
    else if (*p == '\\') printf("\\\\");
    else if (*p == '\n') printf("\\n");
    else if (*p == '\r') printf("\\r");
    else if (*p == '\t') printf("\\t");
    else if (*p < 0x20) printf("\\u%04x", *p);
    else putchar(*p);
  }
  putchar('"');
}

// copy of what to display for a command: its diff (with --diff), the output
// of the run in progress, or else its last completed output; NULL if none
char * output_snapshot(COMMAND *cmd) {
  char *copy = NULL;
  pthread_mutex_lock(&cmd->lock);
  if (args.diff && cmd->diffOut) copy = strdup(cmd->diffOut);
  else if (cmd->out && *cmd->out) copy = strdup(cmd->out);
  else if (cmd->previousOut && *cmd->previousOut) copy = strdup(cmd->previousOut);
  pthread_mutex_unlock(&cmd->lock);
  return copy;
}

void print_json_result(int exit_code) {
  printf("{\"success\":%s,\"elapsed_ms\":%ld,\"commands\":[",
    exit_code == 0 ? "true" : "false", current_time_ms() - args.start_time);
  for (int i = 1; i <= args.nCommands; i++) {
    if (i > 1) printf(",");
    printf("{\"name\":");
    print_json_string(c[i].name);
    printf(",\"command\":");
    print_json_string(c[i].command);
    printf(",\"status\":%d,\"output\":", c[i].status);
    pthread_mutex_lock(&c[i].lock);
    print_json_string(c[i].previousOut);
    pthread_mutex_unlock(&c[i].lock);
    printf("}");
  }
  printf("]}\n");
}

// https://no-color.org: NO_COLOR set and non-empty disables color
int use_color() {
  const char *no_color = getenv("NO_COLOR");
  return !no_color || !*no_color;
}

// remove ANSI color codes (ESC [ ... m) in place
void strip_colors(char *s) {
  char *w = s;
  for (char *r = s; *r; r++) {
    if (r[0] == '\033' && r[1] == '[') {
      char *e = r + 2;
      while ((*e >= '0' && *e <= '9') || *e == ';') e++;
      if (*e == 'm') { r = e; continue; }
    }
    *w++ = *r;
  }
  *w = '\0';
}

// capacity of a sappendf string of length len: the next power of two
static size_t sappendf_cap(size_t len) {
  size_t cap = 1;
  while (cap < len + 1) cap *= 2;
  return cap;
}

// printf onto the end of a heap string of length *len (from strdup("")), growing it geometrically
void sappendf(char **s, size_t *len, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(NULL, 0, fmt, ap);
  va_end(ap);
  if (n < 0) return;
  size_t cap = sappendf_cap(*len + n);
  if (cap > sappendf_cap(*len)) {
    char *grown = realloc(*s, cap);
    if (!grown) {
      perror("await");
      exit(1);
    }
    *s = grown;
  }
  va_start(ap, fmt);
  vsnprintf(*s + *len, n + 1, fmt, ap);
  va_end(ap);
  *len += n;
}

char * colorize_comments(char *string) {
  if (!use_color() || !isatty(STDOUT_FILENO)) return string;
  string = replace("#", "\033[33m#", string);
  string = replace("\n", "\033[0m\n", string);
  return string;
}

char * highlight_differences(const char *old_text, const char *new_text) {
  if (!old_text || !new_text) return NULL;
  if (strcmp(old_text, new_text) == 0) return NULL; // No differences
  
  int old_len = strlen(old_text);
  int new_len = strlen(new_text);
  
  // Allocate enough space for highlighted text (worst case: every char highlighted)
  char *highlighted = malloc(new_len * 10 + 1); // worst case: every char wrapped in ANSI codes
  highlighted[0] = '\0';
  
  int old_pos = 0, new_pos = 0;
  int in_diff = 0;
  
  // Simple character-by-character diff
  while (new_pos < new_len) {
    if (old_pos < old_len && old_text[old_pos] == new_text[new_pos]) {
      // Characters match
      if (in_diff) {
        strcat(highlighted, "\033[0m"); // End highlighting
        in_diff = 0;
      }
      // Add the matching character
      int len = strlen(highlighted);
      highlighted[len] = new_text[new_pos];
      highlighted[len + 1] = '\0';
      old_pos++;
      new_pos++;
    } else {
      // Characters differ or we're at different positions
      if (!in_diff) {
        strcat(highlighted, "\033[32m"); // Start highlighting (green text)
        in_diff = 1;
      }
      
      // Add the differing character from new text
      int len = strlen(highlighted);
      highlighted[len] = new_text[new_pos];
      highlighted[len + 1] = '\0';
      new_pos++;
      
      // Skip characters in old text until we find a match or reach the end
      if (old_pos < old_len) {
        // Look ahead to see if we can find a match
        int found_match = 0;
        for (int lookahead = 1; lookahead <= 10 && (old_pos + lookahead) < old_len; lookahead++) {
          if (new_pos < new_len && old_text[old_pos + lookahead] == new_text[new_pos]) {
            old_pos += lookahead;
            found_match = 1;
            break;
          }
        }
        if (!found_match) {
          old_pos++;
        }
      }
    }
  }

  // Close highlighting if still open
  if (in_diff) {
    strcat(highlighted, "\033[0m");
  }
  
  return highlighted;
}

void help() {
  printf("%s", colorize_comments("await [options] commands\n\n"
  "# runs list of commands and waits for their termination\n"
  "\n\nEXAMPLES:\n"
  "# wait until your deployment is ready\n"
  "  await 'curl 127.0.0.1:3000/healthz' \\\n\t'kubectl wait --for=condition=Ready pod it-takes-forever-8545bd6b54-fk5dz' \\\n\t\"docker inspect --format='{{json .State.Running}}' elasticsearch 2>/dev/null | grep true\" \n\n"

  "# emulate watch https://linux.die.net/man/1/watch\n"
  "  await 'clear; du -h /tmp/file' 'dd if=/dev/random of=/tmp/file bs=1M count=1000 2>/dev/null' -of --silent\n\n"

  "# action on specific file type changes\n"
  "  await 'stat **.c' --change --forever --exec 'gcc *.c -o await -lpthread'\n\n"
  "# Kubernetes: wait for the pod, then forward the port\n"
  "  await 'kubectl get pod myapp | grep Running' --timeout 120 \\\n\t--exec 'kubectl port-forward pod/myapp 8080:80'\n\n"
  "# wait for postgres AND redis, then run the migration\n"
  "  await 'pg_isready -h localhost' 'redis-cli ping' \\\n\t--exec 'python manage.py migrate'\n\n"
  "# poll CI, auto-merge the moment it turns green\n"
  "  await 'gh run view --exit-status' --timeout 1800 --interval 30 \\\n\t--exec 'gh pr merge --auto --squash'\n\n"
  "# connect to whichever replica answers first\n"
  "  await 'curl -sf primary.db/health' 'curl -sf replica.db/health' --any --exec connect_to_db\n\n"
  "# waiting google (or your internet connection) to fail\n"
  "  await 'curl google.com' --fail\n\n"
  "# waiting only google to fail (https://ec.haxx.se/usingcurl/usingcurl-returns)\n"
  "  await 'curl google.com' --status 7\n\n"
  "# lazy version\n"
  "  await 'ls /tmp/redis.sock'; redis-cli -s /tmp/redis.sock\n\n"
  "# daily checking if I am on french reviera. Just in case\n"
  "  await 'curl https://ipapi.co/json 2>/dev/null | jq .city | grep Nice' --interval 86400\n\n"
  "# get pinged the moment your site goes down\n"
  "  await 'curl -sf https://myapp.com' --fail --forever --exec 'ntfy send \"site is down\"'\n\n"
  "# ...as a systemd daemon that survives reboots\n"
  "  await 'curl -sf https://myapp.com' --fail --forever --exec 'ntfy send \"site is down\"' --service site-monitor\n\n"
  "\nOPTIONS:\n"
  "  --help\t\t#print this help\n"
  "  --stdout -o\t\t#print stdout of commands\n"
  "  --no-stderr -E\t#suppress stderr output from commands\n"
  "  --watch -w\t\t#equivalent to -fVodE (fail, silent, stdout, diff, no-stderr)\n"
  "  --silent -V\t\t#do not print spinners and commands\n"
  "  --fail -f\t\t#waiting commands to fail\n"
  "  --status -s\t\t#expected status [default: 0]\n"
  "  --any -a\t\t#terminate if any of command return expected status\n"
  "  --change -c\t\t#waiting for stdout to change (the first run is the baseline) and ignore status codes\n"
  "  --diff -d\t\t#highlight differences between previous and current output (like watch -d)\n"
  "  --exec -e\t\t#run a shell command on success; await exits with its status\n"
  "  --interval -i\t\t#seconds between one round of commands [default: 0.2]\n"
  "  --timeout -T\t\t#seconds to wait before giving up [default: 0 (no timeout)]\n"
  "  --cmd-timeout -t\t#seconds per command run before killing it and everything it started (status 124)\n"
  "  --retry -r\t\t#max number of runs of each command before giving up [default: 0 (unlimited)]\n"
  "  --forever -F\t\t#do not exit ever\n"
  "  --name -n\t\t#label for the next command (shown in spinner, usable as \\name in --exec)\n"
  "  --json -j\t\t#output results as JSON on exit\n"
  "  --lap -l\t\t#show last run duration per command in spinner\n"
  "  --service -S\t\t#create systemd user service with same parameters and activate it\n"
  "  --version -v\t\t#print the version of await\n"

  "  --autocompletions\t#detect installed shells and auto-install completions for all of them\n"
  "  --autocomplete-fish\t#output fish shell autocomplete script\n"
  "  --autocomplete-bash\t#output bash shell autocomplete script\n"
  "  --autocomplete-zsh\t#output zsh shell autocomplete script\n"
  "\n\nNOTES:\n"
  "# \\1, \\2 ... \\n - will be substituted with n-th command stdout (trailing newline trimmed)\n"
  "# \\name - the same for a command labelled with --name\n"
  "# the output is passed as data ($AWAIT_1, $AWAIT_2 ...), so it is never run as shell code\n"
  "# you can use stdout substitution in --exec and in commands itself:\n"
  "  await 'echo 10' 'date +%S' 'expr \\1 + \\2' --exec 'echo \\3' --forever --silent\n"
  "# set NO_COLOR=1 to disable colors\n"

  // "# waiting for pup's author new blog post\n"
  // "  await 'mv /tmp/eric.new /tmp/eric.old &>/dev/null; http \"https://ericchiang.github.io/\" | pup \"a attr{href}\" > /tmp/eric.new; diff /tmp/eric.new /tmp/eric.old' --fail --exec 'ntfy send \"new article $1\"'\n\n"
  ));
  exit(0);
}

// void handle_sigint(int sig) {
//     stop = 1;
// }

int looks_like_bare_url(const char *s) {
  if (strncmp(s, "http://", 7) != 0 && strncmp(s, "https://", 8) != 0)
    return 0;
  // If it contains shell metacharacters or whitespace, the user already
  // wrapped it in a real command (e.g. "curl http://x | grep y").
  return strpbrk(s, " \t|&;<>()$`\\\"'") == NULL;
}

// append to args.args (the command line replayed by --service), growing it as needed
void args_append(const char *s) {
  static size_t len = 0, cap = 0;
  size_t n = strlen(s);
  if (len + n + 1 > cap) {
    size_t new_cap = (len + n + 1) * 2;
    char *grown = realloc(len ? args.args : NULL, new_cap);
    if (!grown) {
      perror("await");
      exit(1);
    }
    args.args = grown;
    cap = new_cap;
  }
  memcpy(args.args + len, s, n + 1);
  len += n;
}

// s as a double-quoted systemd ExecStart argument (new string)
char * systemd_quote(const char *s) {
  char *q = malloc(strlen(s) * 2 + 3), *w = q;
  if (!q) {
    perror("await");
    exit(1);
  }
  *w++ = '"';
  for (const char *p = s; *p; p++) {
    if (*p == '\\' || *p == '"') { *w++ = '\\'; *w++ = *p; }
    else if (*p == '$' || *p == '%') { *w++ = *p; *w++ = *p; }
    else if (*p == '\n') { *w++ = '\\'; *w++ = 'n'; }
    else *w++ = *p;
  }
  *w++ = '"';
  *w = '\0';
  return q;
}

void args_append_quoted(const char *s) {
  char *q = systemd_quote(s);
  args_append(q);
  free(q);
}

void parse_args(int argc, char *argv[]) {
    int getopt;
    char **names = calloc(argc, sizeof(char *));
    c = calloc(argc + 1, sizeof(COMMAND));
    int names_count = 0;

    args.args = NULL;
    args_append("");

    while (1) {
        static struct option long_options[] = {
            {"stdout",  no_argument,       0, 'o'},
            {"silent",  no_argument,       0, 'V'},
            {"any",     no_argument,       0, 'a'},
            {"fail",    no_argument,       0, 'f'},
            {"forever", no_argument,       0, 'F'},
            {"change",  no_argument,       0, 'c'},
            {"help",    no_argument,       0, 'h'},
            {"version", no_argument,       0, 'v'},
            {"diff",    no_argument,       0, 'd'},
            {"service", required_argument, 0, 'S'},
            {"status",  required_argument, 0, 's'},
            {"exec",    required_argument, 0, 'e'},
            {"interval",    required_argument, 0, 'i'},
            {"timeout",     required_argument, 0, 'T'},
            {"cmd-timeout", required_argument, 0, 't'},
            {"retry",       required_argument, 0, 'r'},
            {"no-stderr",   no_argument,       0, 'E'},
            {"watch", no_argument, 0, 'w'},
            {"name",  required_argument, 0, 'n'},
            {"json",  no_argument,       0, 'j'},
            {"lap",   no_argument,       0, 'l'},
            {"autocompletions", no_argument, 0, 0},
            {"autocomplete-fish", no_argument, 0, 0},
            {"autocomplete-bash", no_argument, 0, 0},
            {"autocomplete-zsh", no_argument, 0, 0},
            {0, 0, 0, 0}
          };

        int option_index = 0;
        getopt = getopt_long(argc, argv, "oVafFchdvS:s:e:i:T:t:r:Ewn:jl", long_options, &option_index);

        if (getopt == -1)
          break;

        if (getopt != 'S') {
          for (int i = 0; long_options[i].name; i++) {
            if (long_options[i].val != getopt) continue;
            args_append("--");
            args_append(long_options[i].name);
            if (long_options[i].has_arg) {
              args_append(" ");
              args_append_quoted(optarg);
            }
            args_append(" ");
            break;
          }
        }

        switch (getopt) {
          case 0:
            if (strcmp(long_options[option_index].name, "autocompletions") == 0) {
              install_autocompletions();
              exit(0);
            } else if (strcmp(long_options[option_index].name, "autocomplete-fish") == 0) {
              print_autocomplete_fish();
              exit(0);
            } else if (strcmp(long_options[option_index].name, "autocomplete-bash") == 0) {
              print_autocomplete_bash();
              exit(0);
            } else if (strcmp(long_options[option_index].name, "autocomplete-zsh") == 0) {
              print_autocomplete_zsh();
              exit(0);
            }
            break;

          case 'V': args.silent = 1; break;
          case 'o': args.stdout = 1; break;
          case 'e': args.exec=optarg; break;
          case 's': args.expectedStatus=atoi(optarg); break;
          case 'f': args.fail = 1; break;
          case 'a': args.any = 1; break;
          case 'F': args.forever = 1; break;
          case 'c': args.change = 1; break;
          case 'S': args.service = optarg; break;
          case 'i': args.interval = (int)(atof(optarg) * 1000); break;
          case 'T': args.timeout = (int)(atof(optarg) * 1000); break;
          case 't': args.cmd_timeout = atoi(optarg); break;
          case 'r': args.retry = atoi(optarg); break;
          case 'd': args.diff = 1; break;
          case 'v': printf("2.8.0\n"); exit(0); break;
          case 'h': case '?': help(); break;
          case 1:
            if (strcmp(long_options[option_index].name, "autocomplete-fish") == 0) {
              print_autocomplete_fish();
              exit(0);
            }
            break;
          case 2:
            if (strcmp(long_options[option_index].name, "autocomplete-bash") == 0) {
              print_autocomplete_bash();
              exit(0);
            }
            break;
          case 3:
            if (strcmp(long_options[option_index].name, "autocomplete-zsh") == 0) {
              print_autocomplete_zsh();
              exit(0);
            }
            break;
          case 'E': args.no_stderr = 1; break;
          case 'w':
            args.fail = 1;
            args.silent = 1;
            args.stdout = 1;
            args.diff = 1;
            args.no_stderr = 1;
            break;
          case 'n': names[names_count++] = optarg; break;
          case 'j': args.json = 1; break;
          case 'l': args.lap = 1; break;
        }
      }

    if (!args.exec && args.daemonize)
      printf("NOTICE: --daemon is kinda meaningless without --exec 'command'");

    c[0].command = "";

    while (optind < argc) {
      if (looks_like_bare_url(argv[optind])) {
        fprintf(stderr,
          "await: '%s' looks like a URL, not a command.\n"
          "       await runs shell commands, not URLs directly. Try:\n"
          "         await 'curl -sf %s'\n",
          argv[optind], argv[optind]);
      }
      args_append(" ");
      args_append_quoted(argv[optind]);
      c[++args.nCommands].command = argv[optind];
      c[args.nCommands].name = (args.nCommands <= names_count && names[args.nCommands-1]) ? names[args.nCommands-1] : argv[optind];
      optind++;
    }

    if (args.nCommands == 0) help();
}


int service() {
  FILE * fp;
  const char *home;
  if ((home = getenv("HOME")) == NULL)
      home = getpwuid(getuid())->pw_dir;

  char* service = replace("NAME", args.service, "NAME.service");
  char* f = replace("SERVICE", service, replace("HOME", home, "HOME/.config/systemd/user/SERVICE"));
  char cwd[PATH_MAX];
  getcwd(cwd, sizeof(cwd));
  char binary[PATH_MAX];
  ssize_t len = readlink("/proc/self/exe", binary, sizeof(binary) - 1);
  if (len < 0) {
    fprintf(stderr, "await: --service needs /proc/self/exe (Linux with systemd)\n");
    return 1;
  }
  binary[len] = '\0';

  // mkdir -p without a shell, so any HOME works
  char *dir = replace("HOME", home, "HOME/.config/systemd/user");
  for (char *p = dir + 1; *p; p++) {
    if (*p != '/') continue;
    *p = '\0';
    mkdir(dir, 0755);
    *p = '/';
  }
  mkdir(dir, 0755);
  fp = fopen(f, "w");
  if (!fp) {
    fprintf(stderr, "await: cannot write %s: %s\n", f, strerror(errno));
    return 1;
  }
  char *quoted_binary = systemd_quote(binary);
  char *exec_start = malloc(strlen(quoted_binary) + strlen(args.args) + 2);
  sprintf(exec_start, "%s %s", quoted_binary, args.args);
  fprintf(fp,
    "[Unit]\n"\
    "Description=await %s\n"\
    "After=network-online.target\n"\
    "Wants=network-online.target\n"\
    "StartLimitIntervalSec=0\n"\
    "[Service]\n"\
    "WorkingDirectory=%s\n"\
    "ExecStart=%s\n"\
    "Restart=always\n"\
    "[Install]\n"\
    "WantedBy=default.target\n"
   , args.args, replace("%", "%%", cwd), exec_start);
  fclose(fp);

  system(replace("SERVICE", service, "systemctl --user daemon-reload; systemctl cat --user SERVICE; systemctl enable --user SERVICE; systemctl restart --user SERVICE; journalctl --user --follow --unit SERVICE"));
  return 0;
}

void *shell(void * arg) {
  COMMAND *c = (COMMAND*)arg;
  pthread_mutex_lock(&c->lock);
  c->outCap = CHUNK_SIZE;
  c->out = malloc(c->outCap);
  strcpy(c->out, "");
  c->previousOut = malloc(c->outCap);
  c->previousOut[0] = '\0';
  c->diffOut = NULL;
  pthread_mutex_unlock(&c->lock);

  char buf[BUF_SIZE];
  wait_for_dependencies(c);
  while (1) {
    pthread_mutex_lock(&c->lock);
    c->outPos = 0;
    strcpy(c->out, "");
    pthread_mutex_unlock(&c->lock);

    // out of fds or processes (many commands): try again shortly
    int pipefd[2];
    if (pipe(pipefd) != 0) {
      msleep(50);
      continue;
    }
    // don't leak this pipe into commands other threads fork: the reader
    // only sees EOF once every copy of the write end is closed
    fcntl(pipefd[0], F_SETFD, FD_CLOEXEC);
    fcntl(pipefd[1], F_SETFD, FD_CLOEXEC);

    // built before starting the child: malloc after fork() in a threaded process can deadlock
    char *cmd = replace_placeholders(c->command);
    char **env = command_env();
    c->start_time = current_time_ms();
    // posix_spawn doesn't copy our address space like fork() does; with
    // many commands (many threads, many buffers) fork dominated the runtime
    pid_t child_pid;
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO);
    if (args.no_stderr)
      posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    // --cmd-timeout kills the command's whole process group: `sh -c` and
    // everything it started, not just the shell
    if (args.cmd_timeout > 0) {
      posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
      posix_spawnattr_setpgroup(&attr, 0);
    }
    char *argv[] = {"sh", "-c", cmd, NULL};
    if (posix_spawn(&child_pid, "/bin/sh", &actions, &attr, argv, env) != 0) child_pid = -1;
    posix_spawnattr_destroy(&attr);
    posix_spawn_file_actions_destroy(&actions);
    free(cmd);
    free_command_env(env);
    if (child_pid < 0) {
      close(pipefd[0]);
      close(pipefd[1]);
      msleep(50);
      continue;
    }
    {
      // Parent process
      close(pipefd[1]); // Close write end
      c->pid = child_pid;

    long deadline = args.cmd_timeout > 0 ? c->start_time + args.cmd_timeout * 1000L : 0;
    int timed_out = 0;
    while (1) {
      if (deadline) {
        long left = deadline - current_time_ms();
        struct pollfd pfd = {pipefd[0], POLLIN, 0};
        if (left <= 0 || poll(&pfd, 1, (int)left) == 0) {
          kill(-child_pid, SIGKILL);
          timed_out = 1;
          break;
        }
      }
      ssize_t n = read(pipefd[0], buf, sizeof(buf) - 1);
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) break;
      pthread_mutex_lock(&c->lock);
      if (c->outPos + n + 1 > c->outCap) {
        c->outCap = (c->outPos + n + 1) * 2;
        c->out = realloc(c->out, c->outCap);
        c->previousOut = realloc(c->previousOut, c->outCap);
      }
      memcpy(c->out + c->outPos, buf, n);
      c->outPos += n;
      c->out[c->outPos] = '\0';
      pthread_mutex_unlock(&c->lock);
    }

    // one store, so the display never sees an out-of-range frame
    int frame = c->spinner;
    c->spinner = (frame <= 0 ? (int)(sizeof(spinner)/sizeof(spinner[0])) : frame) - 1;
    
    close(pipefd[0]);
    int status;
    waitpid(c->pid, &status, 0);
    // 124 like timeout(1)
    c->status = timed_out ? 124 : WIFSIGNALED(status) ? 128 + WTERMSIG(status) : WEXITSTATUS(status);
    if (c->status == 127 && !c->warned127) {
      c->warned127 = 1;
      fprintf(stderr, "\nawait: '%s' exited with 127 (command not found).\n"
                       "       Check for a typo, or that it's installed and on PATH.\n",
                       c->command);
    }
    c->prev_duration_ms = c->last_duration_ms;
    c->last_duration_ms = current_time_ms() - c->start_time;
    }

    int changed = 0;
    pthread_mutex_lock(&c->lock);
    if (c->runs > 0) {
      c->change = changed = strcmp(c->previousOut,c->out) != 0;
      
      // Compute differences if diff mode is enabled
      if (args.diff && c->change) {
        if (c->diffOut) free(c->diffOut);
        c->diffOut = highlight_differences(c->previousOut, c->out);
      }
    }

    strcpy(c->previousOut, c->out);
    c->runs++;
    // publish the change only once previousOut holds the new output
    if (changed) c->changes++;
    pthread_mutex_unlock(&c->lock);

    if (args.daemonize) syslog(LOG_NOTICE, "%d %s", c->status, c->command);
    if (stop) {
      break;
    }
    msleep(args.interval);
  }
  return NULL;
}


int main(int argc, char *argv[]) {
  // Ensure the program is in the foreground and can catch SIGINT when run from a bash script
  if (tcgetpgrp(STDIN_FILENO) == getpgrp()) {
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
  }

  // struct sigaction sa;
  // sa.sa_handler = handle_sigint;
  // sigemptyset(&sa.sa_mask);
  // sa.sa_flags = 0;
  // sigaction(SIGINT, &sa, NULL);

  parse_args(argc, argv);

  // every running command holds a pipe; macOS defaults to 256 open files
  struct rlimit nofile;
  if (getrlimit(RLIMIT_NOFILE, &nofile) == 0 && nofile.rlim_cur < nofile.rlim_max) {
    nofile.rlim_cur = nofile.rlim_max == RLIM_INFINITY || nofile.rlim_max > 10240 ? 10240 : nofile.rlim_max;
    setrlimit(RLIMIT_NOFILE, &nofile);
  }
  if (args.service) return service();

  // Ensure the program does not ignore signals when running in a script
  signal(SIGINT, SIG_DFL);
  if (args.daemonize) daemonize();

  FILE *fp;

  for(int i = 0; i <= args.nCommands; i++) {
    c[i].status = -1;
    pthread_mutex_init(&c[i].lock, NULL);
  }
  for(int i = 0; i <= args.nCommands; i++) pthread_create(&c[i].thread, NULL, shell, &c[i]);

  int not_done = 0;
    // TODO: make a clear screen option
    // fprintf(stdout, "\033[2J\033[H");
    // fprintf(stderr, "\033[2J\033[H");
    // fflush(stdout);
    // fflush(stderr);

  static int first_output = 1;
  static char *last_display = NULL;
  static char *last_silent_output = NULL;

  // Start time for --timeout and --json elapsed_ms
  args.start_time = current_time_ms();

  while (1) {
    not_done = 0;
    
    if (!args.silent) {
      // Clear previous output by going to beginning of last display
      if (last_display) {
        int lines = 0;
        for (char *p = last_display; *p; p++) {
          if (*p == '\n') lines++;
        }
        if (lines > 0) {
          fprintf(stderr, "\033[%dA\033[J", lines);
        } else {
          fprintf(stderr, "\r\033[K");
        }
        free(last_display);
      }
      
      // Build the entire display string first
      char *display = strdup("");
      size_t display_len = 0;
      
      for(int i = 1; i <= args.nCommands; i++) {
        int color = c[i].status == -1 ? 7 : c[i].status == args.expectedStatus ? 2 : 1;
        
        // Add status line
        if (args.lap && c[i].last_duration_ms > 0) {
          const char *time_color = "\033[2m";
          if (c[i].prev_duration_ms > 0) {
            if (c[i].last_duration_ms < c[i].prev_duration_ms) time_color = "\033[32m";
            else if (c[i].last_duration_ms > c[i].prev_duration_ms) time_color = "\033[31m";
          }
          sappendf(&display, &display_len, "%s%.2fs\033[0m \033[0;3%dm%s\033[0m %s\n", time_color, c[i].last_duration_ms / 1000.0, color, spinner[atomic_load(&c[i].spinner)], c[i].name ? c[i].name : c[i].command);
        }
        else if (args.lap)
          sappendf(&display, &display_len, "      \033[0;3%dm%s\033[0m %s\n", color, spinner[atomic_load(&c[i].spinner)], c[i].name ? c[i].name : c[i].command);
        else
          sappendf(&display, &display_len, "\033[0;3%dm%s\033[0m %s\n", color, spinner[atomic_load(&c[i].spinner)], c[i].name ? c[i].name : c[i].command);
        
        // Add output if available, or previous output if command has run before
        if (args.stdout) {
          char *output_to_show = output_snapshot(&c[i]);
          
          if (output_to_show) {
            int len = strlen(output_to_show);
            if (len > 0 && output_to_show[len - 1] == '\n') {
                output_to_show[len - 1] = '\0';
            }
            sappendf(&display, &display_len, "%s\n", output_to_show);
            free(output_to_show);
          }
        }
        
        not_done += not_done_cmd(i);
      }
      
      // Print the entire display at once
      if (!use_color()) strip_colors(display);
      fprintf(stderr, "%s", display);
      fflush(stderr);
      
      // Save this display for next iteration
      last_display = display;
    } else {
      // Silent mode - handle stdout only, similar to non-silent mode with clearing
      if (args.stdout) {
        // Clear previous silent output (but not on first run)
        if (last_silent_output && !first_output) {
          int lines = 0;
          for (char *p = last_silent_output; *p; p++) {
            if (*p == '\n') lines++;
          }
          if (lines > 0) {
            printf("\033[%dA\033[J", lines);
          } else {
            printf("\r\033[K");
          }
          free(last_silent_output);
        } else if (last_silent_output) {
          free(last_silent_output);
        }
        
        // Build silent output display
        char *silent_display = strdup("");
        size_t silent_display_len = 0;
        int has_output = 0;
        
        for(int i = 1; i <= args.nCommands; i++) {
          char *output_to_show = output_snapshot(&c[i]);
          
          if (output_to_show) {
            int len = strlen(output_to_show);
            if (len > 0 && output_to_show[len - 1] == '\n') {
                output_to_show[len - 1] = '\0';
            }
            
            if (has_output) {
              sappendf(&silent_display, &silent_display_len, "\n");
            }
            sappendf(&silent_display, &silent_display_len, "%s", output_to_show);
            has_output = 1;
            free(output_to_show);
          }
          
          not_done += not_done_cmd(i);
        }
        
        // Print the silent display
        if (!use_color()) strip_colors(silent_display);
        if (has_output) {
          if (first_output) {
            printf("%s", silent_display);
            first_output = 0;
          } else {
            printf("\r%s", silent_display);
          }
          fflush(stdout);
          last_silent_output = silent_display;
        } else {
          free(silent_display);
        }
      } else {
        // No stdout mode, just check status
        for(int i = 1; i <= args.nCommands; i++) {
          not_done += not_done_cmd(i);
        }
      }
    }

    // reap an --exec started by an earlier trigger (--forever)
    if (exec_pid > 0 && waitpid(exec_pid, NULL, WNOHANG) != 0) exec_pid = 0;

    // with --forever, a trigger during a running --exec waits for it to finish
    if ((not_done == 0 || args.any && not_done < args.nCommands) && !exec_pid) {
      // act on each change only once
      for (int i = 1; i <= args.nCommands; i++) c[i].seenChanges = c[i].changes;

      int exec_status = 0;
      if (args.exec) {
        exec_pid = start_exec();
        if (!args.forever) exec_status = wait_exec(exec_pid);
        else if (exec_pid < 0) exec_pid = 0;
        // exec output was printed below the display; start redrawing from here
        free(last_display);
        last_display = NULL;
        free(last_silent_output);
        last_silent_output = NULL;
        first_output = 1;
      }

      if (!args.forever) {
        if (!args.silent && isatty(STDERR_FILENO)) fprintf(stderr, "\033[%dB\r", args.nCommands + 1);
        if (args.json) print_json_result(exec_status);
        return exec_status;
      }
    }

    // Check timeout
    if (args.timeout > 0) {
      long elapsed = current_time_ms() - args.start_time;
      if (elapsed >= args.timeout) {
        if (!args.silent) {
          fprintf(stderr, use_color() ? "\n\033[0;31mTimeout reached after %ld ms\033[0m\n" : "\nTimeout reached after %ld ms\n", elapsed);
          for (int i = 1; i <= args.nCommands; i++) {
            if (c[i].status == -1) {
              fprintf(stderr, "  '%s': still running / no completed attempt\n", c[i].command);
            } else if (c[i].status == 127) {
              fprintf(stderr, "  '%s': last exit 127 (command not found)\n", c[i].command);
            } else if (c[i].status == 126) {
              fprintf(stderr, "  '%s': last exit 126 (not executable / permission denied)\n", c[i].command);
            } else {
              fprintf(stderr, "  '%s': last exit %d\n", c[i].command, c[i].status);
            }
          }
        }
        if (args.json) print_json_result(1);
        return 1;
      }
    }

    // Check retry limit
    // --retry counts finished attempts: give up once every command has run that many times
    int rounds = -1;
    for (int i = 1; i <= args.nCommands; i++)
      if (rounds < 0 || c[i].runs < rounds) rounds = c[i].runs;
    if (args.retry > 0 && rounds >= args.retry) {
      if (!args.silent) {
        fprintf(stderr, use_color() ? "\n\033[0;31mGiving up after %d attempts\033[0m\n" : "\nGiving up after %d attempts\n", rounds);
      }
      if (args.json) print_json_result(1);
      return 1;
    }

    msleep(args.interval);
  }

  if (args.daemonize) closelog();
  return 0;
}
