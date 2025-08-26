_await() {
    local cur prev opts
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"

    opts="--help --stdout --silent --fail --status --any --change --diff --exec --interval --forever --service --version --no-stderr"

    case "${prev}" in
        --status|--exec|--interval)
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
