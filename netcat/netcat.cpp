// netcat.cpp : 此檔案包含 'main' 函式。程式會於該處開始執行及結束執行。
//

#include "Everything.h"
#include <ClientServer.h>
#include <WinSock2.h>
#pragma comment(lib, "WS2_32.lib")
#include <WS2tcpip.h>

static BOOL SendRequestMessage(REQUEST*, SOCKET);
static BOOL ReceiveResponseMessage(RESPONSE*, SOCKET);

struct sockaddr_in clientSAddr;		/* Client Socket address structure */

struct sockaddr_in srvSAddr;		/* Server's Socket address structure */
struct sockaddr_in connectSAddr;	/* Connected socket with client details   */
WSADATA WSStartData;				/* Socket library data structure   */

enum SERVER_THREAD_STATE {
	SERVER_SLOT_FREE, SERVER_THREAD_STOPPED,
	SERVER_THREAD_RUNNING, SERVER_SLOT_INVALID
};
typedef struct SERVER_ARG_TAG { /* Server thread arguments */
	CRITICAL_SECTION threadCs;
	DWORD	number;
	SOCKET	sock;
	enum SERVER_THREAD_STATE thState;
	HANDLE	hSrvThread;
	HINSTANCE	 hDll; /* Shared libary handle */
} SERVER_ARG;
typedef struct _COMMAND_ARG {
	CHAR cmd_buffer[MAX_COMMAND_LINE];
	TCHAR tempFile[MAX_PATH];
}COMMAND_ARG, *PCOMMAND_ARG ;

# define MAX_CLIENT 10

static BOOL bListen = FALSE, command = FALSE;
static LPTSTR upload_destination = NULL, execute = NULL, target = NULL;
static UINT port = SERVER_PORT;
HANDLE client_semp = NULL;
volatile static LONG shutFlag = 0;

static VOID WINAPI usage()
{
    _tprintf(_T("WinRAT Net Tool\n"));
    _tprintf(_T("\nUsage: netcat -t <target_host> -p <port>\n"));
    _tprintf(_T("-l --listen              - listen on [host]:[port] for incoming connections\n"));
    _tprintf(_T("-e --execute=file_to_run - execute the given file upon receiving a connection\n"));
    _tprintf(_T("-c --command             - initialize a command shell\n"));
    _tprintf(_T("-u --upload=destination  - upon receiving connection upload a file and write to [destination]\n"));
    _tprintf(_T("\nExamples:\n"));
    _tprintf(_T("netcat -t 192.168.0.1 -p 5555 -l -c\n"));
    _tprintf(_T("netcat -t 192.168.0.1 -p 5555 -l -u=c:\\target.exe\n"));
    _tprintf(_T("netcat -t 192.168.0.1 -p 5555 -l -e=\"cat /etc/passwd\"\n"));
    _tprintf(_T("echo 'ABCDEFGHI' | netcat -t 192.168.11.12 -p 135\n"));

    TerminateProcess(GetCurrentProcess(), 0);
}

static VOID WINAPI client_sender(LPTSTR);
static VOID WINAPI server_loop();
static UINT WINAPI client_handler(PVOID);
static UINT WINAPI run_command(PVOID);
BOOL WINAPI Handler(DWORD);

static BOOL ReceiveRequestMessage(REQUEST* pRequest, SOCKET);
static BOOL SendResponseMessage(RESPONSE* pResponse, SOCKET);
static BOOL SendRequestMessage(REQUEST*, SOCKET);
static BOOL ReceiveResponseMessage(RESPONSE*, SOCKET);

