#!/bin/sh
# Generate the await(1) man page from `await --help`, so the two never drift.
#
#   sh man/gen-man.sh path/to/await > await.1
#
# Dependency-free: POSIX sh, sed and awk. Everything (synopsis, options,
# examples, environment) is parsed from the help text, so options added to
# --help show up here without touching this script. SOURCE_DATE_EPOCH, when
# set, fixes the date for reproducible builds.
set -eu

bin=${1:-./await}

version=$("$bin" --version)
esc=$(printf '\033')
help=$(NO_COLOR=1 "$bin" --help | sed "s/${esc}\[[0-9;]*m//g")

if [ -n "${SOURCE_DATE_EPOCH:-}" ]; then
  date=$(date -u -d "@$SOURCE_DATE_EPOCH" +%Y-%m-%d 2>/dev/null ||
         date -u -r "$SOURCE_DATE_EPOCH" +%Y-%m-%d)
else
  date=$(date -u +%Y-%m-%d)
fi

printf '%s\n' "$help" | awk -v version="$version" -v date="$date" '
# troff-escape plain text: backslash, hyphen (so flags copy-paste as ASCII),
# quotes/backtick/caret/tilde (so they are not typeset as typographic glyphs)
# and a leading dot or apostrophe (which would start a request)
function esc(s,    out, i, c) {
  out = ""
  for (i = 1; i <= length(s); i++) {
    c = substr(s, i, 1)
    if (c == "\\") c = "\\e"
    else if (c == "-") c = "\\-"
    else if (c == "\047") c = "\\(aq"
    else if (c == "`") c = "\\(ga"
    else if (c == "^") c = "\\(ha"
    else if (c == "~") c = "\\(ti"
    out = out c
  }
  if (out ~ /^[.]/) out = "\\&" out
  return out
}
function trim(s) { sub(/^[ \t]+/, "", s); sub(/[ \t]+$/, "", s); return s }
# print filled prose, wrapped at word boundaries to keep source lines short
function text(s,    n, w, i, line) {
  n = split(s, w, /[ \t]+/)
  line = ""
  for (i = 1; i <= n; i++) {
    if (w[i] == "") continue
    if (line != "" && length(esc(line " " w[i])) > 78) { print esc(line); line = "" }
    line = line (line == "" ? "" : " ") w[i]
  }
  if (line != "") print esc(line)
}
function sentence(s) {
  s = toupper(substr(s, 1, 1)) substr(s, 2)
  if (s !~ /[.:!?]$/) s = s "."
  return s
}
# "[options] commands" -> "[\fIoptions\fR] \fIcommands\fR"
function synopsis_args(s,    out, w) {
  out = ""
  while (match(s, /[A-Za-z][A-Za-z0-9_]*/)) {
    w = substr(s, RSTART, RLENGTH)
    out = out esc(substr(s, 1, RSTART - 1)) "\\fI" w "\\fR"
    s = substr(s, RSTART + RLENGTH)
  }
  return out esc(s)
}

