#pragma once

#include "Define.h"
#include <stdio.h>
#include <mutex>
#include <queue>

//클라이언트 정보를 담기위한 구조체
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

		//I/O Completion Port객체와 소켓을 연결시킨다.
		if (BindIOCompletionPort(iocpHandle) == false)
			return false;

		return BindRecv();
	}

	//CompletionPort객체와 소켓과 CompletionKey를 연결시키는 역할을 한다.
	bool BindIOCompletionPort(HANDLE handle)
	{
		//socket과 pClientInfo를 CompletionPort객체와 연결시킨다.
		auto hIOCP = CreateIoCompletionPort(
			(HANDLE)GetSock(),
			handle,
			(ULONG_PTR)(this),
			0
		);

		if (hIOCP == INVALID_HANDLE_VALUE)
		{
			printf("[에러] CreateIoCompletionPort()함수 실패: %d\n", GetLastError());
			return false;
		}

		return true;
	}

	//WSARecv Overlapped I/O 작업을 시킨다.
	bool BindRecv()
	{
		DWORD dwFlag = 0;
		DWORD dwRecvNumBytes = 0;

		//Overlapped I/O를 위해 각 정보를 셋팅해준다.
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

		//socket_error이면 Client Socket에 끊어지걸로 처리한다.
		if (nRet == SOCKET_ERROR && (WSAGetLastError()) != ERROR_IO_PENDING)
		{
			printf("[에러] WSARecv()함수 실패 : %d\n", WSAGetLastError());
			return false;
		}

		return true;
	}

	//소켓의 연결을 종료 시킨다.
	void CloseSocket(bool bIsForce = false)
	{
		struct linger stLinger = { 0, 0 };	// SO_DONTLINGER로 설정

		// bIsForce가 true이면 SO_LINGER, timeout = 0으로 설정하여 강제 종료 시킨다. 주의 : 데이터 손실이 있을수 있음 
		if (true == bIsForce)
		{
			stLinger.l_onoff = 1;
		}

		//socketClose소켓의 데이터 송수신을 모두 중단 시킨다.
		shutdown(mSock, SD_BOTH);

		//소켓 옵션을 설정한다.
		setsockopt(mSock, SOL_SOCKET, SO_LINGER, (char*)&stLinger, sizeof(stLinger));

		//Disconnect TimeStamp
		mIsConnect = false;
		mLastClosedTimeSec = chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now().time_since_epoch()).count();

		//소켓 연결을 종료 시킨다. 
		closesocket(mSock);
		mSock = INVALID_SOCKET;
	}

	//WSASend Overlapped I/O작업을 시킨다.
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
		printf("[송신 완료] bytes : %d\n", dataSize_);

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
		printf("클라이언트 접속 : IP(%s) SOCKET(%d)\n", clientIP, (int)mSock);

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

		//socket_error이면 client socket이 끊어진걸로 처리한다.
		if (nRet == SOCKET_ERROR && (WSAGetLastError() != ERROR_IO_PENDING))
		{
			printf("[에러] WSASend()함수 실패 : %d\n", WSAGetLastError());
			return false;
		}

		return true;
	}

private:
	INT32 mIndex = 0;
	//CompletionPort객체 핸들
	HANDLE		mIOCPHandle = INVALID_HANDLE_VALUE;
	
	INT64 mIsConnect = false;
	UINT64 mLastClosedTimeSec = 0;
	SOCKET			mSock;					//Client와 연결되는 소켓

	OverlappedEx	mAcceptContext;			//ACCEPT Overlapped I/O작업을 위한 변수
	char mAcceptBuf[64];					//IPAddr 버퍼

	OverlappedEx	mRecvOverlappedEx;		//RECV Overlapped I/O작업을 위한 변수
	char			mRecvBuf[MAX_SOCKBUF];	//데이터 버퍼	

	mutex mSendLock;
	queue<OverlappedEx*> mSendDataqueue;
};