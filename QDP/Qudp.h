#pragma once

#include <list>
#include <vector>
#include <condition_variable>

template <class T> class ReliableQ
{
private:
	struct Header
	{
		Header(uint16_t seqNo) :mSeqNo(seqNo) {};
		Header()
		{
		}

		uint16_t mSeqNo{ 0 };
		uint16_t mDataSize{ 0 };
	};
	template <class T> struct  Frame {
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

	std::list<Frame<T>> q;
	const size_t qSize{ 50 };

	std::mutex mux;
	std::condition_variable producerSignal;
	std::condition_variable consumerSignal;
	uint16_t mTxSequenceNo{ 0 };
	uint16_t mRxSequenceNo{ 0 };

public:
	ReliableQ()
	{
	};
	ReliableQ(const ReliableQ&) = delete;

	void EnQ(T& data)
	{
		std::unique_lock<std::mutex> lock(mux);
		q.emplace_back(Frame<T>(Header(mTxSequenceNo++), data));
		if (q.size() >= qSize)
		{
			producerSignal.wait(lock, [&] {return q.size() < qSize; });
		}
		else if (q.size() == 1)
		{
			consumerSignal.notify_one();
		}
	}



	T DeQ()
	{
		std::unique_lock<std::mutex> lock(mux);
		if (q.size() == 0)
		{
			consumerSignal.wait(lock);
		}
		bool qAtMax = q.size() >= qSize;

		Frame<T> frame = q.front();
		Header header;
		memcpy(&header, &(frame.mBytes[0]), sizeof(header));
		T body;
		memcpy(&body, &frame.mBytes[sizeof(header)], sizeof(body));

		q.pop_front();
		if (qAtMax)
		{
			producerSignal.notify_one();
		}

		return body;
	}

	size_t Size()
	{
		std::lock_guard<std::mutex> lock(mux);
		return q.size();
	}
};

