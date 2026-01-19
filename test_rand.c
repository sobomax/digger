#include <stdio.h>
#include <stdint.h>
int main() {
    int32_t seed = 12345;
    for(int i=1; i<=3; i++) {
        seed = seed * 1103515245 + 1;
        printf("Level %d: rand = %d\n", i, (int)(seed & 0x7fffffffl) % 10);
    }
    return 0;
}
