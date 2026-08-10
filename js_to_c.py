#!/usr/bin/env python3
"""Helper script to convert JavaScript to C string literal format"""

js_code = """function sanitizeMermaid(c) {
    let r = [];
    if (c.match(/^\\s*sequence\\s+/m)) {
        c = c.replace(/^(\\s*)sequence(\\s+)/gm, '$1sequenceDiagram$2');
        r.push('Fixed: sequence');
    }
    c = c.replace(/\\b(\\w+)\\s+(\\w+)\\s*(?=\\[|\\(|-->)/g, function(m, w1, w2) {
        r.push('Fixed ID: ' + w1 + ' ' + w2);
        return w1 + w2;
    });
    if (r.length > 0) console.log('[Mermaid Auto-Repair]', r);
    return c;
}
document.addEventListener('DOMContentLoaded', function() {
    document.querySelectorAll('.mermaid').forEach(el => {
        el.textContent = sanitizeMermaid(el.textContent);
    });
});"""

# Convert to C string literal format
lines = js_code.split('\n')
for line in lines:
    # Escape backslashes first
    escaped = line.replace('\\', '\\\\')
    # Escape quotes
    escaped = escaped.replace('"', '\\"')
    # Print in C format
    print(f'"{escaped}\\n"')

