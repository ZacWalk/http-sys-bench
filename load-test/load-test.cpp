// load-test.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <vector>
#include <mutex>  
#include <thread>
#include <vector>
#include <atomic>

#define WIN32_LEAN_AND_MEAN 1
#include <conio.h>
#include <windows.h>
#include <winhttp.h>


#pragma comment(lib, "winhttp.lib")


double now()
{
	LARGE_INTEGER tps = { 0 };
	QueryPerformanceFrequency(&tps);

	LARGE_INTEGER pc = { 0 };
	QueryPerformanceCounter(&pc);
	return static_cast<double>(pc.QuadPart) / static_cast<double>(tps.QuadPart);
}


std::string send_get_request(const wchar_t *server, const int port, const wchar_t* path)
{
	DWORD dwSize = 0;
	DWORD dwDownloaded = 0;
	void *pszOutBuffer = nullptr;
	BOOL  bResults = FALSE;
	HINTERNET  hSession = nullptr,
		hConnect = nullptr,
		hRequest = nullptr;

	std::string result;

	hSession = WinHttpOpen(L"load-test/1.0",
		WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
		WINHTTP_NO_PROXY_NAME,
		WINHTTP_NO_PROXY_BYPASS, 0);

	if (hSession)
		hConnect = WinHttpConnect(hSession, server, port, 
			// INTERNET_DEFAULT_HTTPS_PORT
			0);

	if (hConnect)
		hRequest = WinHttpOpenRequest(hConnect, L"GET", path,
			NULL, WINHTTP_NO_REFERER,
			WINHTTP_DEFAULT_ACCEPT_TYPES,
			0 // WINHTTP_FLAG_SECURE
		);

	if (hRequest)
		bResults = WinHttpSendRequest(hRequest,
			WINHTTP_NO_ADDITIONAL_HEADERS, 0,
			WINHTTP_NO_REQUEST_DATA, 0,
			0, 0);


	if (bResults)
		bResults = WinHttpReceiveResponse(hRequest, NULL);

	// Keep checking for data until there is nothing left.
	if (bResults)
	{
		do
		{
			// Check for available data.
			dwSize = 0;
			if (!WinHttpQueryDataAvailable(hRequest, &dwSize))
				printf("Error %u in WinHttpQueryDataAvailable.\n",
					GetLastError());

			// Allocate space for the buffer.
			pszOutBuffer = new char[dwSize + 1];
			if (!pszOutBuffer)
			{
				printf("Out of memory\n");
				dwSize = 0;
			}
			else
			{
				// Read the data.
				ZeroMemory(pszOutBuffer, dwSize + 1);

				if (!WinHttpReadData(hRequest, (LPVOID)pszOutBuffer,
					dwSize, &dwDownloaded))
					printf("Error %u in WinHttpReadData.\n", GetLastError());
				else
					result.append(static_cast<const char*>(pszOutBuffer), dwDownloaded);

				// Free the memory allocated to the buffer.
				delete[] pszOutBuffer;
			}
		} while (dwSize > 0);
	}


	if (!bResults)
		printf("Error %d has occurred.\n", GetLastError());

	if (hRequest) WinHttpCloseHandle(hRequest);
	if (hConnect) WinHttpCloseHandle(hConnect);
	if (hSession) WinHttpCloseHandle(hSession);

	return result;
}

const size_t requests_per_thread = 10000;
const size_t thread_count = 16;
std::atomic<size_t> total_result_count = 0;

void task_func()
{
	Sleep(500);

	for (int i = 0; i < requests_per_thread; i++)
	{
		auto result = send_get_request(L"localhost", 8080, L"/sync");
		total_result_count += 1;
		if (total_result_count.load() % 1000 == 999)
			std::cout << ".";
	}
}

int main()
{
	const auto start_seconds = now();
	std::cout << "Test HTTP GET\n";
	std::cout << "Sending " << requests_per_thread * thread_count << " requests on " << thread_count << " threads\n";

	std::vector<std::thread> threads;

	for (int i = 0; i < thread_count; i++)
	{
		threads.emplace_back(std::thread(task_func));
	}

	for (auto& t : threads)
	{
		t.join();
		std::cout << "X";
	}

	std::cout << std::endl;

	send_get_request(L"localhost", 8080, L"/kill");

	const auto elapsed = now() - start_seconds;
	std::cout << "Completed " << total_result_count << " requests in " << elapsed << " seconds\n";
	std::cout << total_result_count / elapsed << " requests per second\n";
	std::cout << "\npress any key\n";

	_getch();
	return 0;
}