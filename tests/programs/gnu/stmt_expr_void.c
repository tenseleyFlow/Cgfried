// FLAGS: -fsyntax-only -std=gnu17
// ERROR_EXPECTED: initialization to 'int' from 'void'
/* The value of a statement expression is its last item ONLY IF that item is
 * an EXPRESSION STATEMENT. Both shapes below are void, and using a void
 * value where an int is wanted is an error -- gcc says "void value not
 * ignored as it ought to be", we name the types.
 *
 * BOTH shapes are here because they become void for different reasons and a
 * first implementation can easily get one right and the other wrong: the
 * first ends in a DECLARATION, the second in a non-expression STATEMENT. */
int trailing_declaration(void)
{
    int x = ({
        int q = 1;
    });

    return x;
}

int trailing_statement(void)
{
    int y = ({
        if (1)
            ;
    });

    return y;
}
