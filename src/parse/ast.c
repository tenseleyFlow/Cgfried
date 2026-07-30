#include "parse/ast.h"

#include <string.h>

AstNode *ast_new(Arena *a, AstKind k, Span sp)
{
    AstNode *n = arena_alloc(a, sizeof(AstNode), _Alignof(AstNode));

    memset(n, 0, sizeof(*n)); /* arena memory is never pre-zeroed */
    n->kind = k;
    n->span = sp;
    return n;
}

AstType *ast_type_new(Arena *a, AstTypeKind k, Span sp)
{
    AstType *t = arena_alloc(a, sizeof(AstType), _Alignof(AstType));

    memset(t, 0, sizeof(*t));
    t->kind = k;
    t->span = sp;
    return t;
}

AstType *ast_type_chain(AstType *head, AstType *tail)
{
    AstType *p = head;

    if (!head)
        return tail;
    while (p->next)
        p = p->next;
    p->next = tail;
    return head;
}

const char *ast_base_type_name(AstBaseType b)
{
    switch (b) {
    case ABT_NONE:
        return "int"; /* implicit int */
    case ABT_VOID:
        return "void";
    case ABT_CHAR:
        return "char";
    case ABT_SCHAR:
        return "signed char";
    case ABT_UCHAR:
        return "unsigned char";
    case ABT_SHORT:
        return "short";
    case ABT_USHORT:
        return "unsigned short";
    case ABT_INT:
        return "int";
    case ABT_UINT:
        return "unsigned int";
    case ABT_LONG:
        return "long";
    case ABT_ULONG:
        return "unsigned long";
    case ABT_LLONG:
        return "long long";
    case ABT_ULLONG:
        return "unsigned long long";
    case ABT_FLOAT:
        return "float";
    case ABT_DOUBLE:
        return "double";
    case ABT_LDOUBLE:
        return "long double";
    case ABT_BOOL:
        return "_Bool";
    case ABT_RECORD:
        return "record";
    case ABT_ENUM:
        return "enum";
    case ABT_TYPEDEF:
        return "typedef-name";
    }
    CGF_ICE("ast_base_type_name: bad base %d", (int)b);
}

static void render_quals(u32 q, Buf *out)
{
    if (q & AST_QUAL_CONST)
        buf_printf(out, "const ");
    if (q & AST_QUAL_VOLATILE)
        buf_printf(out, "volatile ");
    if (q & AST_QUAL_RESTRICT)
        buf_printf(out, "restrict ");
    if (q & AST_QUAL_ATOMIC)
        buf_printf(out, "_Atomic ");
}

/* Renders the chain OUTSIDE-IN, which is how a declarator reads in
 * English: "array 3 of ptr to func(void) ret int". The chain itself is
 * built inside-out by the parser, so this walk IS the round-trip proof. */
void ast_type_render(const AstType *t, Buf *out)
{
    if (!t) {
        buf_printf(out, "<none>");
        return;
    }
    switch (t->kind) {
    case ATY_PTR:
        /* The quals AFTER the '*' qualify the POINTER, so they lead here:
         * `char *const s` reads "const ptr to char", not "ptr to const
         * char" (which is `const char *s`, a qualifier on the pointee that
         * lives on the BASE node further down the chain). */
        render_quals(t->ptr_quals, out);
        buf_printf(out, "ptr to ");
        ast_type_render(t->next, out);
        return;
    case ATY_ARRAY:
        if (t->array_star)
            buf_printf(out, "array * of ");
        else if (t->array_size)
            buf_printf(out, "array [expr] of ");
        else
            buf_printf(out, "array of ");
        ast_type_render(t->next, out);
        return;
    case ATY_FUNC: {
        u32 i;
        buf_printf(out, "func(");
        if (t->is_kr_list) {
            buf_printf(out, "K&R:");
            for (i = 0; i < t->nparams; i++)
                buf_printf(out, "%s%s", i ? "," : "",
                           t->params[i].name ? t->params[i].name : "?");
        } else if (t->has_no_params) {
            buf_printf(out, "unspecified");
        } else if (t->nparams == 0) {
            buf_printf(out, "void");
        } else {
            for (i = 0; i < t->nparams; i++) {
                if (i)
                    buf_printf(out, ", ");
                ast_type_render(t->params[i].type, out);
            }
            if (t->is_variadic)
                buf_printf(out, ", ...");
        }
        buf_printf(out, ") ret ");
        ast_type_render(t->next, out);
        return;
    }
    case ATY_BASE:
        render_quals(t->quals, out);
        if (t->base == ABT_TYPEDEF)
            buf_printf(out, "%s", t->typedef_name);
        else if (t->base == ABT_RECORD && t->record)
            buf_printf(out, "%s %s", t->record->is_union ? "union" : "struct",
                       t->record->tag ? t->record->tag : "<anon>");
        else if (t->base == ABT_ENUM && t->record)
            buf_printf(out, "enum %s",
                       t->record->tag ? t->record->tag : "<anon>");
        else
            buf_printf(out, "%s", ast_base_type_name(t->base));
        return;
    }
    CGF_ICE("ast_type_render: bad kind %d", (int)t->kind);
}
