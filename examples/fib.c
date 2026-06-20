int fib(int i) {
    if (i < 2) return i;
    return fib(i-1) + fib(i-2);
}

int main() {
    int n;
    
    n = 10;
    printf("fib(%2d) = %d\n", n, fib(n));
    return 0;
}