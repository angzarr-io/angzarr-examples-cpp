#!/usr/bin/env bash
# Acceptance tests for poker example applications (C++).
#
# These tests run against a deployed Kubernetes cluster with the poker apps.
# Commands are sent to aggregate coordinator sidecars via grpcurl.
#
# Environment variables:
#   PLAYER_URL   Player aggregate coordinator (default: localhost:1310)
#   PROTOSET     Path to protobuf descriptor set for type resolution (optional)

set -euo pipefail

PLAYER_URL="${PLAYER_URL:-localhost:1310}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Build grpcurl flags for type resolution.
# If a protoset (descriptor set) file is available, use it so grpcurl can
# resolve Any types. Otherwise fall back to server reflection.
GRPCURL_PROTO_FLAGS=""
if [ -n "${PROTOSET:-}" ] && [ -f "${PROTOSET}" ]; then
    GRPCURL_PROTO_FLAGS="-protoset ${PROTOSET}"
    echo "Using descriptor set: ${PROTOSET}"
fi
PASS=0
FAIL=0
ERRORS=""

# Generate a random hex string to use as a UUID value (base64-encoded for protobuf bytes)
new_uuid_b64() {
    # 16 random bytes, base64-encoded
    head -c 16 /dev/urandom | base64 | tr -d '\n'
}

# Send a command via grpcurl and capture output + exit code
# Args: $1=address $2=json_payload
# Sets: GRPC_OUTPUT, GRPC_EXIT
send_command() {
    local addr="$1"
    local payload="$2"
    GRPC_OUTPUT=""
    GRPC_EXIT=0
    # shellcheck disable=SC2086
    GRPC_OUTPUT=$(grpcurl -plaintext $GRPCURL_PROTO_FLAGS -d "$payload" "$addr" angzarr.CommandHandlerCoordinatorService/HandleCommand 2>&1) || GRPC_EXIT=$?
}

# Build a CommandRequest JSON payload
# Args: $1=domain $2=root_b64 $3=type_url $4=command_value_b64 $5=sequence(default 0)
make_request() {
    local domain="$1"
    local root_b64="$2"
    local type_url="$3"
    local cmd_b64="$4"
    local sequence="${5:-0}"
    local correlation_id
    correlation_id="$(uuidgen 2>/dev/null || cat /proc/sys/kernel/random/uuid)"

    cat <<EOF
{
  "command": {
    "cover": {
      "domain": "${domain}",
      "root": { "value": "${root_b64}" },
      "correlationId": "${correlation_id}"
    },
    "pages": [{
      "header": { "sequence": ${sequence} },
      "command": {
        "@type": "${type_url}",
        ${cmd_b64}
      }
    }]
  },
  "syncMode": "SIMPLE"
}
EOF
}

pass() {
    PASS=$((PASS + 1))
    echo "  PASS: $1"
}

fail() {
    FAIL=$((FAIL + 1))
    ERRORS="${ERRORS}\n  FAIL: $1"
    echo "  FAIL: $1"
}

# ---------------------------------------------------------------------------
# Test 1: Connectivity
# ---------------------------------------------------------------------------
test_connectivity() {
    echo "=== Test: Player Aggregate Connectivity ==="
    local uuid_b64
    uuid_b64=$(new_uuid_b64)

    local payload
    payload=$(make_request "player" "$uuid_b64" \
        "type.googleapis.com/examples.RegisterPlayer" \
        "\"displayName\": \"ConnectivityProbe\", \"email\": \"probe@example.com\", \"playerType\": \"HUMAN\"")

    send_command "$PLAYER_URL" "$payload"

    # Any response (even an error) means the service is reachable.
    # Only a connection failure (exit code from grpcurl for network errors) is a problem.
    if echo "$GRPC_OUTPUT" | grep -q "Failed to dial\|connection refused\|context deadline exceeded\|transport: Error"; then
        fail "Cannot reach player aggregate at $PLAYER_URL"
    else
        pass "Player aggregate reachable at $PLAYER_URL"
    fi
}

# ---------------------------------------------------------------------------
# Test 2: RegisterPlayer
# ---------------------------------------------------------------------------
test_register_player() {
    echo "=== Test: RegisterPlayer ==="
    local uuid_b64
    uuid_b64=$(new_uuid_b64)
    local short_id="${uuid_b64:0:8}"

    local payload
    payload=$(make_request "player" "$uuid_b64" \
        "type.googleapis.com/examples.RegisterPlayer" \
        "\"displayName\": \"AcceptanceTestPlayer\", \"email\": \"test-${short_id}@example.com\", \"playerType\": \"HUMAN\"")

    send_command "$PLAYER_URL" "$payload"

    if [ "$GRPC_EXIT" -ne 0 ]; then
        fail "RegisterPlayer returned error (exit=$GRPC_EXIT): $GRPC_OUTPUT"
        return
    fi

    # Verify events are present in the response
    if echo "$GRPC_OUTPUT" | grep -q '"events"'; then
        pass "RegisterPlayer returned events"
    else
        fail "RegisterPlayer response missing events: $GRPC_OUTPUT"
    fi
}

