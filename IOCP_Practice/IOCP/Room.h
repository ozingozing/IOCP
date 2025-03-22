#pragma once

#include "UserManager.h"
#include "Packet.h"

#include <functional>

using namespace std;

class Room
{
public:
	Room() = default;
	~Room() = default;

	INT32 GetMaxUserCount() { return mMaxUserCount; }

	INT32 GetCurrentUserCount() { return mCurrentUserCount; }

	INT32 GetRoomNumber() { return mRoomNum; }

	void Init(const INT32 roomNum_, const INT32 maxUserCount_)
	{
		mRoomNum = roomNum_;
		mMaxUserCount = maxUserCount_;
	}

	UINT16 EnterUser(User* user_)
	{
		if (mCurrentUserCount >= mMaxUserCount)
			return (UINT16)ERROR_CODE::ENTER_ROOM_FULL_USER;

		mUserList.push_back(user_);
		++mCurrentUserCount;

		user_->EnterRoom(mRoomNum);
		return (UINT16)ERROR_CODE::NONE;
	}

	void LeaveUser(User* leaveUser_)
	{
		mUserList.remove_if([leaveUserId = leaveUser_->GetUserId()](User* pUser) {
			return leaveUserId == pUser->GetUserId();
		});
	}
	
	void GetExistingUserInRoom(INT32 sendTartgetClient)
	{
		//Get Users already exist
		ROOM_USER_LIST_NTIFY roomUserListNtf;
		roomUserListNtf.PacketId = (UINT16)PACKET_ID::ROOM_USER_LIST_NTF;
		roomUserListNtf.PacketLength = sizeof(roomUserListNtf);

		// 사용자 정보 삽입
		for (const auto& user : mUserList)
		{
			if (user->GetNetConnIdx() == sendTartgetClient)
				continue;

			UserInfo userInfo;
			userInfo.userUniqueId = (UINT32)user->GetNetConnIdx(); // UserUniqueId
			const std::string& userID = user->GetUserId();
			userInfo.idLen = (BYTE)sizeof(userInfo.userID); // UserID 길이
			CopyMemory(userInfo.userID, (char*)userID.c_str(), sizeof(userInfo.userID)); // UserID

			roomUserListNtf.userList[roomUserListNtf.userCnt++] = userInfo; // 사용자 정보 배열에 추가
		}
		// 클라이언트에게 데이터 전송
		if (roomUserListNtf.userCnt > 0)
		{
			SendPacketFunc((UINT32)sendTartgetClient, (UINT32)sizeof(roomUserListNtf), (char*)&roomUserListNtf);
		}
	}

	void NotifyEnterUser(INT32 clientIndex_, const char* userID_)
	{
		//Notify New User
		ROOM_ENTER_NEW_USER_NOTIFY roomEnterNewUserNtf;
		roomEnterNewUserNtf.PacketId = (UINT16)PACKET_ID::ROOM_NEW_USER_NTF;
		roomEnterNewUserNtf.PacketLength = sizeof(roomEnterNewUserNtf);

		roomEnterNewUserNtf.UserIDLen = (BYTE)sizeof(roomEnterNewUserNtf.UserID);

		roomEnterNewUserNtf.UserUniqueId = (UINT32)clientIndex_;
		CopyMemory(roomEnterNewUserNtf.UserID, userID_, sizeof(roomEnterNewUserNtf.UserID));
		SendToAllUser(sizeof(roomEnterNewUserNtf), (char*)&roomEnterNewUserNtf, clientIndex_, false);
	}

	void NotifyLeaveUser(INT32 clientIndex_)
	{
		ROOM_LEAVE_USER_NOTIFY roomLeaveUserNtf;
		roomLeaveUserNtf.PacketId = (UINT16)PACKET_ID::ROOM_LEAVE_USER_NTF;
		roomLeaveUserNtf.PacketLength = sizeof(roomLeaveUserNtf);
		roomLeaveUserNtf.UserUniqueId = clientIndex_;
		SendToAllUser(sizeof(roomLeaveUserNtf), (char*)&roomLeaveUserNtf, clientIndex_, false);
	}

	void NotifyChat(INT32 clientIndex_, const char* userID_, const char* msg_)
	{
		ROOM_CHAT_NOTIFY_PACKET roomChatNtfyPkt;
		roomChatNtfyPkt.PacketId = (UINT16)PACKET_ID::ROOM_CHAT_NOTIFY;
		roomChatNtfyPkt.PacketLength = sizeof(roomChatNtfyPkt);

		CopyMemory(roomChatNtfyPkt.UserID, userID_, sizeof(roomChatNtfyPkt.UserID));
		CopyMemory(roomChatNtfyPkt.Msg, msg_, sizeof(roomChatNtfyPkt.Msg));
		SendToAllUser(sizeof(roomChatNtfyPkt), (char*)&roomChatNtfyPkt, clientIndex_, false);
	}

	function<void(UINT32, UINT32, char*)> SendPacketFunc;

private:
	void SendToAllUser(const UINT16 dataSize_, char* data_, const INT32 passUserIndex_, bool exceptMe)
	{
		for (auto pUser : mUserList)
		{
			if (pUser == nullptr)
				continue;
			if (exceptMe && pUser->GetNetConnIdx() == passUserIndex_)
				continue;

			SendPacketFunc((UINT32)pUser->GetNetConnIdx(), (UINT32)dataSize_, data_);
		}
	}

	INT32 mRoomNum = -1;

	std::list<User*> mUserList;

	INT32 mMaxUserCount = 0;

	UINT16 mCurrentUserCount = 0;
};