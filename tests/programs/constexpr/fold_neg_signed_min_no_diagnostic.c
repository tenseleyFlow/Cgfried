// FLAGS: -fsyntax-only
/* CE_FOLD is opportunistic: the same runtime expression is not a required
 * constant context, so failed folding must remain diagnostic-free. */
int fold_neg_signed_min_no_diagnostic(void)
{
    return -(-2147483647 - 1);
}
