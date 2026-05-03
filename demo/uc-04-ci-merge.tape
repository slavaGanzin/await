Output demo/uc-04-ci-merge.mp4

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

# --- setup (hidden): mock gh that shows CI running 3x then passes ---
Set TypingSpeed 0ms
Hide
Type "cat > /tmp/gh << 'SCRIPT'"
Enter
Type "#!/bin/bash"
Enter
Type "c=/tmp/.uc04cnt"
Enter
Type "n=$(cat $c 2>/dev/null || echo 0)"
Enter
Type "n=$((n+1))"
Enter
Type "echo $n > $c"
Enter
Type "[ $n -ge 4 ] && rm $c && echo 'run completed with success' && exit 0"
Enter
Type "echo 'run in progress...'"
Enter
Type "exit 1"
Enter
Type "SCRIPT"
Enter
Type "chmod +x /tmp/gh"
Enter
Type "rm -f /tmp/.uc04cnt"
Enter
Type "export PATH=/tmp:$PATH"
Enter
Show
Set TypingSpeed 55ms
Type "clear"
Enter
Sleep 300ms

# --- demo ---
Type "# Pushed at 6pm. CI takes 20 min. Close the laptop."
Enter
Sleep 800ms

Type "await 'gh run view --exit-status' \\"
Enter
Type "  --cmd-timeout 10 \\"
Enter
Type "  --timeout 300 \\"
Enter
Type "  --exec 'gh pr merge --auto --squash'"
Enter
Sleep 11s

# cleanup
Set TypingSpeed 0ms
Hide
Type "rm -f /tmp/gh /tmp/.uc04cnt"
Enter
Show
