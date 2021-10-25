#pragma once
#include <future>
#include <unordered_map>
#include "QNetwork.h"

template <class T> class QConsumer
{
private:
	BlockingQ<T> mConsumerQ;
	std::shared_ptr<INetwork> mTransport;
	std::future<void> mWorker;
	bool mStop{ false };
	std::unordered_map<uint16_t, Frame<T>> pendingData;

	bool LooksLikeADuplicate(uint16_t lastOrderedSeqenceNumber, Frame<T>& frame)
	{
		constexpr uint16_t maxSeq = -1;
		constexpr uint16_t window = maxSeq / 2;
		const uint16_t minExcludedSequence = lastOrderedSeqenceNumber - window;
		bool isADuplicate = false;
		bool windowWrappedAround = minExcludedSequence > lastOrderedSeqenceNumber;
		bool frameInExclusionWindow = false;
		if (windowWrappedAround)
		{
			frameInExclusionWindow = frame.mHeader.mSeqNo <= lastOrderedSeqenceNumber || frame.mHeader.mSeqNo >= minExcludedSequence;
		}
		else
		{
			frameInExclusionWindow = minExcludedSequence <= frame.mHeader.mSeqNo && frame.mHeader.mSeqNo <= lastOrderedSeqenceNumber;
		}

		if (frameInExclusionWindow)
		{
			Log("Consumer - rx out of window frame %d", frame.mHeader.mSeqNo);
			isADuplicate = true;
		}
		if (pendingData.find(frame.mHeader.mSeqNo) != pendingData.end())
		{
			Log("Consumer - rx duplicate pending frame %d", frame.mHeader.mSeqNo);
			isADuplicate = true;
		}

		return isADuplicate;
	}

	uint16_t ProcessFrame(uint16_t lastOrderedSeqenceNumber, Frame<T>& frame)
	{
		if (LooksLikeADuplicate(lastOrderedSeqenceNumber, frame))
		{
			return lastOrderedSeqenceNumber;
		}

		pendingData.insert({ frame.mHeader.mSeqNo, frame });
		auto nextFrame = pendingData.find(lastOrderedSeqenceNumber + 1);
		while (nextFrame != pendingData.end())
		{
			Log("Consumer - delivering %d", nextFrame->second.mHeader.mSeqNo);
			mConsumerQ.EnQ(nextFrame->second.mBody);
			pendingData.erase(nextFrame);
			++lastOrderedSeqenceNumber;
			nextFrame = pendingData.find(lastOrderedSeqenceNumber + 1);
		}

		std::string frameNumbers;
		for (auto pendingFrame : pendingData)
		{
			frameNumbers += std::to_string(pendingFrame.first) + ",";
		}
		Log("Consumer - pending frames %s", frameNumbers.c_str());

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
				Log("Consumer - acknowledging %d", lastOrderedSeqenceNumber);
				mTransport->ConsumerEnQ(ackFrame.mBytes);
			}
		}
	}


public:
	QConsumer(std::shared_ptr<INetwork>& transport) :mConsumerQ("DeliveredQ"), mTransport(transport)
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