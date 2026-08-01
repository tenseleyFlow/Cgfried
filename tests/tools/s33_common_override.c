int shared = 37;

int read_shared(void);

int main(void)
{
    return read_shared() != 37;
}
