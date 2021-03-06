#include <windows.h>
#include <psapi.h>
#include <iostream>
#include <set>

bool IsThisNotepad(DWORD processID)
{
   if (HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processID))
   {
      wchar_t szProcessName[MAX_PATH]{ 0 };
      GetModuleFileNameEx (hProcess, NULL, szProcessName, sizeof(szProcessName));
      CloseHandle(hProcess);
      static const wchar_t NOTEPAD[]{ L"notepad.exe" };
      static const size_t NOTEPAD_LENGTH = wcslen(NOTEPAD);
      size_t length = wcslen(szProcessName);
      return length >= NOTEPAD_LENGTH && wcscmp(szProcessName + length - NOTEPAD_LENGTH, NOTEPAD) == 0;
   }

   return false;
}

bool IsStillRunning(DWORD processID)
{
   if (HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processID))
   {
      DWORD rc { 0 };
      GetExitCodeProcess(process, &rc);
      CloseHandle(process);
      return rc == STILL_ACTIVE;
   }
   return false;
}

int main(void)
{
   //a set of process IDs of Notepad process running 
   std::set<DWORD> notepad_processes;

   wchar_t curDir[MAX_PATH]{ 0 };
   GetCurrentDirectory(sizeof(curDir), curDir);
   wchar_t hookDllPathName[MAX_PATH]{ 0 };
   swprintf_s(hookDllPathName, sizeof(hookDllPathName), L"%s\\NotepadHookDll.dll", curDir);

   while (true)
   {
      DWORD processes[1024]{ 0 }, cbNeeded{ 0 };

      EnumProcesses(processes, sizeof(processes), &cbNeeded);

      DWORD cProcesses = cbNeeded / sizeof(DWORD);

      for (unsigned i = 0; i < cProcesses; i++)
      {
         DWORD procId = processes[i];

         if (procId != 0 && IsThisNotepad(procId))
         {//notepad found
            if (notepad_processes.find(procId) == notepad_processes.end())
            {//notepad needs to be instrumented
               HANDLE hProcess = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE, FALSE, procId);
               LPVOID remotePathName = (LPVOID)VirtualAllocEx(hProcess, NULL, sizeof(hookDllPathName), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
               WriteProcessMemory(hProcess, remotePathName, hookDllPathName, sizeof(hookDllPathName), NULL);
               CreateRemoteThread(hProcess, NULL, NULL,
                  (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandle(L"kernel32.dll"), "LoadLibraryW"), remotePathName, NULL, NULL);
               CloseHandle(hProcess);
               notepad_processes.insert(procId);
               std::cout << "Notepad " << procId << " hooked\n";
            }
         }
      }
      
      //remove from notepad_processes the ids that are no longer valid

      auto it = notepad_processes.begin();
      while(it != notepad_processes.end())
      {
         if (!IsStillRunning(*it))
         {
            std::cout << "Notepad " << *it << " has quit\n";
            it = notepad_processes.erase(it);
         }
         else
            it++;
      }

      Sleep(250);
   }
}
