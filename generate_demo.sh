#!/usr/bin/env fish

commandline --replace """
await \
  'dig +short facebook.com' \
  'nslookup apple.com' \
  'whois amazon.com' \
  'sleep 1 | telnet netflix.com 443 2>/dev/null' \
  'http google.com'
  """
