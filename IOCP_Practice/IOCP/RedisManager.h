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
			printf("Redis 접속 실패\n");
			return false;
		}

		mIsTaskRun = true;

		for (UINT32 i = 0; i < threadCount_; i++)
		{
			mTaskThreads.emplace_back([this]() {TaskProcessThread(); });
		}

		printf("Redis 동작중...\n");
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
		// 데이터 저장 (SET)
		redisReply* reply = (redisReply*)redisCommand(context, "SET %s %s", userID.c_str(), userPW.c_str());

		if (reply == nullptr) {
			printf("Redis에 데이터 저장 실패\n");
			return false;
		}

		printf("Redis에 데이터 저장 성공: Key='%s', Value='%s'\n", userID.c_str(), userPW.c_str());
		freeReplyObject(reply);

		// 현재 저장된 모든 키와 값을 출력
		redisReply* keysReply = (redisReply*)redisCommand(context, "KEYS *");
		if (keysReply == nullptr) {
			printf("Redis에서 키 조회 실패\n");
			return false;
		}

		if (keysReply->type == REDIS_REPLY_ARRAY) {
			printf("\n현재 Redis에 저장된 모든 데이터:\n");
			for (size_t i = 0; i < keysReply->elements; i++) {
				redisReply* valueReply = (redisReply*)redisCommand(context, "GET %s", keysReply->element[i]->str);
				if (valueReply != nullptr && valueReply->type == REDIS_REPLY_STRING) {
					printf(" - Key: %s, Value: %s\n", keysReply->element[i]->str, valueReply->str);
				}
				freeReplyObject(valueReply);
			}
		}
		else {
			printf("현재 저장된 데이터 없음.\n");
		}

		freeReplyObject(keysReply);
		return true;
	}

	void TaskProcessThread()
	{
		printf("Redis 스레드 시작...\n");

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
					// 데이터 조회 (GET)
					redisReply* reply = (redisReply*)redisCommand(context, "GET %s", pRequest->UserID);

					if (reply == nullptr) {
						printf("Redis 조회 실패: 응답이 NULL입니다.\n");
						return;
					}

					if (reply->type == REDIS_REPLY_STRING) {
						printf("==================== ID 조회 성공!!!! ====================\n");

						std::string redisValue = reply->str;  // Redis에서 가져온 비밀번호 값
						printf("Redis에서 가져온 값: Key='%s', Value='%s'\n", pRequest->UserID, redisValue.c_str());

						if (redisValue == pRequest->UserPW) {  // 비밀번호가 정확하게 일치해야 로그인 성공
							bodyData.Result = (UINT16)ERROR_CODE::NONE; // 로그인 성공
							printf("로그인 성공! UserID: %s\n", pRequest->UserID);
						}
						else {
							bodyData.Result = (UINT16)ERROR_CODE::LOGIN_USER_INVALID_PW; // ID OK / PW 오류
							printf("로그인 실패! 비밀번호 불일치. UserID: %s, 입력된 PW: %s, 저장된 PW: %s\n",
								pRequest->UserID, pRequest->UserPW, redisValue.c_str());
						}
					}
					else if (reply->type == REDIS_REPLY_NIL) {
						// 아이디가 Redis에 존재하지 않는 경우 생성
						printf("==================== ID 조회 실패!!!! ====================\n");
						printf("Redis에 저장되지 않은 키: %s\n", pRequest->UserID);

						if (std::string(pRequest->UserPW).empty() == false) { // ID/PW 첫 생성
							AddUserToRedis(pRequest->UserID, pRequest->UserPW);
							bodyData.Result = (UINT16)ERROR_CODE::LOGIN_USER_CREATE;
							printf("새로운 유저 생성: Key='%s', Value='%s'\n", pRequest->UserID, pRequest->UserPW);
						}
					}

					// **유저 ID를 응답 데이터에 복사**
					strcpy_s(bodyData.UserID, pRequest->UserID);

					// Redis 응답 객체 해제
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
		printf("Redis 스레드 종료\n");
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