# remote.sh: the server half of tools/deploy/deploy. Not run by hand: deploy
# pipes it to `bash -s` over ssh with
#   <verb> <dir> <port> <display> <server-args>
set -euo pipefail

VERB=$1 DIR=$2 PORT=$3 DISP=$4 SERVER_ARGS=$5
case $DIR in /*) ;; *) DIR="$HOME/$DIR" ;; esac
BUILD="$DIR/build-server"
RUN="$DIR/run"
SESSION=$(basename "$DIR")   # the tmux session the server runs in

# running, and not a zombie: a pane process that exited (quit) can sit
# unreaped for a moment, and kill -0 still succeeds on it
alivepid() {
  local st
  kill -0 "$1" 2>/dev/null || return 1
  st=$(ps -o stat= -p "$1" 2>/dev/null || true)
  [ -n "$st" ] && [ "${st:0:1}" != Z ]
}
alive() { [ -f "$1" ] && alivepid "$(cat "$1")"; }
bin() { ls "$BUILD"/pd.* 2>/dev/null | grep -v '\.debug$' | head -1 || true; }

case $VERB in
build)
  cd "$DIR"
  cmake -S . -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
  cmake --build "$BUILD" -j"$(nproc)"
  echo "built $(bin)"
  ;;

start)
  mkdir -p "$RUN/save"
  if alive "$RUN/pd.pid"; then echo "already running (pid $(cat "$RUN/pd.pid"))"; exit 0; fi
  PD=$(bin)
  [ -x "${PD:-/nonexistent}" ] || { echo "no binary under $BUILD - run deploy build"; exit 1; }
  cd "$RUN"
  rm -f "/tmp/.X$DISP-lock"
  setsid nohup Xvfb ":$DISP" -screen 0 320x240x24 -nolisten tcp > xvfb.txt 2>&1 < /dev/null &
  echo $! > xvfb.pid
  sleep 1
  # --dedicated: it hosts, and nobody plays there - its seat's pawn is a ghost
  # (hidden, cloaked, invincible) on every machine.
  # The server runs on a tmux pane so its terminal is the console: deploy
  # console attaches, everyone attached shares the screen and scrollback.
  # run/pd.sh is the pane's command; it records its pid and execs the game.
  # SERVER_ARGS is split on spaces on purpose: it is a flag list
  # shellcheck disable=SC2086
  {
    echo '#!/bin/bash'
    echo 'echo $$ > pd.pid'
    printf 'exec env DISPLAY=%q LIBGL_ALWAYS_SOFTWARE=1 SDL_AUDIODRIVER=dummy LP_NUM_THREADS=1' ":$DISP"
    printf ' %q' "$PD" --basedir "$DIR/basedir/data" --savedir "$RUN/save" \
      --no-sound --no-upnp --no-advertise --log --console \
      --dedicated --port "$PORT" $SERVER_ARGS
    echo
  } > pd.sh
  rm -f pd.pid
  # pd.log rotates itself (the game does it for a dedicated server); out.txt
  # is the pane's transcript, so keep the last five of those the same way
  for i in 4 3 2 1; do
    if [ -f "out.txt.$i" ]; then mv -f "out.txt.$i" "out.txt.$((i + 1))"; fi
  done
  if [ -f out.txt ]; then mv -f out.txt out.txt.1; fi
  tmux kill-session -t "$SESSION" 2>/dev/null || true
  tmux new-session -d -s "$SESSION" -x 160 -y 48 -c "$RUN" "sleep 0.3; exec bash $RUN/pd.sh"
  # keep a dead pane, so attaching after a crash shows how it died
  tmux set-option -t "$SESSION" remain-on-exit on > /dev/null
  # everything the pane shows also lands in out.txt, for status and log
  tmux pipe-pane -t "$SESSION" -o "cat >> $(printf %q "$RUN/out.txt")"
  sleep 2
  if alive pd.pid; then
    echo "up: pid $(cat pd.pid), udp $PORT, $PD"
  else
    echo "it died on boot; last lines:"; tail -n 30 out.txt; exit 1
  fi
  ;;

stop)
  if alive "$RUN/pd.pid"; then
    pid=$(cat "$RUN/pd.pid")
    kill -TERM "$pid"
    for _ in $(seq 15); do alivepid "$pid" || break; sleep 1; done
    if alivepid "$pid"; then echo "no exit after TERM, killing"; kill -KILL "$pid"; fi
    echo "stopped $pid"
  else
    echo "not running"
  fi
  # Xvfb takes a moment to go on TERM; wait, so a restart never races its lock
  if alive "$RUN/xvfb.pid"; then
    xpid=$(cat "$RUN/xvfb.pid")
    kill -TERM "$xpid" 2>/dev/null || true
    for _ in $(seq 10); do alivepid "$xpid" || break; sleep 0.5; done
    kill -KILL "$xpid" 2>/dev/null || true
  fi
  tmux kill-session -t "$SESSION" 2>/dev/null || true
  rm -f "$RUN/pd.pid" "$RUN/xvfb.pid"
  ;;

status)
  [ -f "$DIR/DEPLOYED" ] && echo "deployed: $(cat "$DIR/DEPLOYED")"
  echo "binary:   $(bin)"
  if alive "$RUN/pd.pid"; then
    echo "server:   up, pid $(cat "$RUN/pd.pid"), udp $PORT, console: deploy console (tmux $SESSION)"
  else
    echo "server:   down"
  fi
  if [ -f "$RUN/out.txt" ]; then echo "--- $RUN/out.txt"; tail -n 20 "$RUN/out.txt"; fi
  ;;

log)
  exec tail -n 50 -F "$RUN/out.txt"
  ;;

console)
  # needs a tty: deploy calls this verb over ssh -t
  tmux has-session -t "$SESSION" 2>/dev/null || { echo "not running (deploy start)"; exit 1; }
  echo "attaching to tmux $SESSION - detach with Ctrl-b d, the server keeps running"
  sleep 1
  exec tmux attach -t "$SESSION"
  ;;

setup)
  # Debian/Ubuntu. Build: compiler, cmake, SDL2 + GL + zlib headers.
  # Run: Xvfb and mesa's software GL (llvmpipe).
  sudo apt-get update
  sudo apt-get install -y build-essential cmake pkg-config python3 rsync tmux \
    libsdl2-dev libgl-dev zlib1g-dev xvfb libgl1-mesa-dri
  if command -v ufw > /dev/null && sudo ufw status | grep -q '^Status: active'; then
    sudo ufw allow "$PORT/udp"
  else
    echo "ufw not active: if the host has another firewall (or a provider one), open udp $PORT there"
  fi
  ;;

*) echo "remote.sh: unknown verb $VERB" >&2; exit 2 ;;
esac
