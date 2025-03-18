#pragma once

#include "Define.h"
#include <stdio.h>
#include <mutex>
#include <queue>

//Ŭ���̾�Ʈ ������ ������� ����ü
class ClientInfo
{
public:
	ClientInfo()
	{
		ZeroMemory(mRecvBuf, sizeof(mRecvBuf));
		ZeroMemory(mAcceptBuf, sizeof(mAcceptBuf));
		ZeroMemory(&mRecvOverlappedEx, sizeof(OverlappedEx));
		mSock = INVALID_SOCKET;
	}

	void Init(const UINT32 index, HANDLE iocpHandle_)
	{
		mIndex = index;
		mIOCPHandle = iocpHandle_;
	}

	UINT32 GetIndex() { return mIndex; }

	bool IsConnectd() { return mIsConnect == true; }

	SOCKET GetSock() { return mSock; }

	UINT64 GetLatestClosedTimeSec() { return mLastClosedTimeSec; }

	char* RecvBuffer() { return mRecvBuf; }

	bool OnConnectSocket(HANDLE iocpHandle)
	{
		mIsConnect = true;

		Clear();

		//I/O Completion Port��ü�� ������ �����Ų��.
		if (BindIOCompletionPort(iocpHandle) == false)
			return false;

		return BindRecv();
	}

	//CompletionPort��ü�� ���ϰ� CompletionKey�� �����Ű�� ������ �Ѵ�.
	bool BindIOCompletionPort(HANDLE handle)
	{
		//socket�� pClientInfo�� CompletionPort��ü�� �����Ų��.
		auto hIOCP = CreateIoCompletionPort(
			(HANDLE)GetSock(),
			handle,
			(ULONG_PTR)(this),
			0
		);

		if (hIOCP == INVALID_HANDLE_VALUE)
		{
			printf("[����] CreateIoCompletionPort()�Լ� ����: %d\n", GetLastError());
			return false;
		}

		return true;
	}

	//WSARecv Overlapped I/O �۾��� ��Ų��.
	bool BindRecv()
	{
		DWORD dwFlag = 0;
		DWORD dwRecvNumBytes = 0;

		//Overlapped I/O�� ���� �� ������ �������ش�.
		mRecvOverlappedEx.m_wsaBuf.len = MAX_SOCKBUF;
		mRecvOverlappedEx.m_wsaBuf.buf = mRecvBuf;
		mRecvOverlappedEx.m_eOperation = IOOperation::RECV;

		int nRet = WSARecv(
			mSock,
			&(mRecvOverlappedEx.m_wsaBuf),
			1,
			&dwRecvNumBytes,
			&dwFlag,
			(LPWSAOVERLAPPED) & (mRecvOverlappedEx),
			NULL
		);

		//socket_error�̸� Client Socket�� �������ɷ� ó���Ѵ�.
		if (nRet == SOCKET_ERROR && (WSAGetLastError()) != ERROR_IO_PENDING)
		{
			printf("[����] WSARecv()�Լ� ���� : %d\n", WSAGetLastError());
			return false;
		}

		return true;
	}

	//������ ������ ���� ��Ų��.
	void CloseSocket(bool bIsForce = false)
	{
		struct linger stLinger = { 0, 0 };	// SO_DONTLINGER�� ����

		// bIsForce�� true�̸� SO_LINGER, timeout = 0���� �����Ͽ� ���� ���� ��Ų��. ���� : ������ �ս��� ������ ���� 
		if (true == bIsForce)
		{
			stLinger.l_onoff = 1;
		}

		//socketClose������ ������ �ۼ����� ��� �ߴ� ��Ų��.
		shutdown(mSock, SD_BOTH);

		//���� �ɼ��� �����Ѵ�.
		setsockopt(mSock, SOL_SOCKET, SO_LINGER, (char*)&stLinger, sizeof(stLinger));

		//Disconnect TimeStamp
		mIsConnect = false;
		mLastClosedTimeSec = chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now().time_since_epoch()).count();

