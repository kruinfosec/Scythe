
## UI/UX Polish

### AI Panel First-Open Glitch
**Priority:** Low  
**Issue:** When opening AI panel for the first time, there's a brief (~0.1s) visual glitch  
**Cause:** WebView HTML/CSS/JS loading on first show  
**Possible Solutions:**
- Pre-load WebView in background during startup (but hidden)
- Use lightweight placeholder until WebView fully loads
- Accept as minor WebKit rendering artifact

**Status:** Flagged for later optimization

