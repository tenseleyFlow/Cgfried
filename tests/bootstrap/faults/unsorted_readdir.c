#include <dirent.h>

void emit_in_enumeration_order(DIR *directory)
{
    while (readdir(directory)) {
    }
}
