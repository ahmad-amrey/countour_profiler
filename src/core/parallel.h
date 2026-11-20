// Minimal std::thread based parallel-for (no external dependencies).
#pragma once
#include <algorithm>
#include <thread>
#include <vector>

namespace cp {

template <class F>
void parallelFor(int n, F&& f) {
    if (n <= 0) return;
    int nt = int(std::max(1u, std::thread::hardware_concurrency()));
    nt = std::min(nt, std::max(1, n / 8));
    if (nt <= 1) {
        for (int i = 0; i < n; ++i) f(i);
        return;
    }
    std::vector<std::thread> th;
    const int chunk = (n + nt - 1) / nt;
    for (int t = 0; t < nt; ++t) {
        const int a = t * chunk, b = std::min(n, a + chunk);
        if (a >= b) break;
        th.emplace_back([a, b, &f] {
            for (int i = a; i < b; ++i) f(i);
        });
    }
    for (auto& x : th) x.join();
}

}  // namespace cp
