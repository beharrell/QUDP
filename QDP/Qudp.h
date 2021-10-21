#pragma once

#include <future>
#include <list>
#include <vector>
#include <unordered_map>
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

class INetwork
{
public:
	virtual ~INetwork() {}
	virtual void ProducerEnQ(const std::vector<uint8_t>& data) = 0;
	virtual bool ProducerDeQ(std::vector<uint8_t>& data, std::chrono::duration<int, std::milli>& timeOut) = 0;
	virtual void ConsumerEnQ(std::vector<uint8_t>& data) = 0;
	virtual bool ConsumeDeQ(std::vector<uint8_t>& data, std::chrono::duration<int, std::milli>& timeOut) = 0;
	virtual size_t ConsumerToProducerSize() = 0;
};

class Network : public INetwork
{
private:
	BlockingQ<std::vector<uint8_t>> mProdToConsumer;
	BlockingQ<std::vector<uint8_t>> mConsumerToProducer;

public:
	void ProducerEnQ(const std::vector<uint8_t>& data) override;
	bool ProducerDeQ(std::vector<uint8_t>& data, std::chrono::duration<int, std::milli>& timeOut) override;
	void ConsumerEnQ(std::vector<uint8_t>& data) override;
	bool ConsumeDeQ(std::vector<uint8_t>& data, std::chrono::duration<int, std::milli>& timeOut) override;

	size_t Size()
	{
		return mProdToConsumer.Size() + mConsumerToProducer.Size();
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

	Frame(const std::vector<uint8_t>& data): mBytes(data)
	{
		memcpy(&mHeader, &(mBytes[0]), sizeof(mHeader));
		mHasBody = mHeader.mDataSize != 0;
		if (mHasBody)
		{
			memcpy(&mBody, &mBytes[sizeof(mHeader)], sizeof(mBody));
		}
	}
};

template <class T> class Producer
{
private:
	uint16_t mTxSequenceNo{ 1 };
	BlockingQ<T> mProducerQ;
	std::shared_ptr<INetwork> mTransport;
	std::future<void> mWorker;
	bool mStop{ false };

	void Work()
	{
		std::chrono::duration<int, std::milli> timeOut(100);
		std::chrono::duration<int, std::milli> ackTimeOut(0);
		while (!mStop)
		{
			T data;
			bool hasData = mProducerQ.DeQ(data, timeOut);
			if (hasData)
			{
				Frame<T> frame(Header(mTxSequenceNo++), data);
				mTransport->ProducerEnQ(frame.mBytes);
			}

			std::vector<uint8_t> ackData;
			while (mTransport->ProducerDeQ(ackData, ackTimeOut))
			{
				// just throw away acks for the moment.
			}
		}
	}
public:
	Producer(std::shared_ptr<INetwork>& transport) :mTransport(transport)
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
	std::shared_ptr<INetwork> mTransport;
	std::future<void> mWorker;
	bool mStop{ false };
	std::unordered_map<uint16_t, Frame<T>> pendingData;

	uint16_t ProcessFrame(uint16_t lastOrderedSeqenceNumber, Frame<T>& frame)
	{
		// ignoring seq num wrap around for the moment
		if (frame.mHeader.mSeqNo <= lastOrderedSeqenceNumber || 
			pendingData.find(frame.mHeader.mSeqNo) != pendingData.end())
		{
			// duplicate frame
			return lastOrderedSeqenceNumber;
		}

		pendingData.insert({ frame.mHeader.mSeqNo, frame });
		auto nextFrame = pendingData.find(lastOrderedSeqenceNumber + 1);
		while (nextFrame != pendingData.end())
		{
			mConsumerQ.EnQ(nextFrame->second.mBody);
			++lastOrderedSeqenceNumber;
			nextFrame = pendingData.find(lastOrderedSeqenceNumber + 1);
		}

		return lastOrderedSeqenceNumber;
	}

	void Work()
	{
		uint16_t lastOrderedSeqenceNumber = 0; 
		std::chrono::duration<int, std::milli> timeOut(100);

		while (!mStop)
		{
			std::vector<uint8_t> data;
			bool hasData = mTransport->ConsumeDeQ(data, timeOut);
			if (hasData)
			{
				Frame<T> frame(data);
				if (frame.mHasBody)
				{
					lastOrderedSeqenceNumber = ProcessFrame(lastOrderedSeqenceNumber, frame);
				}
			}

			if (!mStop)
			{
				// dumb down the ack rate later
				Header ackHeader(lastOrderedSeqenceNumber);
				Frame<T> ackFrame(ackHeader);
				mTransport->ConsumerEnQ(ackFrame.mBytes);
			}
		}
	}


public:
	Consumer(std::shared_ptr<INetwork>& transport) :mTransport(transport)
	{
		mWorker = std::async(std::launch::async, [&]() {Work(); });
	}

	void Stop()
	{
		mStop = true;
		mWorker.get();
	}

	void DeQ(T& data)
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
	std::shared_ptr<INetwork> mTransport;
public:
	ReliableQ()
	{
		mTransport = std::shared_ptr<INetwork>(new Network());
		mConsumer = std::make_unique<Consumer<T>>(mTransport);
		mProducer = std::make_unique<Producer<T>>(mTransport);
	};

	ReliableQ(std::shared_ptr<INetwork> network) : mTransport(network)
	{
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

