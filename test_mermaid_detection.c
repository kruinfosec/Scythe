#include <glib.h>
#include <stdio.h>
#include <string.h>

int main() {
    // Simulate the AI response
    const char *text = 
        "Here's the Mermaid.js diagram for the OSI Model:\n"
        "\n"
        "graph TB A[\"Physical\"] --> B[\"Data Link\"] B --> C[\"Network\"]\n"
        "\n"
        "OSI Layer Overview:\n";
    
    printf("Input text:\n%s\n", text);
    printf("================================\n");
    
    const char *p = text;
    int line_num = 1;
    
    while (*p) {
        // Check if at start of line
        if (p == text || *(p-1) == '\n') {
            const char *line_start = p;
            
            // Skip leading whitespace
            while (*line_start == ' ' || *line_start == '\t') line_start++;
            
            // Check for graph
            if (g_str_has_prefix(line_start, "graph")) {
                printf("Line %d: FOUND 'graph' at position %ld\n", line_num, p - text);
                printf("  Line content: %.60s\n", line_start);
                
                // Check full patterns
                if (g_str_has_prefix(line_start, "graph TB")) {
                    printf("  ✅ MATCHED: graph TB\n");
                } else if (g_str_has_prefix(line_start, "graph TD")) {
                    printf("  ✅ MATCHED: graph TD\n");
                } else {
                    printf("  ❌ No TB/TD match\n");
                }
            }
        }
        
        if (*p == '\n') line_num++;
        p++;
    }
    
    return 0;
}
