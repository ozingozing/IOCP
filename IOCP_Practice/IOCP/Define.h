#pragma once

#include <winsock2.h>
#include <Ws2tcpip.h>
#include <mswsock.h>

using namespace std;

const UINT32 MAX_SOCKBUF = 256;	//��Ŷ ũ��
const UINT32 MAX_SOCK_SENDBUF = 4096;	// ���� ������ ũ��
const UINT32 MAX_WORKERTHREAD = 4;  //������ Ǯ�� ���� ������ ��
const UINT64 RE_USE_SESSION_WAIT_TIMESEC = 3;

enum class IOOperation
{
	NONE,
	RECV,
	SEND,
	ACCEPT,
};

//WSAOVERLAPPED����ü�� Ȯ�� ���Ѽ� �ʿ��� ������ �� �־���.
struct OverlappedEx
{
	WSAOVERLAPPED	m_wsaOverlapped;		//Overlapped I/O����ü
	WSABUF			m_wsaBuf;				//Overlapped I/O�۾� ����
	IOOperation		m_eOperation;			//�۾� ���� ����
	UINT32			SessionIndex = 0;

	OverlappedEx() {
		ZeroMemory(&m_wsaOverlapped, sizeof(WSAOVERLAPPED));
		m_wsaBuf.buf = nullptr;
		m_wsaBuf.len = 0;
		m_eOperation = IOOperation::NONE;  // �⺻�� ����
	}
};