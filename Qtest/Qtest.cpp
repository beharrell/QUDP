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
		
		TEST_METHOD(Pack_unpack_expect_ok)
		{
			auto myShared = std::make_shared<ReliableQ<TestBody>>();
			auto producer = std::async(std::launch::async, [&](std::shared_ptr<ReliableQ<TestBody>> p)
				{
					for (int i = 0; i < 100; ++i)
					{
						TestBody d(i);
						p->EnQ(d);
					}
				}, myShared);

			auto consumer = std::thread([&](std::shared_ptr<ReliableQ<TestBody>> p)
				{
					int expected = 0;
					while (producer.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready || p->Size() != 0)
					{
						TestBody&& d = p->DeQ();
						Assert::AreEqual(expected, d.mValue);
						expected = d.mValue + 1;
					}
				}, myShared);

			consumer.join();
		}
	};
}
