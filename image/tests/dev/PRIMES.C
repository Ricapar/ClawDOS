#include <stdio.h>
#include <string.h>
#include <time.h>

#define N 8000
static char comp[N + 1];

int main(void)
{
    unsigned i, j, count = 0;
    clock_t t0 = clock();

    memset(comp, 0, sizeof comp);
    for (i = 2; i * i <= N; i++)
        if (!comp[i])
            for (j = i * i; j <= N; j += i)
                comp[j] = 1;
    for (i = 2; i <= N; i++)
        if (!comp[i]) count++;

    printf("Hello from Turbo C on an 8088!\n");
    printf("Primes up to %u: %u\n", N, count);
    printf("Last few:");
    for (i = N, j = 0; j < 5; i--)
        if (!comp[i]) { printf(" %u", i); j++; }
    printf("\nSieve took %.2f s\n", (clock() - t0) / CLK_TCK);
    return 0;
}
