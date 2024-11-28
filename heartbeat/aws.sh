#!/bin/sh
#
#
#   AWS Helper Scripts
#
#

: ${OCF_FUNCTIONS_DIR=${OCF_ROOT}/lib/heartbeat}
. ${OCF_FUNCTIONS_DIR}/ocf-shellfuncs

# Defaults
OCF_RESKEY_curl_retries_default="4"
OCF_RESKEY_curl_sleep_default="3"

: ${OCF_RESKEY_curl_retries=${OCF_RESKEY_curl_retries_default}}
: ${OCF_RESKEY_curl_sleep=${OCF_RESKEY_curl_sleep_default}}

# Function to enable reusable IMDS token retrieval for efficient repeated access 
# File to store the token and timestamp
TOKEN_FILE="${HA_RSCTMP}/.aws_imds_token"
TOKEN_FUNC="fetch_new_token"  # Used by curl_retry() if saved token is invalid
TOKEN_LIFETIME=21600  # Token lifetime in seconds (6 hours)
TOKEN_EXPIRY_THRESHOLD=3600  # Renew token if less than 60 minutes (1 hour) remaining
DMI_FILE="/sys/devices/virtual/dmi/id/board_asset_tag" # Only supported on nitro-based instances.

# Function to fetch a new token
fetch_new_token() {
    TOKEN=$(curl_retry "$OCF_RESKEY_curl_retries" "$OCF_RESKEY_curl_sleep" "--show-error -sX PUT -H 'X-aws-ec2-metadata-token-ttl-seconds: $TOKEN_LIFETIME'" "http://169.254.169.254/latest/api/token")
    echo "$TOKEN $(date +%s)" > "$TOKEN_FILE"
    chmod 600 "$TOKEN_FILE"
    echo "$TOKEN"
}

# Function to retrieve or renew the token
get_token() {
    if [ -f "$TOKEN_FILE" ]; then
        read -r STORED_TOKEN STORED_TIMESTAMP < "$TOKEN_FILE"
        CURRENT_TIME=$(date +%s)
        ELAPSED_TIME=$((CURRENT_TIME - STORED_TIMESTAMP))

        if [ "$ELAPSED_TIME" -lt "$((TOKEN_LIFETIME - TOKEN_EXPIRY_THRESHOLD))" ]; then
            # Token is still valid
            echo "$STORED_TOKEN"
            return
        fi
    fi
    # Fetch a new token if not valid
    fetch_new_token
}

get_instance_id() {
    local INSTANCE_ID

    # Try to get the EC2 instance ID from DMI first before falling back to IMDS.
    ocf_log debug "EC2: Attempt to get EC2 Instance ID from local file."
    if [ -r "$DMI_FILE" ] && [ -s "$DMI_FILE" ]; then
        INSTANCE_ID="$(cat "$DMI_FILE")"
        case "$INSTANCE_ID" in
            i-0*) echo "$INSTANCE_ID"; return "$OCF_SUCCESS" ;;
        esac
    fi

    INSTANCE_ID=$(curl_retry "$OCF_RESKEY_curl_retries" "$OCF_RESKEY_curl_sleep" "--show-error -s -H 'X-aws-ec2-metadata-token: $TOKEN'" "http://169.254.169.254/latest/meta-data/instance-id")
    if [ $? -ne 0 ]; then
        ocf_exit_reason "Failed to get EC2 Instance ID"
        exit $OCF_ERR_GENERIC
    fi

    echo "$INSTANCE_ID"
    return "$OCF_SUCCESS"
}

get_interface_mac() {
    local MAC_FILE MAC_ADDR rc
    MAC_FILE="/sys/class/net/${OCF_RESKEY_interface}/address"
    if [ -z "$OCF_RESKEY_interface" ]; then
        cmd="curl_retry \"$OCF_RESKEY_curl_retries\" \"$OCF_RESKEY_curl_sleep\" \"--show-error -s -H 'X-aws-ec2-metadata-token: $TOKEN'\" \"http://169.254.169.254/latest/meta-data/mac\""
    elif [ -f "$MAC_FILE" ]; then
        cmd="cat ${MAC_FILE}"
    else
        cmd="ip -br link show dev ${OCF_RESKEY_interface} | tr -s ' ' | cut -d' ' -f3"
    fi
    ocf_log debug "executing command: $cmd"
    MAC_ADDR="$(eval $cmd)"
    rc=$?
    if [ $rc != 0 ]; then
        ocf_log warn "command failed, rc: $rc"
        return $OCF_ERR_GENERIC
    fi
    ocf_log debug "MAC address associated with interface ${OCF_RESKEY_interface}: ${MAC_ADDR}"

    echo $MAC_ADDR
    return $OCF_SUCCESS
}
