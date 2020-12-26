#!/usr/bin/env fish

echo """
await \\
  'dig +short facebook.com' \\
  'nslookup apple.com' \\
  'whois amazon.com' \\
  'sleep 1 | telnet netflix.com 443 2>/dev/null' \\
  'http google.com'
""" | xclip

terminalizer record demo1 --skip-sharing --command fish --config demoTemplate
xdotool key --clearmodifiers ctrl+l
