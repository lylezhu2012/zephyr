#!/bin/bash

set -euo pipefail

# Color definitions
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Logging functions
log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1" >&2
}

# Stop all running btvirt processes
stop_btvirt_processes() {
    log_info "Stopping all running btvirt processes..."

    # Find all btvirt processes
    local pids=$(pgrep -f "btvirt" 2>/dev/null || true)

    if [[ -z "$pids" ]]; then
        log_info "No running btvirt processes found"
        return 0
    fi

    log_info "Found btvirt processes: $pids"

    # Try graceful shutdown (SIGTERM)
    echo "$pids" | xargs -r kill -TERM 2>/dev/null || true

    # Wait for processes to stop
    sleep 2

    # Check if any processes are still running
    local remaining_pids=$(pgrep -f "btvirt" 2>/dev/null || true)

    if [[ -n "$remaining_pids" ]]; then
        log_warn "Some btvirt processes still running, force killing..."
        echo "$remaining_pids" | xargs -r kill -KILL 2>/dev/null || true
        sleep 1
    fi

    # Final check
    local final_check=$(pgrep -f "btvirt" 2>/dev/null || true)
    if [[ -n "$final_check" ]]; then
        log_error "Failed to stop all btvirt processes"
        return 1
    fi

    log_info "All btvirt processes stopped successfully"
    return 0
}

# Start btvirt asynchronously
start_btvirt_async() {
    log_info "Starting btvirt asynchronously..."

    # Check if btvirt command exists
    if ! command -v btvirt &> /dev/null; then
        log_error "btvirt command not found. Please install bluez-tools package."
        return 1
    fi

    # Start btvirt asynchronously and get PID
    sudo btvirt -d -l2 &
    local btvirt_pid=$!

    log_info "Started btvirt with PID: $btvirt_pid"

    # Wait briefly to check if process started successfully
    sleep 1

    # Stop Bluetooth service
    sudo systemctl stop bluetooth

    # Check if process is still running
    if ! kill -0 "$btvirt_pid" 2>/dev/null; then
        log_error "btvirt process failed to start or exited immediately"
        return 1
    fi

    log_info "btvirt started successfully"

    # Wait for virtual HCI devices to initialize
    log_info "Waiting for virtual HCI devices to initialize..."
    sleep 3

    return 0
}

