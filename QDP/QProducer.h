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
	std::list<Frame<T>> mPendingFrames;
	std::chrono::time_point<std::chrono::system_clock> mTimePendingFrameLastSent;
	const uint16_t mMaxPendingFrames = 8;

	void ClearPendingFrames(Frame<T>& ackFrame)
	{
		auto frame = std::find_if(mPendingFrames.begin(), mPendingFrames.end(), [&](Frame<T>& frame)
			{return frame.mHeader.mSeqNo == ackFrame.mHeader.mSeqNo; }
		);
		if (frame != mPendingFrames.end())
		{
			Log("Prod - ack %d clearing pending from %d to %d",
				ackFrame.mHeader.mSeqNo,
				mPendingFrames.begin()->mHeader.mSeqNo,
				frame->mHeader.mSeqNo);
			frame++; // erase has a (] range
			mPendingFrames.erase(mPendingFrames.begin(), frame);
			mTimePendingFrameLastSent = std::chrono::system_clock::now();

			if (mPendingFrames.size() > 0)
			{
				Log("Prod - next pending frame is %d",
					mPendingFrames.begin()->mHeader.mSeqNo);
			}
		}
		else
		{
			Log("Prod - ack %d is old", ackFrame.mHeader.mSeqNo);
		}
	}

	std::chrono::duration<int, std::milli> ResendPendingFrameIfNeeded()
	{
		std::chrono::duration<int, std::milli> resendFrequency(100);
		std::chrono::duration<int, std::milli> timeTillNextSend = resendFrequency;

		if (mPendingFrames.size() != 0)
		{
			auto now = std::chrono::system_clock::now();
			auto timeSinceResend = std::chrono::duration_cast<std::chrono::milliseconds>(now - mTimePendingFrameLastSent);
			if (timeSinceResend >= resendFrequency)
			{
				Log("Prod - resending frame %d",
					mPendingFrames.front().mHeader.mSeqNo);
				mTransport->ProducerEnQ(mPendingFrames.front().mBytes);
				mTimePendingFrameLastSent = now;
			}
			else
			{
				timeTillNextSend = resendFrequency - timeSinceResend;
			}
		}

		return timeTillNextSend;
	}


	void Work()
	{
		//std::chrono::duration<int, std::milli> deQDataTimeOut(100);
		std::chrono::duration<int, std::milli> deQAckTimeOut(0);
		while (!mStop)
		{
			auto timeTillNextResend = ResendPendingFrameIfNeeded();
			if (mPendingFrames.size() >= mMaxPendingFrames)
			{
				Log("Prod - Pending q full, sleeping %dms", timeTillNextResend.count());
				std::this_thread::sleep_for(timeTillNextResend);
			}
			else
			{
				T data;
				bool hasData = mProducerQ.DeQ(data, timeTillNextResend);
				if (hasData)
				{
					Frame<T> frame(Header(mTxSequenceNo++), data);
					Log("Prod - sending new frame %d", frame.mHeader.mSeqNo);
					mTransport->ProducerEnQ(frame.mBytes);
					mPendingFrames.emplace_back(frame);
					Log("Prod - pending q frames %d to %d", 
						mPendingFrames.front().mHeader.mSeqNo,
						mPendingFrames.back().mHeader.mSeqNo);
				}
			}

			std::vector<uint8_t> ackData;
			while (mTransport->ProducerDeQ(ackData, deQAckTimeOut))
			{
				Frame<T> ackFrame(ackData);
				ClearPendingFrames(ackFrame);
			}
		}
	}
public:
	QProducer(std::shared_ptr<INetwork>& transport) :mProducerQ("ToSendQ"), mTransport(transport)
	{
		mTimePendingFrameLastSent = std::chrono::system_clock::now();
		mWorker = std::async(std::launch::async, [&]() {Work(); });
	}

	uint16_t MaxPendingFrames() { return mMaxPendingFrames; }

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