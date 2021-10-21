#include "pch.h"
#include "Qudp.h"


void Network::ProducerEnQ(const std::vector<uint8_t>& data)
{
	mProdToConsumer.EnQ(data);
}

bool Network::ProducerDeQ(std::vector<uint8_t>& data, std::chrono::duration<int, std::milli>& timeOut)
{
	return mConsumerToProducer.DeQ(data, timeOut);
}

void Network::ConsumerEnQ(std::vector<uint8_t>& data)
{
	mConsumerToProducer.EnQ(data);
}

bool Network::ConsumeDeQ(std::vector<uint8_t>& data, std::chrono::duration<int, std::milli>& timeOut)
{
	return mProdToConsumer.DeQ(data, timeOut);
}