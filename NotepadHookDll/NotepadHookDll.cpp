#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include "detours.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mutex>

// Need to link with Ws2_32.lib
#pragma comment (lib, "Ws2_32.lib")

namespace {

   wchar_t g_pathName[MAX_PATH]{ 0 };
   LARGE_INTEGER g_fileSize{ 0 };
   HANDLE g_fileHandle{ 0 };
   //this mutex guards the 3 variables above
   std::mutex g_mutex;
   
   /* The first of these events is signalled whenever the hook detects a write operation and determines the file name and size. 
      It wakes up the worker thread so that it would pass this information to the server.
      The second event signals the worker thread that the Notepad gets closed.
   */
   HANDLE g_events[2];

   HANDLE (*TrueCreateFileW)(
      LPCWSTR               lpFileName,
      DWORD                 dwDesiredAccess,
      DWORD                 dwShareMode,
      LPSECURITY_ATTRIBUTES lpSecurityAttributes,
      DWORD                 dwCreationDisposition,
      DWORD                 dwFlagsAndAttributes,
      HANDLE                hTemplateFile
      ) = CreateFileW;

   /* This function saves the file name and handle, to be found by the CloseHandleHook
   */
   HANDLE CreateFileWHook(
      LPCWSTR               lpFileName,
      DWORD                 dwDesiredAccess,
      DWORD                 dwShareMode,
      LPSECURITY_ATTRIBUTES lpSecurityAttributes,
      DWORD                 dwCreationDisposition,
      DWORD                 dwFlagsAndAttributes,
      HANDLE                hTemplateFile
   )
   {
      HANDLE hFile = TrueCreateFileW(
         lpFileName,
         dwDesiredAccess,
         dwShareMode,
         lpSecurityAttributes,
         dwCreationDisposition,
         dwFlagsAndAttributes,
         hTemplateFile
      );
      //The Notepad's "Save As" window updates thumbnale and icon caches, we need to filter them out
      if ((dwDesiredAccess & GENERIC_WRITE) && (wcsstr(lpFileName, L"AppData\\Local\\Microsoft\\Windows\\Explorer\\thumbcache_") == NULL) &&
         ((wcsstr(lpFileName, L"AppData\\Local\\Microsoft\\Windows\\Explorer\\iconcache_") == NULL)) && dwCreationDisposition != 1)
      {
         std::lock_guard<std::mutex> guard(g_mutex);

         g_fileHandle = hFile;
         g_fileSize.QuadPart = 0;
         wcscpy_s(g_pathName, lpFileName);
      }
      return hFile;
   }

   BOOL (*TrueCloseHandle)(HANDLE hObject) = CloseHandle;

   /* When notepad is done writing a file and closes it, this function queries its  size, wakes up the worker thread and immediately returns.
   */
   BOOL CloseHandleHook(HANDLE hObject) {
      bool bRightHandle = false;
      {
         std::lock_guard<std::mutex> guard(g_mutex);
         if (hObject == g_fileHandle)
         {
            GetFileSizeEx(g_fileHandle, &g_fileSize);
            g_fileHandle = INVALID_HANDLE_VALUE;
            bRightHandle = true;
         }
      }
      if (bRightHandle)
         //wake up the worker thread
         SetEvent(g_events[0]);

      return TrueCloseHandle(hObject);
   }

   HANDLE g_hWorkerThread{ INVALID_HANDLE_VALUE };
   
   /* The worker thread waits for the CreateFileWHook and CloseHandleHook to detect a file save operation
      and then passed the file name and size to the tracking server via a TCP/IP socket connection
   */
   DWORD WorkerThreadProc(LPVOID lpParameter)
   {
      WSADATA wsaData;
      SOCKET ConnectSocket = INVALID_SOCKET;
      struct addrinfo *result = NULL,*ptr = NULL, hints;
      ZeroMemory(&hints, sizeof(hints));
      hints.ai_family = AF_UNSPEC;
      hints.ai_socktype = SOCK_STREAM;
      hints.ai_protocol = IPPROTO_TCP;

      int iResult{ 0 };
      WSAStartup(MAKEWORD(2, 2), &wsaData);
      //for the sake of simplicity, let's assume that the TrackingServer is running on the same computer
      iResult = getaddrinfo("localhost", "27016", &hints, &result);
      bool bServerIsAvailable = (iResult==0);
      if (bServerIsAvailable)
      {
         // Attempt to connect to an address until one succeeds
         for (ptr = result; ptr != NULL; ptr = ptr->ai_next) {

            // Create a SOCKET for connecting to server
            ConnectSocket = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);

            // Connect to server.
            iResult = connect(ConnectSocket, ptr->ai_addr, (int)ptr->ai_addrlen);
            if (iResult == SOCKET_ERROR) {
               closesocket(ConnectSocket);
               ConnectSocket = INVALID_SOCKET;
               continue;
            }
            break;
         }

         freeaddrinfo(result);

         if (ConnectSocket == INVALID_SOCKET)
            bServerIsAvailable = false;
      }

      while (true)
      {
         if (WaitForMultipleObjects(2, g_events, FALSE, INFINITE) == WAIT_OBJECT_0)
         {
            if (bServerIsAvailable)
            {
               wchar_t pathName[MAX_PATH]{ 0 };
               long long fileSize{ 0 };

               {
                  std::lock_guard<std::mutex> guard(g_mutex);
                  wcscpy_s(pathName, g_pathName);
                  fileSize = g_fileSize.QuadPart;
               }

               //format the string to be send
               wchar_t buf[512]{ 0 };
               wsprintf(buf, L"%s , %I64d\n", pathName, fileSize);
               //send the data to the server
               send(ConnectSocket, (const char *)buf, static_cast<int>(sizeof(wchar_t) * wcslen(buf)), 0);
            }
         }
         else
         {//showdown initiated, release resources and quit
            CloseHandle(g_events[0]);
            CloseHandle(g_events[1]);
            if (ConnectSocket != INVALID_SOCKET)
            {
               shutdown(ConnectSocket, SD_SEND);
               closesocket(ConnectSocket);
            }
            WSACleanup(); 
            return 0;
         }
      }
   }
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD dwReason, LPVOID reserved)
{
   if (DetourIsHelperProcess()) {
      return TRUE;
   }

   if (dwReason == DLL_PROCESS_ATTACH) {
      DetourRestoreAfterWith();
      //create transmission and shutdown events
      g_events[0] = CreateEvent(NULL, FALSE, FALSE, NULL);
      g_events[1] = CreateEvent(NULL, FALSE, FALSE, NULL);
      DWORD dummy;
      //start worker thread
      g_hWorkerThread = CreateThread(NULL, 0, WorkerThreadProc, NULL, 0, &dummy);
      //instrument CreateFileW and CloseHandle
      DetourTransactionBegin();
      DetourUpdateThread(GetCurrentThread());
      DetourAttach(&(PVOID&)TrueCreateFileW, CreateFileWHook);
      DetourAttach(&(PVOID&)TrueCloseHandle, CloseHandleHook);
      DetourTransactionCommit();
   }
   else if (dwReason == DLL_PROCESS_DETACH) {
      //remote instrumentation
      DetourTransactionBegin();
      DetourUpdateThread(GetCurrentThread());
      DetourDetach(&(PVOID&)TrueCreateFileW, CreateFileWHook);
      DetourDetach(&(PVOID&)TrueCloseHandle, CloseHandleHook);
      DetourTransactionCommit();
      //shut down the worker thread and release all resources
      SetEvent(g_events[1]);
   }
   return TRUE;
}
