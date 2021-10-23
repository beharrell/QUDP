#pragma once
#include <future>
#include <unordered_map>
#include "QNetwork.h"

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