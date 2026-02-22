Output demo-placeholders.mp4

Set Width 1000
Set Height 600
Set FontSize 26
Set Theme "IR_Black"
Set WindowBar "Colorful"
Set BorderRadius 10
Set Padding 20
Set Margin 40
Set MarginFill "#000000"

Type "cd /Users/slava/work/await"
Enter
Sleep 300ms

Type "./await 'echo 15' 'echo 27' 'expr \\1 + \\2' --exec 'echo Sum: \\3'"
Enter
Sleep 2s
