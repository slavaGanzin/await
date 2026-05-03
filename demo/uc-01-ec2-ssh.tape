Output demo/uc-01-ec2-ssh.mp4

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

# --- setup (hidden) ---
Set TypingSpeed 0ms
Hide
Type "rm -f /tmp/.uc01; (sleep 5 && touch /tmp/.uc01) &"
Enter
Show
Set TypingSpeed 55ms
Type "clear"
Enter
Sleep 300ms

# --- demo ---
Type "# EC2 just provisioned. SSH keeps saying Connection refused."
Enter
Sleep 800ms

Type "await 'test -f /tmp/.uc01' \\"
Enter
Type "  --cmd-timeout 3 \\"
Enter
Type "  --exec 'echo Connected: ubuntu@54.172.14.91'"
Enter
Sleep 9s

# cleanup
Set TypingSpeed 0ms
Hide
Type "rm -f /tmp/.uc01"
Enter
Show
