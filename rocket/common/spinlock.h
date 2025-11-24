#ifndef ROCKET_COMMON_SPINLOCK_H
#define ROCKET_COMMON_SPINLOCK_H

#include <atomic>
#include <thread>

namespace rocket {

/**
 * @brief 基础自旋锁
 *
 * 使用 atomic_flag 实现的简单自旋锁
 * 适用于锁持有时间极短（微秒级）且竞争不频繁的场景
 */
class SpinLock {
public:
  SpinLock() = default;
  ~SpinLock() = default;

  SpinLock(const SpinLock &) = delete;
  SpinLock(SpinLock &&) = delete;
  SpinLock &operator=(const SpinLock &) = delete;
  SpinLock &operator=(SpinLock &&) = delete;

  void lock() {
    while (flag_.test_and_set(std::memory_order_acquire)) {
/*
减少不必要的功耗
改善多核系统的可扩展性​
防止过于激进的内存访问模式
提高超线程CPU的效率
*/
// CPU pause 指令，减少能耗和总线争用
#if defined(__x86_64__) || defined(_M_X64)
      __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(_M_ARM64)
      __asm__ __volatile__("yield");
#endif
    }
  }

  bool try_lock() { return !flag_.test_and_set(std::memory_order_acquire); }

  void unlock() { flag_.clear(std::memory_order_release); }

private:
  std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};

/**
 * @brief 自适应自旋锁
 *
 * 在自旋一定次数后会让出 CPU，避免长时间占用
 * 适合锁持有时间不确定的场景
 */
class AdaptiveSpinLock {
public:
  AdaptiveSpinLock() = default;
  ~AdaptiveSpinLock() = default;

  AdaptiveSpinLock(const AdaptiveSpinLock &) = delete;
  AdaptiveSpinLock(AdaptiveSpinLock &&) = delete;
  AdaptiveSpinLock &operator=(const AdaptiveSpinLock &) = delete;
  AdaptiveSpinLock &operator=(AdaptiveSpinLock &&) = delete;

  void lock() {
    int spin_count = 0;
    while (flag_.test_and_set(std::memory_order_acquire)) {
      if (++spin_count >= MAX_SPIN_COUNT) {
        // 自旋太久，让出 CPU
        std::this_thread::yield();
        spin_count = 0;
      } else {
// CPU pause 指令
#if defined(__x86_64__) || defined(_M_X64)
        __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(_M_ARM64)
        __asm__ __volatile__("yield");
#endif
      }
    }
  }

  bool try_lock() { return !flag_.test_and_set(std::memory_order_acquire); }

  void unlock() { flag_.clear(std::memory_order_release); }

private:
  static constexpr int MAX_SPIN_COUNT = 100;
  std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};

} // namespace rocket

#endif // ROCKET_COMMON_SPINLOCK_H