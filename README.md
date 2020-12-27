# await
*24K small footprint single binary runs list of commands in parallel and waits for their termination*

# Installation
```bash
curl -L https://github.com/slavaGanzin/await/releases/download/0.9/await -o /usr/local/bin/await
chmod +x /usr/local/bin/await
```

# With await you can:
### Healthcheck FAANG
```bash
await 'whois facebook.com' \
      'nslookup apple.com' \
      'dig +short amazon.com' \
      'sleep 1 | telnet netflix.com 443 2>/dev/null' \
      'http google.com' --fail
```

<img src='demo/1.gif' width='100%'></img>


### Notify yourself when your site down
```bash
await "curl 'https://whatnot.ai' &>/dev/null && echo UP || echo DOWN" \
      --forever --change --exec "ntfy send 'whatnot.ai \1'"
```

<img src='demo/2.gif' width='100%'></img>

### Use as [concurrently](https://github.com/kimmobrunfeldt/concurrently)
```bash
await "stylus --watch --compress --out /home/vganzin/work/whatnot/front /home/vganzin/work/whatnot/front/index.styl" \
      "pug /home/vganzin/work/whatnot/front/index.pug --out /home/vganzin/work/whatnot/front --watch --pretty 2>/dev/null" --forever --stdout --silent
```

<img src='demo/3.gif' width='100%'></img>


### [Substitute while substituting](https://memegenerator.net/img/instances/48173775.jpg)
```bash
await 'echo -n 10' 'echo -n $RANDOM' 'expr \1 + \2' --exec 'echo \3' --forever --silent
```

<img src='demo/4.gif' width='100%'></img>

### Furiously wait for new iPhone in background process
```bash
await 'curl "https://www.apple.com/iphone/" -s | pup ".hero-eyebrow text{}" | grep -v 12' --interval 1000 --change --daemon --exec 'ntfy send "\1"'
```

<img src='demo/5.gif' width='100%'></img>


## --help
```bash
await [arguments] commands

# runs list of commands and waits for their termination

OPTIONS:
  --help	#print this help
  --stdout -o	#print stdout of commands
  --silent -V	#print nothing
  --fail -f	#waiting commands to fail
  --status -s	#expected status [default: 0]
  --any -a	#terminate if any of command return expected status
  --change -c	#waiting for stdout to change and ignore status codes
  --exec -e	#run some shell command on success;
  --interval -i	#milliseconds between one round of commands [default: 200]
  --forever -F	#do not exit ever


NOTES:
# \1, \2 ... \n - will be subtituted with n-th command stdout
# you can use stdout substitution in --exec and in commands itself:
  await 'echo -n 10' 'echo -n $RANDOM' 'expr \1 + \2' --exec 'echo \3' --forever --silent


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
  await "curl 'https://whatnot.ai' &>/dev/null && echo 'UP' || echo 'DOWN'" --forever --change\
    --exec "ntfy send \'whatnot.ai \1\'"

# waiting for new iPhone in daemon mode
  await 'curl "https://www.apple.com/iphone/" -s | pup ".hero-eyebrow text{}" | grep -v 12'\
 --change --interval 86400 --daemon --exec "ntfy send \1"

```

