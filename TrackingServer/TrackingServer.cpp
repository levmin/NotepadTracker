#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <fstream>
#include <mutex>

// Need to link with Ws2_32.lib
#pragma comment (lib, "Ws2_32.lib")

class CGlobalData
{
public:
   CGlobalData():ListenSocket(NULL)
   {
      WSAStartup(MAKEWORD(2, 2), &wsaData);
   }
   ~CGlobalData()
   {
      if (ListenSocket != INVALID_SOCKET)
         closesocket(ListenSocket);
      if (output.is_open())
         output.close();
      WSACleanup();
   }
   SOCKET ListenSocket;
   std::wfstream output;
private:
   WSADATA wsaData;

} g_data;


DWORD ClientConnectionThreadProc(LPVOID lpParameter)
{
   static std::mutex mutex; //to avoid race conditions when showing and writing the received data

   SOCKET ClientSocket = (SOCKET)lpParameter;
   int iResult = 0;
   
   do {
      wchar_t recvbuf[512]{ 0 };
      iResult = recv(ClientSocket, (char *)recvbuf, sizeof(recvbuf), 0);
      if (iResult > 0) {
         std::lock_guard<std::mutex> guard(mutex);
         //output the string we received from the client on the console
         wprintf_s(L"%s", recvbuf);
         //save it into a log file as well
         if (!g_data.output.is_open())
         {
            g_data.output.open(L"Notepad_Save_Log.txt", std::ios::out | std::ios::app);
         }
         g_data.output << recvbuf ;
         g_data.output.flush();
      }
   } while (iResult > 0);

   shutdown(ClientSocket, SD_SEND);

   closesocket(ClientSocket);
   return 0;

}  

int main(void)
{
   SOCKET ListenSocket = INVALID_SOCKET;

   struct addrinfo *result = NULL;

   struct addrinfo hints;
   ZeroMemory(&hints, sizeof(hints));
   hints.ai_family = AF_INET;
   hints.ai_socktype = SOCK_STREAM;
   hints.ai_protocol = IPPROTO_TCP;
   hints.ai_flags = AI_PASSIVE;

   // Resolve the server address and port
   getaddrinfo(NULL, "27016", &hints, &result);
   
   // Create a SOCKET for connecting to server
   ListenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
   
   // Setup the TCP listening socket
   bind(ListenSocket, result->ai_addr, (int)result->ai_addrlen);
 
   freeaddrinfo(result);

   g_data.ListenSocket = ListenSocket;

   while (true)
   {
      listen(ListenSocket, SOMAXCONN);
      // Accept a client socket
      SOCKET ClientSocket = accept(ListenSocket, NULL, NULL);
      // Create a worker thread and pass the client socket to it 
      DWORD dummy;
      CreateThread(NULL, 0, ClientConnectionThreadProc, (LPVOID)ClientSocket, 0, &dummy);
   }
}
