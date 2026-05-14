#include "util.h"

bool ends_with(const char *str, const char *suffix) {
    if (!str || !suffix) return false;
    size_t len_str = strlen(str);
    size_t len_suffix = strlen(suffix);

    if (len_suffix > len_str) {
        return false;
    }

    // Compare characters starting from the calculated offset
    return memcmp(str + len_str - len_suffix, suffix, len_suffix) == 0;
}
