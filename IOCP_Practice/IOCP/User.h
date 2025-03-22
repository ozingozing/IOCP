#pragma once
#include <string>

#include "Packet.h"

class User
{

	const UINT32 PACKET_DATA_BUFFER_SIZE = 8096;

public:
	enum class DOMAIN_STATE
	{
		NONE = 0,
		LOGIN = 1,
		ROOM = 2
	};

	User() = default;
	~User() = default;

	void Init(const INT32 index)
	{
		mIndex = index;
		mPacketDataBuffer = new char[PACKET_DATA_BUFFER_SIZE];
	}

	void Clear()
	{
		mRoomIndex = -1;
		mUserID = "";
		mIsConfirm = false;
		mCurDomainState = DOMAIN_STATE::NONE;

		mPacketDataBufferWPos = 0;
		mPacketDataBufferRPos = 0;
	}

	int SetLogin(char* userId_)
	{
		mCurDomainState = DOMAIN_STATE::LOGIN;
		mUserID = userId_;

		return 0;
	}

	void EnterRoom(INT32 roomIndex_)
	{
		mRoomIndex = roomIndex_;
		mCurDomainState = DOMAIN_STATE::ROOM;
	}

	void SetDomainState(DOMAIN_STATE value_) { mCurDomainState = value_; }

	INT32 GetCurrentRoom() { return mRoomIndex; }

	INT32 GetNetConnIdx() { return mIndex; }

	std::string GetUserId() const { return  mUserID; }

	DOMAIN_STATE GetDomainState() { return mCurDomainState; }

	void SetPacketData(const UINT32 dataSize_, char* pData_)
	{
		if ((mPacketDataBufferWPos + dataSize_) >= PACKET_DATA_BUFFER_SIZE)
		{
			auto remainDataSize = mPacketDataBufferWPos - mPacketDataBufferRPos;
			
			if (remainDataSize > 0)
			{
				CopyMemory(&mPacketDataBuffer[0], &mPacketDataBuffer[mPacketDataBufferRPos], remainDataSize);
				mPacketDataBufferWPos = remainDataSize;
			}
			else
				mPacketDataBufferWPos = 0;

			mPacketDataBufferRPos = 0;
		}
		else
		{
			CopyMemory(&mPacketDataBuffer[mPacketDataBufferWPos], pData_, dataSize_);
			mPacketDataBufferWPos += dataSize_;
		}
	}

	PacketInfo GetPacket()
	{
		const int PACKET_SZIE_LENGTH = 2;
		const int PACKET_TYPE_LENGTH = 2;
		short packetSize = 0;

		UINT32 remainByte = mPacketDataBufferWPos - mPacketDataBufferRPos;

		if (remainByte < PACKET_HEADER_LENGTH)
		{
			return PacketInfo();
		}

		auto pHeader = (PACKET_HEADER*)&mPacketDataBuffer[mPacketDataBufferRPos];
		if (pHeader->PacketLength > remainByte)
		{
			return PacketInfo();
		}

		PacketInfo packetInfo;
		packetInfo.PacketId = pHeader->PacketId;
		packetInfo.DataSize = pHeader->PacketLength;
		packetInfo.pDataPtr = &mPacketDataBuffer[mPacketDataBufferRPos];
		mPacketDataBufferRPos += pHeader->PacketLength;

		return packetInfo;
	}

private:
	INT32 mIndex = -1;

	INT32 mRoomIndex = -1;

	std::string mUserID;
	bool mIsConfirm = false;
	std::string mAuthToken;

	DOMAIN_STATE mCurDomainState = DOMAIN_STATE::NONE;

	UINT32 mPacketDataBufferWPos = 0;
	UINT32 mPacketDataBufferRPos = 0;
	char* mPacketDataBuffer = nullptr;
};