# Extract Virtual HCI devices and ensure they are DOWN
extract_virtual_hci() {
    log_info "Extracting Virtual HCI devices..."

    # Declare arrays
    declare -g -a virtual_devices=()
    declare -g -a virtual_addresses=()

    # Check hciconfig command
    if ! command -v hciconfig &> /dev/null; then
        log_error "hciconfig command not found. Please install bluez-utils package."
        return 1
    fi

    # Extract Virtual HCI device information and store in arrays
    local count=0
    while IFS=' ' read -r device address; do
        if [[ -n "$device" && -n "$address" ]]; then
            virtual_devices+=("$device")
            virtual_addresses+=("$address")
            ((count++))
        fi
    done < <(hciconfig -a 2>/dev/null | awk '
    /^hci[0-9]+:/ {
        device = $1
        gsub(/:/, "", device)
        virtual = 0
    }
    /Bus: Virtual/ {
        virtual = 1
    }
    /BD Address:/ && virtual {
        print device " " $3
        virtual = 0
    }')

    if [[ $count -eq 0 ]]; then
        log_warn "No Virtual HCI devices found"
        return 1
    fi

    log_info "Found $count Virtual HCI devices"

    # Check and fix status of all devices
    local has_error=false
    local auto_fix=true  # Set to false if you don't want automatic fixing

    for i in "${!virtual_devices[@]}"; do
        local device="${virtual_devices[$i]}"
        local address="${virtual_addresses[$i]}"

        # Get device status
        local status_output
        status_output=$(hciconfig "$device" 2>/dev/null)

        if echo "$status_output" | grep -q "UP RUNNING\|UP"; then
            log_warn "Device $device ($address) is UP - Expected DOWN"

            if [[ "$auto_fix" == true ]]; then
                log_info "Attempting to bring down $device..."
                if sudo hciconfig "$device" down 2>/dev/null; then
                    log_info "Successfully brought down $device"
                else
                    log_error "Failed to bring down $device"
                    has_error=true
                fi
            else
                log_error "Device $device is not DOWN"
                has_error=true
            fi
        elif echo "$status_output" | grep -q "DOWN"; then
            log_info "Device $device ($address): DOWN - OK"
        else
            log_warn "Device $device ($address): Unknown status"
        fi
    done

    if [[ "$has_error" == true ]]; then
        log_error "Some Virtual HCI devices could not be brought to DOWN state"
        return 1
    fi

    log_info "All Virtual HCI devices are in DOWN state"
    return 0
}

# Display results
display_results() {
    echo ""
    echo "Virtual HCI Devices:"
    echo "==================="

    for i in "${!virtual_devices[@]}"; do
        echo "[$i] Device: ${virtual_devices[$i]}, Address: ${virtual_addresses[$i]}"
    done

    echo ""
    echo "Array Information:"
    echo "Total devices: ${#virtual_devices[@]}"
    echo "All devices: ${virtual_devices[*]}"
    echo "All addresses: ${virtual_addresses[*]}"

    if [[ ${#virtual_devices[@]} -gt 0 ]]; then
        echo "First device: ${virtual_devices[0]}"
        echo "First address: ${virtual_addresses[0]}"
    fi
}

# Build peer device
build_peer_device() {
    log_info "Building peer device (central)..."

    local build_dir="gap_discovery/central"
    local build_cmd="west build -b native_sim tests/bluetooth/classic/gap_discovery -d $build_dir -DCONFIG_TEST_GAP_CENTRAL=y -DCONFIG_TEST_GAP_PERIPHERAL_ADDRESS=\"${virtual_addresses[1]}\""

    log_info "Executing build command: $build_cmd"

    # Execute the build command
    if ! $build_cmd; then
        log_error "Build failed for peer device"
        return 1
    fi

    log_info "Build completed successfully"

    # Check if zephyr.exe was generated
    local zephyr_exe="$build_dir/zephyr/zephyr.exe"

    log_info "Checking for generated executable: $zephyr_exe"

    if [[ ! -f "$zephyr_exe" ]]; then
        log_error "zephyr.exe not found at $zephyr_exe"
        return 1
    fi

    log_info "zephyr.exe found successfully at $zephyr_exe"

    # Optional: Check if the file is executable
    if [[ ! -x "$zephyr_exe" ]]; then
        log_warn "zephyr.exe exists but is not executable"
        chmod +x "$zephyr_exe"
        log_info "Made zephyr.exe executable"
    fi

    log_info "Peer device build verification completed"
    return 0
}

# Run peer device
run_peer_device() {
    log_info "Running tests..."

    local central_exe="gap_discovery/central/zephyr/zephyr.exe"

    # Check if central executable exists
    if [[ ! -f "$central_exe" ]]; then
        log_error "Central executable not found: $central_exe"
        return 1
    fi

    # Check if we have virtual devices
    if [[ ${#virtual_devices[@]} -eq 0 ]]; then
        log_error "No virtual devices available for testing"
        return 1
    fi

    # Get the first virtual device
    local first_device="${virtual_devices[0]}"
    local test_cmd="sudo ./$central_exe --bt-dev=$first_device"

    log_info "Executing test command: $test_cmd"
    log_info "Using virtual device: $first_device (${virtual_addresses[0]})"

    # Execute the test command
    if ! $test_cmd; then
        log_error "Test execution failed"
        return 1
    fi

    log_info "Test execution completed successfully"
    return 0
}

# Run twister
run_twister() {
    log_info "Running twister for peripheral device..."

    # Check if we have at least 2 virtual devices
    if [[ ${#virtual_devices[@]} -lt 2 ]]; then
        log_error "Need at least 2 virtual devices for testing. Found: ${#virtual_devices[@]}"
        return 1
    fi

    # Get the second virtual device for peripheral
    local second_device="${virtual_devices[1]}"
    local central_address="${virtual_addresses[0]}"
    local peripheral_dir="gap_discovery/peripheral"

    local twister_cmd="west twister -b native_sim tests/bluetooth/classic/gap_discovery -d $peripheral_dir -DCONFIG_TEST_GAP_PERIPHERAL=y -DCONFIG_TEST_GAP_CENTRAL_ADDRESS=\"$central_address\" -- --bt-dev=$second_device"

    log_info "Executing twister command: $twister_cmd"
    log_info "Using peripheral device: $second_device (${virtual_addresses[1]})"
    log_info "Central address configured as: $central_address"

    # Execute the twister command
    if ! $twister_cmd; then
        log_error "Twister execution failed"
        return 1
    fi

    log_info "Twister execution completed successfully"
    return 0
}

# Cleanup function
cleanup() {
    log_info "Cleaning up..."
    # Optionally stop btvirt on script exit
    stop_btvirt_processes
}

# Set cleanup on exit
trap cleanup EXIT

# Main function
main() {
    log_info "Starting Virtual HCI extraction with btvirt management..."

    # 1. Stop all btvirt processes
    if ! stop_btvirt_processes; then
        log_error "Failed to stop existing btvirt processes"
        exit 1
    fi

    # 2. Start btvirt asynchronously
    if ! start_btvirt_async; then
        log_error "Failed to start btvirt"
        exit 1
    fi

    # 3. Extract Virtual HCI devices
    if ! extract_virtual_hci; then
        log_error "Failed to extract Virtual HCI devices"
        exit 1
    fi

    # 4. Display results
    display_results

    # 5. Build peer device
    if ! build_peer_device; then
        log_error "Failed to build peer device"
        exit 1
    fi

    # 6. Run peer device
    if ! run_peer_device; then
        log_error "Failed to run peer device"
        exit 1
    fi

    #7. Run twister
    if ! run_twister; then
        log_error "Failed to run twister"
        exit 1
    fi
}

# Run main function
main "$@"
