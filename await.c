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
#include <stdint.h>
#include <spawn.h>
#include <poll.h>
#include <sys/utsname.h>
#include <regex.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#define AWAIT_VERSION "2.11.0"
#define AWAIT_RELEASES "https://github.com/slavaGanzin/await/releases"

int run_update(void);

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
  _Atomic int timed_out; // the last run was killed by --cmd-timeout
  int change;
  int warned127;
  _Atomic int pid;       // the command's running shell, 0 when none
  pthread_t thread;
  long start_time;
  _Atomic long last_duration_ms;
  _Atomic long prev_duration_ms;
  _Atomic int runs;      // completed runs
  _Atomic int changes;   // runs whose stdout differed from the previous run
  int seenChanges;       // changes already acted on by the main loop
  _Atomic int streak;    // consecutive successful checks (--times)
  _Atomic int reached;   // completed --times streaks (--change: every Nth change in a row)
  int seenReached;       // completed streaks already acted on by the main loop
  _Atomic int reachedStreak;  // the streak when the last one completed (--json)
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
  int show_stdout;
  int diff;
  char *exec;
  char* service;
  char* args;
  int nCommands;
  int no_stderr;
  int retry;
  int json;
  int lap;
  char *expect;     // --expect: success is stdout matching this regex
  regex_t expect_re;
  int times;  // --times N: successes in a row a command needs (0: not given, i.e. 1)
  int backoff;  // --backoff MAX in ms: cap of the growing pause after failed checks (0: off)
  int notify;
} ARGS;

// long options without a short letter: one enum, so their getopt values
// never collide (--service replays options by looking them up by value)
enum { OPT_TIMES = 256, OPT_BACKOFF, OPT_NOTIFY };

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

// Options as the shell completions offer them. Descriptions go inside single
// quotes (fish, zsh) and zsh [...], so no ' [ ] in them. arg: 1 takes a
// value, 2 takes a command.
static const struct { const char *name; char letter, arg; const char *desc; } completions[] = {
  {"help", 0, 0, "Print this help"},
  {"version", 'v', 0, "Print the version of await"},
  {"update", 0, 0, "Update await to the latest release"},
  {"stdout", 'o', 0, "Print stdout of commands"},
  {"no-stderr", 'E', 0, "Suppress stderr of commands"},
  {"silent", 'V', 0, "Do not print spinners and commands"},
  {"watch", 'w', 0, "Same as -fVodE (fail, silent, stdout, diff, no-stderr)"},
  {"fail", 'f', 0, "Wait for commands to fail"},
  {"status", 's', 1, "Expected exit status (default: 0)"},
  {"any", 'a', 0, "Done when any command returns the expected status"},
  {"change", 'c', 0, "Wait for stdout to change, ignoring exit status"},
  {"diff", 'd', 0, "Highlight changes between runs"},
  {"exec", 'e', 2, "Run a shell command on success"},
  {"interval", 'i', 1, "Seconds between rounds of commands (default: 0.2)"},
  {"timeout", 'T', 1, "Seconds to wait before giving up"},
  {"cmd-timeout", 't', 1, "Seconds per command run before killing it"},
  {"retry", 'r', 1, "Max runs of each command before giving up"},
  {"forever", 'F', 0, "Never exit"},
  {"name", 'n', 1, "Label for the next command"},
  {"json", 'j', 0, "Print results as JSON on exit"},
  {"lap", 'l', 0, "Show last run duration per command"},
  {"expect", 'x', 1, "Succeed when stdout matches this extended regex"},
  {"times", 0, 1, "Successful checks needed in a row (default: 1)"},
  {"backoff", 0, 1, "Double the interval after each failed check, up to MAX seconds"},
  {"notify", 0, 0, "Desktop notification when await finishes"},
  {"service", 'S', 1, "Create a systemd user service (Linux) or launchd agent (macOS)"},
};
#define N_COMPLETIONS (sizeof(completions) / sizeof(completions[0]))

void print_autocomplete_fish() {
  for (size_t i = 0; i < N_COMPLETIONS; i++) {
    printf("complete -c await -l %s", completions[i].name);
    if (completions[i].letter) printf(" -s %c", completions[i].letter);
    printf(" -d '%s'%s\n", completions[i].desc, completions[i].arg ? " -r" : "");
  }
  printf("\n# For command completion\n"
         "complete -c await -f -a '(__fish_complete_command)'\n");
}