# ---------------------------------------------------------------------------
# Test 3: Register then Deposit
# ---------------------------------------------------------------------------
test_register_and_deposit() {
    echo "=== Test: Register and Deposit ==="
    local uuid_b64
    uuid_b64=$(new_uuid_b64)
    local short_id="${uuid_b64:0:8}"

    # Register
    local register_payload
    register_payload=$(make_request "player" "$uuid_b64" \
        "type.googleapis.com/examples.RegisterPlayer" \
        "\"displayName\": \"DepositTestPlayer\", \"email\": \"deposit-${short_id}@example.com\", \"playerType\": \"HUMAN\"")

    send_command "$PLAYER_URL" "$register_payload"

    if [ "$GRPC_EXIT" -ne 0 ]; then
        fail "Registration failed (exit=$GRPC_EXIT): $GRPC_OUTPUT"
        return
    fi
    echo "  Player registered successfully"

    # Deposit at sequence 1 with retries for eventual consistency
    local max_attempts=30
    local last_output=""
    local success=false

    for attempt in $(seq 1 $max_attempts); do
        local deposit_payload
        deposit_payload=$(make_request "player" "$uuid_b64" \
            "type.googleapis.com/examples.DepositFunds" \
            "\"amount\": { \"amount\": \"1000\", \"currencyCode\": \"USD\" }" \
            1)

        send_command "$PLAYER_URL" "$deposit_payload"
        last_output="$GRPC_OUTPUT"

        if [ "$GRPC_EXIT" -eq 0 ] && echo "$GRPC_OUTPUT" | grep -q '"events"'; then
            success=true
            break
        fi

        echo "  Deposit attempt $attempt/$max_attempts failed, retrying..."
        sleep_secs=$((attempt > 10 ? 2 : 1))
        sleep "$sleep_secs"
    done

    if [ "$success" = true ]; then
        pass "DepositFunds succeeded after registration"
    else
        fail "DepositFunds failed after $max_attempts attempts: $last_output"
    fi
}

# ---------------------------------------------------------------------------
# Test 4: Duplicate Registration Fails
# ---------------------------------------------------------------------------
test_duplicate_registration_fails() {
    echo "=== Test: Duplicate Registration Fails ==="
    local uuid_b64
    uuid_b64=$(new_uuid_b64)
    local short_id="${uuid_b64:0:8}"

    local payload
    payload=$(make_request "player" "$uuid_b64" \
        "type.googleapis.com/examples.RegisterPlayer" \
        "\"displayName\": \"DuplicateTestPlayer\", \"email\": \"dup-${short_id}@example.com\", \"playerType\": \"HUMAN\"")

    # First registration
    send_command "$PLAYER_URL" "$payload"

    if [ "$GRPC_EXIT" -ne 0 ]; then
        fail "First registration failed unexpectedly (exit=$GRPC_EXIT): $GRPC_OUTPUT"
        return
    fi
    echo "  First registration succeeded"

    # Second registration with same ID should fail
    send_command "$PLAYER_URL" "$payload"

    if [ "$GRPC_EXIT" -ne 0 ]; then
        # grpcurl returns non-zero for gRPC errors - check for AlreadyExists or FailedPrecondition
        if echo "$GRPC_OUTPUT" | grep -qE "AlreadyExists|FailedPrecondition"; then
            pass "Duplicate registration correctly rejected"
        else
            # Any error is acceptable since the duplicate was rejected
            pass "Duplicate registration rejected (status: $GRPC_OUTPUT)"
        fi
    else
        fail "Duplicate registration should have been rejected but succeeded"
    fi
}

# ---------------------------------------------------------------------------
# Run all tests
# ---------------------------------------------------------------------------
echo "Acceptance Tests - C++ Examples"
echo "Player URL: $PLAYER_URL"
echo ""

test_connectivity
echo ""
test_register_player
echo ""
test_register_and_deposit
echo ""
test_duplicate_registration_fails

echo ""
echo "================================"
echo "Results: $PASS passed, $FAIL failed"
if [ -n "$ERRORS" ]; then
    echo -e "\nFailures:$ERRORS"
fi
echo "================================"

if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
