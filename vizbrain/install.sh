#!/bin/bash
# vizbrain installer. vizbrain is shared by invitation: each person gets their
# own install link (email Kevin for yours) and pastes one line in Terminal:
#
#   bash -c "$(curl -sSL <your install link>)"
#
# Safe to run again. `vizbrain update` runs it with --update, which skips the
# questions and the key. Nothing here needs an admin password.
set -euo pipefail

VERSION="1.3.1"
PYTHON_VERSION="3.14"          # the version constraints.txt was made with
# vizbrain installs from a git tag of this repo; no GitHub release is published.
REF="vizbrain-v$VERSION"
# A local path or another URL can stand in for either one (used to test a build).
SOURCE="${VIZBRAIN_SOURCE:-vizbrain @ https://github.com/kpow/vizpow/archive/refs/tags/$REF.tar.gz#subdirectory=vizbrain}"
CONSTRAINTS="${VIZBRAIN_CONSTRAINTS:-https://raw.githubusercontent.com/kpow/vizpow/$REF/vizbrain/constraints.txt}"
# The install link this script came from. The site that serves the script
# fills it in, and `vizbrain update` goes back to it.
VIZBRAIN_LINK="${VIZBRAIN_LINK:-}"
MODE="${1:-install}"

say() { printf '\n\033[1m%s\033[0m\n' "$*"; }
die() { printf '\n%s\n' "$*" >&2; exit 1; }
fetch() { case "$1" in http*) curl -fsSL "$1" -o "$2" ;; *) cp "$1" "$2" ;; esac; }

# The vizbrain command lives in ~/.local/bin, and a new Terminal window only
# finds it if a shell startup file puts that directory on PATH. Step 2 below
# puts it on *this* process's PATH, which is why `uv tool update-shell` is no
# help here: it looks at PATH, sees the directory, and writes nothing.
PATH_LINE='export PATH="$HOME/.local/bin:$PATH"'

# The startup file a new Terminal window reads, for the login shell this Mac uses.
shell_profile() {
  case "${SHELL:-/bin/zsh}" in
    */zsh)  printf '%s\n' "$HOME/.zshrc" ;;
    */bash) printf '%s\n' "$HOME/.bash_profile" ;;
  esac
}

# A fresh login shell, started without this process's PATH, the way Terminal
# starts one. zsh reads .zshrc only when interactive, hence -i.
new_terminal_finds_vizbrain() {
  PATH=/usr/bin:/bin:/usr/sbin:/sbin "${SHELL:-/bin/zsh}" -lic \
    'command -v vizbrain' </dev/null >/dev/null 2>&1
}

# Put ~/.local/bin on the PATH of future Terminal windows, once. Returns
# non-zero if a new window still wouldn't find vizbrain, so the caller can say
# what to do instead of promising something that isn't true.
ensure_path() {
  profile="$(shell_profile)"
  if [ -n "$profile" ] && ! grep -q '\.local/bin' "$profile" 2>/dev/null; then
    if [ -s "$profile" ] && [ -n "$(tail -c 1 "$profile")" ]; then printf '\n' >> "$profile"; fi
    printf '\n# Added by the vizbrain installer: where the vizbrain command lives.\n%s\n' \
      "$PATH_LINE" >> "$profile"
  fi
  new_terminal_finds_vizbrain
}

# Just the PATH step and nothing else (tests/test_install.py).
[ "$MODE" = "--ensure-path" ] && { ensure_path; exit $?; }

# 1. Can this Mac run it?
[ "$(uname -s)" = "Darwin" ] || die "vizbrain runs on a Mac."
[ "$(sysctl -n hw.optional.arm64 2>/dev/null || echo 0)" = "1" ] ||
  die "vizbrain needs a Mac with Apple silicon (M1 or newer). This Mac has an Intel chip."
[ "$(uname -m)" = "arm64" ] ||
  die "This Terminal is running under Rosetta. In Finder, select Terminal, choose File > Get Info, untick \"Open using Rosetta\", then open a new Terminal window and try again."
macos="$(sw_vers -productVersion)"
[ "${macos%%.*}" -ge 14 ] || die "vizbrain needs macOS 14 (Sonoma) or newer. This Mac has macOS $macos."
free_gb=$(( $(df -k "$HOME" | awk 'NR==2 {print $4}') / 1048576 ))
[ "$free_gb" -ge 4 ] || die "vizbrain needs about 4 GB of free space. This Mac has $free_gb GB free."

# 2. uv installs vizbrain and brings its own Python, so nothing else is needed.
export PATH="$HOME/.local/bin:$PATH"
if ! command -v uv >/dev/null 2>&1; then
  say "Installing uv (the tool that installs vizbrain)..."
  curl -LsSf https://astral.sh/uv/install.sh | sh
fi
command -v uv >/dev/null 2>&1 || die "uv didn't install. Open a new Terminal window and run this installer again."

# 3. vizbrain itself, at the dependency versions it has been run with.
say "Installing vizbrain $VERSION (about 350 MB to download)..."
tmp="$(mktemp -d)"
fetch "$CONSTRAINTS" "$tmp/constraints.txt" || die "Couldn't download vizbrain $VERSION. Check the internet connection and try again."
uv tool install --force --python "$PYTHON_VERSION" --constraints "$tmp/constraints.txt" --from "$SOURCE" vizbrain
VIZBRAIN="$(uv tool dir --bin)/vizbrain"
[ -x "$VIZBRAIN" ] || die "vizbrain didn't install. Scroll up for the error and send it to Kevin."
if [ -n "$VIZBRAIN_LINK" ]; then
  mkdir -p "$HOME/Library/Application Support/vizbrain"
  (umask 077; printf '%s\n' "$VIZBRAIN_LINK" > "$HOME/Library/Application Support/vizbrain/install-link")
fi

if [ "$MODE" = "--update" ]; then
  "$VIZBRAIN" setup --defaults        # new models if any; restarts the running copy
  ensure_path || true                 # silent if a new Terminal window already finds him
  say "vizbrain is up to date ($("$VIZBRAIN" --version))."
  exit 0
fi

# 4. A few questions, then his voice and ears (about 800 MB, once).
say "Setting up"
"$VIZBRAIN" setup

# 5. The Claude API key, kept in this Mac's Keychain.
if ! security find-generic-password -s vizbrain -a anthropic >/dev/null 2>&1; then
  say "The Claude API key"
  echo "Paste the key you were sent and press Return. Nothing shows as you paste; that's normal."
  "$VIZBRAIN" set-key || echo "No key saved. Add it any time with: vizbrain set-key"
fi

# 6. Run at login, and start now.
say "Starting vizbrain"
"$VIZBRAIN" install-service
path_ok=yes
ensure_path || path_ok=no

# 7. Check everything, so anything left to fix is on screen now.
say "Checking everything"
"$VIZBRAIN" doctor || true

say "Done. Tap the front of vizBot's head and say hello."
echo "His page on this Mac: http://localhost:4050"
if [ "$path_ok" = yes ]; then
  echo "The vizbrain command works in new Terminal windows. If he ever stops answering: vizbrain doctor"
else
  profile="$(shell_profile)"
  profile="${profile:-$HOME/.zshrc}"
  echo "One thing left: Terminal can't find the vizbrain command yet. Paste this line in"
  echo "Terminal, press Return, then open a new Terminal window:"
  echo
  echo "    echo '$PATH_LINE' >> ~/${profile#"$HOME"/}"
  echo
  echo "Then, if he ever stops answering: vizbrain doctor"
fi
open "http://localhost:4050" 2>/dev/null || true
