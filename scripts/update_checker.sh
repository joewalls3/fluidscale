#!/bin/bash

# GitHub repository info
REPO="joewalls/fluid-scale"
BRANCH="main"
INSTALL_DIR="/opt/fluid_measurement"

# Log file
LOG_FILE="/var/log/fluid_scale_update.log"

log() {
    echo "$(date): $1" >> $LOG_FILE
}

log "Starting update check"

# Create a temporary directory
TMP_DIR=$(mktemp -d)
cd $TMP_DIR

# Clone the repository
git clone --depth 1 -b $BRANCH https://github.com/$REPO.git
if [ $? -ne 0 ]; then
    log "Failed to clone repository"
    rm -rf $TMP_DIR
    exit 1
fi

cd $REPO

# Check if there are any differences
DIFF=$(diff -r --exclude=".git" . $INSTALL_DIR)
if [ -z "$DIFF" ]; then
    log "No updates found"
    rm -rf $TMP_DIR
    exit 0
fi

log "Updates found, installing..."

# Build the updated code
make
if [ $? -ne 0 ]; then
    log "Build failed"
    rm -rf $TMP_DIR
    exit 1
fi

# Stop the service
systemctl stop fluid-measurement.service

# Backup the current version
BACKUP_DIR="/opt/fluid_measurement_backup_$(date +%Y%m%d_%H%M%S)"
cp -r $INSTALL_DIR $BACKUP_DIR
log "Backed up current version to $BACKUP_DIR"

# Install the new version
cp fluid_measurement_server $INSTALL_DIR/
cp -r web/* $INSTALL_DIR/web/

# Restart the service
systemctl start fluid-measurement.service
log "Update completed successfully"

# Clean up
rm -rf $TMP_DIR
