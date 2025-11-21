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


char *spinner[] = {"⣾","⣽","⣻","⢿","⡿","⣟","⣯","⣷"};
typedef struct {
  int spinner;
  char *command;
  char *out;
  char *previousOut;
  char *diffOut;  // For storing difference-highlighted output
  size_t outPos;
  int status;
  int change;
  int pid;
  pthread_t thread;
} COMMAND;

COMMAND c[100];
COMMAND exec;

typedef struct {
  int expectedStatus;
  int interval;
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
} ARGS;

ARGS args = {.interval=200, .expectedStatus = 0, .silent=0, .change=0, .nCommands=0, .args=""};

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
  printf("function __fish_await_no_subcommand\n"
         "    set -l cmd (commandline -opc)\n"
         "    for i in $cmd\n"
         "        switch $i\n"
         "            case --help --stdout --silent --fail --status --any --change --diff --exec --interval --forever --service --watch\n"
         "                return 1\n"
         "        end\n"
         "    end\n"
         "    return 0\n"
         "end\n"
         "complete -c await -n '__fish_await_no_subcommand' -l version -s v -d 'Print the version of await'\n"
         "complete -c await -n '__fish_await_no_subcommand' -l help -d 'Print this help'\n"
         "complete -c await -n '__fish_await_no_subcommand' -l stdout -s o -d 'Print stdout of commands'\n"
         "complete -c await -n '__fish_await_no_subcommand' -l silent -s V -d 'Do not print spinners and commands'\n"
         "complete -c await -n '__fish_await_no_subcommand' -l fail -s f -d 'Waiting commands to fail'\n"
         "complete -c await -n '__fish_await_no_subcommand' -l status -s s -d 'Expected status [default: 0]' -r\n"
         "complete -c await -n '__fish_await_no_subcommand' -l any -s a -d 'Terminate if any of command return expected status'\n"
         "complete -c await -n '__fish_await_no_subcommand' -l change -s c -d 'Waiting for stdout to change and ignore status codes'\n"
         "complete -c await -n '__fish_await_no_subcommand' -l diff -s d -d 'Highlight differences between previous and current output'\n"
         "complete -c await -n '__fish_await_no_subcommand' -l exec -s e -d 'Run some shell command on success' -r\n"
         "complete -c await -n '__fish_await_no_subcommand' -l interval -s i -d 'Milliseconds between one round of commands [default: 200]' -r\n"
         "complete -c await -n '__fish_await_no_subcommand' -l forever -s F -d 'Do not exit ever'\n"
         "complete -c await -n '__fish_await_no_subcommand' -l service -s S -d 'Create systemd user service with same parameters and activate it'\n"
         "complete -c await -n '__fish_await_no_subcommand' -l no-stderr -s E -d 'Surpress stderr of commands by adding 2>/dev/null to commands'\n"
         "complete -c await -n '__fish_await_no_subcommand' -l watch -s w -d 'Equivalent to -fVodE (fail, silent, stdout, diff, no-stderr)'\n"
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
         "    opts=\"--help --stdout --silent --fail --status --any --change --diff --exec --interval --forever --service --version --no-stderr --watch\"\n"
         "\n"
         "    case \"${prev}\" in\n"
         "        --status|--exec|--interval)\n"
         "            COMPREPLY=($(compgen -f -- \"${cur}\"))\n"
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
         "    '--status[Expected status (default: 0)]::status' \\\n"
         "    '--any[Terminate if any command returns expected status]' \\\n"
         "    '--change[Wait for stdout to change and ignore status codes]' \\\n"
         "    '--diff[Highlight differences between previous and current output]' \\\n"
         "    '--exec[Run some shell command on success]::command:_command_names' \\\n"
         "    '--interval[Milliseconds between rounds of commands (default: 200)]::interval' \\\n"
         "    '--forever[Do not exit ever]' \\\n"
         "    '--watch[Equivalent to -fVodE (fail, silent, stdout, diff, no-stderr)]' \\\n"
         "    '--service[Create systemd user service with same parameters and activate it]'\n"
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
    snprintf(cmd, sizeof(cmd), "mkdir -p '%s' 2>/dev/null && (grep -q '__fish_await' '%s' 2>/dev/null || '%s' --autocomplete-fish >> '%s' 2>/dev/null)", fish_dir, fish_file, binary_path, fish_file);
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

char * replace_placeholders(char *string) {
  for(int i = 0; i < args.nCommands; i = i + 1) {
    if (!c[i].previousOut) continue;
    char C[3];
    sprintf(C, "\\%d", i+1);
    string = replace(C, c[i].previousOut, string);
  }
  return string;
}

char * colorize_comments(char *string) {
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
  char *highlighted = malloc(new_len * 10); // Generous space for ANSI codes
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
  "# waiting google (or your internet connection) to fail\n"
  "  await 'curl google.com' --fail\n\n"
  "# waiting only google to fail (https://ec.haxx.se/usingcurl/usingcurl-returns)\n"
  "  await 'curl google.com' --status 7\n\n"
  "# waits for redis socket and then connects to\n"
  "  await 'socat -u OPEN:/dev/null UNIX-CONNECT:/tmp/redis.sock' --exec 'redis-cli -s /tmp/redis.sock'\n\n"
  "# lazy version\n"
  "  await 'ls /tmp/redis.sock'; redis-cli -s /tmp/redis.sock\n\n"
  "# daily checking if I am on french reviera. Just in case\n"
  "  await 'curl https://ipapi.co/json 2>/dev/null | jq .city | grep Nice' --interval 86400\n\n"
  "# Yet another server monitor\n"
  "  await \"curl 'https://whatnot.ai' &>/dev/null && echo 'UP' || echo 'DOWN'\" --forever --change\\\n    --exec \"ntfy send \\'whatnot.ai \\1\\'\"\n\n"
  "# waiting for new iPhone in daemon mode\n"
  "  await 'curl \"https://www.apple.com/iphone/\" -s | pup \".hero-eyebrow text{}\" | grep -v 12'\\\n --change --interval 86400 --daemon --exec \"ntfy send \\1\"\n\n"
  "\nOPTIONS:\n"
  "  --help\t\t#print this help\n"
  "  --stdout -o\t\t#print stdout of commands\n"
  "  --no-stderr -E\t#suppress stderr output from commands\n"
  "  --watch -w\t\t#equivalent to -fVodE (fail, silent, stdout, diff, no-stderr)\n"
  "  --silent -V\t\t#do not print spinners and commands\n"
  "  --fail -f\t\t#waiting commands to fail\n"
  "  --status -s\t\t#expected status [default: 0]\n"
  "  --any -a\t\t#terminate if any of command return expected status\n"
  "  --change -c\t\t#waiting for stdout to change and ignore status codes\n"
  "  --diff -d\t\t#highlight differences between previous and current output (like watch -d)\n"
  "  --exec -e\t\t#run some shell command on success;\n"
  "  --interval -i\t\t#milliseconds between one round of commands [default: 200]\n"
  "  --forever -F\t\t#do not exit ever\n"
  "  --service -S\t\t#create systemd user service with same parameters and activate it\n"
  "  --version -v\t\t#print the version of await\n"

  "  --autocompletions\t#detect installed shells and auto-install completions for all of them\n"
  "  --autocomplete-fish\t#output fish shell autocomplete script\n"
  "  --autocomplete-bash\t#output bash shell autocomplete script\n"
  "  --autocomplete-zsh\t#output zsh shell autocomplete script\n"
  "\n\nNOTES:\n"
  "# \\1, \\2 ... \\n - will be subtituted with n-th command stdout\n"
  "# you can use stdout substitution in --exec and in commands itself:\n"
  "  await 'echo -n 10' 'echo -n $RANDOM' 'expr \\1 + \\2' --exec 'echo \\3' --forever --silent\n"

  // "# waiting for pup's author new blog post\n"
  // "  await 'mv /tmp/eric.new /tmp/eric.old &>/dev/null; http \"https://ericchiang.github.io/\" | pup \"a attr{href}\" > /tmp/eric.new; diff /tmp/eric.new /tmp/eric.old' --fail --exec 'ntfy send \"new article $1\"'\n\n"
  ));
  exit(0);
}

