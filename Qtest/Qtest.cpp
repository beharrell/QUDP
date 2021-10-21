#include "pch.h"
#include "CppUnitTest.h"
#include "Qudp.h"

#include <future>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Qtest
{
	struct TestBody
	{
		TestBody(int value) :mValue(value) {}
		TestBody() {};

		int mValue{ 0 };
	};

	TEST_CLASS(Qtest)
	{
	public:
		
		TEST_METHOD(IdealNetworkStress)
		{
			int count = 1000;
			auto queue = std::make_shared<ReliableQ<TestBody>>();
			auto producer = std::async(std::launch::async, [&](std::shared_ptr<ReliableQ<TestBody>> p)
				{
					for (int i = 0; i < count; ++i)
					{
						TestBody d(i);
						p->EnQ(d);
					}
				}, queue);

			auto consumer = std::thread([&](std::shared_ptr<ReliableQ<TestBody>> p)
				{
					int expected = 0;
					while (expected < count)
					{
						TestBody d;
						p->DeQ(d);
						Assert::AreEqual(expected, d.mValue);
						expected = d.mValue + 1;
					}

				}, queue);

			consumer.join();
		}

		Header GetLastAck(std::shared_ptr<INetwork>& network, size_t& waitingAckCount)
		{
			Header header;
			waitingAckCount = network->ConsumerToProducerSize();
			while (network->ConsumerToProducerSize())
			{
				std::chrono::duration<int, std::milli> timeout(100);
				std::vector<uint8_t> data;
				network->ProducerDeQ(data, timeout);
				Frame<TestBody> frame(data);
				header = frame.mHeader;
			}

			return header;
		}

		TEST_METHOD(Consumer_InSequenceMessageDelivered)
		{
			std::shared_ptr<INetwork> network(new Network());
			auto consumer = std::make_unique<Consumer<TestBody>>(network);
			int expected = 10;
			uint16_t seqNo = 1;
			Frame frame(Header(seqNo), TestBody{ expected });

			network->ProducerEnQ(frame.mBytes);
			std::this_thread::sleep_for(std::chrono::duration<int, std::milli>(100));
			TestBody rcvddata;
			consumer->DeQ(rcvddata);

			Assert::AreEqual(expected, rcvddata.mValue);
			consumer->Stop();
			size_t waitingAckCount;
			auto ackHeader = GetLastAck(network, waitingAckCount);
			Assert::AreEqual((int)seqNo, (int)ackHeader.mSeqNo);
		}

		TEST_METHOD(Consumer_SendsAcksWhenNoDataRx)
		{
			auto network = std::shared_ptr<INetwork>(new Network());
			auto consumer = std::make_unique<Consumer<TestBody>>(network);

			std::this_thread::sleep_for(std::chrono::duration<int, std::milli>(1000));
			consumer->Stop();

			size_t waitingAckCount;
			auto ackHeader = GetLastAck(network, waitingAckCount);
			Assert::IsTrue(8 <= waitingAckCount && waitingAckCount <= 10);
			Assert::AreEqual(0, (int)ackHeader.mSeqNo);
		}

		TEST_METHOD(Consumer_OutOfOrderDataIsNotDelivered)
		{
			auto network = std::shared_ptr<INetwork>(new Network());
			auto consumer = std::make_unique<Consumer<TestBody>>(network);

			network->ProducerEnQ(Frame(Header(2), TestBody{ 20 }).mBytes);
			network->ProducerEnQ(Frame(Header(3), TestBody{ 30 }).mBytes);
			std::this_thread::sleep_for(std::chrono::duration<int, std::milli>(500));
			
			Assert::AreEqual(0, (int)consumer->Size());
			consumer->Stop();
			size_t waitingAckCount;
			auto ackHeader = GetLastAck(network, waitingAckCount);
			Assert::AreEqual((int)0, (int)ackHeader.mSeqNo); // consumers initial seq num
		}

		TEST_METHOD(Consumer_OutOfOrderDataIsDeliveredWhenMissingDataArrives)
		{
			auto network = std::shared_ptr<INetwork>(new Network());
			auto consumer = std::make_unique<Consumer<TestBody>>(network);

			network->ProducerEnQ(Frame(Header(2), TestBody{ 20 }).mBytes);
			network->ProducerEnQ(Frame(Header(3), TestBody{ 30 }).mBytes);
			std::this_thread::sleep_for(std::chrono::duration<int, std::milli>(500));
			
			Assert::AreEqual(0, (int)consumer->Size());
			size_t waitingAckCount;
			auto ackHeader = GetLastAck(network, waitingAckCount);
			Assert::AreEqual((int)0, (int)ackHeader.mSeqNo);

			network->ProducerEnQ(Frame(Header(1), TestBody{ 10 }).mBytes);
			std::this_thread::sleep_for(std::chrono::duration<int, std::milli>(500));

			consumer->Stop();
			
			Assert::AreEqual(3, (int)consumer->Size());
			ackHeader = GetLastAck(network, waitingAckCount);
			Assert::AreEqual((int)3, (int)ackHeader.mSeqNo);
			for (int i = 1; i <= 3; ++i)
			{
				TestBody rcvddata;
				consumer->DeQ(rcvddata);
				Assert::AreEqual(i * 10, rcvddata.mValue);
			}
		}

		TEST_METHOD(Consumer_DuplicatePendingFrameIgnored)
		{
			auto network = std::shared_ptr<INetwork>(new Network());
			auto consumer = std::make_unique<Consumer<TestBody>>(network);

			network->ProducerEnQ(Frame(Header(2), TestBody{ 20 }).mBytes);
			network->ProducerEnQ(Frame(Header(3), TestBody{ 30 }).mBytes);
			network->ProducerEnQ(Frame(Header(2), TestBody{ 20 }).mBytes); // duplicate
			std::this_thread::sleep_for(std::chrono::duration<int, std::milli>(500));

			network->ProducerEnQ(Frame(Header(1), TestBody{ 10 }).mBytes); // allows pending frames to be delivered
			std::this_thread::sleep_for(std::chrono::duration<int, std::milli>(500));

			consumer->Stop();

			Assert::AreEqual(3, (int)consumer->Size());
			size_t waitingAckCount;
			auto ackHeader = GetLastAck(network, waitingAckCount);
			Assert::AreEqual((int)3, (int)ackHeader.mSeqNo);
			for (int i = 1; i <= 3; ++i)
			{
				TestBody rcvddata;
				consumer->DeQ(rcvddata);
				Assert::AreEqual(i * 10, rcvddata.mValue);
			}
		}

		TEST_METHOD(Consumer_DuplicateDeliveredFrameIgnored)
		{
			auto network = std::shared_ptr<INetwork>(new Network());
			auto consumer = std::make_unique<Consumer<TestBody>>(network);

			network->ProducerEnQ(Frame(Header(1), TestBody{ 10 }).mBytes);
			network->ProducerEnQ(Frame(Header(2), TestBody{ 20 }).mBytes);
			network->ProducerEnQ(Frame(Header(3), TestBody{ 30 }).mBytes); 
			std::this_thread::sleep_for(std::chrono::duration<int, std::milli>(500));

			Assert::AreEqual(3, (int)consumer->Size());
			size_t waitingAckCount;
			auto ackHeader = GetLastAck(network, waitingAckCount);
			Assert::AreEqual((int)3, (int)ackHeader.mSeqNo);
			for (int i = 1; i <= 3; ++i)
			{
				TestBody rcvddata;
				consumer->DeQ(rcvddata);
				Assert::AreEqual(i * 10, rcvddata.mValue);
			}

			network->ProducerEnQ(Frame(Header(2), TestBody{ 20 }).mBytes); // duplicate of delivered frame
			std::this_thread::sleep_for(std::chrono::duration<int, std::milli>(500));
			Assert::AreEqual(0, (int)consumer->Size());
			consumer->Stop();
			ackHeader = GetLastAck(network, waitingAckCount);
			Assert::AreEqual((int)3, (int)ackHeader.mSeqNo);
		}
	};
}
