#pragma once
#pragma comment(lib, "ws2_32")
#pragma comment(lib, "mswsock.lib")

#include "ClientInfo.h"
#include "Define.h"
#include <thread>
#include <vector>

class IOCPNetwork
{
public:
	IOCPNetwork(void) {}
	~IOCPNetwork(void)
	{   //윈속을 사용해서 끝냄
		WSACleanup();
	}

	//소켓을 초기화하는 함수
	bool InitSocket(const UINT32 maxIOWorkerThreadCount_)
	{
		WSADATA wsaData;

		int nRet = WSAStartup((MAKEWORD(2, 2)), &wsaData);
		if (0 != nRet)
		{
			printf("[에러] WSAStartup()함수 실패 : %d\n", WSAGetLastError());
			return false;
		}

		//연결지향형 TCP, Overlapped I/O소켓을 생성
		mListenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, NULL, WSA_FLAG_OVERLAPPED);

		if (INVALID_SOCKET == mListenSocket)
		{
			printf("[에러] socket()gkatn tlfvo : %d\n", WSAGetLastError());
			return false;
		}

		MaxIOWorkerThreadCount = maxIOWorkerThreadCount_;

		printf("소켓 초기화 성공\n");
		return true;
	}

	//------서버용 함수-------//
	//서버의 주소정보를 소켓과 연결시키고 접속 요청을 받기 위해 
	//소켓을 등록하는 함수
	bool BindandListen(int nBindPort)
	{
		SOCKADDR_IN		stServerAddr;
		stServerAddr.sin_family = AF_INET;
		stServerAddr.sin_port = htons(nBindPort);//서버 포트 설정
		//어떤 주소에서 들어오는 접속이라도 받겠다
		//보통 서버면 이렇게 설정함 만약 한 ip에서만 접속을 받고 싶다면
		//그 주소를 inet_addr함수를이요해 넣으면 된다.
		stServerAddr.sin_addr.s_addr = htonl(INADDR_ANY);

		//위에서 지정한 서버 주소정보와 cIOCompletionPort 소켓을 연결한다
		int nRet = _WINSOCK2API_::bind(mListenSocket, (SOCKADDR*)&stServerAddr, sizeof(SOCKADDR_IN));
		if (0 != nRet)
		{
			printf("[에러] bind()함수 실패 : %d\n", WSAGetLastError());
			return false;
		}

		//접속 요청을 받아들이기 위해 cIOCompletionPort소켓을 등록하고
		//접속대기 큐를 5개로 설정
		nRet = listen(mListenSocket, 5);
		if (0 != nRet)
		{
			printf("[에러] listen()함수 실패 : %d\n", WSAGetLastError());
			return false;
		}

		//CompletionPort객체 생성 요청을 한다.
		mIOCPHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, MaxIOWorkerThreadCount);
		if (NULL == mIOCPHandle)
		{
			printf("[에러] CreateIoCompletionPort()함수 실패: %d\n", GetLastError());
			return false;
		}

		auto hIOCPHandle = CreateIoCompletionPort((HANDLE)mListenSocket, mIOCPHandle, (UINT32)0, 0);
		if (nullptr == hIOCPHandle)
		{
			printf("[에러] listen socket IOCP bind 실패 : %d\n", WSAGetLastError());
			return false;
		}

		printf("서버 등록 성공..\n");
		return true;
	}

	//접속 요청을 수락하고 메세지를 받아서 처리하는 함수
	bool StartServer(const UINT32 maxClientCount)
	{
		CreateClient(maxClientCount);

		bool bRet = CreateWokerThread();
		if (false == bRet)
			return false;

		bRet = CreateAccepterThread();
		if (false == bRet)
			return false;

		printf("서버 시작\n");
		return true;
	}

	//생성되어있는 쓰레드 파괴
	void DestroyThread()
	{
		mIsSenderRun = false;
		CloseHandle(mIOCPHandle);

		if (mSendThread.joinable())
		{
			mSendThread.join();
		}


		//Accepter 쓰레드를 종요한다.
		mIsAccepterRun = false;
		closesocket(mListenSocket);

		if (mAccepterThread.joinable())
		{
			mAccepterThread.join();
		}


		mIsWorkerRun = false;

		for (auto& th : mIOWorkerThreads)
		{
			if (th.joinable())
			{
				th.join();
			}
		}
	}

	// 네트워크 이벤트를 처리할 함수들
	virtual void OnConnect(const UINT32 clientIndex_) {}
	virtual void OnClose(const UINT32 clientIndex_) {}
	virtual void OnReceive(const UINT32 clientIndex_, const UINT32 size_, char* pData_) {}

	void CreateClient(const UINT32 maxClientCount)
	{
		for (UINT32 i = 0; i < maxClientCount; ++i)
		{
			auto client = new ClientInfo();
			client->Init(i, mIOCPHandle);

			mClientInfos.push_back(client);
		}
	}

	bool SendMsg(const UINT32 sessionIndex_, const UINT32 dataSize_, char* pData)
	{
		auto pClient = GetClientInfo(sessionIndex_);
		return pClient->SendMsg(dataSize_, pData);
	}

