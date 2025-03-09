#include <winsock2.h>
#include <windows.h>
#include <stdio.h>

#pragma comment(lib, "Ws2_32.lib") // Winsock2 라이브러리 링크

#define PORT 5150
#define DATA_BUFSIZE 8192 // 데이터 버퍼 크기

// **소켓 정보를 저장하는 구조체**
typedef struct _SOCKET_INFORMATION {
	CHAR Buffer[DATA_BUFSIZE];  // 데이터를 저장할 버퍼
	WSABUF DataBuf;             // WSABUF 구조체 (데이터 버퍼를 가리킴)
	SOCKET Socket;              // 클라이언트 소켓 핸들
	WSAOVERLAPPED Overlapped;   // 중첩(Overlapped) 입출력을 위한 구조체
	DWORD BytesSEND;            // 송신된 바이트 수
	DWORD BytesRECV;            // 수신된 바이트 수
} SOCKET_INFORMATION, * LPSOCKET_INFORMATION;

// **비동기 입출력을 처리하는 워커 쓰레드 함수**
DWORD WINAPI ProcessIO(LPVOID lpParameter);

DWORD EventTotal = 0; // 현재 활성화된 이벤트 개수

// **소켓 및 이벤트 정보 배열**
WSAEVENT EventArray[WSA_MAXIMUM_WAIT_EVENTS]; // WSA 이벤트 배열
LPSOCKET_INFORMATION SocketArray[WSA_MAXIMUM_WAIT_EVENTS]; // 소켓 정보 배열

// **임계 영역(Critical Section)**
// - 여러 스레드에서 공유하는 데이터를 보호하기 위해 사용
CRITICAL_SECTION CriticalSection;

int main(int argc, char** argv)
{
	WSADATA wsaData;
	SOCKET ListenSocket, AcceptSocket;
	SOCKADDR_IN InternetAddr;
	DWORD Flags;
	DWORD ThreadId;
	DWORD RecvBytes;

	// **임계 영역 초기화**
	InitializeCriticalSection(&CriticalSection);

	// **Winsock 초기화**
	if (WSAStartup((2, 2), &wsaData) != 0)
	{
		printf("WSAStartup() failed with error %d\n", WSAGetLastError());
		WSACleanup();
		return 1;
	}
	printf("WSAStartup() looks nice!\n");

	// **리스닝(서버) 소켓 생성**
	if ((ListenSocket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED)) == INVALID_SOCKET)
	{
		printf("Failed to get a socket %d\n", WSAGetLastError());
		return 1;
	}
	printf("WSASocket() is OK!\n");

	// **서버 소켓 주소 구조체 설정**
	InternetAddr.sin_family = AF_INET;
	InternetAddr.sin_addr.s_addr = htonl(INADDR_ANY); // 모든 네트워크 인터페이스에서 연결 수락
	InternetAddr.sin_port = htons(PORT); // 포트 설정

	// **소켓 바인딩**
	if (bind(ListenSocket, (PSOCKADDR)&InternetAddr, sizeof(InternetAddr)) == SOCKET_ERROR)
	{
		printf("bind() failed with error %d\n", WSAGetLastError());
		return 1;
	}
	printf("bind() is working!\n");

	// **클라이언트 연결 대기 시작**
	if (listen(ListenSocket, 5))
	{
		printf("listen() failed with error %d\n", WSAGetLastError());
		return 1;
	}
	printf("listen() is OK!\n");

	// **이벤트 객체 생성**
	if ((EventArray[0] = WSACreateEvent()) == WSA_INVALID_EVENT)
	{
		printf("WSACreateEvent() failed with error %d\n", WSAGetLastError());
		return 1;
	}
	printf("WSACreateEvent() is OK!\n");

	// **워커 스레드 생성**
	if (CreateThread(NULL, 0, ProcessIO, NULL, 0, &ThreadId) == NULL)
	{
		printf("CreateThread() failed with error %d\n", GetLastError());
		return 1;
	}
	printf("Worker thread created!\n");

	EventTotal = 1; // 첫 번째 이벤트가 생성되었으므로 1로 설정

	// **메인 루프: 클라이언트 연결 수락**
	while (true)
	{
		// **클라이언트 연결 대기**
		if ((AcceptSocket = accept(ListenSocket, NULL, NULL)) == INVALID_SOCKET)
		{
			printf("accept() failed with error %d\n", WSAGetLastError());
			return 1;
		}
		printf("accept() is OK!\n");

		// **임계 영역 시작 (다른 스레드의 접근 방지)**
		EnterCriticalSection(&CriticalSection);

		// **소켓 정보 구조체 동적 할당**
		if ((SocketArray[EventTotal] = (LPSOCKET_INFORMATION)GlobalAlloc(GPTR, sizeof(SOCKET_INFORMATION))) == NULL)
		{
			printf("GlobalAlloc() failed with error %d\n", GetLastError());
			return 1;
		}
		printf("Allocated socket info!\n");

		// **새로운 연결 소켓 정보 설정**
		SocketArray[EventTotal]->Socket = AcceptSocket;
		ZeroMemory(&(SocketArray[EventTotal]->Overlapped), sizeof(OVERLAPPED));
		SocketArray[EventTotal]->BytesSEND = 0;
		SocketArray[EventTotal]->BytesRECV = 0;
		SocketArray[EventTotal]->DataBuf.len = DATA_BUFSIZE;
		SocketArray[EventTotal]->DataBuf.buf = SocketArray[EventTotal]->Buffer;

		// **이벤트 객체 생성**
		if ((SocketArray[EventTotal]->Overlapped.hEvent = EventArray[EventTotal] = WSACreateEvent()) == WSA_INVALID_EVENT)
		{
			printf("WSACreateEvent() failed with error %d\n", WSAGetLastError());
			return 1;
		}
		printf("Event created for new connection!\n");

		// **비동기 데이터 수신 시작**
		Flags = 0;
		if (WSARecv(
			SocketArray[EventTotal]->Socket,
			&(SocketArray[EventTotal]->DataBuf),
			1,
			&RecvBytes,
			&Flags,
			&(SocketArray[EventTotal]->Overlapped),
			NULL) == SOCKET_ERROR)
		{
			if (WSAGetLastError() != ERROR_IO_PENDING)
			{
				printf("WSARecv() failed with error %d\n", WSAGetLastError());
				return 1;
			}
		}
		printf("WSARecv() started!\n");

		EventTotal++; // 이벤트 개수 증가
		LeaveCriticalSection(&CriticalSection); // **임계 영역 해제**

		// **워커 스레드에서 이벤트 처리하도록 신호 전송**
		if (WSASetEvent(EventArray[0]) == FALSE)
		{
			printf("WSASetEvent() failed with error %d\n", WSAGetLastError());
			return 1;
		}
	}
}

