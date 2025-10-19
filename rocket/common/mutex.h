#ifndef ROCKET_COMMON_MUTEX_H
#define ROCKET_COMMON_MUTEX_H

#include <pthread.h>


namespace rocket {

template <class T>
class ScopeMutex {

 public:
  ScopeMutex(T& mutex) : mutex_(mutex) {
    mutex_.lock();
    is_lock_ = true;
  }

  ~ScopeMutex() {
    mutex_.unlock();
    is_lock_ = false;
  }

  void lock() {
    if (!is_lock_) {
      mutex_.lock();
    }
  }

  void unlock() {
    if (is_lock_) {
      mutex_.unlock();
    }
  }

 private:

  T& mutex_;

  bool is_lock_ {false};

};


class Mutex {
 public:
  Mutex() {
    pthread_mutex_init(&mutex_, NULL);
  }

  ~Mutex() {
    pthread_mutex_destroy(&mutex_);
  }

  void lock() {
    pthread_mutex_lock(&mutex_);
  }

  void unlock() {
    pthread_mutex_unlock(&mutex_);
  }

  pthread_mutex_t* getMutex() {
    return &mutex_;
  }

 private:
  pthread_mutex_t mutex_;

};

}

#endif