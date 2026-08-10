// Debug test to see HTML output
#include <gtk/gtk.h>
#include "webview_ai.h"

int main() {
    // Test the markdown converter directly
    const char *test = "**Bold with *italic inside***";
    
    // Call the internal markdown function (we need to expose it)
    printf("Input: %s\n", test);
    printf("Testing markdown conversion...\n");
    
    return 0;
}
