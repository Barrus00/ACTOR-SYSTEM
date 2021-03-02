#include "minunit.h"
#include "cacti.h"

#include <stdbool.h>
#include <stdio.h>

int tests_run = 0;

static char *empty()
{
	mu_assert("empty", true);
	return 0;
}

static char *all_tests()
{
    mu_run_test(empty);
    return 0;
}

int main()
{
    char *result = all_tests();
    if (result != 0)
    {
        printf(__FILE__ ": %s\n", result);
    }
    else
    {
        printf(__FILE__ ": ALL TESTS PASSED\n");
    }
    printf(__FILE__ ": Tests run: %d\n", tests_run);

    return result != 0;
}