int _tmain(INT argc, LPTSTR argv[])
{

    BOOL flag[7] = {0};
    UINT iArg = argc;
    LPTSTR lpCmdLine = GetCommandLine(), lpTargv;
    TCHAR lpBuffer[MAX_RQRS_LEN];
    DWORD nRead;

    if (argc <= 1) 
    {
        usage();
    }

    // read the commandline options
    _try{
        Options(argc, (LPCTSTR*)argv, _T("htplecu"),
            &flag[0], &flag[1], &flag[2], &flag[3], &flag[4], &flag[5], &flag[6], NULL);
        if (flag[0] || _tstrstr(lpCmdLine, _T("--help")) != NULL) {
            usage();
        }
        if (flag[1]) {
            iArg = Options(argc, (LPCTSTR*)argv, _T("t"), &flag[1], NULL);
            target = argv[iArg];
        }
        if (flag[2]) {
            iArg = Options(argc, (LPCTSTR*)argv, _T("p"), &flag[2], NULL);
            port = _tstoi(argv[iArg]);
        }
        if (flag[3] || _tstrstr(lpCmdLine, _T("--listen")) != NULL) {
            flag[3] = bListen = TRUE;
        }
        if (flag[4]) {
            iArg = Options(argc, (LPCTSTR*)argv, _T("e"), &flag[4], NULL);
            execute = argv[iArg - 1] + 3;
        }
        if (flag[5] || _tstrstr(lpCmdLine, _T("--command")) != NULL) {
            flag[5] = command = TRUE;
        }
        if (flag[6]) {
            iArg = Options(argc, (LPCTSTR*)argv, _T("u"), &flag[6], NULL);
            upload_destination = argv[iArg - 1] + 3;
        }

        for (int i = 1; i < argc; i++) {
            if ((execute = _tstrstr(argv[i], _T("--execute="))) != NULL) {
                execute += 10;
                flag[4] = TRUE;
            }
            else if ((upload_destination = _tstrstr(argv[i], _T("--upload="))) != NULL) {
                upload_destination += 9;
                flag[6] = TRUE;
            }
        }
    }_except(EXCEPTION_EXECUTE_HANDLER) {
        ReportError(_T("get arguments error"), 0, TRUE);
        usage();
    }

    // are we going to listen or just send data from stdin?
    if (!bListen && port > 0) {
        // read in the buffer from the commandline                  
        // this will block, so send CTRL - D if not sending input                  
        // to stdin
        // ReadConsole(GetStdHandle(STD_INPUT_HANDLE), lpBuffer, MAX_COMMAND_LINE - 1, &nRead, NULL);
        //lpBuffer[nRead<MAX_RQRS_LEN-1?nRead: MAX_RQRS_LEN - 1] = _T('\0');

        // send data off
        client_sender(lpBuffer);
    }

    // we are going to listenand potentially          
    // upload things, execute commands, and drop a shell back          
    // depending on our command line options above
    if (bListen) {
        server_loop();
    }

    return 0;
}

static VOID WINAPI client_sender(LPTSTR buffer) 
{
	SOCKET client = INVALID_SOCKET;
	REQUEST request;	/* See clntcrvr.h */
	RESPONSE response;	/* See clntcrvr.h */
#ifdef WIN32					/* Exercise: Can you port this code to UNIX?
								 * Are there other sysetm dependencies? */
	WSADATA WSStartData;				/* Socket library data structure   */
#endif
	BOOL quit = FALSE;
	DWORD conVal;
	/*	Initialize the WS library. Ver 2.0 */
#ifdef WIN32
	if (WSAStartup(MAKEWORD(2, 0), &WSStartData) != 0)
		ReportError(_T("Cannot support sockets"), 1, TRUE);
#endif	
	client = socket(AF_INET, SOCK_STREAM, 0);
	if (client == INVALID_SOCKET)
		ReportError(_T("Failed client socket() call"), 2, TRUE);
	
	ZeroMemory(&clientSAddr, sizeof(clientSAddr));
	clientSAddr.sin_family = AF_INET;
	if (target!=NULL)
		clientSAddr.sin_addr.s_addr = inet_addr(target);
	else
		clientSAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

	clientSAddr.sin_port = htons(port);


	_try{
		conVal = connect(client, (struct sockaddr*) & clientSAddr, sizeof(clientSAddr));
		
	if (conVal == SOCKET_ERROR) ReportError(_T("Failed client connect() call)"), 3, TRUE);
		
		if (_tcslen(buffer)) {
			send(client, buffer, _tcslen(buffer) + 1, 0);
		}

		while (!quit) {
			_tprintf(_T("%s"), _T("\nNETCAT#> "));
			_fgetts((PCHAR)request.record, MAX_RQRS_LEN - 1, stdin);
			/* Get rid of the new line at the end */
			/* Messages use 8-bit characters */
			request.record[strlen((PCHAR)request.record) - 1] = '\0';
			if (strcmp((PCHAR)request.record, "exit") == 0
				|| strcmp((PCHAR)request.record, "quit") == 0) quit = TRUE;
			SendRequestMessage(&request, client);
			if (!quit) ReceiveResponseMessage(&response, client);

		}
	}__except(EXCEPTION_EXECUTE_HANDLER) {
		_tprintf(_T("[*] Exception! Exiting."));
	}
	
	shutdown(client, SD_BOTH); /* Disallow sends and receives */
#ifdef WIN32
	closesocket(client);
	WSACleanup();
#else
	close(clientSock);
#endif
}

