// FLAGS: -O1
// A prebound label is laid out before the block that defines `cell`.  Address
// folding must resolve the SSA dependency, not assume parent value ids are
// smaller than their field-address children.
// EXIT_CODE: 42
struct Cell {
    int tag;
    long value;
};

static int update(struct Cell *cells, int index, int run)
{
    struct Cell *cell;

    if (run) {
        cell = &cells[index];
        if (cell->tag == 2)
            goto patch;
        cell->value = 1;
        goto patch;
    }
    return 0;

patch:
    cell->value += 40;
    return (int)cell->value + cell->tag;
}

int main(void)
{
    struct Cell cells[2] = {{1, 0}, {2, 0}};
    int result = update(cells, 1, 1);

    return result == 42 && cells[1].value == 40 ? 42 : 1;
}