private:
	//WaitingThread Queue에서 대기할 쓰레드들을 생성
	bool CreateWokerThread()
	{
		mIsWorkerRun = true;
		//WaitngThread Queue에 대기 상태로 넣을 쓰레드들 생성 권장되는 개수 : (cpu개수 * 2) + 1 
		for (int i = 0; i < MAX_WORKERTHREAD; i++)
		{
			mIOWorkerThreads.emplace_back([this]() { WokerThread(); });
		}

		printf("WokerThread 시작..\n");
		return true;
	}

	//사용하지 않는 클라이언트 정보 구조체를 반환한다.
	ClientInfo* GetEmptyClientInfo()
	{
		for (auto& client : mClientInfos)
		{
			if (client->IsConnectd() == false)
			{
				return client;
			}
		}

		return nullptr;
	}

	ClientInfo* GetClientInfo(const UINT32 sessionIndex)
	{
		return mClientInfos[sessionIndex];
	}

	//Overlapped I/O작업에 대한 완료 통보를 받아 
	//그에 해당하는 처리를 하는 함수
	void WokerThread()
	{
		//CompletionKey를 받을 포인터 변수
		ClientInfo* pClientInfo = nullptr;
		//함수호출 성공 여부
		BOOL bSuccess = TRUE;
		//Overlapped I/O작업에서 전송된 데이터 크기
		DWORD dwIoSize = 0;
		//I/O 작업을 위해 요청한 Overlapped 구조체를 받을 포인터
		LPOVERLAPPED lpOverlapped = NULL;

		while (mIsWorkerRun)
		{
			//////////////////////////////////////////////////////
			//이 함수로 인해 쓰레드들은 WaitingThread Queue에
			//대기 상태로 들어가게 된다.
			//완료된 Overlapped I/O작업이 발생하면 IOCP Queue에서
			//완료된 작업을 가져와 뒤 처리를 한다.
			//그리고 PostQueuedCompletionStatus()함수에의해 사용자
			//메세지가 도착되면 쓰레드를 종료한다.
			// **PostQueuedCompletionStatus()**
			// 사용자가 직접 입력한 정보를 IOCP Queue에 추가할 수 있다.
			// 일반적으로 IOCP 모델을 이용한 서버 프로그램을 종료 할 때 스레드를 깨워 정상 종료하기 위한 용도로 자주 쓰인다.
			//////////////////////////////////////////////////////
			bSuccess = GetQueuedCompletionStatus(
				mIOCPHandle,
				&dwIoSize,
				(PULONG_PTR)&pClientInfo,
				&lpOverlapped,
				INFINITE
			);

			//사용자 쓰레드 종료 메세지 처리...
			if (TRUE == bSuccess && 0 == dwIoSize && NULL == lpOverlapped)
			{
				mIsWorkerRun = false;
				continue;
			}

			if (NULL == lpOverlapped)
				continue;

			OverlappedEx* pOverlappedEx = (OverlappedEx*)lpOverlapped;

			//client가 접속을 끊었을때..			
			if (FALSE == bSuccess ||
				(0 == dwIoSize && IOOperation::ACCEPT != pOverlappedEx->m_eOperation))
			{
				//printf("socket(%d) 접속 끊김\n", (int)pClientInfo->m_socketClient);
				CloseSocket(pClientInfo);
				continue;
			}

			if (IOOperation::ACCEPT == pOverlappedEx->m_eOperation)
			{
				pClientInfo = GetClientInfo(pOverlappedEx->SessionIndex);
				if (pClientInfo->AcceptCompletion())
				{
					++mClientCnt;

					OnConnect(pClientInfo->GetIndex());
				}
				else
				{
					CloseSocket(pClientInfo, true);
				}
			}
			//Overlapped I/O Recv작업 결과 뒤 처리
			else if (IOOperation::RECV == pOverlappedEx->m_eOperation)
			{
				OnReceive(pClientInfo->GetIndex(), dwIoSize, pClientInfo->RecvBuffer());
				pClientInfo->BindRecv();
			}
			//Overlapped I/O Send작업 결과 뒤 처리
			else if (IOOperation::SEND == pOverlappedEx->m_eOperation)
			{
				pClientInfo->SendCompleted(dwIoSize);
			}
			//예외상황
			else
			{
				printf("socket(%d)에서 예외상황\n", pClientInfo->GetIndex());
			}
		}
	}

	//소켓의 연결을 종료 시킨다.
	void CloseSocket(ClientInfo* pClientInfo, bool bIsForce = false)
	{
		auto clientIndex = pClientInfo->GetIndex();

		pClientInfo->CloseSocket(bIsForce);

		OnClose(clientIndex);
	}

	//accept요청을 처리하는 쓰레드 생성
	bool CreateAccepterThread()
	{
		//std::thread([this]() { AccepterThread(); })이 쓰레드로
		//메인쓰레드와는 분리돼서 실행되지만 AccepterThread()의 내부는
		//동기적으로 클라이언트 접속을 받음 즉 함수 내부는 block된 상태임
		mAccepterThread = thread([this]() { AccepterThread(); });

		printf("AccepterThread 시작..\n");
		return true;
	}

	//사용자의 접속을 받는 쓰레드
	void AccepterThread()
	{
		while (mIsAccepterRun)
		{
			auto curTimeSec = chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now().time_since_epoch()).count();
			
			for (auto client : mClientInfos)
			{
				if (client->IsConnectd())
					continue;
				if ((UINT64)curTimeSec < client->GetLatestClosedTimeSec())
					continue;

				auto diff = curTimeSec - client->GetLatestClosedTimeSec();
				if (diff <= RE_USE_SESSION_WAIT_TIMESEC)
					continue;

				client->PostAccept(mListenSocket, curTimeSec);
			}

			this_thread::sleep_for(chrono::microseconds(32));
		}
	}


	UINT32 MaxIOWorkerThreadCount = 0;

	//클라이언트 정보 저장 구조체
	vector<ClientInfo*> mClientInfos;

	//클라이언트의 접속을 받기위한 리슨 소켓
	SOCKET		mListenSocket = INVALID_SOCKET;

	//접속 되어있는 클라이언트 수
	int			mClientCnt = 0;

	//IO Worker 스레드
	vector<thread> mIOWorkerThreads;

	//Accept 스레드
	thread	mAccepterThread;

	//CompletionPort객체 핸들
	HANDLE		mIOCPHandle = INVALID_HANDLE_VALUE;

	//Send 스레드
	thread	mSendThread;

	//작업 쓰레드 동작 플래그
	bool		mIsWorkerRun = true;

	//접속 쓰레드 동작 플래그
	bool		mIsAccepterRun = true;

	//Send 쓰레드 동작 플래그
	bool		mIsSenderRun = false;

	//소켓 버퍼
	char		mSocketBuf[1024] = { 0, };
};