# --- collect ---------------------------------------------------------------
NR == 1 { synopsis = $0; next }
/^[A-Z][A-Z ]*:[ \t]*$/ { sec = $0; sub(/:.*/, "", sec); order[++nsec] = sec; next }
sec == "" {
  if ($0 ~ /^#/ && name_desc == "") { name_desc = trim(substr($0, 2)) }
  else if (trim($0) != "") { pre[++npre] = $0 }
  next
}
{ lines[sec, ++nl[sec]] = $0 }

# --- render ----------------------------------------------------------------
# Examples-style content: "# text" lines become paragraphs, indented lines
# (with tab continuations) become verbatim blocks
function render_block(sec,    i, l, inex, npara) {
  inex = 0
  for (i = 1; i <= nl[sec]; i++) {
    l = lines[sec, i]
    if (l ~ /^[ \t]/ && trim(l) != "") {
      if (!inex) { if (npara++) print ".PP"; print ".RS 4"; print ".nf"; inex = 1 }
      sub(/^  /, "", l); gsub(/\t/, "    ", l); sub(/[ \t]+$/, "", l)
      print esc(l)
      continue
    }
    if (inex) { print ".fi"; print ".RE"; inex = 0 }
    if (l ~ /^#/) { if (npara++) print ".PP"; text(sentence(trim(substr(l, 2)))) }
  }
  if (inex) { print ".fi"; print ".RE" }
}

function render_options(sec,    i, l, p, flags, desc, n, f, k, tag) {
  for (i = 1; i <= nl[sec]; i++) {
    l = lines[sec, i]
    if (trim(l) == "") continue
    if (l ~ /^[ \t]*-/) {
      p = index(l, "#")
      flags = trim(p ? substr(l, 1, p - 1) : l)
      desc = p ? trim(substr(l, p + 1)) : ""
      n = split(flags, f, /[ \t,]+/)
      tag = ""
      for (k = 1; k <= n; k++) tag = tag (k > 1 ? ", " : "") "\\fB" esc(f[k]) "\\fR"
      print ".TP"; print tag
      if (desc != "") text(sentence(desc))
    } else {
      # continuation of the previous option description
      l = trim(l); sub(/^#[ \t]*/, "", l)
      if (l != "") text(l)
    }
  }
}

# NOTES: groups of "# ..." lines (a line continues the previous one when it
# starts with "and", "or" or "(", or the previous one ends with "," or ";"). Groups that set environment variables (NAME=value) go
# to ENVIRONMENT, the rest (and indented examples) to DESCRIPTION.
function split_notes(sec,    i, l, t, g, s, v) {
  ng = 0
  for (i = 1; i <= nl[sec]; i++) {
    l = lines[sec, i]
    if (l ~ /^#/) {
      t = trim(substr(l, 2))
      if (ng > 0 && gkind[ng] == "text" && (t ~ /^(and|or)[ \t]/ || t ~ /^\(/ || gtext[ng] ~ /[,;]$/))
        gtext[ng] = gtext[ng] " " t
      else { ng++; gkind[ng] = "text"; gtext[ng] = t }
    } else if (trim(l) != "") {
      if (ng == 0 || gkind[ng] != "example") { ng++; gkind[ng] = "example"; gtext[ng] = "" }
      sub(/^  /, "", l); gsub(/\t/, "    ", l)
      gtext[ng] = gtext[ng] (gtext[ng] == "" ? "" : "\n") l
    }
  }
  for (g = 1; g <= ng; g++) {
    genv[g] = ""
    if (gkind[g] != "text") continue
    s = gtext[g]
    while (match(s, /[A-Z][A-Z0-9_]*=/)) {
      v = substr(s, RSTART, RLENGTH - 1)
      genv[g] = genv[g] (genv[g] == "" ? "" : " ") v
      s = substr(s, RSTART + RLENGTH)
    }
  }
}

END {
  print ".\\\" Generated from `await --help` by man/gen-man.sh; do not edit."
  print ".TH AWAIT 1 \"" date "\" \"await " version "\" \"User Commands\""
  print ".SH NAME"
  print "await \\- " esc(name_desc != "" ? name_desc : "run commands and wait for them")

  print ".SH SYNOPSIS"
  s = synopsis; sub(/^[ \t]*[^ \t]+[ \t]*/, "", s)
  print ".B await"
  if (s != "") print synopsis_args(s)

  print ".SH DESCRIPTION"
  print ".B await"
  text("runs each of the given shell commands in parallel, repeatedly. By default it exits once all of them return the expected status; options such as --change, --any and --forever change when it stops.")
  for (i = 1; i <= npre; i++) { print ".PP"; text(sentence(trim(pre[i]))) }
  if (("NOTES", 1) in lines) split_notes("NOTES")
  for (g = 1; g <= ng; g++) {
    if (genv[g] != "") continue
    if (gkind[g] == "example") {
      print ".PP"; print ".RS 4"; print ".nf"
      n = split(gtext[g], ex, "\n")
      for (k = 1; k <= n; k++) print esc(ex[k])
      print ".fi"; print ".RE"
    } else { print ".PP"; text(sentence(gtext[g])) }
  }

  for (j = 1; j <= nsec; j++) if (order[j] == "OPTIONS") { print ".SH OPTIONS"; render_options("OPTIONS") }

  env = 0
  for (g = 1; g <= ng; g++) {
    if (genv[g] == "") continue
    if (!env) { print ".SH ENVIRONMENT"; env = 1 }
    n = split(genv[g], vs, " ")
    tag = ""
    for (k = 1; k <= n; k++) tag = tag (k > 1 ? ", " : "") "\\fB" esc(vs[k]) "\\fR"
    print ".TP"; print tag
    t = gtext[g]
    text(sentence(t))
  }

  # any other section --help grows (EXAMPLES included) is rendered as prose
  # paragraphs and verbatim blocks
  for (j = 1; j <= nsec; j++) {
    if (order[j] == "OPTIONS" || order[j] == "NOTES") continue
    print ".SH " order[j]
    render_block(order[j])
  }

  print ".SH SEE ALSO"
  print ".BR watch (1),"
  print ".BR timeout (1)"
  print ".PP"
  print esc("https://github.com/slavaGanzin/await")
}
'
