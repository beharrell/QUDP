#pragma once

#include <future>
#include <list>
#include <vector>
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
			auto res = mConsumerSignal.wait_for(lock, timeOut);
			if (res == std::cv_status::timeout)
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

class Network
{
private:
	BlockingQ<std::vector<uint8_t>> mProdToConsumer;
	BlockingQ<std::vector<uint8_t>> mConsumerToProducer;

public:
	void ProducerEnQ(std::vector<uint8_t>& data);
	bool ProducerDeQ(std::vector<uint8_t>& data, std::chrono::duration<int, std::milli> & timeOut);
	void ConsumerEnQ(std::vector<uint8_t>& data);
	bool ConsumeDeQ(std::vector<uint8_t>& data, std::chrono::duration<int, std::milli>& timeOut);
	
	size_t Size()
	{
		return mProdToConsumer.Size() + mConsumerToProducer.Size();
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

template <class T> struct Frame {
	std::vector<uint8_t> mBytes;

	Frame(T& header) :mBytes(sizeof(header))
	{
		header.mDataSize = 0;
		memcpy(&mBytes[0], &header, sizeof(header));
	}

	Frame(Header header, T body) :mBytes(sizeof(header) + sizeof(body))
	{
		header.mDataSize = sizeof(body);
		memcpy(&mBytes[0], &header, sizeof(header));
		memcpy(&mBytes[sizeof(header)], &body, sizeof(body));
	}
};

template <class T> class Producer
{
private:
	uint16_t mTxSequenceNo{ 0 };
	BlockingQ<T> mProducerQ;
	std::shared_ptr<Network> mTransport;
	std::future<void> mWorker;
	bool mStop{ false };

	void Work()
	{
		std::chrono::duration<int, std::milli> timeOut(100);
		while (!mStop)
		{
			T data;
			if (!mProducerQ.DeQ(data, timeOut))
			{
				continue;
			}

			Frame<T> frame(Header(mTxSequenceNo++), data);
			mTransport->ProducerEnQ(frame.mBytes);
		}
	}
public:
	Producer(std::shared_ptr<Network>& transport) :mTransport(transport)
	{
		mWorker = std::async(std::launch::async, [&]() {Work(); });
	}

	void Stop()
	{
		mStop = true;
		mWorker.get();
	}

	void EnQ(T& data)
	{
		mProducerQ.EnQ(data);
	}

	size_t Size()
	{
		return mProducerQ.Size();
	}
};

template <class T> class Consumer
{
private:
	BlockingQ<T> mConsumerQ;
	std::shared_ptr<Network> mTransport;
	std::future<void> mWorker;
	bool mStop{ false };

	void Work()
	{
		std::chrono::duration<int, std::milli> timeOut(100);
		while (!mStop)
		{
			std::vector<uint8_t> data;
			if (!mTransport->ConsumeDeQ(data, timeOut))
			{
				continue;
			}
			Header header;
			memcpy(&header, &(data[0]), sizeof(header));
			T body;
			memcpy(&body, &data[sizeof(header)], sizeof(body));

			mConsumerQ.EnQ(body);
		}
	}
public:
	Consumer(std::shared_ptr<Network>& transport) :mTransport(transport)
	{
		mWorker = std::async(std::launch::async, [&]() {Work(); });
	}

	void Stop()
	{
		mStop = true;
		mWorker.get();
	}

	void DeQ(T &data)
	{
		mConsumerQ.DeQ(data);
	}

	size_t Size()
	{
		return mConsumerQ.Size();
	}
};

template <class T> class ReliableQ
{
private:
	std::unique_ptr<Consumer<T>> mConsumer;
	std::unique_ptr<Producer<T>> mProducer;
	std::shared_ptr<Network> mTransport;
public:
	ReliableQ()
	{
		mTransport = std::make_shared<Network>();
		mConsumer = std::make_unique<Consumer<T>>(mTransport);
		mProducer = std::make_unique<Producer<T>>(mTransport);
	};

	~ReliableQ()
	{
		mConsumer->Stop();
		mProducer->Stop();
	}

	ReliableQ(const ReliableQ&) = delete;

	void EnQ(T& data)
	{
		mProducer->EnQ(data);
	}

	void DeQ(T& data)
	{
		mConsumer->DeQ(data);
	}

	size_t Size()
	{
		// race hazard here but it suits its purpose 
		return mProducer->Size() + mTransport->Size() + mConsumer->Size();
	}
};