static VOID WINAPI server_loop() 
{
	/* Server listening and connected sockets. */
	DWORD iThread, tStatus;
	SERVER_ARG sArgs[MAX_CLIENTS];
	HANDLE hAcceptThread = NULL;
	HINSTANCE hDll = NULL;
	SOCKET server = INVALID_SOCKET;
	LONG addrLen = sizeof(connectSAddr);

	if (!WindowsVersionOK(3, 1))
		ReportError(_T("This program requires Windows NT 3.1 or greater"), 1, FALSE);

	/* Console control handler to permit server shutdown */
	if (!SetConsoleCtrlHandler(Handler, TRUE))
		ReportError(_T("Cannot create Ctrl handler"), 1, TRUE);

	/*	Initialize the WS library. Ver 2.0 */
	if (WSAStartup(MAKEWORD(2, 0), &WSStartData) != 0)
		ReportError(_T("Cannot support sockets"), 1, TRUE);

	for (iThread = 0; iThread < MAX_CLIENTS; iThread++) {
		InitializeCriticalSection(&sArgs[iThread].threadCs);
		sArgs[iThread].number = iThread;
		sArgs[iThread].thState = SERVER_SLOT_FREE;
		sArgs[iThread].sock = 0;
		sArgs[iThread].hDll = hDll;
		sArgs[iThread].hSrvThread = NULL;
	}

	server = socket(PF_INET, SOCK_STREAM, 0);
	if (server == INVALID_SOCKET)
		ReportError(_T("Failed server socket() call"), 1, TRUE);

	srvSAddr.sin_family = AF_INET;
	if (target == NULL)
		srvSAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	srvSAddr.sin_port = htons(port);

	if (bind(server, (struct sockaddr*) & srvSAddr, sizeof(srvSAddr)) == SOCKET_ERROR)
		ReportError(_T("Failed server bind() call"), 2, TRUE);
	if (listen(server, MAX_CLIENTS) != 0)
		ReportError(_T("Server listen() error"), 3, TRUE);

	while (!shutFlag) {
		iThread = 0;
		while (!shutFlag) {
			/* Continously poll the thread thState of all server slots in the sArgs table */
			EnterCriticalSection(&sArgs[iThread].threadCs);
			__try {
				if (sArgs[iThread].thState == SERVER_THREAD_STOPPED) {
					/* This thread stopped, either normally or there's a shutdown request */
					/* Wait for it to stop, and make the slot free for another thread */
					tStatus = WaitForSingleObject(sArgs[iThread].hSrvThread, INFINITE);
					if (tStatus != WAIT_OBJECT_0)
						ReportError(_T("Server thread wait error"), 4, TRUE);
					CloseHandle(sArgs[iThread].hSrvThread);
					sArgs[iThread].hSrvThread = NULL;
					sArgs[iThread].thState = SERVER_SLOT_FREE;
				}
				/* Free slot identified or shut down. Use a free slot for the next connection */
				if (sArgs[iThread].thState == SERVER_SLOT_FREE || shutFlag) break;
			}
			__finally { LeaveCriticalSection(&sArgs[iThread].threadCs); }

			/* Fixed July 25, 2014: iThread = (iThread++) % MAX_CLIENTS; */
			iThread = (iThread + 1) % MAX_CLIENTS;
			if (iThread == 0) Sleep(50); /* Break the polling loop */
			/* An alternative would be to use an event to signal a free slot */
		}
		if (shutFlag) break;
		/* sArgs[iThread] == SERVER_SLOT_FREE */
		/* Wait for a connection on this socket */
		/* Use a separate thread so that we can poll the shutFlag flag */

		sArgs[iThread].sock =
			accept(server, (struct sockaddr*) & connectSAddr, (PINT)&addrLen);
		if (sArgs[iThread].sock == INVALID_SOCKET) {
			ReportError(_T("accept: invalid socket error"), 1, TRUE);
			return ;
		}
		client_handler(&sArgs[iThread]);
		// hAcceptThread = (HANDLE)_beginthreadex(NULL, 0, client_handler, &sArgs[iThread], 0, NULL);
		if (hAcceptThread == NULL)
			ReportError(_T("Error creating AcceptThreadread."), 1, TRUE);
		while (!shutFlag) {
			tStatus = WaitForSingleObject(hAcceptThread, CS_TIMEOUT);
			if (tStatus == WAIT_OBJECT_0) {
				/* Connection is complete. sArgs[iThread] == SERVER_THREAD_RUNNING */
				if (!shutFlag) {
					CloseHandle(hAcceptThread);
					hAcceptThread = NULL;
				}
				break;
			}
		}
	}

	while (TRUE) {
		int nRunningThreads = 0;
		for (iThread = 0; iThread < MAX_CLIENTS; iThread++) {
			EnterCriticalSection(&sArgs[iThread].threadCs);
			__try {
				if (sArgs[iThread].thState == SERVER_THREAD_RUNNING || sArgs[iThread].thState == SERVER_THREAD_STOPPED) {
					if (WaitForSingleObject(sArgs[iThread].hSrvThread, 10000) == WAIT_OBJECT_0) {
						_tprintf(_T("Server thread on slot %d stopped.\n"), iThread);
						CloseHandle(sArgs[iThread].hSrvThread);
						sArgs[iThread].hSrvThread = NULL;
						sArgs[iThread].thState = SERVER_SLOT_INVALID;
					}
					else
						if (WaitForSingleObject(sArgs[iThread].hSrvThread, 10000) == WAIT_TIMEOUT) {
							_tprintf(_T("Server thread on slot %d still running.\n"), iThread);
							nRunningThreads++;
						}
						else {
							_tprintf(_T("Error waiting on server thread in slot %d.\n"), iThread);
							ReportError(_T("Thread wait failure"), 0, TRUE);
						}

				}
			}
			__finally { LeaveCriticalSection(&sArgs[iThread].threadCs); }
		}
		if (nRunningThreads == 0) break;
	}
	/* Redundant shutdown */
	shutdown(server, SD_BOTH);
	closesocket(server);
	WSACleanup();
	if (hAcceptThread != NULL && WaitForSingleObject(hAcceptThread, INFINITE) != WAIT_OBJECT_0)
		ReportError(_T("Failed waiting for accept thread to terminate."), 7, FALSE);
}

