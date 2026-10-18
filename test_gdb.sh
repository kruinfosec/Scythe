#!/bin/bash
export DEBIAN_FRONTEND=noninteractive
apt-get update && apt-get install -y xvfb xdotool
xvfb-run -a -s "-screen 0 1024x768x24" gdb -batch -ex "run" -ex "bt full" -ex "quit" --args ./bin/scythe-fixed &
pid=$!
sleep 5
# Now we need to click the upload image button? Wait, we can't easily.
