complete -c await -l version -s v -d 'Print the version of await'
complete -c await -l help -d 'Print this help'
complete -c await -l stdout -s o -d 'Print stdout of commands'
complete -c await -l silent -s V -d 'Do not print spinners and commands'
complete -c await -l fail -s f -d 'Waiting commands to fail'
complete -c await -l status -s s -d 'Expected status [default: 0]' -r
complete -c await -l any -s a -d 'Terminate if any of command return expected status'
complete -c await -l change -s c -d 'Waiting for stdout to change and ignore status codes'
complete -c await -l diff -s d -d 'Highlight differences between previous and current output'
complete -c await -l exec -s e -d 'Run some shell command on success' -r
complete -c await -l interval -s i -d 'Seconds between one round of commands [default: 0.2]' -r
complete -c await -l timeout -s T -d 'Seconds to wait before giving up [default: 0]' -r
complete -c await -l cmd-timeout -s t -d 'Seconds per command before killing it' -r
complete -c await -l retry -s r -d 'Max number of attempts before giving up [default: 0 (unlimited)]' -r
complete -c await -l forever -s F -d 'Do not exit ever'
complete -c await -l name -s n -d 'Label for the next command (usable as \\name in --exec)' -r
complete -c await -l json -s j -d 'Output results as JSON on exit'
complete -c await -l lap -s l -d 'Show last run duration per command in spinner'
complete -c await -l service -s S -d 'Create systemd user service with same parameters and activate it'
complete -c await -l no-stderr -s E -d 'Surpress stderr of commands by adding 2>/dev/null to commands'
complete -c await -l watch -s w -d 'Equivalent to -fVodE (fail, silent, stdout, diff, no-stderr)'

# For command completion
complete -c await -f -a '(__fish_complete_command)'
