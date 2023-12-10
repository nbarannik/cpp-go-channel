#pragma once

#include <utility>
#include <optional>
#include <mutex>
#include <condition_variable>

template <class T>
class UnbufferedChannel {
public:
    void Send(const T& value) {
        std::unique_lock<std::mutex> lock_senders(sending_mtx_);
        std::unique_lock<std::mutex> lock(mtx_);
        //        ready_to_send_.wait(lock, [this] { return !is_sending_in_process_ || is_closed_;
        //        });
        CheckChannelClose();
        if (receivers_cnt_) {
            --receivers_cnt_;
            SendingInit();
            StoreValue(value);
            is_sender_found_ = true;
            has_senders_.notify_one();
            successful_receive_.wait(lock, [this] { return is_closed_ || is_value_received_; });
            CheckChannelClose();
        } else {
            ++senders_cnt_;
            has_receivers_.wait(lock, [this] { return is_closed_ || is_receiver_found_; });
            is_receiver_found_ = false;
            CheckChannelClose();
            SendingInit();
            StoreValue(value);
            successful_send_.notify_one();
            successful_receive_.wait(lock, [this] { return is_closed_ || is_value_received_; });
            CheckChannelClose();
        }
    }

    std::optional<T> Recv() {
        std::unique_lock<std::mutex> lock(mtx_);
        if (senders_cnt_) {
            --senders_cnt_;
            is_receiver_found_ = true;
            has_receivers_.notify_one();
            successful_send_.wait(lock, [this] { return is_closed_ || is_value_sent_; });
            if (is_closed_) {
                return std::nullopt;
            }
            is_value_sent_ = false;
            T received_value = value_;
            is_value_received_ = true;
            successful_receive_.notify_one();
            return received_value;
        } else {
            ++receivers_cnt_;
            has_senders_.wait(lock, [this] { return is_closed_ || is_sender_found_; });
            is_sender_found_ = false;
            if (is_closed_) {
                return std::nullopt;
            }
            is_value_sent_ = false;
            T received_value = value_;
            is_value_received_ = true;
            successful_receive_.notify_one();
            return received_value;
        }
    }

    void Close() {
        std::scoped_lock lock(mtx_, sending_mtx_);
        is_closed_ = true;
        has_receivers_.notify_all();
        has_senders_.notify_all();
        successful_receive_.notify_all();
        successful_send_.notify_all();
    }

private:
    inline void SendingInit() {
        is_value_sent_ = is_value_received_ = false;
    }

    inline void StoreValue(const T& value) {
        value_ = value;
        is_value_sent_ = true;
    }

    inline void CheckChannelClose() {
        if (is_closed_) {
            throw std::runtime_error("Sending with closed channel");
        }
    }

    std::mutex mtx_;
    std::mutex sending_mtx_;
    bool is_value_sent_ = false;
    bool is_value_received_ = false;
    bool is_closed_ = false;
    bool is_sender_found_ = false;
    bool is_receiver_found_ = false;
    size_t receivers_cnt_ = 0;
    size_t senders_cnt_ = 0;
    std::condition_variable has_receivers_;
    std::condition_variable has_senders_;
    std::condition_variable successful_receive_;
    std::condition_variable successful_send_;
    std::condition_variable ready_to_send_;
    T value_;
};
