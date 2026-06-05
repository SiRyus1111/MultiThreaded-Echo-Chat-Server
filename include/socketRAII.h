#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include "NetCommon.h"

#pragma comment(lib, "ws2_32.lib")

class WinsockGuard {
public:
    WinsockGuard() {
        WSADATA wsa;
        int WSAStartupres = WSAStartup(MAKEWORD(2, 2), &wsa);
        if (WSAStartupres != 0) {
            std::cerr << "에러 코드 : " << WSAStartupres << '\n';
            throw std::runtime_error("윈속 초기화 실패");
        }
    }
    ~WinsockGuard() {
        WSACleanup();
    }
};

class ClientSocket {
private:
    SOCKET client_sock;
public:
    ClientSocket(SOCKET s) : client_sock(s) {}

    ClientSocket(const ClientSocket&) = delete;
    ClientSocket& operator=(const ClientSocket&) = delete;

    // 이동 생성자
    ClientSocket(ClientSocket&& other) noexcept : client_sock(other.client_sock) { // noexcept = 이 함수는 예외를 발생시키지 않는다고 컴파일러에게 알려주기. 그래서 이동 최적화.
        other.client_sock = INVALID_SOCKET;
    }

    int ClientSockSend(NetState& state, const char* msg, int len) {

        int send_res = send_all(client_sock, state, msg, len);
        if (send_res == SOCKET_ERROR) {
            return SOCKET_ERROR;
        }

        return send_res;
    }

    int ClientSockRecv(NetState& state, char* buf, int len) {

        int recv_res = recv_all(client_sock, state, buf, len);
        if (recv_res == SOCKET_ERROR) {
            return SOCKET_ERROR;
        }
        else if (recv_res == 0) {
            return 0;
        }

        return recv_res;
    }

    ~ClientSocket() {
        if (client_sock != INVALID_SOCKET) {
            closesocket(client_sock);
        }
    }
};

class ListenSocket {
private:
    SOCKET listen_sock;

public:
    ListenSocket() {
        listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listen_sock == INVALID_SOCKET) {
            err_display("socket()");
            throw std::runtime_error("socket() 함수 실패");
        }

        int option = 1;
        if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&option, sizeof(option)) == SOCKET_ERROR) {
            err_display("setsockopt()");
        }
    }

    ListenSocket(const ListenSocket& s) = delete;
    ListenSocket& operator=(const ListenSocket&) = delete;

    void ListenSockBind(sockaddr_in* addr) {
        if (bind(listen_sock, (sockaddr*)addr, sizeof(*addr)) == SOCKET_ERROR){
            err_display("bind()");
            throw std::runtime_error("bind() 함수 실패");
        }
    }

    void ListenSockListen() {
        if (listen(listen_sock, SOMAXCONN) == SOCKET_ERROR) {
            err_display("listen()");
            throw std::runtime_error("listen() 함수 실패");
        }
    }

    ClientSocket ListenSockAccept(sockaddr_in* client_addr) {
        int len = sizeof(*client_addr);
        SOCKET client_sock = accept(listen_sock, (sockaddr*)client_addr, &len);
        if (client_sock == INVALID_SOCKET) {
            err_display("accept()");
            throw std::runtime_error("accept() 함수 실패");
        }

        return ClientSocket(client_sock);
    }

    ~ListenSocket() {
        if (listen_sock != INVALID_SOCKET) {
            closesocket(listen_sock);
        }
    }
};
class ConnectSocket {
private:
    SOCKET connect_sock;
public:
    ConnectSocket() {
        connect_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (connect_sock == INVALID_SOCKET) {
            err_display("socket()");
            throw std::runtime_error("socket() 함수 실패");
        }
    }

    ConnectSocket(const ConnectSocket& s) = delete;
    ConnectSocket& operator=(const ConnectSocket&) = delete;

    // 이동 생성자
    ConnectSocket(ConnectSocket&& other) noexcept : connect_sock(other.connect_sock) { // noexcept = 이 함수는 예외를 발생시키지 않는다고 컴파일러에게 알려주기. 그래서 이동 최적화.
        other.connect_sock = INVALID_SOCKET;
    }

    void ConnectSockConnect(sockaddr_in* addr) {
        if (connect(connect_sock, (sockaddr*)addr, sizeof(*addr)) == SOCKET_ERROR) {
            err_display("connect()");
            throw std::runtime_error("connect() 함수 실패");
        }
    }

    int ConnectSockSend(NetState& state, const char* msg, int len) {
        int send_res = send_all(connect_sock, state, msg, len);
        if (send_res == SOCKET_ERROR) {
            return SOCKET_ERROR;
        }

        return send_res;
    }

    int ConnectSockRecv(NetState& state, char* buf, int len) {
        int recv_res = recv_all(connect_sock, state, buf, len);
        if (recv_res == SOCKET_ERROR) {
            return SOCKET_ERROR;
        }
        else if (recv_res == 0) {
            return 0;
        }

        return recv_res;
    }

    ~ConnectSocket() {
        if (connect_sock != INVALID_SOCKET) {
            closesocket(connect_sock);
        }
    }
};