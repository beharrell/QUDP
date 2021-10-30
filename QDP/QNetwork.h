#pragma once
#include <chrono>
#include <condition_variable>
#include <list>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

#include <WinSock2.h>
#include <Windows.h>
#include "debugapi.h"

std::string getTimestamp(const std::chrono::system_clock::time_point& now);
std::string getTimestamp();

template <typename... Args>
void Log(const char* format, Args... args) {
	std::string formatAsString = "QUDP[%s] ";
	formatAsString += format;
	formatAsString += "\n";
	char buffer[2000]; // problem when printing consumer pending buffer if we increase window size
	sprintf_s(buffer, sizeof(buffer), formatAsString.c_str(), getTimestamp().c_str(), args...);
	OutputDebugStringA(buffer);
}


template <class T> class BlockingQ
{
private:
	std::list<T> q;
	std::string mQName;

	std::mutex mMux;
	std::condition_variable mConsumerSignal;

	template <typename... Args>
	void Log(const char* format, Args... args)
	{
		if (!mQName.empty())
		{ 
			::Log(format, args...);
		}
	}

public:
	BlockingQ(const std::string& name) :mQName(name) {}
	BlockingQ() {} // unnamed, no logging

	void EnQ(T data)
	{
		std::unique_lock<std::mutex> lock(mMux);
		q.emplace_back(data);
		if (q.size() == 1)
		{
			Log("%s Waking consumer", mQName.c_str());
			mConsumerSignal.notify_one();
		}
	}

	bool DeQ(T& data, std::chrono::duration<int, std::milli>& timeOut)
	{
		std::unique_lock<std::mutex> lock(mMux);
		if (q.size() == 0)
		{
			Log("%s Consumer waiting for Data", mQName.c_str());
			auto hasData = mConsumerSignal.wait_for(lock, timeOut, [&] {return q.size() > 0; });
			if (!hasData)
			{
				Log("%s Consumer timed out", mQName.c_str());
				return false;
			}
			Log("%s Consumer woke with Data", mQName.c_str());
		}

		data = q.front();
		q.pop_front();
		return true;
	}

	void DeQ(T& data)
	{
		std::unique_lock<std::mutex> lock(mMux);
		if (q.size() == 0)
		{
			Log("%s Consumer waiting for Data", mQName.c_str());
			mConsumerSignal.wait(lock);
		}
		data = q.front();
		q.pop_front();
	}

	size_t Size()
	{
		std::lock_guard<std::mutex> lock(mMux);
		return q.size();
	}
};




class INetwork
{
public:
	virtual ~INetwork() {}
	virtual void ProducerEnQ(const std::vector<uint8_t>& data) = 0;
	virtual bool ProducerDeQ(std::vector<uint8_t>& data, std::chrono::duration<int, std::milli>& timeOut) = 0;
	virtual void ConsumerEnQ(const std::vector<uint8_t>& data) = 0;
	virtual bool ConsumeDeQ(std::vector<uint8_t>& data, std::chrono::duration<int, std::milli>& timeOut) = 0;
	virtual size_t ConsumerToProducerSize() = 0;
	virtual size_t ProducerToConsumerSize() = 0;
};

class IdealNetwork : public INetwork
{
private:
	BlockingQ<std::vector<uint8_t>> mProdToConsumer;
	BlockingQ<std::vector<uint8_t>> mConsumerToProducer;

public:
	IdealNetwork() :mProdToConsumer(/*"P->C"*/), mConsumerToProducer(/*"C->P"*/)
	{}

	void ProducerEnQ(const std::vector<uint8_t>& data) override
	{
		mProdToConsumer.EnQ(data);
	}
	bool ProducerDeQ(std::vector<uint8_t>& data, std::chrono::duration<int, std::milli>& timeOut) override
	{
		return mConsumerToProducer.DeQ(data, timeOut);
	}
	void ConsumerEnQ(const std::vector<uint8_t>& data) override
	{
		mConsumerToProducer.EnQ(data);
	}
	bool ConsumeDeQ(std::vector<uint8_t>& data, std::chrono::duration<int, std::milli>& timeOut) override
	{
		return mProdToConsumer.DeQ(data, timeOut);
	}