static UINT WINAPI run_command(PVOID lpCmd) {
	PCOMMAND_ARG lpCmdA = (PCOMMAND_ARG)lpCmd;
	HANDLE hTmpFile = INVALID_HANDLE_VALUE;
	SECURITY_ATTRIBUTES tempSA = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
	STARTUPINFO startInfoCh;
	PROCESS_INFORMATION procInfo;

	GetStartupInfo(&startInfoCh);

	/* Open the temporary results file. */
	hTmpFile = CreateFile(lpCmdA->tempFile, GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE, &tempSA,
			CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hTmpFile == INVALID_HANDLE_VALUE)
		ReportError(_T("Cannot create temp file"), 1, TRUE);

	/* Open the temporary results file. */
	hTmpFile = CreateFile(lpCmdA->tempFile, GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE, &tempSA,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hTmpFile == INVALID_HANDLE_VALUE)
		ReportError(_T("Cannot create temp file"), 1, TRUE);

	__try {
		startInfoCh.hStdOutput = hTmpFile;
		startInfoCh.hStdError = hTmpFile;
		startInfoCh.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
		startInfoCh.dwFlags = STARTF_USESTDHANDLES;
		if (!CreateProcess(NULL, lpCmdA->cmd_buffer, NULL,
			NULL, TRUE, /* Inherit handles. */
			0, NULL, NULL, &startInfoCh, &procInfo)) {
			PrintMsg(hTmpFile, _T("ERR: Cannot create process."));
			procInfo.hProcess = NULL;
		}
		if (procInfo.hProcess != NULL) {
			CloseHandle(procInfo.hThread);
			WaitForSingleObject(procInfo.hProcess, INFINITE);
			CloseHandle(procInfo.hProcess);
		}
		CloseHandle(hTmpFile);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		WriteConsole(hTmpFile, _T("Failed to execute command.\r\n"), MAX_RQRS_LEN, NULL, NULL);
		return FALSE;
	}
	return TRUE;
}

