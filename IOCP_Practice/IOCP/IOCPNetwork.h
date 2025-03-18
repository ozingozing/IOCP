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
	{   //������ ����ؼ� ����
		WSACleanup();
	}

	//������ �ʱ�ȭ�ϴ� �Լ�
	bool InitSocket(const UINT32 maxIOWorkerThreadCount_)
	{
		WSADATA wsaData;

		int nRet = WSAStartup((MAKEWORD(2, 2)), &wsaData);
		if (0 != nRet)
		{
			printf("[����] WSAStartup()�Լ� ���� : %d\n", WSAGetLastError());
			return false;
		}

		//���������� TCP, Overlapped I/O������ ����
		mListenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, NULL, WSA_FLAG_OVERLAPPED);

		if (INVALID_SOCKET == mListenSocket)
		{
			printf("[����] socket()gkatn tlfvo : %d\n", WSAGetLastError());
			return false;
		}

		MaxIOWorkerThreadCount = maxIOWorkerThreadCount_;

		printf("���� �ʱ�ȭ ����\n");
		return true;
	}

	//------������ �Լ�-------//
	//������ �ּ������� ���ϰ� �����Ű�� ���� ��û�� �ޱ� ���� 
	//������ ����ϴ� �Լ�
	bool BindandListen(int nBindPort)
	{
		SOCKADDR_IN		stServerAddr;
		stServerAddr.sin_family = AF_INET;
		stServerAddr.sin_port = htons(nBindPort);//���� ��Ʈ ����
		//� �ּҿ��� ������ �����̶� �ްڴ�
		//���� ������ �̷��� ������ ���� �� ip������ ������ �ް� �ʹٸ�
		//�� �ּҸ� inet_addr�Լ����̿��� ������ �ȴ�.
		stServerAddr.sin_addr.s_addr = htonl(INADDR_ANY);

		//������ ������ ���� �ּ������� cIOCompletionPort ������ �����Ѵ�
		int nRet = _WINSOCK2API_::bind(mListenSocket, (SOCKADDR*)&stServerAddr, sizeof(SOCKADDR_IN));
		if (0 != nRet)
		{
			printf("[����] bind()�Լ� ���� : %d\n", WSAGetLastError());
			return false;
		}

		//���� ��û�� �޾Ƶ��̱� ���� cIOCompletionPort������ ����ϰ�
		//���Ӵ�� ť�� 5���� ����
		nRet = listen(mListenSocket, 5);
		if (0 != nRet)
		{
			printf("[����] listen()�Լ� ���� : %d\n", WSAGetLastError());
			return false;
		}

		//CompletionPort��ü ���� ��û�� �Ѵ�.
		mIOCPHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, MaxIOWorkerThreadCount);
		if (NULL == mIOCPHandle)
		{
			printf("[����] CreateIoCompletionPort()�Լ� ����: %d\n", GetLastError());
			return false;
		}

		auto hIOCPHandle = CreateIoCompletionPort((HANDLE)mListenSocket, mIOCPHandle, (UINT32)0, 0);
		if (nullptr == hIOCPHandle)
		{
			printf("[����] listen socket IOCP bind ���� : %d\n", WSAGetLastError());
			return false;
		}

		printf("���� ��� ����..\n");
		return true;
	}

	//���� ��û�� �����ϰ� �޼����� �޾Ƽ� ó���ϴ� �Լ�
	bool StartServer(const UINT32 maxClientCount)
	{
		CreateClient(maxClientCount);

		bool bRet = CreateWokerThread();
		if (false == bRet)
			return false;

		bRet = CreateAccepterThread();
		if (false == bRet)
			return false;

		printf("���� ����\n");
		return true;
	}

	//�����Ǿ��ִ� ������ �ı�
	void DestroyThread()
	{
		mIsSenderRun = false;
		CloseHandle(mIOCPHandle);

		if (mSendThread.joinable())
		{
			mSendThread.join();
		}


		//Accepter �����带 �����Ѵ�.
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

	// ��Ʈ��ũ �̺�Ʈ�� ó���� �Լ���
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
	//WaitingThread Queue���� ����� ��������� ����
	bool CreateWokerThread()
	{
		mIsWorkerRun = true;
		//WaitngThread Queue�� ��� ���·� ���� ������� ���� ����Ǵ� ���� : (cpu���� * 2) + 1 
		for (int i = 0; i < MAX_WORKERTHREAD; i++)
		{
			mIOWorkerThreads.emplace_back([this]() { WokerThread(); });
		}

		printf("WokerThread ����..\n");
		return true;
	}

	//������� �ʴ� Ŭ���̾�Ʈ ���� ����ü�� ��ȯ�Ѵ�.
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

	//Overlapped I/O�۾��� ���� �Ϸ� �뺸�� �޾� 
	//�׿� �ش��ϴ� ó���� �ϴ� �Լ�
	void WokerThread()
	{
		//CompletionKey�� ���� ������ ����
		ClientInfo* pClientInfo = nullptr;
		//�Լ�ȣ�� ���� ����
		BOOL bSuccess = TRUE;
		//Overlapped I/O�۾����� ���۵� ������ ũ��
		DWORD dwIoSize = 0;
		//I/O �۾��� ���� ��û�� Overlapped ����ü�� ���� ������
		LPOVERLAPPED lpOverlapped = NULL;

		while (mIsWorkerRun)
		{
			//////////////////////////////////////////////////////
			//�� �Լ��� ���� ��������� WaitingThread Queue��
			//��� ���·� ���� �ȴ�.
			//�Ϸ�� Overlapped I/O�۾��� �߻��ϸ� IOCP Queue����
			//�Ϸ�� �۾��� ������ �� ó���� �Ѵ�.
			//�׸��� PostQueuedCompletionStatus()�Լ������� �����
			//�޼����� �����Ǹ� �����带 �����Ѵ�.
			// **PostQueuedCompletionStatus()**
			// ����ڰ� ���� �Է��� ������ IOCP Queue�� �߰��� �� �ִ�.
			// �Ϲ������� IOCP ���� �̿��� ���� ���α׷��� ���� �� �� �����带 ���� ���� �����ϱ� ���� �뵵�� ���� ���δ�.
			//////////////////////////////////////////////////////
			bSuccess = GetQueuedCompletionStatus(
				mIOCPHandle,
				&dwIoSize,
				(PULONG_PTR)&pClientInfo,
				&lpOverlapped,
				INFINITE
			);

			//����� ������ ���� �޼��� ó��...
			if (TRUE == bSuccess && 0 == dwIoSize && NULL == lpOverlapped)
			{
				mIsWorkerRun = false;
				continue;
			}

			if (NULL == lpOverlapped)
				continue;

			OverlappedEx* pOverlappedEx = (OverlappedEx*)lpOverlapped;

			//client�� ������ ��������..			
			if (FALSE == bSuccess ||
				(0 == dwIoSize && IOOperation::ACCEPT != pOverlappedEx->m_eOperation))
			{
				//printf("socket(%d) ���� ����\n", (int)pClientInfo->m_socketClient);
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
			//Overlapped I/O Recv�۾� ��� �� ó��
			else if (IOOperation::RECV == pOverlappedEx->m_eOperation)
			{
				OnReceive(pClientInfo->GetIndex(), dwIoSize, pClientInfo->RecvBuffer());
				pClientInfo->BindRecv();
			}
			//Overlapped I/O Send�۾� ��� �� ó��
			else if (IOOperation::SEND == pOverlappedEx->m_eOperation)
			{
				pClientInfo->SendCompleted(dwIoSize);
			}
			//���ܻ�Ȳ
			else
			{
				printf("socket(%d)���� ���ܻ�Ȳ\n", pClientInfo->GetIndex());
			}
		}
	}

	//������ ������ ���� ��Ų��.
	void CloseSocket(ClientInfo* pClientInfo, bool bIsForce = false)
	{
		auto clientIndex = pClientInfo->GetIndex();

		pClientInfo->CloseSocket(bIsForce);

		OnClose(clientIndex);
	}

	//accept��û�� ó���ϴ� ������ ����
	bool CreateAccepterThread()
	{
		//std::thread([this]() { AccepterThread(); })�� �������
		//���ξ�����ʹ� �и��ż� ��������� AccepterThread()�� ���δ�
		//���������� Ŭ���̾�Ʈ ������ ���� �� �Լ� ���δ� block�� ������
		mAccepterThread = thread([this]() { AccepterThread(); });

		printf("AccepterThread ����..\n");
		return true;
	}

	//������� ������ �޴� ������
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

	//Ŭ���̾�Ʈ ���� ���� ����ü
	vector<ClientInfo*> mClientInfos;

	//Ŭ���̾�Ʈ�� ������ �ޱ����� ���� ����
	SOCKET		mListenSocket = INVALID_SOCKET;

	//���� �Ǿ��ִ� Ŭ���̾�Ʈ ��
	int			mClientCnt = 0;

	//IO Worker ������
	vector<thread> mIOWorkerThreads;

	//Accept ������
	thread	mAccepterThread;

	//CompletionPort��ü �ڵ�
	HANDLE		mIOCPHandle = INVALID_HANDLE_VALUE;

	//Send ������
	thread	mSendThread;

	//�۾� ������ ���� �÷���
	bool		mIsWorkerRun = true;

	//���� ������ ���� �÷���
	bool		mIsAccepterRun = true;

	//Send ������ ���� �÷���
	bool		mIsSenderRun = false;

	//���� ����
	char		mSocketBuf[1024] = { 0, };
};