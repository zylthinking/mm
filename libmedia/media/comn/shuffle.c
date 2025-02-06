
/* determine if an odd number is prime or not */
static int isprime(int n)
{
    int x;
    for (x = 3; x * x <= n; x += 2)
        if (n % x == 0)
            return 0;
    return 1;
}


#define next(x) (x*m)%(2*m-1)
#define prev(x) (x%m+x/m*n)
static int cycle(int a[], int n, int m, int i)
{
    int x, y, z, c;
    for (x = i, y = next(x), z = a[prev(x)], c = 1;
         y != i; x = y, y = next(x), ++c){
        a[prev(x)] = a[prev(y)];
    }
    a[prev(x)] = z;
    return c;
}

static void rotate(int a[], int n, int m)
{
    int s, t;
    for (s = m + n, t = m; m < n; m++, n++,(t >= m) || (t += n - m), (n < s) || (n = t))
        a[m] ^= a[n] ^= a[m] ^= a[n];
}

// in-place transposition of matrix a[2][n]
void shuffle(int a[], int n)
{
    int m;
    for (; n > 1; n -= m, a += 2 * m) {
        for (m = n & ~1; m >= 2; m -= 2) {
            if (isprime(m * 2 - 1) && isprime(m - 1))
                break;
        }

        if (cycle(a, n, m, 1) != 2 * m - 2)
            cycle(a, n, m, m - 1);
        if (n > m)
            rotate(a, n, m);
    }
}