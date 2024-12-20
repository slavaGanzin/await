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


char *spinner[] = {"⣾","⣽","⣻","⢿","⡿","⣟","⣯","⣷"};
typedef struct {
  int spinner;
  char *command;
  char *out;
  char *previousOut;
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
         "            case --help --stdout --silent --fail --status --any --change --exec --interval --forever --service\n"
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
         "complete -c await -n '__fish_await_no_subcommand' -l exec -s e -d 'Run some shell command on success' -r\n"
         "complete -c await -n '__fish_await_no_subcommand' -l interval -s i -d 'Milliseconds between one round of commands [default: 200]' -r\n"
         "complete -c await -n '__fish_await_no_subcommand' -l forever -s F -d 'Do not exit ever'\n"
         "complete -c await -n '__fish_await_no_subcommand' -l service -s S -d 'Create systemd user service with same parameters and activate it'\n"
         "complete -c await -n '__fish_await_no_subcommand' -l no-stderr -s E -d 'Surpress stderr of commands by adding 2>/dev/null to commands'\n"
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
         "    opts=\"--help --stdout --silent --fail --status --any --change --exec --interval --forever --service --version --no-stderr\"\n"
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
         "    '--exec[Run some shell command on success]::command:_command_names' \\\n"
         "    '--interval[Milliseconds between rounds of commands (default: 200)]::interval' \\\n"
         "    '--forever[Do not exit ever]' \\\n"
         "    '--service[Create systemd user service with same parameters and activate it]'\n"
         "}\n"
         "\n"
         "# Register the completion function\n"
         "compdef _await await\n");
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
  "  --no-stderr -E\t#surpress stderr of commands by adding 2>/dev/null to commands (doesn't work with pipes)\n"
  "  --silent -V\t\t#do not print spinners and commands\n"
  "  --fail -f\t\t#waiting commands to fail\n"
  "  --status -s\t\t#expected status [default: 0]\n"
  "  --any -a\t\t#terminate if any of command return expected status\n"
  "  --change -c\t\t#waiting for stdout to change and ignore status codes\n"
  "  --exec -e\t\t#run some shell command on success;\n"
  "  --interval -i\t\t#milliseconds between one round of commands [default: 200]\n"
  "  --forever -F\t\t#do not exit ever\n"
  "  --service -S\t\t#create systemd user service with same parameters and activate it\n"
  "  --version -v\t\t#print the version of await\n"

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
            {"daemon",  no_argument,       0, 'd'},
            {"service", required_argument, 0, 'S'},
            {"status",  required_argument, 0, 's'},
            {"exec",    required_argument, 0, 'e'},
            {"interval",required_argument, 0, 'i'},
            {"no-stderr", no_argument, 0, 'E'},
            {"autocomplete-fish", no_argument, 0, 0},
            {"autocomplete-bash", no_argument, 0, 0},
            {"autocomplete-zsh", no_argument, 0, 0},
            {0, 0, 0, 0}
          };

        int option_index = 0;
        getopt = getopt_long(argc, argv, "oVafFchdvS:s:e:i:E", long_options, &option_index);

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
            if (strcmp(long_options[option_index].name, "autocomplete-fish") == 0) {
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
          case 'd': args.daemonize = 1; break;
          case 'v': printf("1.0.8\n"); exit(0); break;
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
      if (args.no_stderr) {
        strcat(c[args.nCommands].command, " 2>/dev/null");
      }
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

  char buf[BUF_SIZE];
  while (1) {
    c->outPos = 0;
    strcpy(c->out, "");
    FILE *fp = popen(replace_placeholders(c->command), "r");
    c->pid = fileno(fp);

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
    int status = pclose(fp);
    c->status = WEXITSTATUS(status);

    if (strcmp(c->previousOut, "first run") != 0)
      c->change = strcmp(c->previousOut,c->out) != 0;

    strcpy(c->previousOut, c->out);

    if (args.daemonize) syslog(LOG_NOTICE, "%d %s", c->status, c->command);
    if (stop) {
      break;
    }
    msleep(args.interval);
  }
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

  while (1) {
    not_done = 0;
    int line = 0;

    for(int i = 1; i <= args.nCommands; i++) {
      line+=1;

      if (!args.silent) {
        int color = c[i].status == -1 ? 7 : c[i].status == args.expectedStatus ? 2 : 1;
        fprintf(stderr, "\033[%dB\r\033[K\033[0;3%dm%s\033[0m %s\033[%dA\r", line, color, spinner[c[i].spinner], c[i].command, line);
        fflush(stderr);
      }

      if (args.stdout && c[i].out) {
        int line_count = 0;
        for (char *p = c[i].out; *p != '\0'; p++) {
            if (*p == '\n') {
                line_count++;
            }
        }
        line += line_count;
        fprintf(stdout, "\033[%dB\r%s\033[%dA\r", line-line_count+1, c[i].out, line+1);
        fflush(stdout);
      }

      if (args.change) not_done =  !c[i].change;
      else not_done += c[i].status==-1 || (args.fail && c[i].status == 0) || (!args.fail && c[i].status != args.expectedStatus);
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
            fprintf(stderr, "\033[%dB\r", line+1);
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
