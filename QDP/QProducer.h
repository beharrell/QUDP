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
		std::chrono::duration<int, std::milli> timeOut(0);
		while (!mStop)
		{
			T data;
			bool hasData = mProducerQ.DeQ(data, timeOut);
			if (hasData)
			{
				Frame<T> frame(Header(mTxSequenceNo++), data);
				Log("Prod - sending new frame %d", frame.mHeader.mSeqNo);
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