# Ensure compinit is loaded
autoload -Uz compinit
compinit

# Define the completion function for 'await'
_await() {
  _arguments -s -S \
    '--help[Print this help]' \
    '--version[Display version]' \
    '--stdout[Print stdout of commands]' \
    '--no-stderr[Surpress stderr of commands by adding 2>/dev/null to commands]' \
    '--silent[Do not print spinners and commands]' \
    '--fail[Wait for commands to fail]' \
    '--status[Expected status (default: 0)]:status:' \
    '--any[Terminate if any command returns expected status]' \
    '--change[Wait for stdout to change and ignore status codes]' \
    '--diff[Highlight differences between previous and current output]' \
    '--exec[Run some shell command on success]:command:_command_names' \
    '--interval[Seconds between rounds of commands (default: 0.2)]:interval:' \
    '--timeout[Seconds to wait before giving up (default: 0)]:timeout:' \
    '--cmd-timeout[Seconds per command before killing it]:cmd-timeout:' \
    '--retry[Max number of attempts before giving up (default: 0 = unlimited)]:retry:' \
    '--forever[Do not exit ever]' \
    '--name[Label for the next command]:name:' \
    '--json[Output results as JSON on exit]' \
    '--lap[Show last run duration per command in spinner]' \
    '--watch[Equivalent to -fVodE (fail, silent, stdout, diff, no-stderr)]' \
    '--service[Create systemd user service with same parameters and activate it]:service name:'
}

# Register the completion function
compdef _await await
