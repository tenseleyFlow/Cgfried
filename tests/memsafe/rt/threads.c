typedef unsigned long pthread_t;

int pthread_create(pthread_t *, const void *, void *(*)(void *), void *);
int pthread_join(pthread_t, void **);
void *malloc(unsigned long);
void free(void *);
void cgf_safe_check(const void *, unsigned long, int, unsigned int);

static void *churn(void *argument)
{
    unsigned long i;
    unsigned long salt = (unsigned long)argument;

    for (i = 0; i < 4000; i++) {
        unsigned char *p = malloc(32 + ((i + salt) & 31));

        if (!p)
            return (void *)1;
        cgf_safe_check(p, 1, 1, (unsigned int)(100 + salt));
        p[0] = (unsigned char)i;
        cgf_safe_check(p, 1, 0, (unsigned int)(200 + salt));
        if (p[0] != (unsigned char)i)
            return (void *)2;
        free(p);
    }
    return (void *)0;
}

int main(void)
{
    pthread_t threads[4];
    unsigned long i;

    for (i = 0; i < 4; i++)
        if (pthread_create(&threads[i], (void *)0, churn, (void *)i) != 0)
            return 2;
    for (i = 0; i < 4; i++) {
        void *result = (void *)0;

        if (pthread_join(threads[i], &result) != 0 || result != (void *)0)
            return 3;
    }
    return 0;
}
