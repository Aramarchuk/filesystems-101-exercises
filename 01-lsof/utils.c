#include <ctype.h>


int is_numeric(const char *s)
{
    if (*s == '\0') {
        return 0;
    }

    for (; *s != '\0'; ++s) {
        if (!isdigit((unsigned char)*s)) {
            return 0;
        }
    }

    return 1;
}