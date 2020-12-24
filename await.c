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

static void skeleton_daemon()
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
    signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP, SIG_IGN);

    /* Fork off for the second time*/
    pid = fork();

    if (pid < 0) exit(EXIT_FAILURE);

    /* Success: Let the parent terminate */
    if (pid > 0) exit(EXIT_SUCCESS);

    /* Set new file permissions */
    // umask(0);

    /* Close all open file descriptors */
    // int x;
    // for (x = sysconf(_SC_OPEN_MAX); x>=0; x--) {
    //     close (x);
    // }

    /* Open the log file */
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
int spinnerI = 0;
int expectedStatus = 0;
int any = 0;
static int silent = 0;
static int daemonize = 0;
static int interval = 200;
static int fail = 0;
static int verbose = 0;
static char *onSuccess;
#include <sys/wait.h>

void spin(char *command, int color, int i) {
  if (spinnerI == 0) spinnerI = 7;
  fprintf(stderr, "\033[%dB\r\033[K\033[0;3%dm%s\033[0m %s\033[%dA\r", i, color, spinner[spinnerI--], command, i);

  fflush(stderr);
}

int shell(char *command) {
  FILE *fp;
  int status;
  char path[2024];
  char _command[2024];
  strcpy(_command, command);
  strcat(_command, " 2>&1");

  fflush(stdout);
  fflush(stderr);
  fp = popen(_command, "r");
  if (fp == NULL) /* Handle error */;
    while (fgets(path, 2024, fp) !=NULL)
      if (!silent && verbose) printf("\n\n%s", path);

  status = WEXITSTATUS(pclose(fp));

  return status;
}


void help() {
  printf("await [arguments] commands\n\n"
  "awaits successfull execution of all shell commands\n"
  "\nARGUMENTS:\n"
  "  --help\tprint this help\n"
  "  --verbose -v\tincrease verbosity\n"
  "  --silent -V\tprint nothing\n"
  "  --fail -f\twaiting commands to fail\n"
  "  --status -s\texpected status [default: 0]\n"
  "  --any -a\tterminate if any of command return expected status\n"
  "  --exec -e\trun some shell command on success\n"
  "  --interval -i\tmilliseconds between one round of commands\n"
  "  --forever -F\tdo not exit on success\n"
  "\n"
  "EXAMPLES:\n"
  "  await 'curl google.com --fail'\t# waiting google to fail (or your internet)\n"
  "  await 'curl google.com --status 7'\t# definitely waiting google to fail (https://ec.haxx.se/usingcurl/usingcurl-returns)\n"
  "  await 'socat -u OPEN:/dev/null UNIX-CONNECT:/tmp/redis.sock' --exec 'redis-cli -s /tmp/redis.sock' # waits for redis socket and then connects to\n"
  "  await 'curl https://ipapi.co/json 2>/dev/null | jq .city | grep Nice' # waiting for my vacation on french reviera\n"
  "  await 'http \"https://ericchiang.github.io/\" | pup 'a attr{href}' --change # waiting for pup's author new blog post\n"

);
  exit(0);
}

int totalCommands = 0;
void clean() {
  for(int i = 0; i < totalCommands; i = i + 1 ){
    fprintf(stderr, "\033[%dB\r\033[K\r", i, i);
  }
  fflush(stderr);
}

int main(int argc, char *argv[]){
  int c;

  while (1)
    {
      static struct option long_options[] =
        {
          /* These options set a flag. */
          {"verbose", no_argument,       0, 'v'},
          {"silent",  no_argument,       0, 'V'},
          {"any",    no_argument,        0, 'a'},
          {"fail",    no_argument,       0, 'f'},
          {"status",  required_argument, 0, 's'},
          /* These options don’t set a flag.
             We distinguish them by their indices. */
          {"exec",    required_argument, 0, 'e'},
          {"interval",required_argument, 0, 'i'},
          {"help",       no_argument,       0, 'h'},
          {"daemon", no_argument,       0, 'd'},
          // {"delete",  required_argument, 0, 'd'},
          // {"create",  required_argument, 0, 'c'},
          // {"file",    required_argument, 0, 'f'},
          {0,0}
        };
      /* getopt_long stores the option index here. */
      int option_index = 0;

      c = getopt_long (argc, argv, "e:vVs:afi:d", long_options, &option_index);

      /* Detect the end of the options. */
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

        case 'V': silent = 1; break;
        case 'v': verbose = 1; break;
        case 'e': onSuccess=optarg; break;
        case 's': expectedStatus=atoi(optarg); break;
        case 'f': fail = 1; break;
        case 'a': any = 1; break;
        case 'i': interval = atoi(optarg); break;
        case 'h': help(); break;
        case 'd':
          if (!onSuccess) {
            printf("--exec 'command' is mandatory with --daemon");
            exit(1);
          }
          daemonize = 1;
          break;
        case '?':
          /* getopt_long already printed an error message. */
          break;

        default:
          abort ();
        }
    }

  char *commands[100];
  while (optind < argc)
    commands[totalCommands++] = argv[optind++];

  if (totalCommands == 0) help();
  clean();

  if (daemonize) skeleton_daemon();

  int status = -1;
  int exit = -1;
  char path[2024];
  FILE *fp;

  if (daemonize) syslog (LOG_NOTICE, "started");
  while (1) {
    exit = 0;
    for(int i = 0; i < totalCommands; i = i + 1 ){
      status = shell(commands[i]);
      int done = (status == expectedStatus || fail && status != 0);

      if(!silent) spin(commands[i], done ? 2 : 1, i);
      if (done && any) break;
      if (!done) exit = -1;
      fflush(stderr);
    }
    if (exit > -1) {
      clean();
      if (onSuccess) system(onSuccess);
      break;
    } else msleep(interval);
  }
  if (daemonize) syslog (LOG_NOTICE, "terminated");
  closelog();
}