volatile sig_atomic_t stop = 0;

// void handle_sigint(int sig) {
//     stop = 1;
// }

void parse_args(int argc, char *argv[]) {
    int getopt;

    args.args = malloc(1000);

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
            {"interval",required_argument, 0, 'i'},
            {"no-stderr", no_argument, 0, 'E'},
            {"watch", no_argument, 0, 'w'},
            {"autocompletions", no_argument, 0, 0},
            {"autocomplete-fish", no_argument, 0, 0},
            {"autocomplete-bash", no_argument, 0, 0},
            {"autocomplete-zsh", no_argument, 0, 0},
            {0, 0, 0, 0}
          };

        int option_index = 0;
        getopt = getopt_long(argc, argv, "oVafFchdvS:s:e:i:Ew", long_options, &option_index);

        if (getopt == -1)
          break;

        if (getopt != 'S') {
          strcat(args.args, "--");
          for (int i =0; i<12; i++) {
            if (long_options[i].val == getopt)
              strcat(args.args, long_options[i].name);
          }
          if (optarg) {
            strcat(args.args, " \"");
            strcat(args.args, optarg);
            strcat(args.args, "\"");
          }
          strcat(args.args, " ");
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
          case 'i': args.interval = atoi(optarg); break;
          case 'd': args.diff = 1; break;
          case 'v': printf("2.4.0\n"); exit(0); break;
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
        }
      }

    if (!args.exec && args.daemonize)
      printf("NOTICE: --daemon is kinda meaningless without --exec 'command'");

    c[0].command = "";

    while (optind < argc) {
      strcat(args.args, " \"");
      strcat(args.args, argv[optind]);
      strcat(args.args, "\"");
      c[++args.nCommands].command = argv[optind++];
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
  char binary[BUFSIZ];
  readlink("/proc/self/exe", binary, BUFSIZ);

  fp = fopen(f, "w");
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
   , args.args, cwd, replace("ARGS", args.args, replace("BINARY", binary, "BINARY ARGS")));
  fclose(fp);

  system(replace("SERVICE", service, "systemctl --user daemon-reload; systemctl cat --user SERVICE; systemctl enable --user SERVICE; systemctl restart --user SERVICE; journalctl --user --follow --unit SERVICE"));
  return 0;
}

