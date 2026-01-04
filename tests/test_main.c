#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "../tipi.h"
#define RUN_TEST(fn) { printf("Running %s... ", #fn); fn(); printf("PASS\n"); }

void test_tipi_init(void) {

}

int main(void) {
    printf("Starting TIPI Unit Tests...\n");
    
    RUN_TEST(test_tipi_init);
    
    printf("\nAll tests passed successfully.\n");
    return 0;
}