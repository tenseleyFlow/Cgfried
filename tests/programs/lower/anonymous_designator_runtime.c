struct Outer {
    struct {
        int x;
        int y;
    };
    int z;
};

int main(void)
{
    struct Outer value = {.x = 1, .y = 2, .z = 3};

    return !(value.x == 1 && value.y == 2 && value.z == 3);
}