		//���� ������ ���� ��Ų��. 
		closesocket(mSock);
		mSock = INVALID_SOCKET;
	}

	//WSASend Overlapped I/O�۾��� ��Ų��.
	bool SendMsg(const UINT32 dataSize_, char* pMsg_)
	{
		auto sendOverlappedEx = new OverlappedEx;
		ZeroMemory(sendOverlappedEx, sizeof(OverlappedEx));
		sendOverlappedEx->m_wsaBuf.len = dataSize_;
		sendOverlappedEx->m_wsaBuf.buf = new char[dataSize_];
		CopyMemory(sendOverlappedEx->m_wsaBuf.buf, pMsg_, dataSize_);
		sendOverlappedEx->m_eOperation = IOOperation::SEND;

		lock_guard<mutex> guard(mSendLock);

		mSendDataqueue.push(sendOverlappedEx);

		if (mSendDataqueue.size() == 1)
			SendIO();
		return true;
	}

	void SendCompleted(const UINT32 dataSize_)
	{
		printf("[�۽� �Ϸ�] bytes : %d\n", dataSize_);

		lock_guard<mutex> guard(mSendLock);

		delete[] mSendDataqueue.front()->m_wsaBuf.buf;
		delete mSendDataqueue.front();

		mSendDataqueue.pop();

		if (mSendDataqueue.empty() == false)
			SendIO();
	}

	void Clear()
	{
	}

	bool PostAccept(SOCKET listenSock_, const UINT64 curTimeSec_)
	{
		printf_s("PostAccept. Client Index : %d\n", GetIndex());

		mLastClosedTimeSec = UINT32_MAX;

		mSock = WSASocket(
			AF_INET,
			SOCK_STREAM,
			IPPROTO_IP,
			NULL,
			0,
			WSA_FLAG_OVERLAPPED
		);

		if (mSock == INVALID_SOCKET)
		{
			printf_s("Client SOcket WSASocket Error : %d\n", GetLastError());
			return false;
		}

		ZeroMemory(&mAcceptContext, sizeof(OverlappedEx));

		DWORD bytes = 0;
		DWORD flags = 0;
		mAcceptContext.m_wsaBuf.len = 0;
		mAcceptContext.m_wsaBuf.buf = nullptr;
		mAcceptContext.m_eOperation = IOOperation::ACCEPT;
		mAcceptContext.SessionIndex = mIndex;

		if (AcceptEx(
				listenSock_,
				mSock,
				mAcceptBuf,
				0,
				sizeof(SOCKADDR_IN) + 16,
				sizeof(SOCKADDR_IN) + 16,
				&bytes,
				(LPWSAOVERLAPPED) & (mAcceptContext)) == FALSE)
		{
			if (WSAGetLastError() != WSA_IO_PENDING)
			{
				printf_s("AcceptEX Error : %d\n", GetLastError());
				return false;
			}
		}

		return true;
	}

	bool AcceptCompletion()
	{
		printf_s("AcceptCompletion : SessionIndex(%d)\n", mIndex);

		if (OnConnectSocket(mIOCPHandle) == false)
		{
			return false;
		}

		SOCKADDR_IN clientAddr;
		int nAddrLen = sizeof(SOCKADDR_IN);
		char clientIP[32] = { 0, };
		inet_ntop(AF_INET, &(clientAddr.sin_addr), clientIP, 32 - 1);
		printf("Ŭ���̾�Ʈ ���� : IP(%s) SOCKET(%d)\n", clientIP, (int)mSock);

		return true;
	}

private:
	bool SendIO()
	{
		auto sendOverlappedEx = mSendDataqueue.front();
		DWORD dwRecvNumBytes = 0;

		int nRet = WSASend(
			mSock,
			&(sendOverlappedEx->m_wsaBuf),
			1,
			&dwRecvNumBytes,
			0,
			(LPWSAOVERLAPPED)sendOverlappedEx,
			NULL
		);

		//socket_error�̸� client socket�� �������ɷ� ó���Ѵ�.
		if (nRet == SOCKET_ERROR && (WSAGetLastError() != ERROR_IO_PENDING))
		{
			printf("[����] WSASend()�Լ� ���� : %d\n", WSAGetLastError());
			return false;
		}

		return true;
	}

private:
	INT32 mIndex = 0;
	//CompletionPort��ü �ڵ�
	HANDLE		mIOCPHandle = INVALID_HANDLE_VALUE;
	
	INT64 mIsConnect = false;
	UINT64 mLastClosedTimeSec = 0;
	SOCKET			mSock;					//Client�� ����Ǵ� ����

	OverlappedEx	mAcceptContext;			//ACCEPT Overlapped I/O�۾��� ���� ����
	char mAcceptBuf[64];					//IPAddr ����

	OverlappedEx	mRecvOverlappedEx;		//RECV Overlapped I/O�۾��� ���� ����
	char			mRecvBuf[MAX_SOCKBUF];	//������ ����	

	mutex mSendLock;
	queue<OverlappedEx*> mSendDataqueue;
};