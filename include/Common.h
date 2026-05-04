#pragma once

#define _CRT_SECURE_NO_WARNINGS // 구식 C함수들 경고 끄기
#define _WINSOCK_DEPRECATED_NO_WARNINGS // 구식 윈속함수들 경고 끄기

#include <winsock2.h> // 윈속2 기본헤더
#include <ws2tcpip.h> // 윈속2 확장헤더

#include <tchar.h> // _T(), ...
#include <stdio.h> // printf(), ...
#include <stdlib.h> // exit(), ...
#include <string.h> // strncpy(), ...

#pragma comment(lib, "ws2_32") // ws2_32.lib 라이브러리 링크 명시

// 에러 출력 후 종료
inline void err_quit(const char *msg)
{
	LPVOID lpMsgBuf;
	FormatMessageA(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, WSAGetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(char *)&lpMsgBuf, 0, NULL);
	MessageBoxA(NULL, (const char *)lpMsgBuf, msg, MB_ICONERROR);
	LocalFree(lpMsgBuf);
	exit(1);
}

// 에러 출력 후 그대로 유지
inline void err_display(const char *msg)
{
	LPVOID lpMsgBuf;
	FormatMessageA(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, WSAGetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(char *)&lpMsgBuf, 0, NULL);
	printf("[%s] %s\n", msg, (char *)lpMsgBuf);
	LocalFree(lpMsgBuf);
}

// 에러 출력 후 그대로 유지의 에러코드 버전 (WSAStartup() 용)
inline void err_display(int errcode)
{
	LPVOID lpMsgBuf;
	FormatMessageA(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, errcode,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(char *)&lpMsgBuf, 0, NULL);
	printf("[Error] %s\n", (char *)lpMsgBuf);
	LocalFree(lpMsgBuf);
}
