#include <iostream>
#include <vector>
#include <numeric>

// Тест clang++ поверх собранного libc++ (+libc++abi/libunwind) и lld.
int main() {
    std::vector<int> v(10);
    std::iota(v.begin(), v.end(), 1);
    std::cout << "hello from clang++ / libc++, sum(1..10) = "
              << std::accumulate(v.begin(), v.end(), 0) << "\n";
    return 0;
}
