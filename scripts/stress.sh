#!/bin/bash
# Stress test: N listeners and N senders, all at the same time.
# Run from the project folder:  bash scripts/stress.sh
N=12
PW=password123
BIN=./build
OUT=tmp/stress

LOGIN_WAIT=${LOGIN_WAIT:-5}        # seconds to let listeners log in
DELIVER_WAIT=${DELIVER_WAIT:-2}    # seconds to let deliveries arrive

rm -rf "$OUT"
mkdir -p "$OUT"

# Step 1: register the 2N users (seq -w pads: 01 02 ... 12).
# On a repeat run the server answers "username unavailable"; we hide that.
if [ -z "$SKIP_REGISTER" ]; then
    for i in $(seq -w 1 $N); do
        $BIN/test_register_client register "l$i" $PW > /dev/null
        $BIN/test_register_client register "s$i" $PW > /dev/null
    done
fi

# Step 2: start all listeners in the background and remember their process ids.
lpids=()
for i in $(seq -w 1 $N); do
    stdbuf -oL $BIN/test_chat_client listen "l$i" $PW > "$OUT/l$i.out" &
    lpids+=($!)
done

# Step 3: give the listeners time to log in.
sleep "$LOGIN_WAIT"

# Step 4: start all senders at once, then wait for just those.
spids=()
for i in $(seq -w 1 $N); do
    $BIN/test_chat_client send "s$i" $PW "l$i" "msg-for-l$i" > "$OUT/s$i.out" &
    spids+=($!)
done
wait "${spids[@]}"

# Step 5: let the deliveries arrive, then stop the listeners.
sleep "$DELIVER_WAIT"
kill "${lpids[@]}" 2>/dev/null
wait "${lpids[@]}" 2>/dev/null

# Step 6: check the results.
ok=0
for i in $(seq -w 1 $N); do
    f="$OUT/l$i.out"
    if [ "$(grep -c '^from ' "$f")" -eq 1 ] &&
       grep -q "^from s$i: msg-for-l$i\$" "$f" &&
       grep -q 'text="sent"' "$OUT/s$i.out"; then
        ok=$((ok + 1))
    else
        echo "FAIL for pair $i:"
        cat "$f"
        cat "$OUT/s$i.out"
    fi
done
echo "$ok of $N pairs OK"
