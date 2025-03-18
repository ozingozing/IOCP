#pragma once
#include <unordered_map>

#include "ErrorCode.h"
#include "User.h"

class UserManager
{
public:
	UserManager() = default;
	~UserManager() = default;

	void Init(const INT32 maxUserCount_)
	{
		mMaxUserCnt = maxUserCount_;
		mUserObjPool = std::vector<User*>(mMaxUserCnt);

		for (auto i = 0; i < mMaxUserCnt; i++)
		{
			mUserObjPool[i] = new User();
			mUserObjPool[i]->Init(i);
		}
	}

	INT32 GetCurrentUserCnt() { return mCurrentUserCnt; }

	INT32 GetMaxUserCnt() { return mMaxUserCnt; }

	void IncreaseUserCnt() { mCurrentUserCnt++; }

	void DecreaseUserCnt()
	{
		if (mCurrentUserCnt > 0)
		{
			mCurrentUserCnt--;
		}
	}

	ERROR_CODE AddUser(char* userId_, int clientIndex_)
	{
		//TODO: 유저중복 처리
		if (mUserIDDictionary.find(userId_) != mUserIDDictionary.end())
		{
			return ERROR_CODE::LOGIN_USER_ALREADY;
		}

		auto user_idx = clientIndex_;

		mUserObjPool[user_idx]->SetLogin(userId_);
		mUserIDDictionary.insert(std::pair<char*, int>(userId_, clientIndex_));
		
		return ERROR_CODE::NONE;
	}

	INT32 FindUserIndexByID(char* userID_)
	{
		auto res = mUserIDDictionary.find(userID_);
		if (res != mUserIDDictionary.end())
		{
			return (*res).second;
		}
		return -1;
	}

	void DeleteUserInfo(User* user_)
	{
		mUserIDDictionary.erase(user_->GetUserId());
		user_->Clear();
	}

	User* GetUserByConnIdx(INT32 clientIndex_)
	{
		return mUserObjPool[clientIndex_];
	}

private:
	INT32 mMaxUserCnt = 0;
	INT32 mCurrentUserCnt = 0;

	std::vector<User*> mUserObjPool; //vector로
	std::unordered_map<std::string, int> mUserIDDictionary;
};