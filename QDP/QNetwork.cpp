#include "pch.h"
#include "QNetwork.h"
#include <ws2tcpip.h>

std::string getTimestamp() {
	const auto now = std::chrono::system_clock::now();
	return getTimestamp(now);
}

// adapted from from https://gist.github.com/bschlinker/844a88c09dcf7a61f6a8df1e52af7730
std::string getTimestamp(const std::chrono::system_clock::time_point& now) {
	const auto nowAsTimeT = std::chrono::system_clock::to_time_t(now);
	const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
		now.time_since_epoch()) % 1000;
	tm localTime;
	localtime_s(&localTime, &nowAsTimeT);
	std::stringstream nowSs;
	nowSs
		<< std::put_time(&localTime, "%T")
		<< '.' << std::setfill('0') << std::setw(3) << nowMs.count();
	return nowSs.str();
}

UdpNetwork::UdpNetwork()
{
	InitWinSock();

	constexpr int consumerPort = 31415;
	InitAsProducer("127.0.0.1", consumerPort);
	InitAsConsumer(consumerPort);
}

void UdpNetwork::InitWinSock()
{
	WSAData data;
	auto result = WSAStartup(MAKEWORD(2, 2), &data);
	if (result != 0)
	{
		Log("UdpNetwork - failed to init Winsock %d", result);
		exit(1);
	}
}

void  UdpNetwork::InitAsProducer(const std::string& consumerAddress, int consumerPort)
{
	mProducerSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (mProducerSocket == INVALID_SOCKET)
	{
		auto error = WSAGetLastError();
		Log("UdpNetwork - failed to create producer socket, error %d", error);
		exit(1);
	}

	mConsumersAddress.sin_family = AF_INET;
	auto result = inet_pton(AF_INET, consumerAddress.c_str(), &(mConsumersAddress.sin_addr));
	if (result == 0)
	{
		Log("UdpNetwork - %s is not an IP address", consumerAddress.c_str());
		exit(1);
	}
	else if (result == -1)
	{
		auto error = WSAGetLastError();
		Log("UdpNetwork - inet_pton failed with error %d", error);
		exit(1);
	}
	mConsumersAddress.sin_port = htons(consumerPort);
	mIsProducer = true;
}

void  UdpNetwork::InitAsConsumer(int consumerPort)
{
	consumerSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (consumerSocket == INVALID_SOCKET)
	{
		auto error = WSAGetLastError();
		Log("UdpNetwork - failed to create consumer socket, error %d", error);
		exit(1);
	}

	sockaddr_in bindAddress;
	bindAddress.sin_family = AF_INET;
	bindAddress.sin_addr.s_addr = INADDR_ANY;
	bindAddress.sin_port = htons(consumerPort);
	auto result = bind(consumerSocket, reinterpret_cast<SOCKADDR*>(&bindAddress), sizeof(bindAddress));
	if (result == SOCKET_ERROR)
	{
		auto error = WSAGetLastError();
		Log("UdpNetwork - failed to bind consumer socket, error %d", error);
		exit(1);
	}

	mIsConsumer = true;
}

UdpNetwork::UdpNetwork(const std::string& consumerAddress, int consumerPort)
{
	InitWinSock();
	InitAsProducer(consumerAddress, consumerPort);
}

UdpNetwork::UdpNetwork(int consumerPort)
{
	InitWinSock();
	InitAsConsumer(consumerPort);
}

UdpNetwork::~UdpNetwork()
{
	closesocket(mProducerSocket);
	closesocket(consumerSocket);
	WSACleanup();
}

void UdpNetwork::ProducerEnQ(const std::vector<uint8_t>& data)
{
	if (!mIsProducer)
	{
		Log("UdpNetwork - Must be created as a producer");
		exit(1);
	}
	sendto(mProducerSocket, reinterpret_cast<const char*>(&data[0]), data.size(), 0, reinterpret_cast<SOCKADDR*>(&mConsumersAddress), sizeof(mConsumersAddress));
}

bool WaitData(int socket, std::chrono::duration<int, std::milli>& timeOut)
{
	fd_set readset;
	int result;
	struct timeval tv;

	// Initialize the set.
	FD_ZERO(&readset);
	FD_SET(socket, &readset);

	// Initialize time out struct.
	tv.tv_sec = 0;
	tv.tv_usec = timeOut.count() * 1000;

	result = select(socket + 1, &readset, NULL, NULL, &tv);

	// Timeout with no data.
	if (result == 0) {
		return false;
	}

	if (result < 0) {
		auto error = WSAGetLastError();
		Log("UdpNetwork - select failed, error %d", error);
		return false;
	}
	else if (!FD_ISSET(socket, &readset)) {
		return false; // No data!
	}

	return true;
}

bool UdpNetwork::ReceiveData(int socket, std::vector<uint8_t>& data, std::chrono::duration<int, std::milli>& timeOut,
	sockaddr_in* senderAddress, int* senderAddressSize)
{
	bool haveData = false;
	if (WaitData(socket, timeOut))
	{
		constexpr int maxUpdBodySize = 512;
		char buffer[maxUpdBodySize];

		int numBytes = recvfrom(socket, buffer, maxUpdBodySize, 0, reinterpret_cast<SOCKADDR*>(senderAddress), senderAddressSize);
		if (numBytes == SOCKET_ERROR)
		{
			auto error = WSAGetLastError();
			Log("UdpNetwork - recvfrom failed, error %d", error);
		}

		haveData = numBytes > 0;
		if (haveData)
		{
			data.resize(numBytes);
			memcpy(&data[0], &buffer[0], numBytes);
		}
	}

	return haveData;
}

bool UdpNetwork::ProducerDeQ(std::vector<uint8_t>& data, std::chrono::duration<int, std::milli>& timeOut)
{
	if (!mIsProducer)
	{
		Log("UdpNetwork - Must be created as a producer");
		exit(1);
	}

	sockaddr_in from;
	int size = sizeof(from);
	return ReceiveData(mProducerSocket, data, timeOut, &from, &size);
}

void UdpNetwork::ConsumerEnQ(const std::vector<uint8_t>& data)
{
	if (!mIsConsumer)
	{
		Log("UdpNetwork - Must be created as a consumer");
		exit(1);
	}

	if (mHaveProducerAddr)
	{
		sendto(consumerSocket, reinterpret_cast<const char*>(&data[0]), data.size(), 0, reinterpret_cast<SOCKADDR*>(&mProducersAddress), sizeof(mProducersAddress));
	}

}

bool UdpNetwork::ConsumeDeQ(std::vector<uint8_t>& data, std::chrono::duration<int, std::milli>& timeOut)
{
	if (!mIsConsumer)
	{
		Log("UdpNetwork - Must be created as a consumer");
		exit(1);
	}

	int producerAddressSize = sizeof(mProducersAddress);
	auto haveData = ReceiveData(consumerSocket, data, timeOut, &mProducersAddress, &producerAddressSize);
	if (haveData)
	{
		mHaveProducerAddr = true;
	}
	return haveData;
}

