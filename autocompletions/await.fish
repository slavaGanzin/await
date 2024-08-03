function __fish_await_no_subcommand
    set -l cmd (commandline -opc)
    for i in $cmd
        switch $i
            case --help --stdout --silent --fail --status --any --change --exec --interval --forever --service
                return 1
        end
    end
    return 0
end
complete -c await -n '__fish_await_no_subcommand' -l version -s v -d 'Print the version of await'
complete -c await -n '__fish_await_no_subcommand' -l help -d 'Print this help'
complete -c await -n '__fish_await_no_subcommand' -l stdout -s o -d 'Print stdout of commands'
complete -c await -n '__fish_await_no_subcommand' -l silent -s V -d 'Do not print spinners and commands'
complete -c await -n '__fish_await_no_subcommand' -l fail -s f -d 'Waiting commands to fail'
complete -c await -n '__fish_await_no_subcommand' -l status -s s -d 'Expected status [default: 0]' -r
complete -c await -n '__fish_await_no_subcommand' -l any -s a -d 'Terminate if any of command return expected status'
complete -c await -n '__fish_await_no_subcommand' -l change -s c -d 'Waiting for stdout to change and ignore status codes'
complete -c await -n '__fish_await_no_subcommand' -l exec -s e -d 'Run some shell command on success' -r
complete -c await -n '__fish_await_no_subcommand' -l interval -s i -d 'Milliseconds between one round of commands [default: 200]' -r
complete -c await -n '__fish_await_no_subcommand' -l forever -s F -d 'Do not exit ever'
complete -c await -n '__fish_await_no_subcommand' -l service -s S -d 'Create systemd user service with same parameters and activate it'

# For command completion
complete -c await -f -a '(__fish_complete_command)'
