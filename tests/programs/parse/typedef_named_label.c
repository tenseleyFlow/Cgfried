// FLAGS: --dump-ast
// CHECK: GOTO Ins
// CHECK: LABEL Ins
// A typedef name remains available in the ordinary namespace while the same
// spelling names a label in the function-wide label namespace. QBE uses this
// exact shape in its parser.
typedef struct Ins Ins;

void parse_one(int jump)
{
    if (jump)
        goto Ins;
Ins:
    return;
}