	size_t Size()
	{
		return mProdToConsumer.Size() + mConsumerToProducer.Size();
	}

	size_t ProducerToConsumerSize() override
	{
		return mProdToConsumer.Size();
	}

	size_t ConsumerToProducerSize() override
	{
		return mConsumerToProducer.Size();
	}
};


// refactor to producer
class UdpNetwork : public INetwork
{
private:
	int mProducerSocket;
	sockaddr_in mConsumersAddress{};

	int consumerSocket;
	sockaddr_in mProducersAddress{};  // not known until first frame from the producer
	bool mHaveProducerAddr{ false };

	bool ReceiveData(int socket, std::vector<uint8_t>& data, std::chrono::duration<int, std::milli>& timeOut,
		sockaddr_in* senderAddress, int* senderAddressSize);

	bool mIsProducer{false};
	bool mIsConsumer{ false };

	void InitWinSock();
	void  InitAsProducer(const std::string& consumerAddress, int consumerPort);
	void  InitAsConsumer(int consumerPort);

public:
	// init as prod/consumer on loopback address
	UdpNetwork();

	// init as producer
	UdpNetwork(const std::string& consumerAddress, int consumerPort);

	// init as consumer
	UdpNetwork(int consumerPort);

	~UdpNetwork();

	void ProducerEnQ(const std::vector<uint8_t>& data) override;

	bool ProducerDeQ(std::vector<uint8_t>& data, std::chrono::duration<int, std::milli>& timeOut) override;

	void ConsumerEnQ(const std::vector<uint8_t>& data) override;

	bool ConsumeDeQ(std::vector<uint8_t>& data, std::chrono::duration<int, std::milli>& timeOut) override;

	/// <summary>
	/// pull size methods out to a testing interface
	/// </summary>
	/// <returns></returns>
	size_t Size()
	{
		return 0;
	}

	size_t ProducerToConsumerSize() override
	{
		return 0;
	}

	size_t ConsumerToProducerSize() override
	{
		return 0;
	}
};

struct FrameId 
{
	FrameId(uint16_t seqNo, uint16_t txTime_sec) :mSeqNo(seqNo), mTxTime_sec(txTime_sec) {}
	uint16_t mSeqNo{ 0 };      // unique for a mTxTime_sec value
	uint16_t mTxTime_sec{ 0 }; // 18 hrs without wrap around
};

struct Header
{
	Header(uint16_t seqNo) :mId(seqNo, 0) {}; // just to get things to compile for the moment
	Header(uint16_t seqNo, uint16_t txTime_sec) :mId(seqNo, txTime_sec){};
	Header()
	{
	}

	FrameId mId{0,0};
	uint16_t mDataSize{ 0 };
};

/// <summary>
/// Assuming same endian and padding in structs. In real life would look at 
/// using protocol buffers.
/// </summary>
/// <typeparam name="T"></typeparam>
template <class T> struct Frame {
	std::vector<uint8_t> mBytes;
	Header mHeader;
	T mBody;
	bool mHasBody;

	Frame(const Header& header) :mBytes(sizeof(header)), mHeader(header), mHasBody(false)
	{
		mHeader.mDataSize = 0;
		memcpy(&mBytes[0], &mHeader, sizeof(mHeader));
	}

	Frame(const Header& header, const T& body) :mBytes(sizeof(header) + sizeof(body)), mHeader(header),
		mBody(body), mHasBody(true)
	{
		mHeader.mDataSize = sizeof(body);
		memcpy(&mBytes[0], &mHeader, sizeof(mHeader));
		memcpy(&mBytes[sizeof(mHeader)], &mBody, sizeof(mBody));
	}

	Frame(const std::vector<uint8_t>& data) : mBytes(data)
	{
		memcpy(&mHeader, &(mBytes[0]), sizeof(mHeader));
		mHasBody = mHeader.mDataSize != 0;
		if (mHasBody)
		{
			memcpy(&mBody, &mBytes[sizeof(mHeader)], sizeof(mBody));
		}
	}

	Frame() {};
};