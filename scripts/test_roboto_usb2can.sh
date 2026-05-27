#!/bin/bash

# =================Configuration Area=================
# Target bitrate
BITRATE=1000000      # 1Mbps
# Define list of interfaces to test
INTERFACES=("can0" "can1" "can2" "can3")
# Transmit interval (microseconds) - cangen accepts the -g parameter
# For floating-point values like 1.5, support depends on the cangen version
# Practical test: Standard cangen -g accepts milliseconds; decimals may be truncated
# Suggest using -g (milliseconds) for safety
# For 1.5ms interval: In Linux cangen, the -g parameter is usually in milliseconds
# Newer cangen versions support microseconds (us) for -g: use -g 1500 (if us) or floating-point ms?
# Standard can-utils cangen -g uses milliseconds; input 1.5 may be truncated or supported
# Use floating-point ms for safety to approximate 1.5ms
TEST_INTERVAL=0.5
# =====================================================

# Check if running as root
if [ "$EUID" -ne 0 ]; then
  echo "Error: Please run this script with sudo"
  exit
fi

# Exit trap: Clean up background processes
trap 'echo -e "\nStopping test..."; sudo killall cangen 2>/dev/null; exit' INT

echo ">>> Initializing CAN interfaces (Mode: Classic CAN 2.0)..."
echo "    Bitrate: $BITRATE bps"

# --- Initialization Loop ---
for IF in "${INTERFACES[@]}"; do
    # Check if interface exists
    if [ ! -d "/sys/class/net/$IF" ]; then
        echo "Warning: Interface $IF does not exist, skipping..."
        continue
    fi

    echo "    Configuring $IF ..."
    ip link set $IF down

    # [Core Modification 1] Force MTU to 16 (standard frame length)
    # Helps inform the driver to only send short packets, attempting to mitigate
    # USB transmission zero-padding issues
    ip link set $IF mtu 16 2>/dev/null

    # [Core Modification 2] Enable automatic Bus-Off recovery (restart-ms)
    # Default Linux CAN driver deadlocks after Bus-Off until manual restart
    # Set auto-restart after 100ms to resolve "no transmission after replug" issue
    ip link set $IF type can bitrate $BITRATE restart-ms 100

    # Increase transmit queue length to prevent packet loss during USB congestion
    ip link set $IF txqueuelen 2000

    ip link set $IF up
done

echo ">>> Starting cangen traffic generator..."

# --- Start Generator Loop ---
for IF in "${INTERFACES[@]}"; do
    if [ -d "/sys/class/net/$IF" ]; then
        # [Core Fix]
        # -g 1.5: 1.5ms interval (~666pps/device). Total ~2600pps for 4 devices
        #   NEVER use -g 0, otherwise the device with the smallest ID (can0)
        #   will hog the bus due to the highest priority!
        # [Solution: Random ID (-I R)]
        # Under high load (e.g., 0.1ms interval), to prevent ID 0x100 from
        # hogging the bus permanently, all interfaces must get a chance to send
        # "high-priority" (small ID) frames.
        # Using -I R (Random ID) ensures all interfaces compete for the bus
        # fairly on a statistical basis.
        cangen $IF -g $TEST_INTERVAL -I R -L 8 -D i -i 2>/dev/null &
    fi
done

echo ">>> Starting dashboard..."
sleep 1

# --- Helper Function: Read kernel counters directly (ultra-fast) ---
read_sys_val() {
    cat "/sys/class/net/$1/statistics/$2" 2>/dev/null || echo 0
}

# Initialize old values
declare -A rx_old tx_old
for IF in "${INTERFACES[@]}"; do
    rx_old[$IF]=$(read_sys_val $IF "rx_packets")
    tx_old[$IF]=$(read_sys_val $IF "tx_packets")
done

# --- Monitoring Loop ---
while true; do
    sleep 1

    clear
    echo "========================================================================"
    echo "      4-Channel CAN Multi-Node Interconnection Stress Test - $(date +%T)"
    echo "      (Transmit Interval: ${TEST_INTERVAL}ms | Auto Recovery: 100ms)"
    echo "========================================================================"
    printf "%-6s %-12s %-12s %-8s %-12s %-15s\n" "IFace" "Rate(TX/RX)" "Total Pkts(T/R)" "Errors(T/R)" "State" "HW Cnt(TEC/REC)"
    echo "------------------------------------------------------------------------"

    for IF in "${INTERFACES[@]}"; do
        if [ -d "/sys/class/net/$IF" ]; then
            # Read new values
            rx_new=$(read_sys_val $IF "rx_packets")
            tx_new=$(read_sys_val $IF "tx_packets")
            errors=$(read_sys_val $IF "tx_errors")
            rx_errors=$(read_sys_val $IF "rx_errors")
            
            # Get CAN state (iproute2 required)
            can_state=$(ip -d link show $IF | grep "state" | grep -o "ERROR-ACTIVE\|ERROR-WARNING\|ERROR-PASSIVE\|BUS-OFF\|STOPPED" | head -1)
            [ -z "$can_state" ] && can_state="UNK"

            # [Ultimate Diagnostics] Attempt to read hardware error counters (TEC/REC)
            # SocketCAN device directories typically do not have a standard node
            # exposing berr_counter directly, but we can extract it via ip -d link show
            # ip output example: "can state ERROR-ACTIVE (berr-counter tx 0 rx 0) restart-ms 100"
            berr_info=$(ip -d link show $IF | grep "berr-counter" | sed -E 's/.*berr-counter tx ([0-9]+) rx ([0-9]+).*/TX:\1 RX:\2/')
            if [ -z "$berr_info" ]; then berr_info="N/A"; fi

            # Calculated display ID
            hex_id="Random"

            # Calculate rates
            tx_rate=$((tx_new - tx_old[$IF]))
            rx_rate=$((rx_new - rx_old[$IF]))

            # Color logic
            state_color="\033[32m" # Green for ACTIVE
            if [ "$can_state" != "ERROR-ACTIVE" ]; then state_color="\033[31m"; fi # Red for non-ACTIVE

            # Print row
            printf "%-6s %-12s %-12s %-8s ${state_color}%-12s\033[0m %-15s\n" \
                "$IF" "${tx_rate}/${rx_rate}" "${tx_new}/${rx_new}" "${errors}/${rx_errors}" "$can_state" "$berr_info"

            # Update old values
            rx_old[$IF]=$rx_new
            tx_old[$IF]=$tx_new
        fi
    done

    echo "========================================================================"
    echo " Tuning Note: Attempted to set MTU to 16 to optimize USB transmission efficiency."
    echo " Operation Tip: Press Ctrl+C to stop the test and shut down all traffic sources."
done