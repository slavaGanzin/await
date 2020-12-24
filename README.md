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
  --interval -i	milliseconds between one round of commands

EXAMPLES:
  await 'curl google.com --fail'	# waiting google to fail (or your internet)
  await 'curl google.com --status 7'	# definitely waiting google to fail (https://ec.haxx.se/usingcurl/usingcurl-returns)
  await 'socat -u OPEN:/dev/null UNIX-CONNECT:/tmp/redis.sock' --exec 'redis-cli -s /tmp/redis.sock' # waits for redis socket and then connects to
  await 'curl https://ipapi.co/json 2>/dev/null | jq .city | grep Nice' # waiting for my vacation on french reviera
  await 'http "https://ericchiang.github.io/" | pup 'a attr{href}' --change # waiting for pup's author new blog post

```

