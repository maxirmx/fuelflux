#!/bin/bash
# FuelFlux Installation Script
# Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
# All rights reserved.

set -e

# =============================================================================
# Configuration
# =============================================================================
SERVICE_NAME="fuelflux"
SERVICE_USER="fuelflux"
SERVICE_GROUP="fuelflux"

INSTALL_DIR="/opt/fuelflux"
BIN_DIR="${INSTALL_DIR}/bin"
CONFIG_DIR="/etc/fuelflux"
DATA_DIR="/var/fuelflux"
DB_DIR="${DATA_DIR}/db"
LOG_DIR="${DATA_DIR}/logs"

SYSTEMD_DIR="/etc/systemd/system"
SERVICE_FILE="${SERVICE_NAME}.service"

# Hardware groups for GPIO, I2C, SPI, serial access
HARDWARE_GROUPS="gpio dialout i2c spi"

# Script directory (where this script is located)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# =============================================================================
# Helper Functions
# =============================================================================
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[OK]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

check_root() {
    if [[ $EUID -ne 0 ]]; then
        log_error "This script must be run as root (use sudo)"
        exit 1
    fi
}

# =============================================================================
# User and Group Management
# =============================================================================
setup_user_group() {
    log_info "Setting up user and group..."

    # Create group if it doesn't exist
    if getent group "${SERVICE_GROUP}" > /dev/null 2>&1; then
        log_info "Group '${SERVICE_GROUP}' already exists"
    else
        groupadd -r "${SERVICE_GROUP}"
        log_success "Created group '${SERVICE_GROUP}'"
    fi

    # Create user if it doesn't exist
    if id "${SERVICE_USER}" > /dev/null 2>&1; then
        log_info "User '${SERVICE_USER}' already exists"
        # Ensure user is in the correct primary group
        usermod -g "${SERVICE_GROUP}" "${SERVICE_USER}" 2>/dev/null || true
    else
        useradd -r -s /sbin/nologin -g "${SERVICE_GROUP}" -d "${INSTALL_DIR}" "${SERVICE_USER}"
        log_success "Created user '${SERVICE_USER}'"
    fi

    # Add user to hardware groups (ignore errors for non-existent groups)
    for grp in ${HARDWARE_GROUPS}; do
        if getent group "${grp}" > /dev/null 2>&1; then
            usermod -aG "${grp}" "${SERVICE_USER}" 2>/dev/null || true
            log_info "Added '${SERVICE_USER}' to group '${grp}'"
        else
            log_warn "Group '${grp}' does not exist, skipping"
        fi
    done
}

# =============================================================================
# Directory Management
# =============================================================================
setup_directories() {
    log_info "Setting up directories..."

    # Create installation directory
    if [[ ! -d "${INSTALL_DIR}" ]]; then
        mkdir -p "${INSTALL_DIR}"
        log_success "Created ${INSTALL_DIR}"
    fi

    # Create bin directory
    if [[ ! -d "${BIN_DIR}" ]]; then
        mkdir -p "${BIN_DIR}"
        log_success "Created ${BIN_DIR}"
    fi

    # Create config directory
    if [[ ! -d "${CONFIG_DIR}" ]]; then
        mkdir -p "${CONFIG_DIR}"
        log_success "Created ${CONFIG_DIR}"
    fi

    # Create data directory
    if [[ ! -d "${DATA_DIR}" ]]; then
        mkdir -p "${DATA_DIR}"
        log_success "Created ${DATA_DIR}"
    fi

    # Create database directory
    if [[ ! -d "${DB_DIR}" ]]; then
        mkdir -p "${DB_DIR}"
        log_success "Created ${DB_DIR}"
    fi

    # Create logs directory
    if [[ ! -d "${LOG_DIR}" ]]; then
        mkdir -p "${LOG_DIR}"
        log_success "Created ${LOG_DIR}"
    fi

    # Create app config directory (for logging.json, etc.)
    local app_config_dir="${INSTALL_DIR}/config"
    if [[ ! -d "${app_config_dir}" ]]; then
        mkdir -p "${app_config_dir}"
        log_success "Created ${app_config_dir}"
    fi

    # Set ownership
    chown -R "${SERVICE_USER}:${SERVICE_GROUP}" "${INSTALL_DIR}"
    chown -R "${SERVICE_USER}:${SERVICE_GROUP}" "${DATA_DIR}"
    chown root:root "${CONFIG_DIR}"
    chmod 755 "${CONFIG_DIR}"

    log_success "Directory permissions set"
}

