// FLAGS: --dump-ast
// CHECK: SWITCH (count % 8)
// CHECK: CASE 0
// CHECK: DO
// CHECK: CASE 7
// CHECK: CASE 6
// CHECK: CASE 1
// CHECK: DOWHILE ((-- n) > 0)
// Duff's device: `case` labels sitting inside a nested do-while body. This
// parses only because case/default are ORDINARY labeled statements found
// wherever parse_stmt runs, not a production belonging to the switch body.
void send(int *to, int *from, int count) {
    int n = (count + 7) / 8;
    switch (count % 8) {
    case 0: do { *to = *from++;
    case 7:      *to = *from++;
    case 6:      *to = *from++;
    case 1:      *to = *from++;
            } while (--n > 0);
    }
}
