/*
 * blockingqueue.h
 *
 *  Created on: May 20, 2016
 *      Author: bravegag
 */

#ifndef BLOCKINGQUEUE_H_
#define BLOCKINGQUEUE_H_

#include <condition_variable>
#include <mutex>
#include <thread>
#include <iostream>
#include <queue>
#include <chrono>
#include <cassert>

using namespace std;

template<class Job>
class blocking_queue {
private:
    queue<Job> queue_;
    mutex mutex_;
    condition_variable not_empty_cond_;
    condition_variable not_full_cond_;
    int capacity_ = 0;

public:
    blocking_queue(int capacity) : capacity_(capacity) {
        // empty
    }

    void put(const Job& job) {
        unique_lock<mutex> lock(mutex_);
        while (queue_.size() >= capacity_) {
            not_full_cond_.wait(lock, [&]() { return (queue_.size() < capacity_); });
        }
        queue_.push(job);
        not_empty_cond_.notify_one();
        assert(!queue_.empty());
    }

    template <class duration>
    bool take(Job& job, duration const& d) {
        unique_lock<mutex> lock(mutex_);
        while (queue_.empty()) {
            if (!not_empty_cond_.wait_for(lock, d, [&]() { return !queue_.empty(); }))
                return false;
        }
        job = queue_.front();
        queue_.pop();
        not_full_cond_.notify_one();
        assert(queue_.size() < capacity_);
        return true;
    }

    int size() const {
        return queue_.size();
    }

    virtual ~blocking_queue() {
        // empty
    }
};

#endif /* BLOCKINGQUEUE_H_ */