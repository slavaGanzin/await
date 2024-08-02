_await() {
    local cur prev opts
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"

    opts="--help --stdout -o --silent -V --fail -f --status -s --any -a --change -c --exec -e --interval -i --forever -F --service -S"

    case "${prev}" in
        --status|-s|--exec|-e|--interval|-i)
            COMPREPLY=($(compgen -f -- "${cur}"))
            return 0
            ;;
    esac

    if [[ ${cur} == -* ]]; then
        COMPREPLY=($(compgen -W "${opts}" -- "${cur}"))
        return 0
    fi

    COMPREPLY=($(compgen -c -- "${cur}"))
    return 0
}

complete -F _await await
