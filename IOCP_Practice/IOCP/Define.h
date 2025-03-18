#pragma once

#include <winsock2.h>
#include <Ws2tcpip.h>
#include <mswsock.h>

using namespace std;

const UINT32 MAX_SOCKBUF = 256;	//패킷 크기
const UINT32 MAX_SOCK_SENDBUF = 4096;	// 소켓 버퍼의 크기
const UINT32 MAX_WORKERTHREAD = 4;  //쓰레드 풀에 넣을 쓰레드 수
const UINT64 RE_USE_SESSION_WAIT_TIMESEC = 3;

enum class IOOperation
{
	NONE,
	RECV,
	SEND,
	ACCEPT,
};

//WSAOVERLAPPED구조체를 확장 시켜서 필요한 정보를 더 넣었다.
struct OverlappedEx
{
	WSAOVERLAPPED	m_wsaOverlapped;		//Overlapped I/O구조체
	WSABUF			m_wsaBuf;				//Overlapped I/O작업 버퍼
	IOOperation		m_eOperation;			//작업 동작 종류
	UINT32			SessionIndex = 0;

	OverlappedEx() {
		ZeroMemory(&m_wsaOverlapped, sizeof(WSAOVERLAPPED));
		m_wsaBuf.buf = nullptr;
		m_wsaBuf.len = 0;
		m_eOperation = IOOperation::NONE;  // 기본값 설정
	}
};