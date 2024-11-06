#!/bin/sh
#
#
#   AWS Helper Scripts
#
#

: ${OCF_FUNCTIONS_DIR=${OCF_ROOT}/lib/heartbeat}
. ${OCF_FUNCTIONS_DIR}/ocf-shellfuncs

# Defaults
OCF_RESKEY_curl_retries_default="3"
OCF_RESKEY_curl_sleep_default="1"

: ${OCF_RESKEY_curl_retries=${OCF_RESKEY_curl_retries_default}}
: ${OCF_RESKEY_curl_sleep=${OCF_RESKEY_curl_sleep_default}}

# Function to enable reusable IMDS token retrieval for efficient repeated access 
# File to store the token and timestamp
TOKEN_FILE="${HA_RSCTMP}/.aws_imds_token"
TOKEN_LIFETIME=21600  # Token lifetime in seconds (6 hours)
TOKEN_EXPIRY_THRESHOLD=3600  # Renew token if less than 60 minutes (1 hour) remaining

# Function to fetch a new token
fetch_new_token() {
    TOKEN=$(curl_retry "$OCF_RESKEY_curl_retries" "$OCF_RESKEY_curl_sleep" "--show-error -sX PUT -H 'X-aws-ec2-metadata-token-ttl-seconds: $TOKEN_LIFETIME'" "http://169.254.169.254/latest/api/token")
    echo "$TOKEN $(date +%s)" > "$TOKEN_FILE"
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