# =============================================================================
# Service Installation
# =============================================================================
install_service() {
    log_info "Installing systemd service..."

    # Check if service file exists in script directory
    local source_service="${SCRIPT_DIR}/${SERVICE_FILE}"
    if [[ ! -f "${source_service}" ]]; then
        log_error "Service file not found: ${source_service}"
        exit 1
    fi

    # Stop service if running
    if systemctl is-active --quiet "${SERVICE_NAME}" 2>/dev/null; then
        log_info "Stopping running service..."
        systemctl stop "${SERVICE_NAME}"
    fi

    # Copy service file
    cp "${source_service}" "${SYSTEMD_DIR}/${SERVICE_FILE}"
    chmod 644 "${SYSTEMD_DIR}/${SERVICE_FILE}"
    log_success "Installed service file to ${SYSTEMD_DIR}/${SERVICE_FILE}"

    # Reload systemd
    systemctl daemon-reload
    log_success "Systemd daemon reloaded"
}

# =============================================================================
# Binary Installation
# =============================================================================
install_binary() {
    local binary_path="$1"

    if [[ -z "${binary_path}" ]]; then
        log_warn "No binary path specified, skipping binary installation"
        log_info "You can install the binary later with: sudo cp <binary> ${BIN_DIR}/"
        return
    fi

    if [[ ! -f "${binary_path}" ]]; then
        log_error "Binary not found: ${binary_path}"
        exit 1
    fi

    log_info "Installing binary..."

    # Stop service if running
    if systemctl is-active --quiet "${SERVICE_NAME}" 2>/dev/null; then
        log_info "Stopping running service..."
        systemctl stop "${SERVICE_NAME}"
    fi

    cp "${binary_path}" "${BIN_DIR}/${SERVICE_NAME}"
    chmod 755 "${BIN_DIR}/${SERVICE_NAME}"
    chown "${SERVICE_USER}:${SERVICE_GROUP}" "${BIN_DIR}/${SERVICE_NAME}"
    log_success "Installed binary to ${BIN_DIR}/${SERVICE_NAME}"
}

# =============================================================================
# Configuration
# =============================================================================
setup_config() {
    log_info "Setting up configuration..."

    local env_file="${CONFIG_DIR}/fuelflux.env"

    # Create default environment file if it doesn't exist
    if [[ ! -f "${env_file}" ]]; then
        cat > "${env_file}" << 'EOF'
# FuelFlux Environment Configuration
# Uncomment and modify as needed

# Controller ID (unique identifier for this device)
# FUELFLUX_CONTROLLER_ID=232390330480218

# Pump GPIO configuration
# FUELFLUX_PUMP_GPIO_CHIP=/dev/gpiochip0
# FUELFLUX_PUMP_RELAY_PIN=17
# FUELFLUX_PUMP_ACTIVE_LOW=true
EOF
        chmod 600 "${env_file}"
        log_success "Created default config at ${env_file}"
    else
        log_info "Config file already exists: ${env_file}"
    fi

    # Copy logging.json if it exists in script directory
    local logging_config="${SCRIPT_DIR}/logging.json"
    local app_config_dir="${INSTALL_DIR}/config"
    if [[ -f "${logging_config}" ]]; then
        cp "${logging_config}" "${app_config_dir}/logging.json"
        chown "${SERVICE_USER}:${SERVICE_GROUP}" "${app_config_dir}/logging.json"
        chmod 644 "${app_config_dir}/logging.json"
        log_success "Installed logging config to ${app_config_dir}/logging.json"
    else
        log_warn "logging.json not found in ${SCRIPT_DIR}, skipping"
        log_info "Copy manually: sudo cp config/logging.json ${app_config_dir}/"
    fi
}

# =============================================================================
# Service Management
# =============================================================================
enable_service() {
    log_info "Enabling service..."
    systemctl enable "${SERVICE_NAME}"
    log_success "Service enabled (will start on boot)"
}

start_service() {
    log_info "Starting service..."
    systemctl start "${SERVICE_NAME}"
    
    # Wait a moment and check status
    sleep 2
    if systemctl is-active --quiet "${SERVICE_NAME}"; then
        log_success "Service started successfully"
    else
        log_error "Service failed to start. Check: journalctl -u ${SERVICE_NAME} -e"
        exit 1
    fi
}

stop_service() {
    if systemctl is-active --quiet "${SERVICE_NAME}" 2>/dev/null; then
        log_info "Stopping service..."
        systemctl stop "${SERVICE_NAME}"
        log_success "Service stopped"
    else
        log_info "Service is not running"
    fi
}

disable_service() {
    if systemctl is-enabled --quiet "${SERVICE_NAME}" 2>/dev/null; then
        log_info "Disabling service..."
        systemctl disable "${SERVICE_NAME}"
        log_success "Service disabled"
    else
        log_info "Service is not enabled"
    fi
}

show_status() {
    echo ""
    log_info "Service status:"
    systemctl status "${SERVICE_NAME}" --no-pager || true
    echo ""
    log_info "Recent logs:"
    journalctl -u "${SERVICE_NAME}" -n 20 --no-pager || true
}

