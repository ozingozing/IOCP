#pragma once
#include "Room.h"

class RoomManager
{
public:
	RoomManager() = default;
	~RoomManager() = default;

	void Intit(const INT32 beginRoomNumber_, const INT32 maxRoomCount_, const INT32 maxRoomUserCount_)
	{
		mBeginRoomNumber = beginRoomNumber_;
		mMaxRoomCount = maxRoomCount_;
		mEndRoomNumber = beginRoomNumber_ + maxRoomCount_;

		mRoomList = vector<Room*>(maxRoomCount_);

		for (auto i = 0; i < maxRoomCount_; i++)
		{
			mRoomList[i] = new Room();
			mRoomList[i]->SendPacketFunc = SendPacketFunc;
			mRoomList[i]->Init((i + beginRoomNumber_), maxRoomUserCount_);
		}
	}

	UINT16 GetMaxRoomCount() { return mMaxRoomCount; }

	UINT16 EnterUser(INT32 roomNumver_, User* user_)
	{
		auto pRoom = GetRoomByNumber(roomNumver_);
		if (pRoom == nullptr)
			return (UINT16)ERROR_CODE::ROOM_INVALID_INDEX;

		return pRoom->EnterUser(user_);
	}

	INT16 LeaveUser(INT32 roomNumber_, User* user_)
	{
		auto pRoom = GetRoomByNumber(roomNumber_);
		if (pRoom == nullptr)
			return (INT16)ERROR_CODE::ROOM_INVALID_INDEX;

		user_->SetDomainState(User::DOMAIN_STATE::LOGIN);
		pRoom->LeaveUser(user_);
		return (UINT16)ERROR_CODE::NONE;
	}

	Room* GetRoomByNumber(INT32 number_)
	{
		if (number_ < mBeginRoomNumber || number_ >= mEndRoomNumber)
			return nullptr;

		auto index = (number_ - mBeginRoomNumber);
		return mRoomList[index];
	}

	function<void(UINT32, UINT16, char*)> SendPacketFunc;

private:
	std::vector<Room*> mRoomList;
	INT32 mBeginRoomNumber = 0;
	INT32 mEndRoomNumber = 0;
	INT32 mMaxRoomCount = 0;
};