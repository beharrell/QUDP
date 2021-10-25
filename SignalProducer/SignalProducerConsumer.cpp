#include "QProducer.h"
#include "QConsumer.h"

#include <future>
#define _USE_MATH_DEFINES
#include <math.h>

constexpr double uSecInASec = 1000000.0;

struct SignalData
{
	SignalData(double value, double timeStamp) :mValue(value), mTimeStamp_sec(timeStamp){}
	SignalData() : mValue(0), mTimeStamp_sec(0){};

	double mValue;
	double mTimeStamp_sec;
};

using namespace std::chrono;

double GenerateSignal(const microseconds & uSecSinceStart)
{
	double uSecPart = uSecSinceStart.count() % static_cast<int>(uSecInASec);
	double secPart = uSecPart / uSecInASec;
	double signal = sin(secPart * 2.0 * M_PI);

	return signal;
}

int main()
{
	auto producer = std::async(std::launch::async, []()
		{
			auto processStart = system_clock::now();
			duration<int, std::milli> sleepTime_ms(10);
			std::shared_ptr<INetwork> qudp(new UdpNetwork("127.0.0.1", 31415));
			auto qProducer = std::make_unique<QProducer<SignalData>>(qudp);
			while (true)
			{
				std::this_thread::sleep_for(sleepTime_ms); 
				const auto uSecSinceStart = duration_cast<microseconds>(system_clock::now() - processStart);
				const auto signal = GenerateSignal(uSecSinceStart);
				const auto secSinceStart = static_cast<double>(uSecSinceStart.count()) / uSecInASec;
				SignalData data{
				signal, secSinceStart
				};
				qProducer->EnQ(data);
			}
		});

	auto consumer = std::thread([]()
		{
			std::shared_ptr<INetwork> qudp(new UdpNetwork(31415));
			auto qConsumer = std::make_unique<QConsumer<SignalData>>(qudp);
			while (true)
			{
				SignalData data;
				qConsumer->DeQ(data);
				printf("Time stamp %f \t\t Signal %f\n", data.mTimeStamp_sec, data.mValue);
			}
		});

	consumer.join();
}

