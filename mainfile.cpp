/*#include "thread_pool.hpp"
#include <iostream>
#include <vector>
#include <future>

int main() {
    thread_pool pool(false, 4); // 初始 4 个线程，不自动调整
    std::vector<std::future<int>> results;

    // 提交 10 个任务
    for (int i = 0; i < 10; ++i) {
        results.emplace_back(pool.addtask([i]() {
            return i * i; // 计算平方
            }));
    }

    // 在主线程中顺序输出结果
    std::cout << "基本功能测试结果: ";
    for (auto& res : results) {
        std::cout << res.get() << " ";
    }
    std::cout << std::endl;

    return 0;
}
*/
#include "thread_pool.hpp"
#include <iostream>
#include <chrono>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <future>

// 线程安全的时间戳记录器
struct TaskRecorder {
    std::mutex mtx;
    std::vector<std::pair<int, std::chrono::high_resolution_clock::time_point>> records;

    void record(int priority) {
        std::lock_guard<std::mutex> lock(mtx);
        records.emplace_back(priority, std::chrono::high_resolution_clock::now());
    }

    void print() {
        for (const auto& record : records) {
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                record.second.time_since_epoch()
            );
            std::cout << "Priority " << record.first << " started at "
                << ms.count() << "ms\n";
        }
    }
};

int main() {
    // 初始化线程池（自动调整开启，初始4线程）
    thread_pool pool(true);
    TaskRecorder recorder;
    std::vector<std::promise<void>> blockers(4);
    std::vector<std::future<void>> blocker_futures;

    // 步骤1：用4个长期任务卡住所有初始线程
    std::cout << "===== Blocking all initial threads =====\n";
    for (int i = 0; i < 4; ++i) {
        auto task = [&recorder, &blockers, i]() {
            std::promise<void> local_promise;
            blockers[i] = std::move(local_promise);
            recorder.record(0);  // 记录阻塞任务开始时间
            blockers[i].get_future().wait();  // 永久等待
            };
        blocker_futures.push_back(pool.addtask(1, task));
    }

    // 等待所有阻塞任务实际开始执行
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 步骤2：添加测试任务（注意优先级顺序）
    std::cout << "\n===== Adding test tasks =====\n";
    const std::vector<int> test_priorities = { 5, 3, 4, 2, 6 };
    for (int prio : test_priorities) {
        pool.addtask(prio, [prio, &recorder] {
            recorder.record(prio);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            });
    }

    // 步骤3：释放所有阻塞任务（在关闭前）
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::cout << "\n===== Releasing blockers =====\n";
    for (auto& blocker : blockers) {
        blocker.set_value();
    }

    // 等待任务完成
    std::this_thread::sleep_for(std::chrono::seconds(1));
    pool.shutdown();

    // 步骤4：验证执行顺序
    std::cout << "\n===== Execution Order =====\n";
    recorder.print();

    return 0;
}
/*
#include "thread_pool.hpp"
#include <iostream>
#include <vector>
#include <future>
#include <chrono>

int main() {
    thread_pool pool(true, 4); // 初始 4 个线程，自动调整
    std::vector<std::future<void>> results;

    // 提交 20 个任务
    for (int i = 0; i < 20; ++i) {
        results.emplace_back(pool.addtask([i]() {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            // 假设线程池有接口打印当前线程数
            }));
    }

    // 等待所有任务完成
    for (auto& res : results) {
        res.get();
    }

    std::cout << "动态线程调整测试完成\n";
    return 0;
}*/
/*
#include "thread_pool.hpp"
#include <iostream>
#include <vector>
#include <future>

long long fibonacci(int n) {
    if (n <= 1) return n;
    return fibonacci(n - 1) + fibonacci(n - 2);
}

int main() {
    thread_pool pool(false, 16); // 初始 16 个线程
    std::vector<std::future<long long>> results;

    // 提交 50 个任务
    for (int i = 0; i < 50; ++i) {
        results.emplace_back(pool.addtask([]() {
            return fibonacci(40); // 计算密集型任务
            }));
    }

    // 等待完成
    for (auto& res : results) {
        res.get();
    }

    std::cout << "高负载测试完成\n";
    return 0;
}*/
