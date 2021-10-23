#pragma once
#include <vector>
#include <list>
#include <condition_variable>

template <class T> class BlockingQ
{
private:
	std::list<T> q;

	std::mutex mMux;
	std::condition_variable mConsumerSignal;
public:
	void EnQ(T data)
	{
		std::unique_lock<std::mutex> lock(mMux);
		q.emplace_back(data);
		if (q.size() == 1)
		{
			mConsumerSignal.notify_one();
		}
	}

	bool DeQ(T& data, std::chrono::duration<int, std::milli>& timeOut)
	{
		std::unique_lock<std::mutex> lock(mMux);
		if (q.size() == 0)
		{
			auto hasData = mConsumerSignal.wait_for(lock, timeOut, [&] {return q.size() > 0; });
			if (!hasData)
			{
				return false;
			}
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

struct Header
{
	Header(uint16_t seqNo) :mSeqNo(seqNo) {};
	Header()
	{
	}

	uint16_t mSeqNo{ 0 };
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