# Ensure compinit is loaded
autoload -Uz compinit
compinit

# Define the completion function for 'await'
_await() {
  _arguments -s -S \
    '--help[Print this help]' \
    '--stdout[Print stdout of commands]' \
    '-o[Print stdout of commands]' \
    '--silent[Do not print spinners and commands]' \
    '-V[Do not print spinners and commands]' \
    '--fail[Wait for commands to fail]' \
    '-f[Wait for commands to fail]' \
    '--status[Expected status (default: 0)]::status' \
    '-s[Expected status (default: 0)]::status' \
    '--any[Terminate if any command returns expected status]' \
    '-a[Terminate if any command returns expected status]' \
    '--change[Wait for stdout to change and ignore status codes]' \
    '-c[Wait for stdout to change and ignore status codes]' \
    '--exec[Run some shell command on success]::command:_command_names' \
    '-e[Run some shell command on success]::command:_command_names' \
    '--interval[Milliseconds between rounds of commands (default: 200)]::interval' \
    '-i[Milliseconds between rounds of commands (default: 200)]::interval' \
    '--forever[Do not exit ever]' \
    '-F[Do not exit ever]' \
    '--service[Create systemd user service with same parameters and activate it]' \
    '-S[Create systemd user service with same parameters and activate it]'
}

# Register the completion function
compdef _await await
