Output demo/uc-03-docker-stack.mp4

Set Width 1100
Set Height 560
Set FontSize 22
Set Theme "IR_Black"
Set WindowBar "Colorful"
Set WindowBarSize 28
Set BorderRadius 12
Set Padding 28
Set Margin 40
Set MarginFill "#000000"

# --- setup (hidden): mock services that become ready after 6s ---
Set TypingSpeed 0ms
Hide
Type "mkdir -p /tmp/uc03bin"
Enter
Type "cat > /tmp/uc03bin/pg_isready << 'SCRIPT'"
Enter
Type "#!/bin/bash"
Enter
Type "test -f /tmp/.uc03pg && rm /tmp/.uc03pg && exit 0 || exit 1"
Enter
Type "SCRIPT"
Enter
Type "cat > /tmp/uc03bin/redis-cli << 'SCRIPT'"
Enter
Type "#!/bin/bash"
Enter
Type "test -f /tmp/.uc03redis && rm /tmp/.uc03redis && exit 0 || exit 1"
Enter
Type "SCRIPT"
Enter
Type "chmod +x /tmp/uc03bin/pg_isready /tmp/uc03bin/redis-cli"
Enter
Type "export PATH=/tmp/uc03bin:$PATH"
Enter
Type "rm -f /tmp/.uc03pg /tmp/.uc03redis"
Enter
Type "(sleep 6 && touch /tmp/.uc03pg /tmp/.uc03redis) &"
Enter
Show
Set TypingSpeed 55ms
Type "clear"
Enter
Sleep 300ms

# --- demo ---
Type "# docker compose up running. nothing is ready yet."
Enter
Sleep 800ms

Type "await 'pg_isready -h localhost' \\"
Enter
Type "  'redis-cli ping' \\"
Enter
Type "  --timeout 30 \\"
Enter
Type "  --exec 'python manage.py migrate && python manage.py runserver'"
Enter
Sleep 12s

# cleanup
Set TypingSpeed 0ms
Hide
Type "rm -rf /tmp/uc03bin /tmp/.uc03pg /tmp/.uc03redis"
Enter
Show
