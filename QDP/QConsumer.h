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
	std::unordered_map<uint16_t, Frame<T>> mPendingData;
	std::chrono::system_clock::time_point mTimeLastDelivered;
	std::function<void(uint16_t)> mMissedFrameCallBack;

	bool LooksLikeADuplicate(FrameId lastDeliveredId, Frame<T>& frame)
	{
		constexpr uint16_t maxSeq = -1;
		constexpr uint16_t window = maxSeq / 2;
		const uint16_t minExcludedSequence = lastDeliveredId.mSeqNo - window;
		bool isADuplicate = true;
		bool windowWrappedAround = minExcludedSequence > lastDeliveredId.mSeqNo;
		bool frameInExclusionWindow = false;
		const auto newFramesId = frame.mHeader.mId;

		if (windowWrappedAround)
		{
			frameInExclusionWindow = newFramesId.mSeqNo <= lastDeliveredId.mSeqNo || newFramesId.mSeqNo >= minExcludedSequence;
		}
		else
		{
			frameInExclusionWindow = minExcludedSequence <= newFramesId.mSeqNo && newFramesId.mSeqNo <= lastDeliveredId.mSeqNo;
		}

		if (frameInExclusionWindow)
		{
			Log("Consumer - rx out of window frame %d", newFramesId.mSeqNo);
		}
		else if (newFramesId.mTxTime_sec < lastDeliveredId.mTxTime_sec)
		{   
			Log("Consumer - rejected frame with timestamp %d last delievred was %d", newFramesId.mTxTime_sec, lastDeliveredId.mTxTime_sec);
		}
		else if (mPendingData.find(newFramesId.mSeqNo) != mPendingData.end())
		{
			Log("Consumer - rx duplicate pending frame %d", newFramesId.mSeqNo);
		}
		else
		{
			isADuplicate = false;
		}

		return isADuplicate;
	}

	FrameId ProcessPendingFrames(FrameId lastDeliveredID)
	{
		auto nextFrame = mPendingData.find(lastDeliveredID.mSeqNo + 1);
		auto now = std::chrono::system_clock::now();
		if (nextFrame == mPendingData.end())
		{
			constexpr std::chrono::duration<int, std::milli> timeOut(200);
			if ((now - mTimeLastDelivered) > timeOut)
			{
				Log("Consumer - waiting too long for %d, moving on", lastDeliveredID.mSeqNo);
				if (mMissedFrameCallBack != nullptr)
				{
					mMissedFrameCallBack(lastDeliveredID.mSeqNo);
				}
				lastDeliveredID.mSeqNo++;
				mTimeLastDelivered = now;
				nextFrame = mPendingData.find(lastDeliveredID.mSeqNo + 1);
			}
		}


		while (nextFrame != mPendingData.end())
		{
			auto& frameToDeliver = nextFrame->second;
			Log("Consumer - delivering %d", frameToDeliver.mHeader.mId.mSeqNo);
			mConsumerQ.EnQ(frameToDeliver.mBody);
			lastDeliveredID = frameToDeliver.mHeader.mId;
			mPendingData.erase(nextFrame);
			mTimeLastDelivered = now;
			nextFrame = mPendingData.find(lastDeliveredID.mSeqNo + 1);
		}

		std::string frameNumbers;
		for (auto pendingFrame : mPendingData)
		{
			frameNumbers += std::to_string(pendingFrame.first) + ",";
		}
		Log("Consumer - pending frames %s", frameNumbers.c_str());

		return lastDeliveredID;
	}

	void Work()
	{
		FrameId lastDeliveredID{0,0};
		std::chrono::duration<int, std::milli> timeOut(100);
		mTimeLastDelivered = std::chrono::system_clock::now();

		while (!mStop)
		{
			std::vector<uint8_t> data;
			bool hasData = mTransport->ConsumeDeQ(data, timeOut);
			if (hasData)
			{
				Frame<T> frame(data);
				if (frame.mHasBody)
				{
					if (!LooksLikeADuplicate(lastDeliveredID, frame))
					{
						mPendingData.insert({ frame.mHeader.mId.mSeqNo, frame });
					}				
				}
			}
			lastDeliveredID = ProcessPendingFrames(lastDeliveredID);
		}
	}


public:
	QConsumer(std::shared_ptr<INetwork>& transport) :QConsumer(transport, nullptr) {}

	QConsumer(std::shared_ptr<INetwork>& transport, std::function<void(uint16_t)> missedFrameCallback) :
		mConsumerQ("DeliveredQ"), mTransport(transport), mMissedFrameCallBack(missedFrameCallback)
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