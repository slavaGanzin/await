Output demo/uc-02-k8s-pod.mp4

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

# --- setup (hidden): create mock kubectl that prints Pending 4x then Running ---
Set TypingSpeed 0ms
Hide
Type "cat > /tmp/kubectl << 'SCRIPT'"
Enter
Type "#!/bin/bash"
Enter
Type "c=/tmp/.uc02cnt"
Enter
Type "n=$(cat $c 2>/dev/null || echo 0)"
Enter
Type "n=$((n+1))"
Enter
Type "echo $n > $c"
Enter
Type "[ $n -ge 5 ] && rm $c && echo Running && exit 0"
Enter
Type "echo Pending"
Enter
Type "exit 1"
Enter
Type "SCRIPT"
Enter
Type "chmod +x /tmp/kubectl"
Enter
Type "rm -f /tmp/.uc02cnt"
Enter
Type "export PATH=/tmp:$PATH"
Enter
Show
Set TypingSpeed 55ms
Type "clear"
Enter
Sleep 300ms

# --- demo ---
Type "# Pod is Pending. Need to port-forward once it's Running."
Enter
Sleep 800ms

Type "await 'kubectl get pod myapp -o jsonpath={.status.phase} | grep Running' \\"
Enter
Type "  --cmd-timeout 5 \\"
Enter
Type "  --timeout 120 \\"
Enter
Type "  --exec 'kubectl port-forward pod/myapp 8080:80'"
Enter
Sleep 14s

# cleanup
Set TypingSpeed 0ms
Hide
Type "rm -f /tmp/kubectl /tmp/.uc02cnt"
Enter
Show
