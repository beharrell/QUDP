#include "pch.h"
#include "CppUnitTest.h"
#include "Qudp.h"


using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Qtest
{
	struct TestBody
	{
		TestBody(int value) :mValue(value) {}
		TestBody() {};

		int mValue{ 0 };
	};

	class ImperfectNetwork : public IdealNetwork
	{
		std::vector<uint8_t> mCopyOfProducerData;
		std::vector<uint8_t> mCopyOfConsumerData;
		float mPrbLost{ 0 };
		float mChanceOfADuplicate{ 0 };
		float mPrbDelay{ 0 };

		bool TakeAChance(float probability)
		{
			float prob = (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX)) * 100.0f;
			return prob < probability;
		}

		void TryToQ(const std::string & direction, const std::vector<uint8_t>& data, std::vector<uint8_t> dataCopy,
			std::function<void(const std::vector<uint8_t>&)> sendFunction)
		{
			bool sendData = true;
			Frame<TestBody> frame(data);

			if (TakeAChance(mChanceOfADuplicate))
			{
				if (dataCopy.size() != 0)
				{
					sendFunction(dataCopy);
				}
				dataCopy = data;
				Log("%s Duplicating %d", direction.c_str(), frame.mHeader.mSeqNo);
			}
			else if (TakeAChance(mPrbDelay))
			{
				if (dataCopy.size() != 0)
				{
					sendFunction(dataCopy);
				}
				dataCopy = data;
				sendData = false;
				Log("%s Delaying %d", direction.c_str(), frame.mHeader.mSeqNo);
			}
			else if (TakeAChance(mPrbLost))
			{
				sendData = false;
				Log("%s Lost %d", direction.c_str(), frame.mHeader.mSeqNo);
			}

			if (sendData)
			{
				sendFunction(data);
			}
		}

	public:


		ImperfectNetwork(float prbLost, float prbDuplicate, float prbDelay) :
			mPrbLost{ prbLost }, mChanceOfADuplicate{ prbDuplicate }, mPrbDelay{ prbDelay }
		{
			std::srand(std::time(nullptr));
		}



		void ProducerEnQ(const std::vector<uint8_t>& data) override
		{
			TryToQ("**Prod Data Error**",data, mCopyOfProducerData,
				[&](const std::vector<uint8_t>& data) {IdealNetwork::ProducerEnQ(data); });

		}

		void ConsumerEnQ(const std::vector<uint8_t>& data) override
		{
			TryToQ("**Consumer Ack Error**",data, mCopyOfConsumerData,
				[&](const std::vector<uint8_t>& data) {IdealNetwork::ConsumerEnQ(data); });
		}
	};


	TEST_CLASS(QtestStress)
	{
	private:
		void StressTestNetwork(std::shared_ptr<INetwork> network, uint16_t numberOfFrames)
		{
			auto queue = std::make_shared<ReliableQ<TestBody>>(network);
			auto producer = std::async(std::launch::async, [&](std::shared_ptr<ReliableQ<TestBody>> p)
				{
					for (uint16_t i = 0; i < numberOfFrames; ++i)
					{
						TestBody d(i);
						p->EnQ(d);
					}
				}, queue);

			auto consumer = std::thread([&](std::shared_ptr<ReliableQ<TestBody>> p)
				{
					int expected = 0;
					while (expected < numberOfFrames)
					{
						TestBody d;
						p->DeQ(d);
						Assert::AreEqual(expected, d.mValue);
						expected = d.mValue + 1;
					}

				}, queue);

			consumer.join();
		}
	public:	
		TEST_METHOD(StressIdealNetwork)
		{
			auto network = std::make_shared<IdealNetwork>();
			StressTestNetwork(network, 200);
		}

		TEST_METHOD(StressDuplicatingNetwork)
		{
			auto network = std::make_shared<ImperfectNetwork>(0.0f, 50.0f, 0.0f);
			StressTestNetwork(network, 200);
		}

		TEST_METHOD(StressReorderingNetwork)
		{
			auto network = std::make_shared<ImperfectNetwork>(0.0f, 0.0f, 50.0f);
			StressTestNetwork(network, 200);
		}

		TEST_METHOD(StressLosyNetwork)
		{
			auto network = std::make_shared<ImperfectNetwork>(50.0f, 0.0f, 0.0f);
			StressTestNetwork(network, 200);
		}

		TEST_METHOD(StressReallyBadNetwork)
		{
			auto network = std::make_shared<ImperfectNetwork>(50.0f/3.0f, 50.0f / 3.0f, 50.0f / 3.0f);
			StressTestNetwork(network, /*60000*/200);
		}

		TEST_METHOD(StressUdpLoopBackNetwork)
		{
			auto network = std::make_shared<UdpNetwork>();
			StressTestNetwork(network, 200);
		}

		
	};
}