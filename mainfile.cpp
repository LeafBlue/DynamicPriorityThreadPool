/*#include "thread_pool.hpp"
#include <iostream>
#include <vector>
#include <future>

int main() {
    thread_pool pool(false, 4); // ��ʼ 4 ���̣߳����Զ�����
    std::vector<std::future<int>> results;

    // �ύ 10 ������
    for (int i = 0; i < 10; ++i) {
        results.emplace_back(pool.addtask([i]() {
            return i * i; // ����ƽ��
            }));
    }

    // �����߳���˳��������
    std::cout << "�������ܲ��Խ��: ";
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

// �̰߳�ȫ��ʱ�����¼��
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
    // ��ʼ���̳߳أ��Զ�������������ʼ4�̣߳�
    thread_pool pool(true);
    TaskRecorder recorder;
    std::vector<std::promise<void>> blockers(4);
    std::vector<std::future<void>> blocker_futures;

    // ����1����4����������ס���г�ʼ�߳�
    std::cout << "===== Blocking all initial threads =====\n";
    for (int i = 0; i < 4; ++i) {
        auto task = [&recorder, &blockers, i]() {
            std::promise<void> local_promise;
            blockers[i] = std::move(local_promise);
            recorder.record(0);  // ��¼��������ʼʱ��
            blockers[i].get_future().wait();  // ���õȴ�
            };
        blocker_futures.push_back(pool.addtask(1, task));
    }

    // �ȴ�������������ʵ�ʿ�ʼִ��
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // ����2����Ӳ�������ע�����ȼ�˳��
    std::cout << "\n===== Adding test tasks =====\n";
    const std::vector<int> test_priorities = { 5, 3, 4, 2, 6 };
    for (int prio : test_priorities) {
        pool.addtask(prio, [prio, &recorder] {
            recorder.record(prio);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            });
    }

    // ����3���ͷ��������������ڹر�ǰ��
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::cout << "\n===== Releasing blockers =====\n";
    for (auto& blocker : blockers) {
        blocker.set_value();
    }

    // �ȴ��������
    std::this_thread::sleep_for(std::chrono::seconds(1));
    pool.shutdown();

    // ����4����ִ֤��˳��
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
    thread_pool pool(true, 4); // ��ʼ 4 ���̣߳��Զ�����
    std::vector<std::future<void>> results;

    // �ύ 20 ������
    for (int i = 0; i < 20; ++i) {
        results.emplace_back(pool.addtask([i]() {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            // �����̳߳��нӿڴ�ӡ��ǰ�߳���
            }));
    }

    // �ȴ������������
    for (auto& res : results) {
        res.get();
    }

    std::cout << "��̬�̵߳����������\n";
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
    thread_pool pool(false, 16); // ��ʼ 16 ���߳�
    std::vector<std::future<long long>> results;

    // �ύ 50 ������
    for (int i = 0; i < 50; ++i) {
        results.emplace_back(pool.addtask([]() {
            return fibonacci(40); // �����ܼ�������
            }));
    }

    // �ȴ����
    for (auto& res : results) {
        res.get();
    }

    std::cout << "�߸��ز������\n";
    return 0;
}*/
