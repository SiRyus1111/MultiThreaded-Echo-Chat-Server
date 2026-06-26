#pragma once

#include <iostream>
#include <utility>
#include <sstream> // ostringstream 사용하기 위해 인클루드 / ostringstream은 숫자나 다른 타입의 변수들을 문자열 형태로 결합할 때 씀.
#include <stdint.h>
#include <mutex>
#include <string>
#include "NetCommon.h"

class LineLogger{
private:
    std::mutex output_mutex_;
    LineLogger() = default; // 싱글톤 패턴을 위해 생성자를 숨김(private)
public:

    enum class LogType {
        CONNECTED, // 클라이언트 연결됨(accept() 직후, 실제로는 ClientSession의 생성자에서 출력)
        RECEIVING, // 수신 중(recv_all() 내부에서 recv() 함수를 호출한 직후마다)
        RECV_COMPLETE, // 수신 완료(ClientSession::RecvPacket() 내부 or ClientSocket::ClientSockRecv() 함수 내부)(후자가 더 자연스럽긴 함..)
        SENDING, // 송신 중(send_all() 내부에서 send() 함수를 호출한 직후마다)
        SEND_COMPLETE, // 송신 완료(ClientSession::SendPacket() 내부 or ClientSocket::ClientSockSend() 함수 내부)(이것도 후자가 더 자연스럽긴 함..)
        DISCONNECTED, // 정상 종료(HandleTransportException() 함수 내부의 정상 종료 부분에서)
        PROTOCOL_ERROR, // 사용자 정의 애플리케이션 레벨 프로토콜 에러(이것도 HandleTransportException() 함수 내부의 Protocol Error 처리 부분에서)
        TRANSPORT_ERROR, // L4 에러, 송수신 중 에러, 즉 SOCKET_ERROR가 반환되었을 때(이것도 HandleTransportException() 함수 내부의 Transport Error 처리 부분에서)
        RECEIVE_ERROR_PACKET, // header.type == PacketType::HEADER_ERROR인 패킷을 받은 경우(이것도 HandleTransportException() 함수 내부의 Peer Error 처리 부분에서)
        SEND_ERROR_PACKET // header.type == PacketType::HEADER_ERROR인 패킷을 송신하는 경우(이것도 HandleTransportException() 함수 내부의 Protocol Error 처리 부분에서)
    };

    static const char* LogTypeToCstyleString(LogType l) {
        switch (l) {
            case LogType::CONNECTED: return "CONNECTED";
            case LogType::RECEIVING: return "RECEIVING";
            case LogType::RECV_COMPLETE: return "RECV_COMPLETE";
            case LogType::SENDING: return "SENDING";
            case LogType::SEND_COMPLETE: return "SEND_COMPLETE";
            case LogType::DISCONNECTED: return "DISCONNECTED";
            case LogType::PROTOCOL_ERROR: return "PROTOCOL_ERROR";
            case LogType::TRANSPORT_ERROR: return "TRANSPORT_ERROR";
            case LogType::RECEIVE_ERROR_PACKET: return "RECEIVE_ERROR_PACKET";
            case LogType::SEND_ERROR_PACKET: return "SEND_ERROR_PACKET";
            default: return "UNKNOWN_LOGTYPE";
        }
    }

    // 복사 생성자 및 대입 연산자, 이동 생성자 까지 전부 차단
    LineLogger& operator=(const LineLogger&) = delete;
    LineLogger(const LineLogger&) = delete;
    LineLogger& operator=(LineLogger&&) = delete;
    LineLogger(LineLogger&&) = delete;

    // 정적 메서드로 유일한 LineLogger 객체를 반환해서 싱글톤 패턴 완성
    // 항상 동일한 객체의 참조를 반환함.
    static LineLogger& GetInstance() {
        static LineLogger instance; // 최초 호출 시 한 번만 객체가 생성됨.
        return instance;
    }
    

    template <typename... Args>
    void WriteLog(Args&&... args) {
        std::ostringstream oss;
        (oss << ... << std::forward<Args>(args));
        oss << '\n';

        std::lock_guard<std::mutex> lock(output_mutex_); // 락을 잡는 시간을 최소화해서 락 경합 최소화 및 데드락 가능성 감소
        std::cout << oss.str();
    }

    template <typename... Args>
    void WriteInputLog(Args&&... args) {
        std::ostringstream oss;
        (oss << ... << std::forward<Args>(args));
        
        std::lock_guard<std::mutex> lock(output_mutex_);
        std::cout << oss.str();
    }

    // [SessionID : ID][IP:Port][LogType] Message
    template <typename... Args>
    void WriteSessionLog(
        uint64_t sessionId, 
        std::string nickname,
        const char* ipaddr, 
        uint16_t port, 
        LogType logType, 
        Args&&... args) {
    
        WriteLog("[SessionID ", sessionId, "]",
                "[Nickname ", nickname, "]",
                "[", ipaddr, ":", port, "]",
                "[", LogTypeToCstyleString(logType), "] ",
                std::forward<Args>(args)...);
    }

    template <typename... Args>
    void WriteChatLog(
        std::string nickname,
        Args&&... args) {
        WriteLog('[', nickname, ']', 
                 std::forward<Args>(args)...);
    }

};