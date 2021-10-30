#pragma once
#include <future>
#include "QNetwork.h"

template <class T> class QProducer
{
private:
	uint16_t mTxSequenceNo{ 1 };
	BlockingQ<T> mProducerQ;
	std::shared_ptr<INetwork> mTransport;
	std::future<void> mWorker;
	bool mStop{ false };

	void Work()
	{
		using namespace std::chrono;
		duration<int, std::milli> timeOut(50);
		const auto startTime = system_clock::now();
		while (!mStop)
		{
			T data;
			bool hasData = mProducerQ.DeQ(data, timeOut);
			if (hasData)
			{
				auto secSinceStart = duration_cast<seconds>(system_clock::now() - startTime);
				Frame<T> frame(Header(mTxSequenceNo++, static_cast<uint16_t>(secSinceStart.count())), data);
				Log("Prod - sending new frame (%d,%d)", frame.mHeader.mId.mTxTime_sec, frame.mHeader.mId.mSeqNo);
				mTransport->ProducerEnQ(frame.mBytes);
			}
		}
	}

public:
	QProducer(std::shared_ptr<INetwork>& transport) :mProducerQ("ToSendQ"), mTransport(transport)
	{
		mWorker = std::async(std::launch::async, [&]() {Work(); });
	}

	void Stop()
	{
		mStop = true;
		mWorker.get();
	}

	void EnQ(const T& data)
	{
		mProducerQ.EnQ(data);
	}

	size_t Size()
	{
		return mProducerQ.Size();
	}
};