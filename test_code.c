#include <stdio.h>
#define MAX 100

// Main function of the test code
int main() {
    int count = 10;
    char *message = "Hello, rip pager!";
    for (int i = 0; i < count; i++) {
        printf("%d: %s\n", i, message);
    }
    return 0;
}
