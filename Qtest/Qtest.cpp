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

	TEST_CLASS(QtestUnit)
	{
	private:
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

		Frame<TestBody> GetLastProduced(std::shared_ptr<INetwork>& network, size_t& producedCount)
		{
			Frame<TestBody> lastFrame;
			producedCount = network->ProducerToConsumerSize();
			while (network->ProducerToConsumerSize())
			{
				std::chrono::duration<int, std::milli> timeout(100);
				std::vector<uint8_t> data;
				network->ConsumeDeQ(data, timeout);
				Frame<TestBody> frame(data);
				lastFrame = frame;
			}

			return lastFrame;
		}

	public:
		TEST_METHOD(Producer_allInWondowMessagesDelivered)
		{
			std::shared_ptr<INetwork> network(new IdealNetwork());
			auto producer = std::make_unique<QProducer<TestBody>>(network);
			producer->EnQ(TestBody{ 10 });
			producer->EnQ(TestBody{ 20 });
			producer->EnQ(TestBody{ 30 });
			std::this_thread::sleep_for(std::chrono::duration<int, std::milli>(5));
			producer->Stop();

			size_t deliveryCount = 0;
			auto lastFrame = GetLastProduced(network, deliveryCount);
			Assert::AreEqual(3, static_cast<int>(deliveryCount));
			Assert::AreEqual(3, static_cast<int>(lastFrame.mHeader.mSeqNo));
		}

		TEST_METHOD(Producer_lastPendingResent)
		{
			std::shared_ptr<INetwork> network(new IdealNetwork());
			auto producer = std::make_unique<QProducer<TestBody>>(network);
			producer->EnQ(TestBody{ 10 });
			producer->EnQ(TestBody{ 20 });
			producer->EnQ(TestBody{ 30 });
			std::this_thread::sleep_for(std::chrono::duration<int, std::milli>(200));
			producer->Stop();

			size_t deliveryCount = 0;
			auto lastFrame = GetLastProduced(network, deliveryCount);
			Assert::AreEqual(4, static_cast<int>(deliveryCount));
			Assert::AreEqual(1, static_cast<int>(lastFrame.mHeader.mSeqNo));
		}

		TEST_METHOD(Producer_AckClearPending)
		{
			std::shared_ptr<INetwork> network(new IdealNetwork());
			auto producer = std::make_unique<QProducer<TestBody>>(network);
			producer->EnQ(TestBody{ 10 });
			producer->EnQ(TestBody{ 20 });
			producer->EnQ(TestBody{ 30 });
			std::this_thread::sleep_for(std::chrono::duration<int, std::milli>(10)); // time to process frames
			network->ConsumerEnQ(Frame<TestBody>(Header(2)).mBytes); // frame 2 ackd, 1 and 2 removed from pending
			                                                         // next resend will be frame 3
			std::this_thread::sleep_for(std::chrono::duration<int, std::milli>(250)); // time for a resend
			producer->Stop();

			size_t deliveryCount = 0;
			auto lastFrame = GetLastProduced(network, deliveryCount);
			Assert::AreEqual(4, static_cast<int>(deliveryCount));
			Assert::AreEqual(3, static_cast<int>(lastFrame.mHeader.mSeqNo));
		}

		TEST_METHOD(Producer_OutOfOrderAckIgnored)
		{
			std::shared_ptr<INetwork> network(new IdealNetwork());
			auto producer = std::make_unique<QProducer<TestBody>>(network);
			producer->EnQ(TestBody{ 10 });
			producer->EnQ(TestBody{ 20 });
			producer->EnQ(TestBody{ 30 });
			std::this_thread::sleep_for(std::chrono::duration<int, std::milli>(10)); // time to process frames
			network->ConsumerEnQ(Frame<TestBody>(Header(2)).mBytes); // frame 2 ackd, 1 and 2 removed from pending
																	 // next resend will be frame 3
			network->ConsumerEnQ(Frame<TestBody>(Header(1)).mBytes); // out of order ack
			std::this_thread::sleep_for(std::chrono::duration<int, std::milli>(250)); // time for a resend
			producer->Stop();

			size_t deliveryCount = 0;
			auto lastFrame = GetLastProduced(network, deliveryCount);
			Assert::AreEqual(4, static_cast<int>(deliveryCount));
			Assert::AreEqual(3, static_cast<int>(lastFrame.mHeader.mSeqNo));
		}

		TEST_METHOD(Producer_WindowThreshholdExceeded_onlyWindowSent)
		{
			std::shared_ptr<INetwork> network(new IdealNetwork());
			auto producer = std::make_unique<QProducer<TestBody>>(network);
			for (int i = 1; i <= producer->MaxPendingFrames() + 5; ++i)
			{
				producer->EnQ(TestBody{ i * 10 });
			}
			std::this_thread::sleep_for(std::chrono::duration<int, std::milli>(10));
			producer->Stop();

			size_t deliveryCount = 0;
			auto lastFrame = GetLastProduced(network, deliveryCount);
			Assert::AreEqual(static_cast<int>(producer->MaxPendingFrames()), static_cast<int>(deliveryCount));
			Assert::AreEqual(static_cast<int>(5), static_cast<int>(producer->Size()));
		}

		TEST_METHOD(Producer_FullWindowCleared_remainingFramesSent)
		{
			std::shared_ptr<INetwork> network(new IdealNetwork());
			auto producer = std::make_unique<QProducer<TestBody>>(network);
			auto framesToSend = producer->MaxPendingFrames() + 5;
			for (int i = 1; i <= framesToSend; ++i)
			{
				producer->EnQ(TestBody{ i * 10 });
			}
			std::this_thread::sleep_for(std::chrono::duration<int, std::milli>(10));
			network->ConsumerEnQ(Frame<TestBody>(Header(producer->MaxPendingFrames())).mBytes); //ACK
			std::this_thread::sleep_for(std::chrono::duration<int, std::milli>(100));
			producer->Stop();

			size_t deliveryCount = 0;
			auto lastFrame = GetLastProduced(network, deliveryCount);
			Assert::AreEqual(static_cast<int>(framesToSend), static_cast<int>(deliveryCount));
			Assert::AreEqual(static_cast<int>(0), static_cast<int>(producer->Size()));
		}

		TEST_METHOD(Consumer_InSequenceMessageDelivered)
		{
			std::shared_ptr<INetwork> network(new IdealNetwork());
			auto consumer = std::make_unique<QConsumer<TestBody>>(network);
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
			auto network = std::shared_ptr<INetwork>(new IdealNetwork());
			auto consumer = std::make_unique<QConsumer<TestBody>>(network);

			std::this_thread::sleep_for(std::chrono::duration<int, std::milli>(1000));
			consumer->Stop();

			size_t waitingAckCount;
			auto ackHeader = GetLastAck(network, waitingAckCount);
			Assert::IsTrue(8 <= waitingAckCount && waitingAckCount <= 10);
			Assert::AreEqual(0, (int)ackHeader.mSeqNo);
		}

		TEST_METHOD(Consumer_OutOfOrderDataIsNotDelivered)
		{
			auto network = std::shared_ptr<INetwork>(new IdealNetwork());
			auto consumer = std::make_unique<QConsumer<TestBody>>(network);

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
			auto network = std::shared_ptr<INetwork>(new IdealNetwork());
			auto consumer = std::make_unique<QConsumer<TestBody>>(network);

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
			auto network = std::shared_ptr<INetwork>(new IdealNetwork());
			auto consumer = std::make_unique<QConsumer<TestBody>>(network);

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
			auto network = std::shared_ptr<INetwork>(new IdealNetwork());
			auto consumer = std::make_unique<QConsumer<TestBody>>(network);

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
