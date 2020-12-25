# await
```bash
await [arguments] commands

runs list of commands and waits for their termination

ARGUMENTS:
  --help	#print this help
  --verbose -v	#increase verbosity
  --silent -V	#print nothing
  --fail -f	#waiting commands to fail
  --status -s	#expected status [default: 0]
  --any -a	#terminate if any of command return expected status
  --exec -e	#run some shell command on success; $1, $2 ... $n - will be subtituted nth command stdout
  --interval -i	#milliseconds between one round of commands [default: 200]
  --no-exit -E	#do not exit

EXAMPLES:
# waiting google (or your internet connection) to fail
  await 'curl google.com' --fail

# waiting only google to fail (https://ec.haxx.se/usingcurl/usingcurl-returns)
  await 'curl google.com' --status 7

# waits for redis socket and then connects to
  await 'socat -u OPEN:/dev/null UNIX-CONNECT:/tmp/redis.sock' --exec 'redis-cli -s /tmp/redis.sock'

# lazy version
  await 'ls /tmp/redis.sock'; redis-cli -s /tmp/redis.sock

# daily checking if I am on french reviera. Just in case
  await 'curl https://ipapi.co/json 2>/dev/null | jq .city | grep Nice' --interval 86400

# Yet another server monitor
  await 'http https://whatnot.ai' --forever --fail --exec "ntfy send \'whatnot.ai down\'"'

```