// **입출력 이벤트 처리 워커 스레드**
DWORD WINAPI ProcessIO(LPVOID lpParameter)
{
	DWORD Index;
	DWORD Flags;
	LPSOCKET_INFORMATION SI;
	DWORD BytesTransferred;
	DWORD i;
	DWORD RecvBytes, SendBytes;

	while (TRUE)
	{
		// **이벤트 대기**
		if ((Index = WSAWaitForMultipleEvents(EventTotal, EventArray, FALSE, WSA_INFINITE, FALSE)) == WSA_WAIT_FAILED)
		{
			printf("WSAWaitForMultipleEvents() failed %d\n", WSAGetLastError());
			return 0;
		}

		//Index - WSA_WAIT_EVENT_0 == 0 이라는 것은 EventArray[0]에서 이벤트가 발생했음을 의미함.
		//EventArray[0]은 서버의 리스닝(듣기) 소켓이므로,
		//클라이언트가 연결을 요청할 때 이벤트가 발생함.
		//하지만 클라이언트 연결을 처리하는 부분은 accept()에서 따로 처리됨,
		//즉 accept()로 새 클라이언트 연결을 처리하기 때문에 여기서는 특별히 추가 작업이 필요 없음.
		if ((Index - WSA_WAIT_EVENT_0) == 0)
		{
			WSAResetEvent(EventArray[0]);
			continue;
		}

		// **이벤트가 발생한 소켓 가져오기**
		SI = SocketArray[Index - WSA_WAIT_EVENT_0];
		WSAResetEvent(EventArray[Index - WSA_WAIT_EVENT_0]);

		// **데이터 전송 완료 확인**
		if (WSAGetOverlappedResult(SI->Socket, &(SI->Overlapped), &BytesTransferred, FALSE, &Flags) == FALSE || BytesTransferred == 0)
		{
			printf("Closing socket %d\n", SI->Socket);
			closesocket(SI->Socket);
			GlobalFree(SI);
			WSACloseEvent(EventArray[Index - WSA_WAIT_EVENT_0]);
			continue;
		}

		// **데이터를 다시 클라이언트에게 전송 (에코 서버)**
		SI->DataBuf.len = BytesTransferred;
		WSASend(SI->Socket, &(SI->DataBuf), 1, &SendBytes, 0, &(SI->Overlapped), NULL);
	}
}
