#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// Ŭ���̾�Ʈ�� ���� ��Ŷ�� �����ϴ� ����ü
struct PacketData
{
	UINT32 SessionIndex = 0;
	UINT32 DataSize = 0;
	char* pPacketData = nullptr;

	void Set(PacketData& value)
	{
		this->SessionIndex = value.SessionIndex;
		this->DataSize = value.DataSize;

		pPacketData = new char[value.DataSize];
		CopyMemory(pPacketData, value.pPacketData, value.DataSize);
	}

	void Set(UINT32 sessionIndex, UINT32 dataSize, char* pData)
	{
		SessionIndex = sessionIndex;
		DataSize = dataSize;

		pPacketData = new char[dataSize];
		CopyMemory(pPacketData, pData, dataSize);
	}

	void Release()
	{
		delete pPacketData;
	}
};

struct PacketInfo
{
	UINT32 ClientIndex = 0;
	UINT16 PacketId = 0;
	UINT16 DataSize = 0;
	char* pDataPtr = nullptr;
};


enum class  PACKET_ID : UINT16
{
	//SYSTEM
	SYS_USER_CONNECT = 11,
	SYS_USER_DISCONNECT = 12,
	SYS_END = 30,

	//DB
	DB_END = 199,

	//Client
	LOGIN_REQUEST = 201,
	LOGIN_RESPONSE = 202,

	ROOM_ENTER_REQUEST = 206,
	ROOM_ENTER_RESPONSE = 207,
	ROOM_NEW_USER_NTF = 208,
	ROOM_USER_LIST_NTF = 209,

	ROOM_LEAVE_REQUEST = 215,
	ROOM_LEAVE_RESPONSE = 216,
	ROOM_LEAVE_USER_NTF = 217,

	ROOM_CHAT_REQUEST = 221,
	ROOM_CHAT_RESPONSE = 222,
	ROOM_CHAT_NOTIFY = 223,
};

#pragma pack(push,1)
struct PACKET_HEADER
{
	UINT16 PacketLength;
	UINT16 PacketId;
	UINT8 Type; //���࿩�� ��ȣȭ���� �� �Ӽ��� �˾Ƴ��� ��
};

const UINT32 PACKET_HEADER_LENGTH = sizeof(PACKET_HEADER);

//- �α��� ��û
const int MAX_USER_ID_LEN = 32;
const int MAX_USER_PW_LEN = 32;

struct LOGIN_REQUEST_PACKET : public PACKET_HEADER
{
	char UserID[MAX_USER_ID_LEN + 1];
	char UserPW[MAX_USER_PW_LEN + 1];
};
const size_t LOGIN_REQUEST_PACKET_SIZE = sizeof(LOGIN_REQUEST_PACKET);


struct LOGIN_RESPONSE_PACKET : public PACKET_HEADER
{
	UINT16 Result;
};



//- �뿡 ���� ��û
//const int MAX_ROOM_TITLE_SIZE = 32;
struct ROOM_ENTER_REQUEST_PACKET : public PACKET_HEADER
{
	INT32 RoomNumber;
};

struct ROOM_ENTER_RESPONSE_PACKET : public PACKET_HEADER
{
	INT16 Result;
	//char RivalUserID[MAX_USER_ID_LEN + 1] = { 0, };
};

// ����� ������ ���� ����ü ����
struct UserInfo
{
	int64_t userUniqueId = 0; // UserUniqueId (64��Ʈ)
	BYTE idLen = 0;           // UserID ���� (1����Ʈ)
	char userID[MAX_USER_ID_LEN + 1] = { 0, }; // UserID (�ִ� ũ��)
};

#define MAX_USER_COUNT 100 // �ִ� ����� ��
struct ROOM_USER_LIST_NTIFY : public PACKET_HEADER
{
	BYTE userCnt = 0;                 // ����� ��
	UserInfo userList[MAX_USER_COUNT]; // ����� ���� �迭
};

struct ROOM_ENTER_NEW_USER_NOTIFY : public PACKET_HEADER
{
	int64_t UserUniqueId;
	BYTE UserIDLen;
	char UserID[MAX_USER_ID_LEN + 1] = { 0, };
};


//- �� ������ ��û
struct ROOM_LEAVE_REQUEST_PACKET : public PACKET_HEADER
{
};

struct ROOM_LEAVE_RESPONSE_PACKET : public PACKET_HEADER
{
	INT16 Result;
};

struct ROOM_LEAVE_USER_NOTIFY :public PACKET_HEADER
{
	int64_t UserUniqueId;
};

// �� ä��
const int MAX_CHAT_MSG_SIZE = 256;
struct ROOM_CHAT_REQUEST_PACKET : public PACKET_HEADER
{
	char Message[MAX_CHAT_MSG_SIZE + 1] = { 0, };
};

struct ROOM_CHAT_RESPONSE_PACKET : public PACKET_HEADER
{
	INT16 Result;
};

struct ROOM_CHAT_NOTIFY_PACKET : public PACKET_HEADER
{
	char UserID[MAX_USER_ID_LEN + 1] = { 0, };
	char Msg[MAX_CHAT_MSG_SIZE + 1] = { 0, };
};
#pragma pack(pop) //���� ������ ��ŷ������ �����