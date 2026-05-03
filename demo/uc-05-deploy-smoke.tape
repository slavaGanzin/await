Output demo/uc-05-deploy-smoke.mp4

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

# --- setup (hidden): start http server with /health after 5s ---
Set TypingSpeed 0ms
Hide
Type "pkill -f 'python3.*http.server 13001' 2>/dev/null; true"
Enter
Type "mkdir -p /tmp/uc05 && echo ok > /tmp/uc05/health"
Enter
Type "(sleep 5 && python3 -m http.server 13001 --directory /tmp/uc05 &>/dev/null) &"
Enter
Show
Set TypingSpeed 55ms
Type "clear"
Enter
Sleep 300ms

# --- demo ---
Type "# Deployed to staging. Takes 2 min to propagate. Don't sleep 120."
Enter
Sleep 800ms

Type "await 'curl -sf localhost:13001/health' \\"
Enter
Type "  --cmd-timeout 5 \\"
Enter
Type "  --timeout 120 \\"
Enter
Type "  --exec 'pytest tests/smoke/ -x'"
Enter
Sleep 11s

# cleanup
Set TypingSpeed 0ms
Hide
Type "pkill -f 'python3.*http.server 13001' 2>/dev/null; rm -rf /tmp/uc05; true"
Enter
Show
