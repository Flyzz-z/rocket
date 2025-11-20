#ifndef ROCKET_COMMON_SINGLETON_H
#define ROCKET_COMMON_SINGLETON_H

template <typename T> class Singleton {
public:
  Singleton(const Singleton &) = delete;
  Singleton &operator=(const Singleton &) = delete;

	static T* GetInstance() {
		static T instance;
		return &instance;
	}

	// 完美转发版，支持带参数的构造函数
  // template <typename... Args> static T &getInstance(Args &&...args) {
  //   static std::once_flag init_flag;
  //   static std::unique_ptr<T> instance;

  //   std::call_once(init_flag, [&args...] {
  //     instance = std::make_unique<T>(std::forward<Args>(args)...);
  //   });
  // }

protected:
  Singleton() = default;
};

#endif