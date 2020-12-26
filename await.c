#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <regex.h>
#include <unistd.h>
#include <getopt.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <syslog.h>
#include <string.h>

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

char *spinner[] = {"⣾","⣽","⣻","⢿","⡿","⣟","⣯","⣷"};
int spinnerI[100];
char *commands[100];
char *out[100];
size_t outPos[100];

struct ARGS {
  int expectedStatus;
  int interval;
  int any;
  int change;
  int silent;
  int forever;
  int daemonize;
  int fail;
  int verbose;
  char *onSuccess;
};
struct ARGS args = {.interval=200};

int totalCommands = 0;


void clean() {
  for(int i = 0; i < totalCommands; i = i + 1 ){
    fprintf(stderr, "\033[%dB\r\033[K\r\033[%dA", i, i);
  }
  fflush(stderr);
}

void spin(char *command, int color, int i) {
  if (!spinnerI[i] || spinnerI[i] == 0) spinnerI[i] = 7;
  fprintf(stderr, "\033[%dB\r\033[K\033[0;3%dm%s\033[0m %s\033[%dA\r", i, color, spinner[spinnerI[i]--], command, i);
  fflush(stderr);
}

#define BUF_SIZE 1024
#define CHUNK_SIZE BUF_SIZE * 10

int * shell(int i) {
  if(!args.silent) spin(commands[i], 7, i);
  char buf[BUF_SIZE];
  fflush(stdout);
  fflush(stderr);

  outPos[i] = 0;
  strcpy(out[i], "");

  FILE *fp;
  fp = popen(commands[i], "r");
  if (!fp) exit(EXIT_FAILURE);
  if (fp == NULL) /* Handle error */;

  while (fgets(buf, BUF_SIZE, fp) !=NULL) {
    outPos[i] += BUF_SIZE;
    if (outPos[i] % CHUNK_SIZE > CHUNK_SIZE*0.8) {
      out[i] = realloc(out[i], outPos[i] + outPos[i] % CHUNK_SIZE + CHUNK_SIZE);
    }

    sprintf(out[i], "%s%s", out[i], buf);
    if (!args.silent && args.verbose) printf("\n\n%s", buf);
  }

  static int s[2];
  s[0] = WEXITSTATUS(pclose(fp));
  s[1] = 0;

  return s;
}

void help() {
  printf("await [arguments] commands\n\n"
  "# runs list of commands and waits for their termination\n"
  "\nARGUMENTS:\n"
  "  --help\t#print this help\n"
  "  --verbose -v\t#increase verbosity\n"
  "  --silent -V\t#print nothing\n"
  "  --fail -f\t#waiting commands to fail\n"
  "  --status -s\t#expected status [default: 0]\n"
  "  --any -a\t#terminate if any of command return expected status\n"
  "  --exec -e\t#run some shell command on success;\n"
  "               \\1, \\2 ... \\n - will be subtituted nth command stdout\n"
  "  --interval -i\t#milliseconds between one round of commands [default: 200]\n"
  "  --no-exit -E\t#do not exit\n"
  "\n"
  "EXAMPLES:\n"
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
  "  await 'http https://whatnot.ai' --forever --fail --exec \"ntfy send \\'whatnot.ai down\\'\"'\n\n"
  "# waiting for new iPhone in daemon mode\n"
  "  await 'curl \"https://www.apple.com/iphone/\" -s | pup \".hero-eyebrow text{}\" | grep -v 12'\\\n --interval 86400 --daemon --exec \"ntfy send \1\" \n\n"

  // "# waiting for pup's author new blog post\n"
  // "  await 'mv /tmp/eric.new /tmp/eric.old &>/dev/null; http \"https://ericchiang.github.io/\" | pup \"a attr{href}\" > /tmp/eric.new; diff /tmp/eric.new /tmp/eric.old' --fail --exec 'ntfy send \"new article $1\"'\n\n"
);
  exit(0);
}

void parse_args(int argc, char *argv[]) {
    int c;

    while (1)
      {
        static struct option long_options[] =
          {
            {"verbose", no_argument,       0, 'v'},
            {"silent",  no_argument,       0, 'V'},
            {"any",    no_argument,        0, 'a'},
            {"fail",    no_argument,       0, 'f'},
            {"forever", no_argument,       0, 'F'},
            {"change", no_argument,       0, 'c'},
            {"status",  required_argument, 0, 's'},
            {"exec",    required_argument, 0, 'e'},
            {"interval",required_argument, 0, 'i'},
            {"help",       no_argument,   0, 'h'},
            {"daemon", no_argument,       0, 'd'},
          };
        int option_index = 0;

        c = getopt_long (argc, argv, "e:vVs:ai:dFfc", long_options, &option_index);

        if (c == -1)
          break;

        switch (c)
          {
          case 0:
            /* If this option set a flag, do nothing else now. */
            if (long_options[option_index].flag != 0)
              break;
            printf ("option %s", long_options[option_index].name);
            if (optarg)
              printf (" with arg %s", optarg);
            printf ("\n");
            break;

          case 'V': args.silent = 1; break;
          case 'v': args.verbose = 1; break;
          case 'e': args.onSuccess=optarg; break;
          case 's': args.expectedStatus=atoi(optarg); break;
          case 'f': args.fail = 1; break;
          case 'a': args.any = 1; break;
          case 'F': args.forever = 1; break;
          case 'c': args.change = 1; break;
          case 'i': args.interval = atoi(optarg); break;
          case 'd': args.daemonize = 1; break;
          case 'h': case '?': help(); break;
          default:
            abort ();
          }
      }

    if (!args.onSuccess && args.daemonize) {
      printf("--daemon is meaningless without --exec 'command'");
    }
    while (optind < argc)
      commands[totalCommands++] = argv[optind++];

    if (totalCommands == 0) help();
}

int main(int argc, char *argv[]){
  parse_args(argc, argv);
  if (args.daemonize) daemonize();

  int status = -1;
  int _exit = -1;
  FILE *fp;

  for(int i = 0; i < totalCommands; i = i + 1 ){
    out[i] = malloc(CHUNK_SIZE * sizeof(char));
  }

  while (1) {
    _exit = 1;
    for(int i = 0; i < totalCommands; i = i + 1 ){
      int *s = shell(i);
      status = s[0];
      int change = s[1];
      int done = ((args.fail && status != 0) || (!args.fail && status == args.expectedStatus));
      if(!args.silent) spin(commands[i], status == args.expectedStatus ? 2 : 1, i);
      syslog (LOG_NOTICE, "%d %s", status, commands[i]);
      if (done && args.any && !args.forever) break;
      if (!done) _exit = 0;
      fflush(stderr);
    }
    if (_exit == 1) {
      if (args.onSuccess) {
        for(int i = 0; i < totalCommands; i = i + 1) {
          char C[5];
          // sprintf(C, "\\%d", i+1);
          args.onSuccess = replace(C, out[i], args.onSuccess);
        }
        clean();
        system(args.onSuccess);
      }
      if (!args.forever) break; else msleep(args.interval);
    } else msleep(args.interval);
  }
  for(int i = 0; i < totalCommands; i = i + 1 ){
    free(out[i]);
  }
  closelog();
}
