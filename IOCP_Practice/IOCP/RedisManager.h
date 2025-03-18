#pragma once

#include "RedisTaskDefine.h"
#include "ErrorCode.h"

#include "../thirdparty/CRedisConn.h"
#include <vector>
#include <deque>
#include <thread>
#include <mutex>

using namespace std;

class RedisManager
{
public :
	RedisManager() = default;
	~RedisManager() = default;

	bool Run(string ip_, UINT16 port_, const UINT32 threadCount_)
	{
		if (Connect(ip_, port_) == false)
		{
			printf("Redis ���� ����\n");
			return false;
		}

		mIsTaskRun = true;

		for (UINT32 i = 0; i < threadCount_; i++)
		{
			mTaskThreads.emplace_back([this]() {TaskProcessThread(); });
		}

		printf("Redis ������...\n");
		return true;
	}

	void End()
	{
		mIsTaskRun = false;

		for (auto& thread : mTaskThreads)
		{
			if (thread.joinable())
			{
				thread.join();
			}
		}
	}

	void PushTask(RedisTask task_)
	{
		lock_guard<mutex> guard(mReqLock);
		mRequestTask.push_back(task_);
	}

	RedisTask TakeResponseTask()
	{
		lock_guard<mutex> guard(mResLock);

		if (mResponseTask.empty())
		{
			return RedisTask();
		}

		auto task = mResponseTask.front();
		mResponseTask.pop_front();

		return task;
	}

private:
	redisContext* context = nullptr;
	bool Connect(std::string ip_, UINT16 port_)
	{
		context = redisConnect((char*)ip_.c_str(), port_);
		//if (mConn.connect(ip_, port_) == false)
		if(context == nullptr || context->err)
		{
			std::cout << "connect error " << mConn.getErrorStr() << std::endl;
			return false;
		}
		else
		{
			std::cout << "connect success !!!" << std::endl;
		}

		return true;
	}

	bool AddUserToRedis(const std::string& userID, const std::string& userPW)
	{
		// ������ ���� (SET)
		redisReply* reply = (redisReply*)redisCommand(context, "SET %s %s", userID.c_str(), userPW.c_str());

		if (reply == nullptr) {
			printf("Redis�� ������ ���� ����\n");
			return false;
		}

		printf("Redis�� ������ ���� ����: Key='%s', Value='%s'\n", userID.c_str(), userPW.c_str());
		freeReplyObject(reply);

		// ���� ����� ��� Ű�� ���� ���
		redisReply* keysReply = (redisReply*)redisCommand(context, "KEYS *");
		if (keysReply == nullptr) {
			printf("Redis���� Ű ��ȸ ����\n");
			return false;
		}

		if (keysReply->type == REDIS_REPLY_ARRAY) {
			printf("\n���� Redis�� ����� ��� ������:\n");
			for (size_t i = 0; i < keysReply->elements; i++) {
				redisReply* valueReply = (redisReply*)redisCommand(context, "GET %s", keysReply->element[i]->str);
				if (valueReply != nullptr && valueReply->type == REDIS_REPLY_STRING) {
					printf(" - Key: %s, Value: %s\n", keysReply->element[i]->str, valueReply->str);
				}
				freeReplyObject(valueReply);
			}
		}
		else {
			printf("���� ����� ������ ����.\n");
		}

		freeReplyObject(keysReply);
		return true;
	}

	void TaskProcessThread()
	{
		printf("Redis ������ ����...\n");

		while (mIsTaskRun)
		{
			auto task = TakeRequestTask();
			if (task.TaskID != RedisTaskID::INVALID)
			{
				if (task.TaskID == RedisTaskID::REQUEST_LOGIN)
				{
					auto pRequest = (RedisLoginReq*)task.pData;

					RedisLoginRes bodyData;
					bodyData.Result = (UINT16)ERROR_CODE::LOGIN_USER_INVALID_PW;
					
					string value;
					// ������ ��ȸ (GET)
					redisReply* reply = (redisReply*)redisCommand(context, "GET %s", pRequest->UserID);

					if (reply == nullptr) {
						printf("Redis ��ȸ ����: ������ NULL�Դϴ�.\n");
						return;
					}

					if (reply->type == REDIS_REPLY_STRING) {
						printf("==================== ID ��ȸ ����!!!! ====================\n");

						std::string redisValue = reply->str;  // Redis���� ������ ��й�ȣ ��
						printf("Redis���� ������ ��: Key='%s', Value='%s'\n", pRequest->UserID, redisValue.c_str());

						if (redisValue == pRequest->UserPW) {  // ��й�ȣ�� ��Ȯ�ϰ� ��ġ�ؾ� �α��� ����
							bodyData.Result = (UINT16)ERROR_CODE::NONE; // �α��� ����
							printf("�α��� ����! UserID: %s\n", pRequest->UserID);
						}
						else {
							bodyData.Result = (UINT16)ERROR_CODE::LOGIN_USER_INVALID_PW; // ID OK / PW ����
							printf("�α��� ����! ��й�ȣ ����ġ. UserID: %s, �Էµ� PW: %s, ����� PW: %s\n",
								pRequest->UserID, pRequest->UserPW, redisValue.c_str());
						}
					}
					else if (reply->type == REDIS_REPLY_NIL) {
						// ���̵� Redis�� �������� �ʴ� ��� ����
						printf("==================== ID ��ȸ ����!!!! ====================\n");
						printf("Redis�� ������� ���� Ű: %s\n", pRequest->UserID);

						if (std::string(pRequest->UserPW).empty() == false) { // ID/PW ù ����
							AddUserToRedis(pRequest->UserID, pRequest->UserPW);
							bodyData.Result = (UINT16)ERROR_CODE::LOGIN_USER_CREATE;
							printf("���ο� ���� ����: Key='%s', Value='%s'\n", pRequest->UserID, pRequest->UserPW);
						}
					}

					// **���� ID�� ���� �����Ϳ� ����**
					strcpy_s(bodyData.UserID, pRequest->UserID);

					// Redis ���� ��ü ����
					freeReplyObject(reply);


					RedisTask resTask;
					resTask.UserIndex = task.UserIndex;
					resTask.TaskID = RedisTaskID::RESPONSE_LOGIN;
					resTask.DataSize = sizeof(RedisLoginRes);
					resTask.pData = new char[resTask.DataSize];
					CopyMemory(resTask.pData, (char*)&bodyData, resTask.DataSize);
					PushResponse(resTask);
				}

				task.Release();
			}

			if (task.TaskID == RedisTaskID::INVALID)
			{
				this_thread::sleep_for(chrono::microseconds(1));
				continue;
			}
		}
		printf("Redis ������ ����\n");
	}

	RedisTask TakeRequestTask()
	{
		lock_guard<mutex> guard(mReqLock);

		if (mRequestTask.empty())
		{
			return RedisTask();
		}

		auto task = mRequestTask.front();
		mRequestTask.pop_front();

		return task;
	}

	void PushResponse(RedisTask task_)
	{
		lock_guard<mutex> guard(mResLock);
		mResponseTask.push_back(task_);
	}

	RedisCpp::CRedisConn mConn;

	bool		mIsTaskRun = false;
	std::vector<std::thread> mTaskThreads;

	std::mutex mReqLock;
	std::deque<RedisTask> mRequestTask;

	std::mutex mResLock;
	std::deque<RedisTask> mResponseTask;
};