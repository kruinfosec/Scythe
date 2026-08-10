//  ===== MERMAID SYNTAX AUTO-REPAIR =====
function sanitizeMermaid(code) {
    const original = code;
    let repairs = [];

    // Rule 1: Fix 'sequence' -> 'sequenceDiagram'
    if (code.match(/^\s*sequence\s+/m)) {
        code = code.replace(/^(\s*)sequence(\s+)/gm, '$1sequenceDiagram$2');
        repairs.push('Fixed: sequence → sequenceDiagram');
    }

    // Rule 2: Remove spaces from node IDs (e.g., 'Network Layer' -> 'NetworkLayer')
    // Match patterns like: 'id' followed by space then 'word' before brackets or arrows
    code = code.replace(/\b(\w+)\s+(\w+)\s*(?=\[|\(|-->|--|\||;)/g, function (match, word1, word2) {
        repairs.push(`Fixed ID: '${word1} ${word2}' → '${word1}${word2}'`);
        return word1 + word2;
    });

    // Rule 3: Split chained arrows (A --> B --> C) into separate lines
    const lines = code.split('\n');
    const expandedLines = [];
    lines.forEach(line => {
        const arrowCount = (line.match(/-->/g) || []).length;
        if (arrowCount > 1) {
            // Parse and split: 'A --> B --> C' becomes 'A --> B' and 'B --> C'
            const parts = line.split('-->').map(p => p.trim());
            for (let i = 0; i < parts.length - 1; i++) {
                expandedLines.push(`    ${parts[i]} --> ${parts[i + 1]}`);
            }
            repairs.push(`Split chained arrows: ${arrowCount} arrows`);
        } else {
            expandedLines.push(line);
        }
    });
    code = expandedLines.join('\n');

    // Rule 4: Fix common typos in diagram keywords
    code = code.replace(/\bgrapTD\b/gi, 'graph TD');
    code = code.replace(/\bflowchar\b/gi, 'flowchart');

    // Log repairs to console for debugging
    if (repairs.length > 0) {
        console.log('%c[Mermaid Auto-Repair]', 'color: #10b981; font-weight: bold');
        repairs.forEach(r => console.log(`  ✓ ${r}`));
        console.log('Original:', original);
        console.log('Sanitized:', code);
    }

    return code;
}

// Apply sanitization to all Mermaid blocks before rendering
function initMermaid() {
    document.querySelectorAll('.mermaid').forEach(el => {
        const original = el.textContent;
        const sanitized = sanitizeMermaid(original);
        el.textContent = sanitized;
    });
}

// Call initMermaid before Mermaid initialization
initMermaid();
