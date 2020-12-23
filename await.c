#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <regex.h>
#include <unistd.h>
#include <getopt.h>

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
int race = 0;
static int silent = 0;
static int verbose = 0;
static char *onSuccess;
#include <sys/wait.h>

void spin(char *command, int color){
  if (spinnerI == 0) spinnerI = 7;
  fprintf(stderr, "\r                                                                                                                                                         \r");
  fprintf(stderr, "\033[0;3%dm%s\033[0m %s", color, spinner[spinnerI--], command);

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
      if (!silent && verbose) printf("%s", path);

  status = WEXITSTATUS(pclose(fp));

  if(status == expectedStatus && onSuccess) {
    fp = popen(onSuccess, "r");
    if (!silent) while (fgets(path, 2024, fp) !=NULL) printf("%s", path);
    pclose(fp);
  }

  return status;
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
          {"race",    no_argument,       0, 'r'},
          {"status",  required_argument, 0, 's'},
          /* These options don’t set a flag.
             We distinguish them by their indices. */
          {"command",    required_argument, 0, 'c'},
          // {"append",  no_argument,       0, 'b'},
          // {"delete",  required_argument, 0, 'd'},
          // {"create",  required_argument, 0, 'c'},
          // {"file",    required_argument, 0, 'f'},
          {0,0}
        };
      /* getopt_long stores the option index here. */
      int option_index = 0;

      c = getopt_long (argc, argv, "c:vVs:r", long_options, &option_index);

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
        case 'c': onSuccess=optarg; break;
        case 's': expectedStatus=atoi(optarg); break;
        case 'r': race = 1; break;
        case '?':
          /* getopt_long already printed an error message. */
          break;

        default:
          abort ();
        }
    }

  char *commands[100];
  int totalCommands = 0;
  while (optind < argc)
    commands[totalCommands++] = argv[optind++];

  int status = -1;
  int exit = -1;
  while (exit == -1) {
    exit = 0;
    for(int i = 0; i < totalCommands; i = i + 1 ){
      status = shell(commands[i]);
      if(!silent) spin(commands[i], status != expectedStatus ? 1 : 2);
      if (status != expectedStatus) exit = -1;
      msleep(200);
      fflush(stderr);
      if (status == expectedStatus && race) break;
    }
  }
  fprintf(stderr, "\r                                                                                                                                                         \r");
  fflush(stderr);
}
