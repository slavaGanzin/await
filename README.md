# await
```bash
await [arguments] commands

awaits successfull execution of all shell commands

ARGUMENTS:
  --help	print this help
  --verbose -v	increase verbosity
  --silent -V	print nothing
  --fail -f	waiting commands to fail
  --status -s	expected status [default: 0]
  --any -a	terminate if any of command return expected status
  --exec -e	run some shell command on success

EXAMPLES:
  await 'curl google.com --status 7'	# waiting google to fail
  await 'date +%b | grep October' --exec 'xdg-open https://www.youtube.com/watch?v=NU9JoFKlaZ0'	# waits till september ends
  await 'socat -u OPEN:/dev/null UNIX-CONNECT:/tmp/redis.sock'; redis-cli -s /tmp/redis.sock # waits for redis socket and then connects to
  await 'curl https://ipapi.co/json 2>/dev/null | jq .city | grep Nice' # waiting for my vacation on french reviera

```

