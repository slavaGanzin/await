#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/stat.h>
#include <syslog.h>
#include <threads.h>

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
}

static void daemonize()
{
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

thrd_t thread;
char *spinner[] = {"⣾","⣽","⣻","⢿","⡿","⣟","⣯","⣷"};
typedef struct {
  int spinner;
  char *command;
  char *out;
  char *previousOut;
  size_t outPos;
  int status;
  int change;
} COMMAND;

COMMAND c[100];

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
  char *onSuccess;
  int nCommands;
} ARGS;
ARGS args = {.interval=200, .expectedStatus = 0, .silent=0, .change=0, .nCommands=0};

// void clean() {
//   for(int i = 0; i < totalCommands; i = i + 1 ){
//     fprintf(stderr, "\033[%dB\r\033[K\r\033[%dA", i, i);
//   }
//   fflush(stderr);
// }

int const BUF_SIZE = 1024;
int const CHUNK_SIZE = BUF_SIZE * 100;

char * replace_outs(char *string) {
  for(int i = 0; i < args.nCommands; i = i + 1) {
    if (!c[i].previousOut || strcmp(c[i].previousOut, "first run")) continue;
    char C[3];
    sprintf(C, "\\%d", i+1);
    string = replace(C, c[i].previousOut, string);
  }
  return string;
}

int shell(void * arg) {
  COMMAND *c = (COMMAND*)arg;
  c->out = malloc(CHUNK_SIZE * sizeof(char));
  strcpy(c->out, "");
  c->previousOut = malloc(CHUNK_SIZE * sizeof(char));
  strcpy(c->previousOut, "first run");

  char buf[BUF_SIZE];
  while (1) {
    c->outPos = 0;
    strcpy(c->out, "");
    FILE *fp = popen(replace_outs(c->command), "r");

    while (fgets(buf, BUF_SIZE, fp) !=NULL) {
      c->outPos += BUF_SIZE;

      if (c->outPos % CHUNK_SIZE > CHUNK_SIZE*0.8) {
        c->out = realloc(c->out, c->outPos + c->outPos % CHUNK_SIZE + CHUNK_SIZE);
        c->previousOut = realloc(c->previousOut, c->outPos + c->outPos % CHUNK_SIZE + CHUNK_SIZE);
      }
      sprintf(c->out, "%s%s", c->out, buf);
      if (args.stdout) printf("%s", buf);
    }

    if (!c->spinner || c->spinner == 0) c->spinner = sizeof(spinner)/sizeof(spinner[0]);
    c->spinner--;
    c->status = WEXITSTATUS(pclose(fp));

    if (strcmp(c->previousOut, "first run") != 0)
      c->change = strcmp(c->previousOut,c->out) != 0;

    strcpy(c->previousOut, c->out);

    syslog(LOG_NOTICE, "%d %s", c->status, c->command);
    msleep(args.interval);
  }
  return 0;
}

void help() {
  printf("await [arguments] commands\n\n"
  "# runs list of commands and waits for their termination\n"
  "\nOPTIONS:\n"
  "  --help\t#print this help\n"
  "  --stdout -o\t#print stdout of commands\n"
  "  --silent -V\t#print nothing\n"
  "  --fail -f\t#waiting commands to fail\n"
  "  --status -s\t#expected status [default: 0]\n"
  "  --any -a\t#terminate if any of command return expected status\n"
  "  --change -c\t#waiting for stdout to change and ignore status codes\n"
  "  --exec -e\t#run some shell command on success;\n"
  "  --interval -i\t#milliseconds between one round of commands [default: 200]\n"
  "  --forever -F\t#do not exit ever\n"

  "\n\nNOTES:\n"
  "# \\1, \\2 ... \\n - will be subtituted with n-th command stdout\n"
  "# you can use stdout substitution in --exec and in commands itself:\n"
  "  await 'echo -n 10' 'echo -n $RANDOM' 'expr \\1 + \\2' --exec 'echo \\3' --forever --silent\n"
  "\n\nEXAMPLES:\n"
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

  // "# waiting for pup's author new blog post\n"
  // "  await 'mv /tmp/eric.new /tmp/eric.old &>/dev/null; http \"https://ericchiang.github.io/\" | pup \"a attr{href}\" > /tmp/eric.new; diff /tmp/eric.new /tmp/eric.old' --fail --exec 'ntfy send \"new article $1\"'\n\n"
);
  exit(0);
}

void parse_args(int argc, char *argv[]) {
    int getopt;

    while (1) {
        static struct option long_options[] = {
            {"stdout",  no_argument,       0, 'o'},
            {"silent",  no_argument,       0, 'V'},
            {"any",     no_argument,       0, 'a'},
            {"fail",    no_argument,       0, 'f'},
            {"forever", no_argument,       0, 'F'},
            {"change",  no_argument,       0, 'c'},
            {"status",  required_argument, 0, 's'},
            {"exec",    required_argument, 0, 'e'},
            {"interval",required_argument, 0, 'i'},
            {"help",    no_argument,       0, 'h'},
            {"daemon",  no_argument,       0, 'd'},
          };
        int option_index = 0;

        getopt = getopt_long(argc, argv, "e:oVs:ai:dFfc", long_options, &option_index);

        if (getopt == -1)
          break;

        switch (getopt)
          {
          case 0:
            /* If this option set a flag, do nothing else now. */
            if (long_options[option_index].flag != 0)
              break;
            printf ("option %s", long_options[option_index].name);
            if (optarg) printf (" with arg %s", optarg);
            printf ("\n");
            break;

          case 'V': args.silent = 1; break;
          case 'o': args.stdout = 1; break;
          case 'e': args.onSuccess=optarg; break;
          case 's': args.expectedStatus=atoi(optarg); break;
          case 'f': args.fail = 1; break;
          case 'a': args.any = 1; break;
          case 'F': args.forever = 1; break;
          case 'c': args.change = 1; break;
          case 'i': args.interval = atoi(optarg); break;
          case 'd': args.daemonize = 1; break;
          case 'h': case '?': help(); break;
          default: abort();
          }
      }

    if (!args.onSuccess && args.daemonize)
      printf("NOTICE: --daemon is kinda meaningless without --exec 'command'");

    while (optind < argc)
      c[args.nCommands++].command = argv[optind++];

    if (args.nCommands == 0) help();
}


int main(int argc, char *argv[]) {
  thrd_t thread;

  parse_args(argc, argv);
  if (args.daemonize) daemonize();

  FILE *fp;

  for(int i = 0; i < args.nCommands; i++) {
    c[i].status = -1;
    thrd_create(&thread, shell, &c[i]);
  }

  int not_done;
  while (1) {
    not_done = 0;
    for(int i = 0; i < args.nCommands; i++) {
      if (!args.silent) {
        int color = c[i].status == -1 ? 7 : c[i].status == args.expectedStatus ? 2 : 1;
        fprintf(stderr, "\033[%dB\r\033[K\033[0;3%dm%s\033[0m %s\033[%dA\r", i, color, spinner[c[i].spinner], c[i].command, i);
      }

      if (args.change) not_done =  !c[i].change;
      else not_done += c[i].status==-1 || (args.fail && c[i].status == 0) || (!args.fail && c[i].status != args.expectedStatus);
    }
    fflush(stdout);
    fflush(stderr);

    if (not_done == 0 || args.any && not_done < args.nCommands) {
      if (args.onSuccess) system(replace_outs(args.onSuccess));
      if (!args.forever) exit(0);
    }
    msleep(40);
  }
  closelog();
}
