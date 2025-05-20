#!/bin/bash

# Modify this line to QUORUM="" for final. No quorum should be specified
QUORUM="--quorum=2"

# Change the next line to select branch. Acceptable values are "dev" and "release"
RELEASE_TYPE="release"
# Set your GitHub repo here instead of build URL
GITHUB_REPO="https://github.com/ishurusia5/xahau-hooks.git"

# Do not change below this line unless you know what you're doing
BASE_DIR=/opt/xahaud
USER=xahaud
PROGRAM=xahaud
BIN_DIR=$BASE_DIR/bin
DL_DIR=$BASE_DIR/downloads
DB_DIR=$BASE_DIR/db
ETC_DIR=$BASE_DIR/etc
LOG_DIR=$BASE_DIR/log
SCRIPT_LOG_FILE=$LOG_DIR/update.log
SERVICE_NAME="$PROGRAM.service"
SERVICE_FILE="/etc/systemd/system/$SERVICE_NAME"
CONFIG_FILE="$ETC_DIR/$PROGRAM.cfg"
VALIDATORS_FILE="$ETC_DIR/validators-xahau.txt"

if [ -z "$(find "$BASE_DIR" -mindepth 1 -type f -size +0c -print -quit)" ] && [ -z "$(find "$DIRECTORY" -mindepth 1 -type d -empty -print -quit)" ]; then
  NOBASE=true
else
  NOBASE=false
fi

# For systemd
EXEC_COMMAND="ExecStart=$BIN_DIR/$PROGRAM $QUORUM --net --silent --conf $ETC_DIR/$PROGRAM.cfg"

log() {
  if [[ "$FIRST_RUN" == true ]]; then
    echo $1
  else
    echo $1  
    echo "$(date +"%Y-%m-%d %H:%M:%S") $1" >> "$SCRIPT_LOG_FILE"
  fi
}

clean() {
  systemctl stop $SERVICE_NAME
  systemctl disable $SERVICE_NAME
  rm $SERVICE_FILE
  systemctl daemon-reload
  userdel $USER
  rm -rf $BASE_DIR
}

[[ $EUID -ne 0 ]] && echo "This script must be run as root" && exit 1

if ! command -v gpg >/dev/null || ! command -v curl >/dev/null || ! command -v git >/dev/null; then
  echo "Error: One or more of the required dependencies (gpg, curl, git) is not installed."
  exit 1
fi

if pgrep -x "xahaud" >/dev/null; then
  xahaud_pid=$(pgrep xahaud)
  xahaud_path=$(readlink -f /proc/$xahaud_pid/exe)
  if [ "$xahaud_path" = "/opt/xahaud/bin/xahaud" ]; then
    echo "xahaud is running in the expected location with PID: $xahaud_pid."
  else
    echo "xahaud is running with PID: $xahaud_pid, but not from expected path: $xahaud_path"
    exit 1
  fi
else
  echo "xahaud is not running. Continuing with installation"
fi

if [ $(id -u $USER > /dev/null 2>&1) ] || [ -f "$BASE_DIR/.firstrun" ]; then
  FIRST_RUN=false  
else  
  FIRST_RUN=true
fi

if ! id "$USER" >/dev/null 2>&1; then
  log "Creating user $USER..."
  useradd --system --no-create-home --shell /bin/false "$USER" &> /dev/null
fi

if [[ "$FIRST_RUN" == true ]]; then
  log "Creating directories..."
  mkdir -p "$DL_DIR" "$BIN_DIR" "$ETC_DIR" "$DB_DIR" "$LOG_DIR"
  touch "$BASE_DIR/.firstrun" "$SCRIPT_LOG_FILE"
  chown -R $USER:$USER $BASE_DIR
  cp "$0" /usr/local/bin/.
  echo "This script has been copied to /usr/local/bin."
fi

log "Cloning and building $PROGRAM from GitHub..."

BUILD_DIR="$DL_DIR/source"
if [ -d "$BUILD_DIR" ]; then
  log "Removing old source..."
  rm -rf "$BUILD_DIR"
fi

git clone --branch "$RELEASE_TYPE" "$GITHUB_REPO" "$BUILD_DIR"
if [ $? -ne 0 ]; then
  log "Error cloning repository!"
  exit 1
fi