void print_autocomplete_bash() {
  printf("_await() {\n"
         "    local cur prev opts\n"
         "    COMPREPLY=()\n"
         "    cur=\"${COMP_WORDS[COMP_CWORD]}\"\n"
         "    prev=\"${COMP_WORDS[COMP_CWORD-1]}\"\n"
         "\n"
         "    opts=\"");
  for (size_t i = 0; i < N_COMPLETIONS; i++)
    printf("%s--%s", i ? " " : "", completions[i].name);
  printf("\"\n"
         "\n"
         "    case \"${prev}\" in\n"
         "        --exec)\n"
         "            COMPREPLY=($(compgen -c -- \"${cur}\"))\n"
         "            return 0\n"
         "            ;;\n"
         "        ");
  const char *sep = "";
  for (size_t i = 0; i < N_COMPLETIONS; i++)
    if (completions[i].arg == 1) { printf("%s--%s", sep, completions[i].name); sep = "|"; }
  printf(")\n"
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
         "  _arguments -s -S");
  for (size_t i = 0; i < N_COMPLETIONS; i++)
    printf(" \\\n    '--%s[%s]%s%s%s'", completions[i].name, completions[i].desc,
           completions[i].arg == 2 ? ":command:_command_names" : completions[i].arg ? ":" : "",
           completions[i].arg == 1 ? completions[i].name : "", completions[i].arg == 1 ? ":" : "");
  printf("\n}\n"
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

_Atomic int stop = 0;

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
  // \0... is never a placeholder: it's an octal escape (printf '\001', echo -e)
  for (size_t j = 1; p[1] != '0' && p[j] >= '0' && p[j] <= '9' && j < 9; j++) {
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

// a waitpid status as a shell would report it
int exit_code(int status) {
  return WIFSIGNALED(status) ? 128 + WTERMSIG(status) : WEXITSTATUS(status);
}

int wait_exec(pid_t pid) {
  if (pid < 0) return 1;
  int status;
  while (waitpid(pid, &status, 0) < 0)
    if (errno != EINTR) return 1;
  return exit_code(status);
}

// is a single check of a command successful (before --times counts them)?
int check_ok(int status, int changed) {
  if (args.change) return changed;
  // --expect: a timed-out run (124) neither matched nor failed to match, so
  // it is not a successful check with or without --fail (it breaks a streak)
  if (args.expect && status == 124) return 0;
  return args.fail ? status != 0 : status == args.expectedStatus;
}

int not_done_cmd(int i) {
  if (args.times) {
    // a completed streak not acted on yet counts even if a later check has
    // broken the streak before the main loop got to see it
    if (c[i].reached != c[i].seenReached) return 0;
    // --change: every Nth change in a row is an event to act on once
    if (args.change) return 1;
    return c[i].streak < args.times;
  }
  if (args.change) return c[i].changes == c[i].seenChanges;
  // --expect: a timed-out run (124) neither matched nor failed to match
  if (args.expect && c[i].status == 124) return 1;
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

// what a command's last exit status means, for messages ("" when nothing special)
const char *status_note(COMMAND *cmd) {
  if (cmd->timed_out) return " (--cmd-timeout)";
  if (cmd->status == 127) return " (command not found)";
  if (cmd->status == 126) return " (not executable / permission denied)";
  return "";
}

long elapsed_ms(void) {
  return current_time_ms() - args.start_time;
}

void print_json_result(int exit_code) {
  printf("{\"success\":%s,\"elapsed_ms\":%ld,\"commands\":[",
    exit_code == 0 ? "true" : "false", elapsed_ms());
  for (int i = 1; i <= args.nCommands; i++) {
    if (i > 1) printf(",");
    printf("{\"name\":");
    print_json_string(c[i].name);
    printf(",\"command\":");
    print_json_string(c[i].command);
    printf(",\"status\":%d,", c[i].status);
    if (args.times) {
      // a command whose completed streak ended the wait reports that streak,
      // not whatever its checks since (the thread keeps running) made of it
      int streak = !args.forever && c[i].reached > 0 ? c[i].reachedStreak : c[i].streak;
      printf("\"streak\":%d,\"times\":%d,", streak, args.times);
    }
    printf("\"output\":");
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
  "# wait until the service reports it is up, whatever curl exits with\n"
  "  await 'curl -s localhost:8080/health' --expect '\"status\": *\"up\"'\n\n"
  "# wait for a line in the log\n"
  "  await 'tail -n 20 app.log' --expect 'Server started'\n\n"
  "# waiting google (or your internet connection) to fail\n"
  "  await 'curl google.com' --fail\n\n"
  "# waiting only google to fail (https://ec.haxx.se/usingcurl/usingcurl-returns)\n"
  "  await 'curl google.com' --status 7\n\n"
  "# lazy version\n"
  "  await 'ls /tmp/redis.sock'; redis-cli -s /tmp/redis.sock\n\n"
  "# daily checking if I am on french reviera. Just in case\n"
  "  await 'curl https://ipapi.co/json 2>/dev/null | jq .city | grep Nice' --interval 86400\n\n"
  "# don't trust a flapping service: wait for 3 healthy checks in a row\n"
  "  await 'curl -sf localhost:8080/health' --times 3 --interval 1\n\n"
  "# poll a flaky API politely: 0.2s, 0.4s, 0.8s ... up to a minute between failed checks\n"
  "  await 'curl -sf https://api.example.com' --backoff 60\n\n"
  "# get pinged once per outage: 3 failed checks in a row (without --times, every failed check)\n"
  "  await 'curl -sf https://myapp.com' --fail --times 3 --forever --exec 'ntfy send \"site is down\"'\n\n"
  "# ...as a systemd/launchd daemon that survives reboots\n"
  "  await 'curl -sf https://myapp.com' --fail --times 3 --forever --exec 'ntfy send \"site is down\"' --service site-monitor\n\n"
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
  "  --expect -x\t\t#succeed when stdout matches this POSIX extended regex (^ and $ match at each line; exit status is ignored)\n"
  "  --times\t\t#a command is done only after N successful checks in a row (failures with --fail); a miss starts over [default: 1]\n"
  "  --backoff\t\t#after each failed check double the command's interval (±10% jitter), up to MAX seconds; a success resets it\n"
  "  --notify\t\t#desktop notification when await finishes (and on each --exec with --forever), even with --silent\n"
  "  --service -S\t\t#create systemd user service (Linux) or launchd agent (macOS) with same parameters and activate it\n"
  "  --version -v\t\t#print the version of await\n"
  "  --update\t\t#update await to the latest release (checksum-verified; the old binary is kept as <path>.old)\n"

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
  "# with --times and --forever, --exec runs once each time a streak reaches N, not on every check after\n"
  "# (--change --times N: at every Nth change in a row)\n"
  "# under --expect, --times and --backoff take a match as a successful check (a non-match with --fail);\n"
  "# a run killed by --cmd-timeout is neither, so it starts a --times streak over\n"
  "# --backoff spaces out the runs of each command (--retry still counts runs); --timeout still ends the wait on time\n"
  "# set NO_COLOR=1 to disable colors\n"
  "# --notify tries, in order: the terminal's own notifications (iTerm2, WezTerm, ghostty, kitty, foot, Windows Terminal,\n"
  "# VS Code; also through tmux and ssh), then terminal-notifier/osascript (macOS), notify-send/gdbus/kdialog (Linux),\n"
  "# a PowerShell toast (Windows), and finally a terminal bell; over ssh only the terminal is used\n"
  "# in an interactive terminal, await checks for a newer release in the background (at most daily)\n"
  "# and mentions it on stderr; set AWAIT_NO_UPDATE_CHECK=1 to turn this off,\n"
  "# or AWAIT_AUTO_UPDATE=1 to have that check run --update for you (a failure is reported on the next run)\n"

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

// the command line as given (getopt_long permutes argv) and every --service
// value, so the launchd agent can replay the exact arguments minus --service
static char **orig_argv;
static int orig_argc;
static char **service_optargs;
static int service_optargs_n;

void parse_args(int argc, char *argv[]) {
    int getopt;
    orig_argc = argc;
    orig_argv = calloc(argc + 1, sizeof(char *));
    service_optargs = calloc(argc + 1, sizeof(char *));
    if (!orig_argv || !service_optargs) {
      perror("await");
      exit(1);
    }
    memcpy(orig_argv, argv, argc * sizeof(char *));
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
            {"expect", required_argument, 0, 'x'},
            {"times", required_argument, 0, OPT_TIMES},
            {"backoff", required_argument, 0, OPT_BACKOFF},
            {"notify", no_argument,      0, OPT_NOTIFY},
            {"update", no_argument, 0, 0},
            {"autocompletions", no_argument, 0, 0},
            {"autocomplete-fish", no_argument, 0, 0},
            {"autocomplete-bash", no_argument, 0, 0},
            {"autocomplete-zsh", no_argument, 0, 0},
            {0, 0, 0, 0}
          };

        int option_index = 0;
        getopt = getopt_long(argc, argv, "oVafFchdvS:s:e:i:T:t:r:Ewn:jlx:", long_options, &option_index);

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
            if (strcmp(long_options[option_index].name, "update") == 0) {
              exit(run_update());
            } else if (strcmp(long_options[option_index].name, "autocompletions") == 0) {
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
          case 'o': args.show_stdout = 1; break;
          case 'e': args.exec=optarg; break;
          case 's': args.expectedStatus=atoi(optarg); break;
          case 'f': args.fail = 1; break;
          case 'a': args.any = 1; break;
          case 'F': args.forever = 1; break;
          case 'c': args.change = 1; break;
          case 'S': args.service = optarg; service_optargs[service_optargs_n++] = optarg; break;
          case 'i': args.interval = (int)(atof(optarg) * 1000); break;
          case 'T': args.timeout = (int)(atof(optarg) * 1000); break;
          case 't': args.cmd_timeout = atoi(optarg); break;
          case 'r': args.retry = atoi(optarg); break;
          case 'd': args.diff = 1; break;
          case 'v': printf("%s\n", AWAIT_VERSION); exit(0); break;
          case 'h': case '?': help(); break;
          case 'E': args.no_stderr = 1; break;
          case 'w':
            args.fail = 1;
            args.silent = 1;
            args.show_stdout = 1;
            args.diff = 1;
            args.no_stderr = 1;
            break;
          case 'n': names[names_count++] = optarg; break;
          case 'j': args.json = 1; break;
          case 'l': args.lap = 1; break;
          case 'x': args.expect = optarg; break;
          case OPT_TIMES: {
            char *end;
            errno = 0;
            long n = strtol(optarg, &end, 10);
            if (errno || end == optarg || *end || n < 1 || n > INT_MAX) {
              fprintf(stderr, "await: --times needs a positive integer, got '%s'\n", optarg);
              exit(2);
            }
            args.times = (int)n;
            break;
          }
          case OPT_BACKOFF: {
            char *end;
            double max = strtod(optarg, &end);
            if (end == optarg || *end || !(max >= 0.001) || max > INT_MAX / 1000) {
              fprintf(stderr, "await: --backoff needs a positive number of seconds, got '%s'\n", optarg);
              exit(2);
            }
            args.backoff = (int)(max * 1000);
            break;
          }
          case OPT_NOTIFY: args.notify = 1; break;
        }
      }

    if (args.expect) {
      int err = regcomp(&args.expect_re, args.expect, REG_EXTENDED | REG_NOSUB | REG_NEWLINE);
      if (err) {
        char msg[256];
        regerror(err, &args.expect_re, msg, sizeof(msg));
        fprintf(stderr, "await: invalid --expect regex '%s': %s\n", args.expect, msg);
        exit(2);
      }
      // a run's status is 0 when its stdout matches, so that is what we wait for
      args.expectedStatus = 0;
    }

    // checked once all options are in: --interval may come after --backoff
    if (args.backoff && args.backoff < args.interval) {
      fprintf(stderr, "await: --backoff (%gs) must be at least --interval (%gs)\n",
              args.backoff / 1000.0, args.interval / 1000.0);
      exit(2);
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


static int self_path(char *out, size_t size);

// mkdir -p without a shell, so any HOME works
static void mkdir_p(const char *path) {
  char *dir = strdup(path);
  if (!dir) return;
  for (char *p = dir + 1; *p; p++) {
    if (*p != '/') continue;
    *p = '\0';
    mkdir(dir, 0755);
    *p = '/';
  }
  mkdir(dir, 0755);
  free(dir);
}

// the name ends up in a file name and a systemd unit name or launchd label
static int valid_service_name(const char *name) {
  if (!*name) return 0;
  for (const char *p = name; *p; p++) {
    if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9')
        || *p == '.' || *p == '_' || *p == '-')
      continue;
#ifndef __APPLE__
    if (*p == ':') continue; // valid in systemd unit names, kept for compatibility
#endif
    return 0;
  }
  return strcmp(name, ".") && strcmp(name, "..");
}

static int service_no_activate(void) {
  const char *v = getenv("AWAIT_SERVICE_NO_ACTIVATE");
  return v && !strcmp(v, "1");
}

#ifdef __APPLE__
// the original arguments (without argv[0]) minus every --service/-S and its
// value, found by the optarg pointers getopt handed us; NULL-terminated
static char **service_argv(void) {
  char **out = calloc(orig_argc + 1, sizeof(char *));
  char *drop = calloc(orig_argc + 1, 1);
  char **cut = calloc(orig_argc + 1, sizeof(char *));
  if (!out || !drop || !cut) {
    perror("await");
    exit(1);
  }
  for (int i = 0; i < service_optargs_n; i++) {
    uintptr_t o = (uintptr_t)service_optargs[i];
    for (int j = 1; j < orig_argc; j++) {
      const char *a = orig_argv[j];
      uintptr_t start = (uintptr_t)a;
      size_t len = strlen(a);
      if (o == start) {
        // separate value: the option is the element right before it
        drop[j] = 1;
        const char *opt = orig_argv[j - 1];
        size_t olen = strlen(opt);
        if (opt[1] == '-' || olen <= 2) drop[j - 1] = 1;  // --service NAME, -S NAME
        else if (!cut[j - 1]) cut[j - 1] = strndup(opt, olen - 1); // -fS NAME -> -f
        break;
      }
      if (o > start && o <= start + len) {
        // inline value: --service=NAME, -SNAME or -fSNAME
        size_t keep = (size_t)(o - start) - 1;
        if (a[1] == '-' || keep <= 1) drop[j] = 1;
        else cut[j] = strndup(a, keep);
        break;
      }
    }
  }
  int n = 0;
  for (int j = 1; j < orig_argc; j++) {
    if (drop[j]) continue;
    out[n++] = cut[j] ? cut[j] : orig_argv[j];
  }
  free(drop);
  free(cut);
  return out;
}

static void plist_string(FILE *fp, const char *indent, const char *s) {
  fprintf(fp, "%s<string>", indent);
  for (; *s; s++) {
    switch (*s) {
      case '&': fputs("&amp;", fp); break;
      case '<': fputs("&lt;", fp); break;
      case '>': fputs("&gt;", fp); break;
      case '"': fputs("&quot;", fp); break;
      case '\'': fputs("&apos;", fp); break;
      case '\r': fputs("&#13;", fp); break; // a literal CR would be read back as \n
      default: fputc(*s, fp);
    }
  }
  fputs("</string>\n", fp);
}

// run argv without a shell and return its exit status; quiet discards its output
static int run_argv(char *const argv[], int quiet) {
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  if (quiet) {
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
  }
  pid_t pid;
  int rc = posix_spawnp(&pid, argv[0], &actions, NULL, argv, environ);
  posix_spawn_file_actions_destroy(&actions);
  if (rc != 0) return -1;
  int status;
  while (waitpid(pid, &status, 0) < 0)
    if (errno != EINTR) return -1;
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

// --service on macOS: a launchd agent in ~/Library/LaunchAgents
static int service(const char *home, const char *binary, const char *cwd) {
  size_t n = strlen(home) + strlen(args.service) + 64;
  char *label = malloc(n), *agents = malloc(n), *plist = malloc(n), *logs = malloc(n), *log = malloc(n);
  if (!label || !agents || !plist || !logs || !log) {
    perror("await");
    return 1;
  }
  snprintf(label, n, "await.%s", args.service);
  snprintf(agents, n, "%s/Library/LaunchAgents", home);
  snprintf(plist, n, "%s/%s.plist", agents, label);
  snprintf(logs, n, "%s/Library/Logs", home);
  snprintf(log, n, "%s/await-%s.log", logs, args.service);
  mkdir_p(agents);
  mkdir_p(logs);

  FILE *fp = fopen(plist, "w");
  if (!fp) {
    fprintf(stderr, "await: cannot write %s: %s\n", plist, strerror(errno));
    return 1;
  }
  fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\">\n"
        "<dict>\n"
        "  <key>Label</key>\n", fp);
  plist_string(fp, "  ", label);
  fputs("  <key>ProgramArguments</key>\n  <array>\n", fp);
  plist_string(fp, "    ", binary);
  char **av = service_argv();
  for (char **a = av; *a; a++) plist_string(fp, "    ", *a);
  fputs("  </array>\n  <key>WorkingDirectory</key>\n", fp);
  plist_string(fp, "  ", cwd);
  // launchd's default PATH is minimal; keep the one the commands were tried with
  const char *path = getenv("PATH");
  if (path) {
    fputs("  <key>EnvironmentVariables</key>\n  <dict>\n    <key>PATH</key>\n", fp);
    plist_string(fp, "    ", path);
    fputs("  </dict>\n", fp);
  }
  // like Restart=always in the systemd unit: start at login, restart whenever it exits
  fputs("  <key>RunAtLoad</key>\n  <true/>\n"
        "  <key>KeepAlive</key>\n  <true/>\n"
        "  <key>StandardOutPath</key>\n", fp);
  plist_string(fp, "  ", log);
  fputs("  <key>StandardErrorPath</key>\n", fp);
  plist_string(fp, "  ", log);
  fputs("</dict>\n</plist>\n", fp);
  if (fclose(fp) != 0) {
    fprintf(stderr, "await: cannot write %s: %s\n", plist, strerror(errno));
    return 1;
  }
  printf("await: wrote %s\n", plist);
  if (service_no_activate()) return 0;

  char domain[64];
  snprintf(domain, sizeof(domain), "gui/%d", (int)getuid());
  char *target = malloc(strlen(domain) + strlen(label) + 2);
  if (!target) {
    perror("await");
    return 1;
  }
  sprintf(target, "%s/%s", domain, label);
  char *bootout[] = {"launchctl", "bootout", target, NULL};
  run_argv(bootout, 1); // fails when it is not loaded yet, which is fine
  char *bootstrap[] = {"launchctl", "bootstrap", domain, plist, NULL};
  if (run_argv(bootstrap, 0) != 0) {
    char *load[] = {"launchctl", "load", "-w", plist, NULL};
    if (run_argv(load, 0) != 0) {
      fprintf(stderr, "await: launchctl could not load %s\n", plist);
      return 1;
    }
  }
  printf("await: started %s\n"
         "  logs: tail -f '%s'\n"
         "  stop: launchctl bootout gui/$UID/%s\n",
         label, log, label);
  return 0;
}
#else
// --service on Linux: a systemd user unit in ~/.config/systemd/user
static int service(const char *home, const char *binary, const char *cwd) {
  FILE * fp;
  char* service = replace("NAME", args.service, "NAME.service");
  char* f = replace("SERVICE", service, replace("HOME", home, "HOME/.config/systemd/user/SERVICE"));

  mkdir_p(replace("HOME", home, "HOME/.config/systemd/user"));
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

  if (service_no_activate()) {
    printf("await: wrote %s\n", f);
    return 0;
  }
  system(replace("SERVICE", service, "systemctl --user daemon-reload; systemctl cat --user SERVICE; systemctl enable --user SERVICE; systemctl restart --user SERVICE; journalctl --user --follow --unit SERVICE"));
  return 0;
}
#endif

// NULL if s can go into the service file, else what is wrong with it: systemd
// ignores unit file lines that are not UTF-8, and a plist (XML 1.0) also cannot
// hold control characters other than tab, newline and carriage return
static const char *service_text_problem(const char *s) {
  const unsigned char *p = (const unsigned char *)s;
  while (*p) {
    unsigned c = *p;
    if (c < 0x80) {
#ifdef __APPLE__
      if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') return "a control character";
#endif
      p++;
      continue;
    }
    int n;
    unsigned cp;
    if (c >= 0xC2 && c <= 0xDF) { n = 1; cp = c & 0x1F; }
    else if (c >= 0xE0 && c <= 0xEF) { n = 2; cp = c & 0x0F; }
    else if (c >= 0xF0 && c <= 0xF4) { n = 3; cp = c & 0x07; }
    else return "invalid UTF-8";
    for (int i = 1; i <= n; i++) {
      if ((p[i] & 0xC0) != 0x80) return "invalid UTF-8"; // also stops at the terminating NUL
      cp = (cp << 6) | (p[i] & 0x3F);
    }
    if ((n == 2 && cp < 0x800) || (n == 3 && (cp < 0x10000 || cp > 0x10FFFF))
        || (cp >= 0xD800 && cp <= 0xDFFF))
      return "invalid UTF-8";
#ifdef __APPLE__
    if (cp == 0xFFFE || cp == 0xFFFF) return "a non-character";
#endif
    p += n + 1;
  }
  return NULL;
}

#ifdef __APPLE__
#define SERVICE_FILE "a launchd plist"
#else
#define SERVICE_FILE "a systemd unit"
#endif

static int service_text_ok(const char *what, const char *s) {
  const char *problem = service_text_problem(s);
  if (!problem) return 1;
  fprintf(stderr, "await: --service: %s contains %s, which %s cannot hold; nothing was written\n",
          what, problem, SERVICE_FILE);
  return 0;
}

int service_main() {
  if (!valid_service_name(args.service)) {
    fprintf(stderr, "await: invalid --service name '%s': use only letters, digits, '.', '_' and '-'\n", args.service);
    return 2;
  }
  const char *home;
  if ((home = getenv("HOME")) == NULL)
      home = getpwuid(getuid())->pw_dir;
  char binary[PATH_MAX];
  if (self_path(binary, sizeof(binary)) != 0) {
    fprintf(stderr, "await: --service cannot find the path of this binary\n");
    return 1;
  }
  // the service runs from here; falling back to / would break relative paths
  char cwd[PATH_MAX];
  struct stat named, here;
  if (!getcwd(cwd, sizeof(cwd))) {
    fprintf(stderr, "await: --service cannot use the current directory as the service's working directory: %s\n",
            strerror(errno));
    return 1;
  }
  // getcwd may still name a directory that was deleted (or replaced) since
  if (stat(cwd, &named) != 0 || stat(".", &here) != 0
      || named.st_dev != here.st_dev || named.st_ino != here.st_ino) {
    fprintf(stderr, "await: --service cannot use the current directory as the service's working directory: "
            "%s no longer exists\n", cwd);
    return 1;
  }

  for (int i = 1; i < orig_argc; i++) {
    char what[32];
    snprintf(what, sizeof(what), "argument %d", i);
    if (!service_text_ok(what, orig_argv[i])) return 2;
  }
  if (!service_text_ok("the current directory", cwd) || !service_text_ok("HOME", home)
      || !service_text_ok("the path of this binary", binary))
    return 2;
#ifdef __APPLE__
  const char *path = getenv("PATH");
  if (path && !service_text_ok("PATH", path)) return 2;
#endif
  return service(home, binary, cwd);
}

// does out (len bytes, possibly with NULs) match --expect? regexec reads C
// strings, so match each NUL-separated segment (REG_STARTEND isn't portable)
int expect_matches(const char *out, size_t len) {
  const char *p = out, *end = out + len;
  do {
    if (regexec(&args.expect_re, p, 0, NULL, 0) == 0) return 1;
    p += strlen(p) + 1;
  } while (p < end);
  return 0;
}

static unsigned xorshift32(unsigned *state) {
  unsigned x = *state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return *state = x;
}

// --backoff: the pause before a command's next check. *wait is the pause the
// next failed check gets: --interval after a success, doubling with each
// failure in a row, up to MAX. Failed checks' pauses get +-10% jitter, so
// commands backing off together don't stay in lockstep, and never pass MAX
// nor the --timeout deadline (no point sleeping past the end). Once the
// deadline has passed (await is about to give up), pauses are left alone
// rather than cut to nothing, so the command isn't rerun back to back.
long backoff_pause(long *wait, int ok, unsigned *rng) {
  if (ok) {
    *wait = args.interval;
    return args.interval;
  }
  long base = *wait;
  *wait = base <= 0 ? 1 : base > args.backoff / 2 ? args.backoff : base * 2;
  long pause = base + base * ((long)(xorshift32(rng) % 2001) - 1000) / 10000;
  if (pause > args.backoff) pause = args.backoff;
  if (args.timeout > 0) {
    long left = args.start_time + args.timeout - current_time_ms();
    if (left > 0 && pause > left) pause = left;
  }
  return pause;
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
  int run_status = -1;
  // --backoff state; the jitter's generator is this thread's own
  long backoff_wait = args.interval;
  unsigned rng = (unsigned)time(NULL) ^ ((unsigned)getpid() << 16) ^ (unsigned)((uintptr_t)arg * 2654435761u);
  if (!rng) rng = 1;
  xorshift32(&rng);
  int timed_out = 0;
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

    run_status = -1;
    long deadline = args.cmd_timeout > 0 ? c->start_time + args.cmd_timeout * 1000L : 0;
    timed_out = 0;
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
    c->pid = 0;
    // 124 like timeout(1); published below together with the output
    run_status = timed_out ? 124 : WIFSIGNALED(status) ? 128 + WTERMSIG(status) : WEXITSTATUS(status);
    // --expect: the run succeeds (status 0) when its stdout matches, whatever
    // it exited with, and has status 1 when it doesn't; a timed-out run keeps 124
    int expect_status = -1;
    if (args.expect && !timed_out)
      expect_status = expect_matches(c->out, c->outPos) ? 0 : 1;
    // matching output means the command ran: no "command not found" hint
    if (run_status == 127 && expect_status != 0 && !c->warned127) {
      c->warned127 = 1;
      fprintf(stderr, "\nawait: '%s' exited with 127 (command not found).\n"
                       "       Check for a typo, or that it's installed and on PATH.\n",
                       c->command);
    }
    c->prev_duration_ms = c->last_duration_ms;
    c->last_duration_ms = current_time_ms() - c->start_time;
    if (expect_status >= 0) run_status = expect_status;
    }

    // out/previousOut are only written by this thread, so comparing and
    // diffing them needs no lock; the lock only covers publishing
    int changed = c->runs > 0 && strcmp(c->previousOut, c->out) != 0;
    char *diff = changed && args.diff ? highlight_differences(c->previousOut, c->out) : NULL;

    pthread_mutex_lock(&c->lock);
    c->change = changed;
    if (diff) {
      free(c->diffOut);
      c->diffOut = diff;
    }
    strcpy(c->previousOut, c->out);
    // publish status, run and change only once previousOut holds this run's
    // output, so whoever acts on them (--exec, --json, \1) sees that output
    c->status = run_status;
    c->timed_out = timed_out;
    c->runs++;
    // with --expect only a change to matching output counts (for --times too)
    int counted_change = changed && (!args.expect || run_status == 0);
    if (counted_change) c->changes++;
    if (args.times) {
      // an unsuccessful check starts the streak over
      c->streak = !check_ok(run_status, counted_change) ? 0 : c->streak < INT_MAX ? c->streak + 1 : c->streak;
      // a streak completes at N successes in a row (--change: at every Nth change in a row)
      if (c->streak > 0 && (args.change ? c->streak % args.times == 0 : c->streak == args.times)) {
        c->reachedStreak = c->streak;
        c->reached++;
      }
    }
    pthread_mutex_unlock(&c->lock);

    if (args.daemonize) syslog(LOG_NOTICE, "%d %s", c->status, c->command);
    if (stop) {
      break;
    }
    // with --retry N a command runs at most N times
    if (args.retry > 0 && c->runs >= args.retry) break;
    if (args.backoff) {
      // a successful check is what await waits for (--fail: a failure, --change:
      // a change, --expect: a match), the same test --times counts streaks with
      int ok = check_ok(run_status, counted_change);
      msleep(backoff_pause(&backoff_wait, ok, &rng));
    } else {
      msleep(args.interval);
    }
  }
  return NULL;
}


// compare dotted versions numerically ("2.10.0" > "2.9.0"), ignoring a leading "v"
int version_cmp(const char *a, const char *b) {
  if (*a == 'v') a++;
  if (*b == 'v') b++;
  while (*a || *b) {
    long x = strtol(a, (char **)&a, 10), y = strtol(b, (char **)&b, 10);
    if (x != y) return x < y ? -1 : 1;
    if (*a == '.') a++;
    if (*b == '.') b++;
    if ((*a && (*a < '0' || *a > '9')) || (*b && (*b < '0' || *b > '9'))) break;
  }
  return 0;
}

// Shell helpers shared by the update check and --update. The latest tag comes
// from the redirect of <releases>/latest (-> .../releases/tag/X), not the GitHub
// API, which rate-limits unauthenticated callers to 60 requests an hour per IP.
#define UPDATE_SH_HELPERS \
  "have() { command -v \"$1\" >/dev/null 2>&1; }\n" \
  "fetch() { if have curl; then curl -fsSL --max-time 60 -o \"$2\" \"$1\"; " \
  "elif have wget; then wget -q -T 60 -O \"$2\" \"$1\"; else return 2; fi; }\n" \
  "latest_tag() { if have curl; then loc=$(curl -fsI --max-time 10 \"$1/latest\"); " \
  "elif have wget; then loc=$(wget -q -S --max-redirect=0 -T 10 -O /dev/null \"$1/latest\" 2>&1); fi; " \
  "loc=$(printf '%s\\n' \"$loc\" | tr -d '\\r' | sed -n 's/^ *[Ll]ocation: *//p' | tail -n1); " \
  "tag=${loc##*/tag/}; [ -n \"$loc\" ] && [ \"$tag\" != \"$loc\" ] && printf '%s\\n' \"$tag\"; }\n"

// where releases live; AWAIT_RELEASES_URL points it elsewhere (tests)
static const char *releases_url(void) {
  const char *url = getenv("AWAIT_RELEASES_URL");
  return url && *url ? url : AWAIT_RELEASES;
}

// the release archive built for this machine ("" when there is none);
// AWAIT_UPDATE_TARGET overrides it (tests)
static const char *release_target(void) {
  const char *forced = getenv("AWAIT_UPDATE_TARGET");
  if (forced) return forced;
  struct utsname u;
  if (uname(&u) != 0) return "";
  int arm = !strcmp(u.machine, "arm64") || !strcmp(u.machine, "aarch64");
  int x86 = !strcmp(u.machine, "x86_64") || !strcmp(u.machine, "amd64");
  if (!strcmp(u.sysname, "Darwin")) return arm ? "aarch64-apple-darwin" : x86 ? "x86_64-apple-darwin" : "";
  // Linux gets the static (musl) build: it runs regardless of the distro's libc
  if (!strcmp(u.sysname, "Linux")) return arm ? "aarch64-unknown-linux-musl" : x86 ? "x86_64-unknown-linux-musl" : "";
  // Windows: the MSYS2 build, for MSYS2 (uname: MSYS_NT-...) and Git Bash or
  // MSYS2's MinGW shells (MINGW64_NT-..., MINGW32_NT-...). A plain Cygwin
  // install (CYGWIN_NT-...) lacks msys-2.0.dll, so it gets no prebuilt build.
  if (!strncmp(u.sysname, "MSYS_NT", 7) || !strncmp(u.sysname, "MINGW", 5)) return x86 ? "x86_64-pc-windows-msys" : "";
  return "";
}

// absolute, symlink-free path of this binary
static int self_path(char *out, size_t size) {
#ifdef __APPLE__
  char raw[PATH_MAX];
  uint32_t raw_size = sizeof(raw);
  if (_NSGetExecutablePath(raw, &raw_size) != 0) return -1;
  return realpath(raw, out) ? 0 : -1;
#else
  ssize_t len = readlink("/proc/self/exe", out, size - 1);
  if (len < 0) return -1;
  out[len] = '\0';
  return 0;
#endif
}

// await --update: replace this binary with the latest release, or explain why
// not. Order matters so the worst case is "not updated", never a broken await:
// refuse package-managed installs, check permissions, download and verify the
// checksum, test-run the new binary, keep a backup, then rename atomically
// (a running await keeps its old file; nothing is overwritten in place, which
// would also invalidate the code signature on macOS).
int run_update(void) {
  char self[PATH_MAX];
  if (self_path(self, sizeof(self)) != 0) {
    fprintf(stderr, "await: can't find where this binary is installed\n");
    return 1;
  }
  const char *script = UPDATE_SH_HELPERS
    "self=$1 cur=$2 base=$3 target=$4 force=$5\n"
    "say() { printf 'await: %s\\n' \"$*\" >&2; }\n"
    "case \"$self\" in\n"
    "  /nix/store/*) say \"installed by Nix ($self): update it through Nix\"; exit 3;;\n"
    "  */Cellar/*|/home/linuxbrew/*) say \"installed by Homebrew: run brew upgrade await\"; exit 3;;\n"
    "esac\n"
    "if have pacman && pacman -Qo \"$self\" >/dev/null 2>&1; then say \"installed by pacman/AUR: update it with your AUR helper (e.g. yay -S await)\"; exit 3; fi\n"
    "if have dpkg && dpkg -S \"$self\" >/dev/null 2>&1; then say \"installed by a system package: update it with your package manager\"; exit 3; fi\n"
    "[ -n \"$target\" ] || { say \"no prebuilt await for $(uname -sm); build it from source\"; exit 4; }\n"
    "have curl || have wget || { say 'need curl or wget to update'; exit 5; }\n"
    "tag=$(latest_tag \"$base\") || { say \"couldn't find the latest release at $base/latest\"; exit 5; }\n"
    "new=${tag#v}\n"
    "newer() { awk -v a=\"$1\" -v b=\"$2\" 'BEGIN { n = split(a, x, \".\"); m = split(b, y, \".\"); if (m > n) n = m;"
    " for (i = 1; i <= n; i++) { if (x[i] + 0 > y[i] + 0) exit 0; if (x[i] + 0 < y[i] + 0) exit 1 } exit 1 }'; }\n"
    "[ -n \"$force\" ] || newer \"$new\" \"$cur\" || { say \"already up to date ($cur)\"; exit 0; }\n"
    "dir=$(dirname \"$self\")\n"
    "[ -w \"$dir\" ] || { say \"no permission to replace $self; run: sudo $self --update\"; exit 6; }\n"
    // one update at a time (a manual one and a background one could otherwise
    // both back up and swap); a lock left by a killed update expires after 10m
    "lock=$dir/.await-update.lock\n"
    "if ! mkdir \"$lock\" 2>/dev/null; then\n"
    "  [ -n \"$(find \"$lock\" -maxdepth 0 -mmin +10 2>/dev/null)\" ] && rmdir \"$lock\" 2>/dev/null && mkdir \"$lock\" 2>/dev/null"
    " || { say \"another await --update is running ($lock)\"; exit 12; }\n"
    "fi\n"
    "tmp=\n"
    "trap 'rm -rf \"$tmp\"; rmdir \"$lock\"' EXIT\n"
    "trap 'exit 130' HUP INT TERM\n"
    "tmp=$(mktemp -d \"$dir/.await-update.XXXXXX\") || { say \"can't create a temporary directory in $dir\"; exit 6; }\n"
    "archive=await-$tag-$target.tar.gz\n"
    "say \"downloading $base/download/$tag/$archive\"\n"
    "fetch \"$base/download/$tag/$archive\" \"$tmp/$archive\" || { say \"release $tag has no $target build ($archive)\"; exit 7; }\n"
    "fetch \"$base/download/$tag/SHA256SUMS\" \"$tmp/SHA256SUMS\" || { say \"release $tag publishes no SHA256SUMS; not installing an unverified binary\"; exit 8; }\n"
    "want=$(awk -v f=\"$archive\" '$2 == f || $2 == \"*\" f { print $1 }' \"$tmp/SHA256SUMS\")\n"
    "if have sha256sum; then got=$(sha256sum \"$tmp/$archive\"); elif have shasum; then got=$(shasum -a 256 \"$tmp/$archive\");"
    " else say 'need sha256sum or shasum to verify the download'; exit 8; fi\n"
    "got=${got%% *}\n"
    "[ -n \"$want\" ] && [ \"$want\" = \"$got\" ] || { say \"checksum mismatch for $archive; not installing\"; exit 8; }\n"
    // (the Windows archive holds await.exe)
    "mkdir \"$tmp/x\" && tar -xzf \"$tmp/$archive\" -C \"$tmp/x\" || { say \"couldn't unpack $archive\"; exit 9; }\n"
    "bin=$tmp/x/await; [ -f \"$bin.exe\" ] && bin=$bin.exe\n"
    "[ -f \"$bin\" ] || { say \"couldn't unpack $archive\"; exit 9; }\n"
    "chmod +x \"$bin\"\n"
    "ran=$(\"$bin\" --version 2>/dev/null)\n"
    "[ \"$ran\" = \"$new\" ] || { say \"the downloaded await doesn't run here (--version gave '${ran:-nothing}'); staying on $cur\"; exit 10; }\n"
    // the backup is copied inside our own temp dir and renamed into place:
    // rename replaces whatever is at $self.old (a symlink included) instead of
    // writing through it. No backup, no update.
    "cp -p \"$self\" \"$tmp/old\" || { say \"couldn't back up $self; not updating\"; exit 11; }\n"
    "rm -f \"$self.old\" 2>/dev/null\n"
    "{ [ ! -e \"$self.old\" ] && [ ! -L \"$self.old\" ] && mv -f \"$tmp/old\" \"$self.old\"; }"
    " || { say \"couldn't keep a backup at $self.old; not updating\"; exit 11; }\n"
    "mv -f \"$bin\" \"$self\" || { say \"couldn't replace $self\"; exit 11; }\n"
    // refresh an installed man page (never create one, never write through a
    // symlink); a failure here is only a note, the update has already succeeded
    "m=$dir/../share/man/man1/await.1 mt=\n"
    "if [ -f \"$tmp/x/await.1\" ] && [ -f \"$m\" ] && [ ! -L \"$m\" ]; then\n"
    "  { [ -w \"$m\" ] && mt=$(mktemp \"${m%/*}/.await.1.XXXXXX\") && cp \"$tmp/x/await.1\" \"$mt\" && chmod 644 \"$mt\" && mv -f \"$mt\" \"$m\"; } 2>/dev/null"
    " || { [ -z \"$mt\" ] || rm -f \"$mt\"; say \"note: couldn't update the man page $m\"; }\n"
    "fi\n"
    "say \"updated $cur -> $new ($self; the previous version is kept at $self.old)\"\n";
  fflush(stdout);
  fflush(stderr);
  pid_t pid = fork();
  if (pid == 0) {
    // AWAIT_UPDATE_FORCE (CI only, undocumented): install the latest release even
    // if it isn't newer, to exercise a real update from a fresh build
    const char *force = getenv("AWAIT_UPDATE_FORCE");
    execl("/bin/sh", "sh", "-c", script, "await-update", self, AWAIT_VERSION, releases_url(), release_target(),
          force && *force && strcmp(force, "0") != 0 ? "1" : "", NULL);
    _exit(127);
  }
  if (pid < 0) {
    perror("await: --update");
    return 1;
  }
  int status;
  while (waitpid(pid, &status, 0) < 0)
    if (errno != EINTR) return 1;
  return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

// Update notifier: when a newer release is cached, say so; when the cache is
// older than a day (or missing), refresh it in a detached background process,
// so await itself never waits on the network. Only in interactive sessions,
// and never when AWAIT_NO_UPDATE_CHECK is set. With AWAIT_AUTO_UPDATE=1 the
// background check also runs --update. Must run before any threads.
void update_check() {
  const char *off = getenv("AWAIT_NO_UPDATE_CHECK");
  if ((off && *off) || !isatty(STDERR_FILENO)) return;

  const char *home = getenv("HOME");
  if (!home || !*home) {
    struct passwd *pw = getpwuid(getuid());
    if (!pw) return;
    home = pw->pw_dir;
  }
  const char *xdg = getenv("XDG_CACHE_HOME");
  char dir[PATH_MAX], cache[PATH_MAX];
  if (xdg && *xdg) snprintf(dir, sizeof(dir), "%s/await", xdg);
  else snprintf(dir, sizeof(dir), "%s/.cache/await", home);
  snprintf(cache, sizeof(cache), "%s/latest-version", dir);

  // notify from whatever the cache holds, even if it's due for a refresh
  char latest[64] = "";
  FILE *f = fopen(cache, "r");
  if (f) {
    if (fscanf(f, "%63s", latest) != 1) latest[0] = '\0';
    fclose(f);
  }
  if (*latest && version_cmp(latest, AWAIT_VERSION) > 0 && !args.silent)
    fprintf(stderr, use_color()
      ? "\033[33mawait %s is available (you have %s): run await --update\033[0m\n"
      : "await %s is available (you have %s): run await --update\n",
      latest, AWAIT_VERSION);

  // AWAIT_AUTO_UPDATE=1: the background check also updates this binary
  const char *autoupdate = getenv("AWAIT_AUTO_UPDATE");
  int autoupdating = autoupdate && *autoupdate && strcmp(autoupdate, "0") != 0;
  char self[PATH_MAX] = "", stamp[PATH_MAX], error[PATH_MAX];
  snprintf(stamp, sizeof(stamp), "%s/auto-update", dir);
  snprintf(error, sizeof(error), "%s/update-error", dir);
  if (autoupdating && self_path(self, sizeof(self)) != 0) autoupdating = 0;

  // an automatic update runs out of sight, so say when the last one failed
  char failure[512] = "";
  if (autoupdating && (f = fopen(error, "r"))) {
    if (!fgets(failure, sizeof(failure), f)) failure[0] = '\0';
    fclose(f);
    failure[strcspn(failure, "\n")] = '\0';
  }
  if (*failure && !args.silent)
    fprintf(stderr, use_color()
      ? "\033[33mautomatic update failed: %s (log: %s/update.log)\033[0m\n"
      : "automatic update failed: %s (log: %s/update.log)\n",
      failure, dir);

  struct stat st;
  time_t now = time(NULL);
  int check = !(stat(cache, &st) == 0 && now - st.st_mtime < 24 * 60 * 60);
  // a newer version may already be cached (e.g. AWAIT_AUTO_UPDATE was set after
  // the last check): install it without waiting for the cache to expire, but
  // attempt that at most once a day too
  int install = autoupdating && (check || (*latest && version_cmp(latest, AWAIT_VERSION) > 0
    && !(stat(stamp, &st) == 0 && now - st.st_mtime < 24 * 60 * 60)));
  if (!check && !install) return;
  if (!install) self[0] = '\0';

  // touch first, so a failed or offline check also waits a day before retrying.
  // An automatic update keeps its output in update.log and its failure (last
  // line) in update-error, which the next interactive run shows.
  const char *script = UPDATE_SH_HELPERS
    "mkdir -p \"$1\" || exit\n"
    "if [ -n \"$5\" ]; then\n"
    "  touch \"$2\" || exit\n"
    "  tag=$(latest_tag \"$3\") || exit\n"
    "  printf '%s\\n' \"$tag\" > \"$2.$$\" && mv \"$2.$$\" \"$2\"\n"
    "fi\n"
    "[ -n \"$4\" ] || exit 0\n"
    "touch \"$1/auto-update\"\n"
    "\"$4\" --update > \"$1/update.log\" 2>&1\n"
    "case $? in\n"
    "  0) rm -f \"$1/update-error\";;\n"
    "  12) ;;  # another update was running: not a failure\n"
    "  *) tail -n 1 \"$1/update.log\" | sed 's/^await: //' > \"$1/update-error\";;\n"
    "esac\n";

  fflush(stdout);
  fflush(stderr);
  pid_t pid = fork();
  if (pid == 0) {
    // detach: new session, and let the intermediate child exit so the check
    // is reparented and never becomes a zombie of ours
    setsid();
    if (fork() != 0) _exit(0);
    int devnull = open("/dev/null", O_RDWR);
    dup2(devnull, STDIN_FILENO);
    dup2(devnull, STDOUT_FILENO);
    dup2(devnull, STDERR_FILENO);
    // don't hold on to anything else the caller gave us (e.g. a pipe it waits
    // on for EOF) while the fetch runs
    long max_fd = sysconf(_SC_OPEN_MAX);
    if (max_fd < 0 || max_fd > 65536) max_fd = 65536;
    for (int fd = STDERR_FILENO + 1; fd < max_fd; fd++) close(fd);
    execl("/bin/sh", "sh", "-c", script, "await-update-check", dir, cache, releases_url(), self, check ? "1" : "", NULL);
    _exit(127);
  }
  if (pid > 0) waitpid(pid, NULL, 0);
}

// on exit, don't leave commands that are still running behind: with
// --cmd-timeout each has its own process group, so everything it started
// goes; otherwise its shell is stopped
void stop_running_commands(void) {
  stop = 1;
  for (int i = 0; c && i <= args.nCommands; i++) {
    int pid = c[i].pid;
    if (pid > 0) kill(args.cmd_timeout > 0 ? -pid : pid, SIGTERM);
  }
}

/* --notify: tell the user how the run went, through the first of these that
 * works (stopping at the first success):
 *   1. a notification escape sequence written to the terminal (/dev/tty), when
 *      the terminal is known to support one (term_notifiers below); over SSH
 *      an unrecognised terminal gets OSC 9 followed by a bell anyway
 *   2. over SSH, native notifiers would pop up on the remote machine: stop here
 *   3. macOS: terminal-notifier, else osascript
 *   4. Linux/BSD with a desktop session: notify-send, else gdbus, else kdialog
 *   5. Windows (Cygwin/MSYS2): a PowerShell toast
 *   6. a terminal bell
 * Notifiers run without a shell (posix_spawnp, argv), with stdio on /dev/null;
 * the whole chain gets NOTIFY_BUDGET_MS, and a notifier still running after
 * that is killed (with its process group) and counts as failed. Terminal
 * writes are non-blocking within the same budget (a terminal stopped with
 * Ctrl-S just fails the step). So a broken notifier or terminal never delays
 * exit by more than that or changes the exit code. */
#define NOTIFY_BUDGET_MS 2000
#define NOTIFY_BODY_MAX 200
#define NOTIFY_LABEL_MAX 60

// s made safe to show: control characters (C0, DEL, C1: ESC, BEL, newlines...)
// become spaces (runs of them one space), bidi overrides and invalid UTF-8 are
// dropped, and the result is cut to at most max bytes on a character
// boundary, ending in "…" when cut. New string.
char *notify_sanitize(const char *s, size_t max) {
  const unsigned char *p = (const unsigned char *)(s ? s : "");
  size_t n = strlen((const char *)p);
  char *out = malloc(n + 4), *w = out;
  if (!out) return strdup("");
  while (*p) {
    unsigned cp, len;
    if (*p < 0x80) { cp = *p; len = 1; }
    else if ((*p & 0xE0) == 0xC0) { cp = *p & 0x1F; len = 2; }
    else if ((*p & 0xF0) == 0xE0) { cp = *p & 0x0F; len = 3; }
    else if ((*p & 0xF8) == 0xF0) { cp = *p & 0x07; len = 4; }
    else { p++; continue; }  // stray continuation or invalid lead byte
    unsigned k;
    for (k = 1; k < len; k++) {
      if ((p[k] & 0xC0) != 0x80) break;
      cp = (cp << 6) | (p[k] & 0x3F);
    }
    if (k < len) { p++; continue; }  // truncated sequence: drop the lead byte
    static const unsigned min[] = {0, 0, 0x80, 0x800, 0x10000};
    int invalid = cp < min[len] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF);
    int control = cp < 0x20 || (cp >= 0x7F && cp <= 0x9F) || cp == 0x2028 || cp == 0x2029;
    int bidi = (cp >= 0x202A && cp <= 0x202E) || (cp >= 0x2066 && cp <= 0x2069);
    if (control) {
      if (w > out && w[-1] != ' ') *w++ = ' ';
    } else if (!invalid && !bidi) {
      if (!(cp == ' ' && (w == out || w[-1] == ' '))) {
        memcpy(w, p, len);
        w += len;
      }
    }
    p += len;
  }
  while (w > out && w[-1] == ' ') w--;
  *w = '\0';
  if ((size_t)(w - out) > max && max >= 4) {
    size_t cut = max - 3;  // room for "…"
    while (cut > 0 && ((unsigned char)out[cut] & 0xC0) == 0x80) cut--;
    while (cut > 0 && out[cut - 1] == ' ') cut--;
    memcpy(out + cut, "\xE2\x80\xA6", 4);
  }
  return out;
}

// "12s", "5m", "5m 3s", "1h 2m"
void format_duration(long ms, char *buf, size_t size) {
  long s = ms / 1000;
  if (ms < 10000) snprintf(buf, size, "%.1fs", ms / 1000.0);
  else if (s < 60) snprintf(buf, size, "%lds", s);
  else if (s < 3600) {
    if (s % 60) snprintf(buf, size, "%ldm %lds", s / 60, s % 60);
    else snprintf(buf, size, "%ldm", s / 60);
  } else snprintf(buf, size, "%ldh %ldm", s / 3600, s / 60 % 60);
}

// how command i ended up: "<label> exited 7 (...)", "<label> changed"...
void describe_command(int i, char **s, size_t *len) {
  char *label = notify_sanitize(c[i].name ? c[i].name : c[i].command, NOTIFY_LABEL_MAX);
  int status = c[i].status;
  int not_done = not_done_cmd(i);
  if (args.change) sappendf(s, len, "%s %s", label, not_done ? "didn't change" : "changed");
  else if (status == -1) sappendf(s, len, "%s never finished", label);
  // --expect: the status is 0/1 for match/no match, unless the run timed out
  else if (args.expect && !c[i].timed_out) sappendf(s, len, "%s %s --expect", label, status == 0 ? "matched" : "didn't match");
  else sappendf(s, len, "%s exited %d%s", label, status, status_note(&c[i]));
  // --times: the streak that completed, or how far the current one got
  if (args.times && status != -1) {
    int streak = !not_done && c[i].reached > 0 ? c[i].reachedStreak : c[i].streak;
    sappendf(s, len, " (%d of %d %s in a row)", streak, args.times, args.change ? "changes" : "checks");
  }
  free(label);
}

// how the commands went, e.g. "3/3 commands succeeded" or "curl … exited 7";
// not_done as counted by not_done_cmd. New string.
char *outcome_summary(int not_done) {
  char *s = strdup("");
  size_t len = 0;
  if (args.nCommands == 1) {
    describe_command(1, &s, &len);
    return s;
  }
  char verb[64];
  if (args.change) snprintf(verb, sizeof(verb), "changed");
  else if (args.expect) snprintf(verb, sizeof(verb), "%s --expect", args.fail ? "didn't match" : "matched");
  else if (args.fail) snprintf(verb, sizeof(verb), "failed");
  else if (args.expectedStatus) snprintf(verb, sizeof(verb), "exited %d", args.expectedStatus);
  else snprintf(verb, sizeof(verb), "succeeded");
  if (args.times > 1) {
    size_t n = strlen(verb);
    snprintf(verb + n, sizeof(verb) - n, " %d times in a row", args.times);
  }
  sappendf(&s, &len, "%d/%d commands %s", args.nCommands - not_done, args.nCommands, verb);
  // name the first one that didn't
  for (int i = 1; not_done && i <= args.nCommands; i++) {
    if (!not_done_cmd(i)) continue;
    sappendf(&s, &len, "; ");
    describe_command(i, &s, &len);
    break;
  }
  return s;
}

static int env_set(const char *name) {
  const char *v = getenv(name);
  return v && *v;
}

// Terminals known to show a notification for an escape sequence. The first
// matching row wins; add a terminal by adding a row.
enum { OSC_9, OSC_777, OSC_99 };
enum { MATCH_SET, MATCH_EQUAL, MATCH_PREFIX, MATCH_CONTAINS };
static const struct {
  const char *env, *value;
  int match, osc;
} term_notifiers[] = {
  {"KITTY_WINDOW_ID", NULL,        MATCH_SET,      OSC_99},   // kitty: OSC 99
  {"TERM",            "kitty",     MATCH_CONTAINS, OSC_99},
  {"TERM",            "foot",      MATCH_PREFIX,   OSC_777},  // foot: OSC 777 (title and body)
  {"TERM_PROGRAM",    "ghostty",   MATCH_EQUAL,    OSC_777},  // ghostty: OSC 777 or 9
  {"TERM",            "ghostty",   MATCH_CONTAINS, OSC_777},
  {"TERM_PROGRAM",    "iTerm.app", MATCH_EQUAL,    OSC_9},    // iTerm2: OSC 9
  {"LC_TERMINAL",     "iTerm2",    MATCH_EQUAL,    OSC_9},    // iTerm2, also through ssh/tmux
  {"TERM_PROGRAM",    "WezTerm",   MATCH_EQUAL,    OSC_9},    // WezTerm: OSC 9 or 777
  {"WT_SESSION",      NULL,        MATCH_SET,      OSC_9},    // Windows Terminal
  {"TERM_PROGRAM",    "vscode",    MATCH_EQUAL,    OSC_9},    // VS Code's terminal
};

static int term_notifier(void) {
  for (size_t i = 0; i < sizeof(term_notifiers) / sizeof(term_notifiers[0]); i++) {
    const char *v = getenv(term_notifiers[i].env), *want = term_notifiers[i].value;
    if (!v || !*v) continue;
    switch (term_notifiers[i].match) {
      case MATCH_SET: return term_notifiers[i].osc;
      case MATCH_EQUAL: if (!strcmp(v, want)) return term_notifiers[i].osc; break;
      case MATCH_PREFIX: if (!strncmp(v, want, strlen(want))) return term_notifiers[i].osc; break;
      case MATCH_CONTAINS: if (strstr(v, want)) return term_notifiers[i].osc; break;
    }
  }
  return -1;
}

// the controlling terminal, or AWAIT_NOTIFY_TTY (tests); -1 if none
static int open_tty(void) {
  const char *path = getenv("AWAIT_NOTIFY_TTY");
  // non-blocking: a terminal stopped by flow control (Ctrl-S) or a FIFO
  // without a reader must not hold up exit
  return open(path && *path ? path : "/dev/tty", O_WRONLY | O_APPEND | O_NOCTTY | O_CLOEXEC | O_NONBLOCK);
}

// write all of s to non-blocking fd, waiting for room until deadline; -1 if it can't
static int write_all(int fd, const char *s, size_t n, long deadline) {
  while (n > 0) {
    ssize_t k = write(fd, s, n);
    if (k < 0 && errno == EINTR) continue;
    if (k < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      long left = deadline - current_time_ms();
      if (left <= 0) return -1;
      struct pollfd pfd = {fd, POLLOUT, 0};
      if (poll(&pfd, 1, (int)left) < 0 && errno != EINTR) return -1;
      continue;
    }
    if (k <= 0) return -1;
    s += k;
    n -= k;
  }
  return 0;
}

// write escape sequence seq to the terminal; inside tmux wrapped in its DCS
// passthrough (ESC P tmux; ... ESC \, every ESC inside doubled), which tmux
// forwards to the outer terminal when allow-passthrough is on
static int write_sequence(int fd, const char *seq, long deadline) {
  if (!env_set("TMUX")) return write_all(fd, seq, strlen(seq), deadline);
  char *wrapped = strdup("\033Ptmux;");
  size_t len = strlen(wrapped);
  for (const char *p = seq; *p; p++) sappendf(&wrapped, &len, *p == '\033' ? "\033\033" : "%c", *p);
  sappendf(&wrapped, &len, "\033\\");
  int r = write_all(fd, wrapped, len, deadline);
  free(wrapped);
  return r;
}

// ';' separates OSC 777 fields
static char *without_semicolons(const char *s) {
  char *copy = strdup(s);
  for (char *p = copy; *p; p++) if (*p == ';') *p = ',';
  return copy;
}

static int notify_terminal(int fd, int osc, const char *title, const char *body, long deadline) {
  char *seq = strdup("");
  size_t len = 0;
  if (osc == OSC_99) {
    // kitty: title, then body, of notification "await"; ST-terminated
    sappendf(&seq, &len, "\033]99;i=await:d=0;%s\033\\\033]99;i=await:p=body;%s\033\\", title, body);
  } else if (osc == OSC_777) {
    char *t = without_semicolons(title), *b = without_semicolons(body);
    sappendf(&seq, &len, "\033]777;notify;%s;%s\a", t, b);
    free(t);
    free(b);
  } else {
    // one message; it starts with "await", never a digit, which ConEmu and
    // Windows Terminal would read as an OSC 9;N subcommand
    sappendf(&seq, &len, "\033]9;%s: %s\a", title, body);
  }
  int r = write_sequence(fd, seq, deadline);
  free(seq);
  return r;
}

// run a notifier until it exits or the deadline passes; 0 if it exited 0.
// extra_env (NULL-terminated, may be NULL) is added to our environment.
static int run_notifier(char *const argv[], char *const extra_env[], long deadline) {
  if (current_time_ms() >= deadline) return -1;
  char **env = environ, **own = NULL;
  if (extra_env) {
    size_t n = 0, k = 0, extra = 0;
    while (environ[n]) n++;
    while (extra_env[extra]) extra++;
    own = malloc((n + extra + 1) * sizeof(char *));
    if (!own) return -1;
    for (size_t i = 0; i < n; i++) {
      int replaced = 0;
      for (size_t j = 0; j < extra; j++) {
        size_t name = strcspn(extra_env[j], "=") + 1;
        if (!strncmp(environ[i], extra_env[j], name)) replaced = 1;
      }
      if (!replaced) own[k++] = environ[i];
    }
    for (size_t j = 0; j < extra; j++) own[k++] = extra_env[j];
    own[k] = NULL;
    env = own;
  }
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
  posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
  posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
  // its own process group, so a hung notifier goes with everything it started
  posix_spawnattr_t attr;
  posix_spawnattr_init(&attr);
  posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
  posix_spawnattr_setpgroup(&attr, 0);
  pid_t pid;
  int err = posix_spawnp(&pid, argv[0], &actions, &attr, argv, env);
  posix_spawnattr_destroy(&attr);
  posix_spawn_file_actions_destroy(&actions);
  free(own);
  if (err != 0) return -1;
  int status;
  while (1) {
    pid_t r = waitpid(pid, &status, WNOHANG);
    if (r == pid) break;
    if (r < 0 && errno != EINTR) return -1;
    if (current_time_ms() >= deadline) {
      kill(-pid, SIGKILL);
      while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
      return -1;
    }
    msleep(10);
  }
  return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
}

static int notify_native(const char *title, const char *body, long deadline) {
#if defined(__APPLE__)
  if (run_notifier((char *const[]){"terminal-notifier", "-title", (char *)title, "-message", (char *)body, NULL},
                   NULL, deadline) == 0) return 0;
  // the text reaches AppleScript as arguments, never as source
  return run_notifier((char *const[]){"osascript", "-e", "on run argv",
                        "-e", "display notification (item 2 of argv) with title (item 1 of argv)",
                        "-e", "end run", (char *)title, (char *)body, NULL}, NULL, deadline);
#elif defined(__CYGWIN__)
  // a toast from Windows PowerShell; the text comes from the environment,
  // never from the script. Untested outside Windows.
  static char script[] =
    "$ErrorActionPreference='Stop';"
    "$m=[Windows.UI.Notifications.ToastNotificationManager,Windows.UI.Notifications,ContentType=WindowsRuntime];"
    "$x=$m::GetTemplateContent([Windows.UI.Notifications.ToastTemplateType]::ToastText02);"
    "$t=$x.GetElementsByTagName('text');"
    "[void]$t.Item(0).AppendChild($x.CreateTextNode($env:AWAIT_NOTIFY_TITLE));"
    "[void]$t.Item(1).AppendChild($x.CreateTextNode($env:AWAIT_NOTIFY_BODY));"
    "$n=New-Object Windows.UI.Notifications.ToastNotification $x;"
    "$m::CreateToastNotifier('{1AC14E77-02E7-4E5D-B744-2EB1AE5198B7}\\WindowsPowerShell\\v1.0\\powershell.exe').Show($n)";
  char *t = malloc(strlen(title) + 32), *b = malloc(strlen(body) + 32);
  if (!t || !b) { free(t); free(b); return -1; }
  sprintf(t, "AWAIT_NOTIFY_TITLE=%s", title);
  sprintf(b, "AWAIT_NOTIFY_BODY=%s", body);
  int r = run_notifier((char *const[]){"powershell.exe", "-NoProfile", "-NonInteractive", "-Command", script, NULL},
                       (char *const[]){t, b, NULL}, deadline);
  free(t);
  free(b);
  return r;
#else
  if (!env_set("WAYLAND_DISPLAY") && !env_set("DISPLAY") && !env_set("DBUS_SESSION_BUS_ADDRESS")) return -1;
  if (run_notifier((char *const[]){"notify-send", "-a", "await", (char *)title, (char *)body, NULL},
                   NULL, deadline) == 0) return 0;
  // gdbus parses arguments as GVariant text: pass the strings as literals
  char *args_text[2];
  const char *text[2] = {title, body};
  for (int i = 0; i < 2; i++) {
    args_text[i] = strdup("\"");
    size_t len = 1;
    for (const char *p = text[i]; *p; p++)
      sappendf(&args_text[i], &len, *p == '"' || *p == '\\' ? "\\%c" : "%c", *p);
    sappendf(&args_text[i], &len, "\"");
  }
  int r = run_notifier((char *const[]){"gdbus", "call", "--session",
                         "--dest", "org.freedesktop.Notifications",
                         "--object-path", "/org/freedesktop/Notifications",
                         "--method", "org.freedesktop.Notifications.Notify",
                         "await", "0", "''", args_text[0], args_text[1], "[]", "{}", "5000", NULL},
                       NULL, deadline);
  free(args_text[0]);
  free(args_text[1]);
  if (r == 0) return 0;
  return run_notifier((char *const[]){"kdialog", "--title", (char *)title, "--passivepopup", (char *)body, "5", NULL},
                      NULL, deadline);
#endif
}

// send a --notify notification: body is printf-style
void notify(const char *fmt, ...) {
  if (!args.notify) return;
  long deadline = current_time_ms() + NOTIFY_BUDGET_MS;
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(NULL, 0, fmt, ap);
  va_end(ap);
  if (n < 0) return;
  char *raw = malloc(n + 1);
  if (!raw) return;
  va_start(ap, fmt);
  vsnprintf(raw, n + 1, fmt, ap);
  va_end(ap);
  char *body = notify_sanitize(raw, NOTIFY_BODY_MAX);
  free(raw);
  // never let the text look like an option to a notifier
  if (*body == '-') {
    char *prefixed = malloc(strlen(body) + 8);
    if (prefixed) {
      sprintf(prefixed, "await: %s", body);
      free(body);
      body = notify_sanitize(prefixed, NOTIFY_BODY_MAX);
      free(prefixed);
    }
  }
  char *title;
  if (args.nCommands == 1 && c[1].name && c[1].name != c[1].command) {
    char *name = notify_sanitize(c[1].name, NOTIFY_LABEL_MAX);
    title = malloc(strlen(name) + 8);
    if (title) sprintf(title, "await: %s", name);
    free(name);
  } else title = strdup("await");
  if (!title) { free(body); return; }

  fflush(stdout);
  fflush(stderr);
  int ssh = env_set("SSH_CONNECTION") || env_set("SSH_TTY");
  int done = 0;
  int tty = open_tty();
  int osc = term_notifier();
  if (tty >= 0 && osc >= 0) done = notify_terminal(tty, osc, title, body, deadline) == 0;
  // over ssh an unknown terminal gets OSC 9 anyway (terminals ignore OSC
  // they don't know), and a bell in case it did
  if (!done && tty >= 0 && ssh)
    done = notify_terminal(tty, OSC_9, title, body, deadline) == 0 && write_all(tty, "\a", 1, deadline) == 0;
  if (!done && !ssh) done = notify_native(title, body, deadline) == 0;
  if (!done && tty >= 0) write_all(tty, "\a", 1, deadline);
  if (tty >= 0) close(tty);
  free(title);
  free(body);
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
  if (args.service) {
#ifdef __CYGWIN__
    fprintf(stderr, "await: --service isn't supported on Windows\n");
    return 2;
#else
    return service_main();
#endif
  }
  update_check();

  // Ensure the program does not ignore signals when running in a script
  signal(SIGINT, SIG_DFL);
  if (args.daemonize) daemonize();

  FILE *fp;

  atexit(stop_running_commands);
  // Start time for --timeout and --json elapsed_ms (and --backoff, in the commands' threads)
  args.start_time = current_time_ms();
  for(int i = 0; i <= args.nCommands; i++) {
    c[i].status = -1;
    pthread_mutex_init(&c[i].lock, NULL);
  }
  for(int i = 0; i <= args.nCommands; i++) {
    pthread_create(&c[i].thread, NULL, shell, &c[i]);
    pthread_detach(c[i].thread);  // a command's thread may finish (e.g. after its --retry runs)
  }

  int not_done = 0;
  char *exec_summary = NULL;  // --forever: how the commands went when the running --exec started
    // TODO: make a clear screen option
    // fprintf(stdout, "\033[2J\033[H");
    // fprintf(stderr, "\033[2J\033[H");
    // fflush(stdout);
    // fflush(stderr);

  static int first_output = 1;
  static char *last_display = NULL;
  static char *last_silent_output = NULL;

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
        // --times: on a streak that hasn't reached N yet
        if (args.times && !args.change && c[i].streak > 0 && c[i].streak < args.times) color = 3;
        
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
        if (args.show_stdout) {
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
      if (args.show_stdout) {
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
    if (exec_pid > 0) {
      int exec_wait;
      pid_t reaped = waitpid(exec_pid, &exec_wait, WNOHANG);
      if (reaped == exec_pid) notify("%s; --exec finished (exit %d)", exec_summary, exit_code(exec_wait));
      if (reaped != 0) {
        exec_pid = 0;
        free(exec_summary);
        exec_summary = NULL;
      }
    }

    // --times: act once per streak, so a trigger needs a streak that reached
    // N since the last one (only matters with --forever)
    int new_streak = !args.times;
    for (int i = 1; i <= args.nCommands && !new_streak; i++)
      new_streak = c[i].reached != c[i].seenReached;

    // with --forever, a trigger during a running --exec waits for it to finish
    if ((not_done == 0 || args.any && not_done < args.nCommands) && new_streak && !exec_pid) {
      // before the changes and streaks are marked seen, which not_done_cmd looks at
      char *summary = args.notify ? outcome_summary(not_done) : NULL;
      // act on each change only once, and on each completed --times streak
      // once: streaks completed while an --exec ran each get their own run
      for (int i = 1; i <= args.nCommands; i++) {
        c[i].seenChanges = c[i].changes;
        if (c[i].seenReached != c[i].reached) c[i].seenReached++;
      }

      int exec_status = 0;
      if (args.exec) {
        exec_pid = start_exec();
        if (!args.forever) exec_status = wait_exec(exec_pid);
        else if (exec_pid < 0) exec_pid = 0;
        else {
          // told when it finishes
          exec_summary = summary;
          summary = NULL;
        }
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
        char took[32];
        format_duration(elapsed_ms(), took, sizeof(took));
        if (args.exec) notify("done in %s: %s; --exec finished (exit %d)", took, summary, exec_status);
        else notify("done in %s: %s", took, summary);
        free(summary);
        return exec_status;
      }
      free(summary);
    }

    // Check timeout
    if (args.timeout > 0) {
      long elapsed = elapsed_ms();
      if (elapsed >= args.timeout) {
        if (!args.silent) {
          fprintf(stderr, use_color() ? "\n\033[0;31mTimeout reached after %ld ms\033[0m\n" : "\nTimeout reached after %ld ms\n", elapsed);
          for (int i = 1; i <= args.nCommands; i++) {
            if (c[i].status == -1) {
              fprintf(stderr, "  '%s': still running / no completed attempt\n", c[i].command);
            } else if (args.expect && !c[i].timed_out) {
              fprintf(stderr, "  '%s': last output %s --expect\n", c[i].command, c[i].status == 0 ? "matched" : "did not match");
            } else {
              fprintf(stderr, "  '%s': last exit %d%s\n", c[i].command, c[i].status, status_note(&c[i]));
            }
            if (args.times && c[i].status != -1)
              fprintf(stderr, "    %d of %d %s in a row\n", c[i].streak, args.times, args.change ? "changes" : "checks");
          }
        }
        if (args.json) print_json_result(1);
        if (args.notify) {
          char took[32], *summary = outcome_summary(not_done);
          format_duration(elapsed, took, sizeof(took));
          notify("timed out after %s: %s", took, summary);
          free(summary);
        }
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
      if (args.notify) {
        char *summary = outcome_summary(not_done);
        notify("gave up after %d attempts: %s", rounds, summary);
        free(summary);
      }
      return 1;
    }

    msleep(args.interval);
  }

  if (args.daemonize) closelog();
  return 0;
}