static UINT WINAPI client_handler(PVOID pArg)
{
	SERVER_ARG* pThArg = (SERVER_ARG*)pArg;

	BOOL done = FALSE;

	REQUEST request;	/* Defined in ClientServer.h */
	RESPONSE response;	/* Defined in ClientServer.h.*/
	SOCKET connectSock;

	int commandLen;

	FILE* fp = NULL;
	COMMAND_ARG cmdA;

	enum SERVER_THREAD_STATE threadState;

	/* A new connection. Create a server thread */
	EnterCriticalSection(&(pThArg->threadCs));
	if(command != NULL)
	__try {
		connectSock = pThArg->sock;

		cmdA.tempFile[sizeof(cmdA.tempFile) / sizeof(TCHAR) - 1] = _T('\0');
		_stprintf_s(cmdA.tempFile, sizeof(cmdA.tempFile) / sizeof(TCHAR) - 1, _T("ServerTemp%d.tmp"), pThArg->number);
	
		while (!done && !shutFlag) { 	/* Main Server Command Loop. */
			done = ReceiveRequestMessage(&request, connectSock);

			request.record[sizeof(request.record) - 1] = '\0';
			commandLen = (int)(strcspn((PCHAR)request.record, "\n\t"));
			CopyMemory(cmdA.cmd_buffer, request.record, commandLen);
			cmdA.cmd_buffer[commandLen] = '\0';

			/* Restest shutFlag, as it can be set from the console control handler. */
			done = done || (strcmp((PCHAR)request.record, "quit") == 0) || shutFlag || (strcmp((PCHAR)request.record, "exit") == 0);
			if (done) continue;
		}

		pThArg->hSrvThread = (HANDLE)_beginthreadex(NULL, 0, run_command, &cmdA, 0, NULL);
		if (pThArg->hSrvThread == NULL)
			ReportError(_T("Failed creating server thread"), 1, TRUE);
		pThArg->thState = SERVER_THREAD_RUNNING;

		/* Send the temp file, one line at a time, with header, to the client. */
		if (_tfopen_s(&fp, cmdA.tempFile, _T("r")) == 0) {
			{
				response.rsLen = MAX_RQRS_LEN;
				while ((fgets(PCHAR(response.record), MAX_RQRS_LEN, fp) != NULL))
					SendResponseMessage(&response, connectSock);
			}
			/* Send a zero length message. Messages are 8-bit characters, not UNICODE. */
			response.record[0] = '\0';
			SendResponseMessage(&response, connectSock);
			fclose(fp); fp = NULL;
			DeleteFile(cmdA.tempFile);
		}
		else {
			ReportError(_T("Failed to open temp file with command results"), 0, TRUE);
		}

	}
	__finally { LeaveCriticalSection(&(pThArg->threadCs)); }

	shutdown(connectSock, SD_BOTH);
	closesocket(connectSock);

	EnterCriticalSection(&(pThArg->threadCs));
	__try {
		threadState = pThArg->thState = SERVER_THREAD_STOPPED;
	}
	__finally { LeaveCriticalSection(&(pThArg->threadCs)); }

	return threadState;
}

BOOL WINAPI Handler(DWORD CtrlEvent)
{
	/* Recives ^C. Shutdown the system */
	_tprintf(_T("In console control handler\n"));
	InterlockedIncrement(&shutFlag);
	return TRUE;
}

BOOL ReceiveRequestMessage(REQUEST* pRequest, SOCKET sd)
{
	BOOL disconnect = FALSE;
	LONG32 nRemainRecv = 0, nXfer;
	LPBYTE pBuffer;

	/*	Read the request. First the header, then the request text. */
	nRemainRecv = RQ_HEADER_LEN;
	pBuffer = (LPBYTE)pRequest;

	while (nRemainRecv > 0 && !disconnect) {
		nXfer = recv(sd, (PCHAR)pBuffer, nRemainRecv, 0);
		if (nXfer == SOCKET_ERROR)
			ReportError(_T("server request recv() failed"), 11, TRUE);
		disconnect = (nXfer == 0);
		nRemainRecv -= nXfer; pBuffer += nXfer;
	}

	/*	Read the request record */
	nRemainRecv = pRequest->rqLen;
	/* Exclude buffer overflow */
	nRemainRecv = min(nRemainRecv, MAX_RQRS_LEN);

	pBuffer = (LPBYTE)pRequest->record;
	while (nRemainRecv > 0 && !disconnect) {
		nXfer = recv(sd, (PCHAR)pBuffer, nRemainRecv, 0);
		if (nXfer == SOCKET_ERROR)
			ReportError(_T("server request recv() failed"), 12, TRUE);
		disconnect = (nXfer == 0);
		nRemainRecv -= nXfer; pBuffer += nXfer;
	}

	return disconnect;
}

