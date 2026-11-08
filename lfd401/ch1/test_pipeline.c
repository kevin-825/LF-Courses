
#define SCALE_FACTOR 42

int compute(int x) {
    int dead_var = 999;
    return x * SCALE_FACTOR;
}

int main(void) { 
    return compute(2); 
}
