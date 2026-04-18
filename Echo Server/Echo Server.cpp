#include <iostream>
#include <ws2tcpip.h>
#include <winsock2.h>
#include "Common.h" // err_quit, err_display
#include "NetCommon.h" // 기존의 에코 서버의 상태 관리 구조체, 헤더 구조체, 송 / 수신 함수
#include <thread>
#include <map>
#include <mutex>

#pragma comment(lib, "Ws2_32.lib")

class ClientManager {
private:
	// 여기에다가 ClientMagager 클래스가 관리할 공유 컨테이너 선언
	std::mutex client_mutex;
public:
	// 여기에다가 공유 컨테이너에 삽입, 삭제, 클라이언트 수 체크를 담당할 함수 선언
};

struct client_info {
	SOCKET sock;
	sockaddr_in addr;
};

const int SERVER_PORT = htonl(9000);
const int PAYLOAD_SIZE = 4096;
const int BUFFER_SIZE = 4096;
const int HEADER_SIZE = 8;
const int HEADER_TYPE_SIZE = 4;
const int HEADER_LENGTH_SIZE = 4;

const int32_t HEADER_ERROR = 0;
const int32_t HEADER_SAFE = -1;

int client_thread(client_info info) { // 지금은 단순 값 복사. 스마트 포인터 배우면 std::shared_ptr 사용해볼 예정.

}

int main() {

	WSADATA wsa;
	int WSAStartupRes = WSAStartup(MAKEWORD(2, 2), &wsa);
	if (WSAStartupRes != 0) {
		std::cout << "[ERROR]윈속 초기화 실패 : 에러 코드 = " << WSAStartupRes << '\n';
		return 1;
	}

	SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listen_sock == INVALID_SOCKET) {
		std::cout << "[ERROR] Listen용 소켓 생성 실패 : \n";
		err_quit("socket()");
		return 1;
	}

	sockaddr_in server_addr{};
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = SERVER_PORT;

	if (bind(listen_sock, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
		std::cout << "[ERROR] Bind 실패 : \n";
		err_quit("bind()");
		return 1;
	}

	if (listen(listen_sock, SOMAXCONN) == SOCKET_ERROR) {
		std::cout << "[ERROR] Listen 실패 : \n";
		err_quit("listen()");
		return 1;
	}

	while (true) {
		sockaddr_in client_addr{};
		int client_addr_len = sizeof(client_addr);
		SOCKET client_sock = accept(listen_sock, (sockaddr*)&client_addr, &client_addr_len);
		
		if (client_sock == INVALID_SOCKET) {
			std::cout << "[ERROR] accept 실패. 다시 accept() 대기합니다. : \n";
			err_display("accept()");
			continue;
		}


	}
}