BOOL SendResponseMessage(RESPONSE* pResponse, SOCKET sd)
{
	BOOL disconnect = FALSE;
	LONG32 nRemainRecv = 0, nXfer, nRemainSend;
	LPBYTE pBuffer;

	/*	Send the response up to the string end. Send in
		two parts - header, then the response string. */
	nRemainSend = RS_HEADER_LEN;
	pResponse->rsLen = (long)(strlen(PCHAR(pResponse->record)) + 1);
	pBuffer = (LPBYTE)pResponse;
	while (nRemainSend > 0 && !disconnect) {
		nXfer = send(sd, (PCHAR)pBuffer, nRemainSend, 0);
		if (nXfer == SOCKET_ERROR) ReportError(_T("server send() failed"), 13, TRUE);
		disconnect = (nXfer == 0);
		nRemainSend -= nXfer; pBuffer += nXfer;
	}

	nRemainSend = pResponse->rsLen;
	pBuffer = (LPBYTE)pResponse->record;
	while (nRemainSend > 0 && !disconnect) {
		nXfer = send(sd, (PCHAR)pBuffer, nRemainSend, 0);
		if (nXfer == SOCKET_ERROR) ReportError(_T("server send() failed"), 14, TRUE);
		disconnect = (nXfer == 0);
		nRemainSend -= nXfer; pBuffer += nXfer;
	}
	return disconnect;
}

BOOL SendRequestMessage(REQUEST* pRequest, SOCKET sd)
{
	/* Send the the request to the server on socket sd */
	BOOL disconnect = FALSE;
	LONG32 nRemainSend, nXfer;
	LPBYTE pBuffer;

	/* The request is sent in two parts. First the header, then
	   the command string proper. */

	nRemainSend = RQ_HEADER_LEN;
	pRequest->rqLen = (DWORD)(strlen(PCHAR(pRequest->record)) + 1);
	pBuffer = (LPBYTE)pRequest;
	while (nRemainSend > 0 && !disconnect) {
		/* send does not guarantee that the entire message is sent */
		nXfer = send(sd, (PCHAR)pBuffer, nRemainSend, 0);
		if (nXfer == SOCKET_ERROR) ReportError(_T("client send() failed"), 4, TRUE);
		disconnect = (nXfer == 0);
		nRemainSend -= nXfer; pBuffer += nXfer;
	}

	nRemainSend = pRequest->rqLen;
	pBuffer = (LPBYTE)pRequest->record;
	while (nRemainSend > 0 && !disconnect) {
		nXfer = send(sd, (PCHAR)pBuffer, nRemainSend, 0);
		if (nXfer == SOCKET_ERROR) ReportError(_T("client send() failed"), 5, TRUE);
		disconnect = (nXfer == 0);
		nRemainSend -= nXfer; pBuffer += nXfer;
	}
	return disconnect;
}


BOOL ReceiveResponseMessage(RESPONSE* pResponse, SOCKET sd)
{
	BOOL disconnect = FALSE, LastRecord = FALSE;
	LONG32 nRemainRecv, nXfer;
	LPBYTE pBuffer;

	/*  Read the response records - there may be more than one.
		As each is received, write it to std out. */

		/*	Read each response and send it to std out.
			First, read the record header, and then
			read the rest of the record.  */

	while (!LastRecord) {
		/*  Read the header */
		nRemainRecv = RS_HEADER_LEN; pBuffer = (LPBYTE)pResponse;
		while (nRemainRecv > 0 && !disconnect) {
			nXfer = recv(sd, (PCHAR)pBuffer, nRemainRecv, 0);
			if (nXfer == SOCKET_ERROR) ReportError(_T("client header recv() failed"), 6, TRUE);
			disconnect = (nXfer == 0);
			nRemainRecv -= nXfer; pBuffer += nXfer;
		}
		/*	Read the response record */
		nRemainRecv = pResponse->rsLen;
		/* Exclude buffer overflow */
		nRemainRecv = min(nRemainRecv, MAX_RQRS_LEN);
		LastRecord = (nRemainRecv <= 1);  /* The terminating null is counted */
		pBuffer = (LPBYTE)pResponse->record;
		while (nRemainRecv > 0 && !disconnect) {
			nXfer = recv(sd, (PCHAR)pBuffer, nRemainRecv, 0);
			if (nXfer == SOCKET_ERROR) ReportError(_T("client response recv() failed"), 7, TRUE);
			disconnect = (nXfer == 0);
			nRemainRecv -= nXfer; pBuffer += nXfer;
		}

		if (!disconnect)
			_tprintf(_T("%s"), pResponse->record);
	}
	return disconnect;
}
