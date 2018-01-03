struct Msg {
    long   i;
    int    j;
    short  k;
    double d;
};

int dummy(const Msg& m) {
    return m.j;
}