void *shell(void * arg) {
  COMMAND *c = (COMMAND*)arg;
  c->out = malloc(CHUNK_SIZE * sizeof(char));
  strcpy(c->out, "");
  c->previousOut = malloc(CHUNK_SIZE * sizeof(char));
  c->diffOut = NULL;

  char buf[BUF_SIZE];
  while (1) {
    c->outPos = 0;
    strcpy(c->out, "");
    
    int pipefd[2];
    pipe(pipefd);
    
    pid_t child_pid = fork();
    if (child_pid == 0) {
      // Child process
      close(pipefd[0]); // Close read end
      dup2(pipefd[1], STDOUT_FILENO); // Redirect stdout to pipe
      
      if (args.no_stderr) {
        // Redirect stderr to /dev/null
        int devnull = open("/dev/null", O_WRONLY);
        dup2(devnull, STDERR_FILENO);
        close(devnull);
      }
      
      close(pipefd[1]);
      execl("/bin/sh", "sh", "-c", replace_placeholders(c->command), NULL);
      exit(1);
    } else {
      // Parent process
      close(pipefd[1]); // Close write end
      FILE *fp = fdopen(pipefd[0], "r");
      c->pid = child_pid;

    while (fgets(buf, BUF_SIZE, fp) !=NULL) {
      c->outPos += BUF_SIZE;

      if (c->outPos % CHUNK_SIZE > CHUNK_SIZE*0.8) {
        c->out = realloc(c->out, c->outPos + c->outPos % CHUNK_SIZE + CHUNK_SIZE);
        c->previousOut = realloc(c->previousOut, c->outPos + c->outPos % CHUNK_SIZE + CHUNK_SIZE);
      }
      sprintf(c->out, "%s%s", c->out, buf);
    }

    if (!c->spinner || c->spinner == 0) c->spinner = sizeof(spinner)/sizeof(spinner[0]);
    c->spinner--;
    
    fclose(fp);
    int status;
    waitpid(c->pid, &status, 0);
    c->status = WEXITSTATUS(status);
    }

    if (strcmp(c->previousOut, "first run") != 0) {
      c->change = strcmp(c->previousOut,c->out) != 0;
      
      // Compute differences if diff mode is enabled
      if (args.diff && c->change) {
        if (c->diffOut) free(c->diffOut);
        c->diffOut = highlight_differences(c->previousOut, c->out);
      }
    }

    strcpy(c->previousOut, c->out);

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
  pthread_t exec_thread;

  // struct sigaction sa;
  // sa.sa_handler = handle_sigint;
  // sigemptyset(&sa.sa_mask);
  // sa.sa_flags = 0;
  // sigaction(SIGINT, &sa, NULL);

  parse_args(argc, argv);
  if (args.service) return service();

  // Ensure the program does not ignore signals when running in a script
  signal(SIGINT, SIG_DFL);
  if (args.daemonize) daemonize();

  FILE *fp;

  for(int i = 0; i <= args.nCommands; i++) {
    c[i].status = -1;
    pthread_create(&c[i].thread, NULL, shell, &c[i]);
  }

  int not_done = 0;
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
      char *display = malloc(10000); // Large buffer for display
      strcpy(display, "");
      
      for(int i = 1; i <= args.nCommands; i++) {
        int color = c[i].status == -1 ? 7 : c[i].status == args.expectedStatus ? 2 : 1;
        
        // Add status line
        char status_line[1000];
        sprintf(status_line, "\033[0;3%dm%s\033[0m %s\n", color, spinner[c[i].spinner], c[i].command);
        strcat(display, status_line);
        
        // Add output if available, or previous output if command has run before
        if (args.stdout) {
          char *output_to_show = NULL;
          
          // If diff mode is enabled and we have diff output, use that
          if (args.diff && c[i].diffOut) {
            output_to_show = malloc(strlen(c[i].diffOut) + 1);
            strcpy(output_to_show, c[i].diffOut);
          } else if (c[i].out && strlen(c[i].out) > 0) {
            // Use current output
            output_to_show = malloc(strlen(c[i].out) + 1);
            strcpy(output_to_show, c[i].out);
          } else if (c[i].previousOut && strlen(c[i].previousOut) > 0 && strcmp(c[i].previousOut, "first run") != 0) {
            // Use previous output if current is empty but we have previous
            output_to_show = malloc(strlen(c[i].previousOut) + 1);
            strcpy(output_to_show, c[i].previousOut);
          }
          
          if (output_to_show) {
            int len = strlen(output_to_show);
            if (len > 0 && output_to_show[len - 1] == '\n') {
                output_to_show[len - 1] = '\0';
            }
            strcat(display, output_to_show);
            strcat(display, "\n");
            free(output_to_show);
          }
        }
        
        if (args.change) not_done =  !c[i].change;
        else not_done += c[i].status==-1 || (args.fail && c[i].status == 0) || (!args.fail && c[i].status != args.expectedStatus);
      }
      
      // Print the entire display at once
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
        char *silent_display = malloc(10000);
        strcpy(silent_display, "");
        int has_output = 0;
        
        for(int i = 1; i <= args.nCommands; i++) {
          char *output_to_show = NULL;
          
          // If diff mode is enabled and we have diff output, use that
          if (args.diff && c[i].diffOut) {
            output_to_show = malloc(strlen(c[i].diffOut) + 1);
            strcpy(output_to_show, c[i].diffOut);
          } else if (c[i].out && strlen(c[i].out) > 0) {
            // Use current output
            output_to_show = malloc(strlen(c[i].out) + 1);
            strcpy(output_to_show, c[i].out);
          } else if (c[i].previousOut && strlen(c[i].previousOut) > 0 && strcmp(c[i].previousOut, "first run") != 0) {
            // Use previous output if current is empty but we have previous
            output_to_show = malloc(strlen(c[i].previousOut) + 1);
            strcpy(output_to_show, c[i].previousOut);
          }
          
          if (output_to_show) {
            int len = strlen(output_to_show);
            if (len > 0 && output_to_show[len - 1] == '\n') {
                output_to_show[len - 1] = '\0';
            }
            
            if (has_output) {
              strcat(silent_display, "\n");
            }
            strcat(silent_display, output_to_show);
            has_output = 1;
            free(output_to_show);
          }
          
          if (args.change) not_done =  !c[i].change;
          else not_done += c[i].status==-1 || (args.fail && c[i].status == 0) || (!args.fail && c[i].status != args.expectedStatus);
        }
        
        // Print the silent display
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
          if (args.change) not_done =  !c[i].change;
          else not_done += c[i].status==-1 || (args.fail && c[i].status == 0) || (!args.fail && c[i].status != args.expectedStatus);
        }
      }
    }

    if (not_done == 0 || args.any && not_done < args.nCommands) {
      if (args.exec) {
        exec.command = args.exec;
        exec.spinner = 1;
        pthread_create(&exec_thread, NULL, shell, &exec);
      }

      if (!args.forever) {
        while (1) {
          int color = exec.status == -1 ? 7 : exec.status == args.expectedStatus ? 2 : 1;
          if (exec.spinner == 0) {
            fprintf(stderr, "\033[%dB\r", args.nCommands + 1);
            return 0;
          }
        }
      }
    }
    msleep(args.interval);
  }

  if (args.daemonize) closelog();
  return 0;
}
