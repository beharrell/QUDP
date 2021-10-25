#pragma once


#include "QProducer.h"
#include "QConsumer.h"


template <class T> class ReliableQ
{
private:
	std::unique_ptr<QConsumer<T>> mConsumer;
	std::unique_ptr<QProducer<T>> mProducer;
	std::shared_ptr<INetwork> mTransport;
public:

	ReliableQ(std::shared_ptr<INetwork> network) : mTransport(network)
	{
		mConsumer = std::make_unique<QConsumer<T>>(mTransport);
		mProducer = std::make_unique<QProducer<T>>(mTransport);
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

