/* `{}` is not in the C17 grammar (an initializer-list needs >= 1 item), but
 * gcc accepts it as an extension with a pedwarn, so it belongs in the ACCEPT
 * corpus: the reject corpus is compared against gcc -pedantic-errors, which
 * would flag this. C23 makes it standard. */
int a[] = {};