cd "$BUILD_DIR"
log "Building the binary..."
mkdir -p build && cd build
cmake .. && make -j$(nproc)

if [ ! -f "$BUILD_DIR/build/$PROGRAM" ]; then
  log "Build failed: $PROGRAM binary not found"
  exit 1
fi

cp "$BUILD_DIR/build/$PROGRAM" "$DL_DIR/"
chmod +x "$DL_DIR/$PROGRAM"
chown $USER:$USER "$DL_DIR/$PROGRAM"

current_file=$(readlink "$BIN_DIR/$PROGRAM")
if [[ "$current_file" != "$DL_DIR/$PROGRAM" ]]; then
  ln -snf "$DL_DIR/$PROGRAM" "$BIN_DIR/$PROGRAM"
  DO_RESTART=true
else
  DO_RESTART=false
fi

if [[ ! -f $SERVICE_FILE ]]; then
  log "Installing systemd service..."
  cat << EOF > "$SERVICE_FILE"
[Unit]
Description=$PROGRAM Daemon
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
$EXEC_COMMAND
Restart=on-failure
User=$USER
Group=root
LimitNOFILE=65536

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable $SERVICE_NAME
fi

if [[ ! -f $CONFIG_FILE ]]; then
  log "Creating config file..."
  cat << EOF > "$CONFIG_FILE"
# Config content as in your original script
[peers_max]
20

[overlay]
ip_limit = 1024

[ledger_history]
full

[network_id]
21338

[server]
port_rpc_admin_local
port_peer
port_ws_admin_local

[port_rpc_admin_local]
port = 5009
ip = 127.0.0.1
admin = 127.0.0.1
protocol = http

[port_peer]
port = 21338
ip = 0.0.0.0
protocol = peer

[port_ws_admin_local]
port = 6009
ip = 127.0.0.1
admin = 127.0.0.1
protocol = ws

[node_size]
medium

[node_db]
type=NuDB
path=$DB_DIR/nudb
advisory_delete=0

[database_path]
$DB_DIR

[debug_logfile]
$LOG_DIR/debug.log

[sntp_servers]
time.windows.com
time.apple.com
time.nist.gov
pool.ntp.org

[validators_file]
$VALIDATORS_FILE

[rpc_startup]
{ "command": "log_level", "severity": "warn" }

[ssl_verify]
0

[peer_private]
0

[ips]
79.110.60.122 21338
79.110.60.124 21338
79.110.60.125 21338
79.110.60.121 21338
EOF
fi

if [[ ! -f $VALIDATORS_FILE ]]; then
  log "Creating validators file..."
  cat << EOF > "$VALIDATORS_FILE"
[validators]
nHDs6fHVnhb4ZbSFWF2dTTPHoZ6Rr39i2UfLotzgf8FKUi7iZdxx
nHUvgFxT8EGrXqQZ1rqMw67UtduefbaCHiVtVahk9RXDJP1g1mB4
nHU7Vn6co7xEFMBesV7qw7FXE8ucKrhVWQiYZB5oGyMhvqrnZrnJ
nHBoJCE3wPgkTcrNPMHyTJFQ2t77EyCAqcBRspFCpL6JhwCm94VZ
nHUVv4g47bFMySAZFUKVaXUYEmfiUExSoY4FzwXULNwJRzju4XnQ
nHBvr8avSFTz4TFxZvvi4rEJZZtyqE3J6KAAcVWVtifsE7edPM7q
nHUH3Z8TRU57zetHbEPr1ynyrJhxQCwrJvNjr4j1SMjYADyW1WWe

[import_vl_keys]
ED264807102805220DA0F312E71FC2C69E1552C9C5790F6C25E3729DEB573D5860
EOF
fi

chown -R $USER:$USER $BASE_DIR

if [[ ! -d "/etc/opt/$PROGRAM" ]]; then
  mkdir -p /etc/opt/xahau
  ln -snf $ETC_DIR/$PROGRAM.cfg /etc/opt/xahau/$PROGRAM.cfg
fi

ln -snf $BIN_DIR/$PROGRAM /usr/local/bin/$PROGRAM

if [[ "$DO_RESTART" == true ]]; then
  log "New version of $PROGRAM installed from GitHub."
  log "Restart with:"
  log "systemctl stop $PROGRAM.service"
  log "systemctl start $PROGRAM.service"
fi
