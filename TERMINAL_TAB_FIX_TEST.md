# Terminal Tab Dynamic Title Update - Test Guide

## Bug Fixed
Terminal tab names now update dynamically to reflect:
1. **Username changes** (kruinfosec → root after `su` or `sudo su`)
2. **Directory changes** (~ → ~/Desktop → /etc)

## How It Works
- Reads the actual process UID from `/proc/<pid>/status`
- Converts UID to username using `getpwuid()`
- Polls every 1 second to detect changes
- Updates tab title: `username@hostname:directory`

## Test Steps

### Test 1: Directory Changes
1. Open Scythe terminal
2. Verify tab shows: `kruinfosec@knochen:~`
3. Run: `cd /tmp`
4. Wait 1-2 seconds
5. ✅ Tab should update to: `kruinfosec@knochen:/tmp`
6. Run: `cd ~/Desktop`
7. Wait 1-2 seconds
8. ✅ Tab should update to: `kruinfosec@knochen:~/Desktop`

### Test 2: User Changes with `sudo su`
1. In terminal, run: `sudo su`
2. Enter password
3. Wait 1-2 seconds
4. ✅ Tab should update to: `root@knochen:~` (or current directory)
5. Run: `cd /etc`
6. Wait 1-2 seconds
7. ✅ Tab should update to: `root@knochen:/etc`
8. Run: `exit` to return to kruinfosec
9. Wait 1-2 seconds
10. ✅ Tab should update back to: `kruinfosec@knochen:/etc`

### Test 3: User Changes with `su root`
1. In terminal, run: `su root`
2. Enter root password
3. Wait 1-2 seconds
4. ✅ Tab should update to: `root@knochen:~`
5. Run: `cd /var/log`
6. Wait 1-2 seconds
7. ✅ Tab should update to: `root@knochen:/var/log`

### Test 4: Multiple Tabs
1. Open 3 terminals
2. In Tab 1: stay as kruinfosec in ~
3. In Tab 2: `sudo su` and `cd /etc`
4. In Tab 3: `cd ~/Desktop`
5. ✅ Each tab should show different titles:
   - Tab 1: `kruinfosec@knochen:~`
   - Tab 2: `root@knochen:/etc`
   - Tab 3: `kruinfosec@knochen:~/Desktop`

## Technical Details

### Files Modified
- `src/terminal.c`

### Changes Made
1. Added `#include <pwd.h>` and `#include <sys/types.h>`
2. Modified `update_vte_tab_title()` to:
   - Read UID from `/proc/<pid>/status`
   - Convert UID to username with `getpwuid()`
   - Use dynamic username instead of `g_get_user_name()`
3. Properly free allocated username string

### Polling Mechanism
- Already existed: `terminal_poll_title_cb()` runs every 1000ms
- Signals connected: `window-title-changed`, `current-directory-uri-changed`
- Reads foreground process group (handles su/sudo correctly)

## Expected Behavior
✅ Tab titles update within 1-2 seconds of any:
- `cd` command (directory change)
- `su`, `sudo su`, `su root` (user change)
- `exit` from su session (user change back)

## Known Limitations
- 1 second polling interval (by design, to avoid excessive CPU usage)
- Custom-titled tabs (user renamed) won't auto-update (by design)
