# P2 Fixes Test Plan

## SEC-001: Command Injection in ZIP Export
**Status:** 
**Steps:**
1. Open Notes feature in Scythe
2. Create a new note with title containing special characters: `Test$(whoami);&ls`
3. Add some content to the note
4. Click Export → Export as ZIP
5. Check that ZIP is created successfully without command execution
6. Verify ZIP contains the note file

**Expected:** ZIP export succeeds, no commands executed
**Actual:** 

---

## PERF-001: Memory Leak in session_list_saved()
**Status:**
**Steps:**
1. Create 5-10 saved sessions
2. Open "Load Session" dialog repeatedly (10+ times)
3. Close dialog without loading
4. Monitor memory usage in `htop` or `top`

**Expected:** Memory remains stable, no continuous growth
**Actual:**

---

## PERF-002: Memory Leak in note_list_by_session()
**Status:**
**Steps:**
1. Create a session with 10+ notes
2. Switch between sessions multiple times
3. Reload note list repeatedly
4. Monitor memory usage

**Expected:** Memory remains stable, no leaks
**Actual:**

---

## SEC-002: XSS Vulnerability in WebView
**Status:**
**Steps:**
1. Open AI chat interface
2. Send message: `<script>alert('XSS')</script>`
3. Send message: `<img src=x onerror=alert('XSS')>`
4. Send message with HTML: `<b>Bold</b> <i>Italic</i>`
5. Check AI response display

**Expected:** Script tags displayed as text, not executed. No alert popups.
**Actual:**

---

## PERF-003: Excessive Rendering Performance
**Status:**
**Steps:**
1. Open Notes editor
2. Type rapidly: `**bold** *italic* - list item`
3. Type continuously for 30 seconds with markdown syntax
4. Observe typing lag and CPU usage

**Expected:** Smooth typing, no lag, moderate CPU usage
**Actual:**

---

## FUNC-001: Undo Stack Pollution
**Status:**
**Steps:**
1. Open Notes editor
2. Type: `This is **bold** text`
3. Wait 1 second for markdown rendering
4. Press Ctrl+Z (undo) once
5. Observe what gets undone
6. Type: `- First item` then Enter
7. Type: `- Second item`
8. Press Ctrl+Z twice
9. Verify undo behavior

**Expected:** 
- First undo removes "text", not the bold formatting tags
- Undo only removes user-typed content, not auto-continued list markers
**Actual:**

---

## Additional Tests

### Memory Leak Detection with Valgrind
Run: `valgrind --leak-check=full --track-origins=yes ./bin/scythe-fixed 2>&1 | tee valgrind_output.txt`

Perform all operations above, then close app and check for:
- "definitely lost" bytes
- "indirectly lost" bytes

**Expected:** Minimal leaks (GTK has some unavoidable leaks)
**Actual:**