# =============================================================================
# Uninstallation
# =============================================================================
uninstall() {
    log_info "Uninstalling ${SERVICE_NAME}..."

    # Stop and disable service
    stop_service
    disable_service

    # Remove service file
    if [[ -f "${SYSTEMD_DIR}/${SERVICE_FILE}" ]]; then
        rm -f "${SYSTEMD_DIR}/${SERVICE_FILE}"
        systemctl daemon-reload
        log_success "Removed service file"
    fi

    # Ask about data removal
    echo ""
    read -p "Remove data directory ${DATA_DIR}? [y/N] " -n 1 -r
    echo ""
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        rm -rf "${DATA_DIR}"
        log_success "Removed data directory"
    else
        log_info "Keeping data directory"
    fi

    # Ask about config removal
    read -p "Remove config directory ${CONFIG_DIR}? [y/N] " -n 1 -r
    echo ""
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        rm -rf "${CONFIG_DIR}"
        log_success "Removed config directory"
    else
        log_info "Keeping config directory"
    fi

    # Ask about installation directory
    read -p "Remove installation directory ${INSTALL_DIR}? [y/N] " -n 1 -r
    echo ""
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        rm -rf "${INSTALL_DIR}"
        log_success "Removed installation directory"
    else
        log_info "Keeping installation directory"
    fi

    # Ask about user removal
    if id "${SERVICE_USER}" > /dev/null 2>&1; then
        read -p "Remove user '${SERVICE_USER}'? [y/N] " -n 1 -r
        echo ""
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            userdel "${SERVICE_USER}" 2>/dev/null || true
            log_success "Removed user '${SERVICE_USER}'"
        else
            log_info "Keeping user '${SERVICE_USER}'"
        fi
    fi

    log_success "Uninstallation complete"
}

# =============================================================================
# Usage
# =============================================================================
usage() {
    cat << EOF
FuelFlux Installation Script

Usage: $(basename "$0") [COMMAND] [OPTIONS]

Commands:
    install [binary]    Full installation (user, dirs, service, optional binary)
    update [binary]     Update binary and restart service
    uninstall           Remove service and optionally data/config
    start               Start the service
    stop                Stop the service
    restart             Restart the service
    enable              Enable service to start on boot
    disable             Disable service from starting on boot
    status              Show service status and recent logs
    help                Show this help message

Examples:
    $(basename "$0") install                    # Install service only
    $(basename "$0") install ./fuelflux         # Install service and binary
    $(basename "$0") update ./fuelflux          # Update binary and restart
    $(basename "$0") status                     # Check service status

Directories:
    Installation:  ${INSTALL_DIR}
    Binary:        ${BIN_DIR}
    Config:        ${CONFIG_DIR}
    Data/Logs:     ${DATA_DIR}

EOF
}

# =============================================================================
# Main
# =============================================================================
main() {
    local command="${1:-help}"
    local binary_path="${2:-}"

    case "${command}" in
        install)
            check_root
            echo ""
            log_info "========================================="
            log_info "FuelFlux Installation"
            log_info "========================================="
            echo ""
            setup_user_group
            setup_directories
            setup_config
            install_service
            install_binary "${binary_path}"
            enable_service
            echo ""
            log_success "========================================="
            log_success "Installation complete!"
            log_success "========================================="
            echo ""
            if [[ -n "${binary_path}" ]]; then
                log_info "Start the service with: sudo systemctl start ${SERVICE_NAME}"
            else
                log_info "Install the binary with: sudo cp <binary> ${BIN_DIR}/${SERVICE_NAME}"
                log_info "Then start with: sudo systemctl start ${SERVICE_NAME}"
            fi
            log_info "Check status with: sudo systemctl status ${SERVICE_NAME}"
            log_info "View logs with: journalctl -u ${SERVICE_NAME} -f"
            echo ""
            ;;
        update)
            check_root
            if [[ -z "${binary_path}" ]]; then
                log_error "Binary path required for update"
                log_info "Usage: $(basename "$0") update <binary_path>"
                exit 1
            fi
            stop_service
            install_binary "${binary_path}"
            start_service
            ;;
        uninstall)
            check_root
            uninstall
            ;;
        start)
            check_root
            start_service
            ;;
        stop)
            check_root
            stop_service
            ;;
        restart)
            check_root
            stop_service
            start_service
            ;;
        enable)
            check_root
            enable_service
            ;;
        disable)
            check_root
            disable_service
            ;;
        status)
            show_status
            ;;
        help|--help|-h)
            usage
            ;;
        *)
            log_error "Unknown command: ${command}"
            usage
            exit 1
            ;;
    esac
}

main "$@"
