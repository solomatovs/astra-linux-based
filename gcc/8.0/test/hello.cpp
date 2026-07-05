#include <iostream>
#include <vector>
#include <numeric>

// Тест тулчейна: cc1plus + as + ld + libstdc++ + crt/libc.
// iostream и vector тянут статическую инициализацию и libstdc++,
// поэтому успешный запуск подтверждает всю цепочку целиком.
int main() {
    std::vector<int> v(10);
    std::iota(v.begin(), v.end(), 1);
    int sum = std::accumulate(v.begin(), v.end(), 0);
    std::cout << "hello from g++ " << __VERSION__ << "\n";
    std::cout << "sum(1..10) = " << sum << "\n";
    return sum == 55 ? 0 : 1;
}
