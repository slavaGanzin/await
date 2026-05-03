Output demo/uc-06-site-monitor.mp4

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
Set TypingSpeed 55ms

Sleep 300ms

Type "# Set it and forget it. Fires the moment your site goes down."
Enter
Sleep 800ms

Type "await 'curl -sf https://await.beer' \\"
Enter
Type "  --forever \\"
Enter
Type "  --fail \\"
Enter
Type "  --exec 'ntfy send await.beer is down'"
Enter
Sleep 6s
Ctrl+C
Sleep 800ms
