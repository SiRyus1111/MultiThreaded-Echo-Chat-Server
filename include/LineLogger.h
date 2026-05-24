#pragma once

#include <iostream>
#include <utility>
#include <sstream> // ostringstream 사용하기 위해 인클루드 / ostringstream은 숫자나 다른 타입의 변수들을 문자열 형태로 결합할 때 씀.
#include <stdint.h>
#include <mutex>
#include <string>

class LineLogger{
private:
    std::mutex output_mutex_;
public:
    LineLogger() = default;
    LineLogger& operator=(const LineLogger&) = delete;
    LineLogger(const LineLogger&) = delete;
    LineLogger& operator=(LineLogger&&) = delete;
    LineLogger(LineLogger&&) = delete;
    

    template <typename... Args>
    void WriteLog(Args&&... args) {
        std::ostringstream oss;
        (oss << ... << std::forward<Args>(args));
        oss << '\n';

        std::lock_guard<std::mutex> lock(output_mutex_); // 락을 잡는 시간을 최소화해서 락 경합 최소화 및 데드락 가능성 감소
        std::cout << oss.str();
    }

    template <typename... Args>
    void WriteSessionLog(
        uint64_t sessionId, 
        const std::string& ipaddr, 
        uint16_t port, 
        const std::string& logType, 
        Args&&... args) {
    
        WriteLog("[SessionID ", sessionId, "]",
                "[", ipaddr, ":", port, "]",
                "[", logType, "] ",
                std::forward<Args>(args)...);
    }

    
};