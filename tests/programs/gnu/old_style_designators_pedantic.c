// FLAGS: -std=c17 -pedantic -fsyntax-only
// WARNING_EXPECTED: obsolete use of designated initializer with ':'
// WARN_COUNT: 1
struct Item {
    int value;
};

// WARN_CHECK: pedantic obsolete use of designated initializer with ':'
struct Item item = { value: 1 };

__extension__ struct Item quiet = { value: 2 };
