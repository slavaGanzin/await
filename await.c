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
//
// int strlen(char* str) {
// 	int ret = 0;
// 	while (str[ret]) {
// 		ret++;
// 	}
// 	return ret;
// }

/**
 * Replaces all found instances of the passed substring in the passed string.
 *
 * @param search The substring to look for
 * @param replace The substring with which to replace the found substrings
 * @param subject The string in which to look
 *
 * @return A new string with the search/replacement performed
 **/
char* str_replace(char* search, char* replace, char* subject) {
	int i, j, k;

	int searchSize = strlen(search);
	int replaceSize = strlen(replace);
	int size = strlen(subject);

	char* ret;

	if (!searchSize) {
		ret = malloc(size + 1);
		for (i = 0; i <= size; i++) {
			ret[i] = subject[i];
		}
		return ret;
	}

	int retAllocSize = (strlen(subject) + 1) * 2; // Allocation size of the return string.
    // let the allocation size be twice as that of the subject initially
	ret = malloc(retAllocSize);

	int bufferSize = 0; // Found characters buffer counter
	char* foundBuffer = malloc(searchSize); // Found character bugger

	for (i = 0, j = 0; i <= size; i++) {
		/**
         * Double the size of the allocated space if it's possible for us to surpass it
		 **/
		if (retAllocSize <= j + replaceSize) {
			retAllocSize *= 2;
			ret = (char*) realloc(ret, retAllocSize);
		}
		/**
         * If there is a hit in characters of the substring, let's add it to the
         * character buffer
		 **/
		else if (subject[i] == search[bufferSize]) {
			foundBuffer[bufferSize] = subject[i];
			bufferSize++;

			/**
             * If the found character's bugger's counter has reached the searched substring's
             * length, then there's a hit. Let's copy the replace substring's characters
             * onto the return string.
			 **/
			if (bufferSize == searchSize) {
				bufferSize = 0;
				for (k = 0; k < replaceSize; k++) {
					ret[j++] = replace[k];
				}
			}
		}
		/**
         * If the character is a miss, let's put everything back from the buffer
         * to the return string, and set the found character buffer counter to 0.
		 **/
		else {
			for (k = 0; k < bufferSize; k++) {
				ret[j++] = foundBuffer[k];
			}
			bufferSize = 0;
			/**
             * Add the current character in the subject string to the return string.
			 **/
			ret[j++] = subject[i];
		}
	}

	/**
	 * Free memory
	 **/
	free(foundBuffer);

	return ret;
}

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
int spinnerI[10];
char *commands[10];
int expectedStatus = 0;
int any = 0;
static int silent = 0;
static int forever = 0;
static int daemonize = 0;
static int interval = 200;
static int fail = 0;
static int verbose = 0;
static char *onSuccess;
int totalCommands = 0;
char out[10][1 * 1024 * 1024];

void spin(char *command, int color, int i) {
  if (!spinnerI[i] || spinnerI[i] == 0) spinnerI[i] = 7;
  fprintf(stderr, "\033[%dA\r\033[K\033[0;3%dm%s\033[0m %s\033[%dB\r", i, color, spinner[spinnerI[i]--], command, i);
  fflush(stderr);
}

int shell(char *command, char *out) {
  FILE *fp;
  int status;
  char buf[2024];
  char _command[2024];
  strcpy(_command, command);
  strcat(_command, " 2>&1");

  fflush(stdout);
  fflush(stderr);
  fp = popen(_command, "r");
  if (fp == NULL) /* Handle error */;
  while (fgets(buf, 2024, fp) !=NULL) {
    sprintf(out, "%s%s", out, buf);
    if (!silent && verbose) printf("\n\n%s", buf);
  }

  status = pclose(fp);

  return WEXITSTATUS(status);
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
  "  --exec -e\trun some shell command on success; $1, $2 ... $n - will be subtituted nth command stdout\n"
  "  --interval -i\tmilliseconds between one round of commands\n"
  "  --no-exit -E\tdo not exit\n"
  "\n"
  "EXAMPLES:\n"
  "# waiting google (or your internet connection) to fail\n"
  "  await 'curl google.com --fail'\n\n"
  "# definitely waiting google to fail (https://ec.haxx.se/usingcurl/usingcurl-returns)\n"
  "  await 'curl google.com --status 7\n\n"
  "# waits for redis socket and then connects to\n"
  "  await 'socat -u OPEN:/dev/null UNIX-CONNECT:/tmp/redis.sock' --exec 'redis-cli -s /tmp/redis.sock'\n\n"
  "# daily checking am I on french reviera. Just in case\n"
  "  await 'curl https://ipapi.co/json 2>/dev/null | jq .city | grep Nice' --interval 86400\n\n"
  "# Yet another http monitor\n"
  "  await 'curl https://whatnot.ai --fail --exec \"ntfy send \\'whatnot.ai down\\'\"'\n\n"
  "# waiting for pup's author new blog post\n"
  "  await 'mv /tmp/eric.new /tmp/eric.old &>/dev/null; http \"https://ericchiang.github.io/\" | pup \"a attr{href}\" > /tmp/eric.new; diff /tmp/eric.new /tmp/eric.old' --fail --exec 'ntfy send \"new article $1\"'\n\n"
);
  exit(0);
}

void clean() {
  for(int i = 0; i < totalCommands; i = i + 1 ){
    fprintf(stderr, "\033[%dA\r\033[K\r", i, i);
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
          {"forever", no_argument,       0, 'F'},
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

      c = getopt_long (argc, argv, "e:vVs:ai:dFf", long_options, &option_index);

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
        case 'F': forever = 1; break;
        case 'i': interval = atoi(optarg); break;
        case 'h': help(); break;
        case 'd':
          daemonize = 1;
          break;
        case '?':
          /* getopt_long already printed an error message. */
          break;

        default:
          abort ();
        }
    }

  if (!onSuccess && daemonize) {
    printf("--daemon is meaningless without --exec 'command'");
  }
  while (optind < argc)
    commands[totalCommands++] = argv[optind++];

  if (totalCommands == 0) help();

  if (daemonize) skeleton_daemon();

  int status = -1;
  int exit = -1;
  FILE *fp;

  while (1) {
    exit = 1;
    for(int i = 0; i < totalCommands; i = i + 1 ){
      status = shell(commands[i], out[i]);
      int done = ((fail && status != 0) || (!fail && status == expectedStatus));

      if(!silent) spin(commands[i], status == expectedStatus ? 2 : 1, i);
      // if (daemonize)
      syslog (LOG_NOTICE, "%d %s", status, commands[i]);
      if (done && any && !forever) break;
      if (!done) exit = 0;
      // fflush(stderr);
    }
    if (exit == 1) {
      if (onSuccess) {
        clean();
        for(int i = 0; i < totalCommands; i = i + 1 ){
          char *replace;
          sprintf(replace, "$%d", i+1);
          onSuccess  = str_replace(replace, out[i], onSuccess);
        }
        system(onSuccess);
      }
      if (!forever) break; else msleep(interval);
    } else msleep(interval);
  }
  closelog();
}
