#ifndef ROCKET_COMMON_SINGLETON_H
#define ROCKET_COMMON_SINGLETON_H 


template<typename T>
class Singleton {
public:
    static T* GetInstance() {
        static T t;
        return &t;
    }

		Singleton(const Singleton&) = delete;

protected:
    Singleton() = default;
};

#endif 