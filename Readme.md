Notepad Tracker

This sample project demonstrates how to instrument Windows Notepad
to collect file names and sizes when files are saved on disk. The 
active Notepad instances are injected with a DLL with hook functions
for Win32 APIs CreateFileW and CloseHandle. The hooking mechanism is 
implemented with Microsoft Detours library. To preserve Notepad's 
responsiveness, the data are passed to a worker thread running inside
the DLL that sends them through a socket connection to a tracking 
server. There, they are shown in a console window and stored in a 
text file log.

The project has 3 binary components: the hook DLL NotepadHookDll.dll,
the instrumentation program NotepadTracker.exe and the server 
TrackingServer.exe. Their sources comprise 3 Visual C++ projects of
the Visual Studio solution file NotepadTracker.sln. Only 64-bit 
solution configurations are provided. The configuration build process
places the binaries into the Staging folder in a solution directory.

To evaluate the project, build it and run runme.bat in the Staging 
folder. It will launch the instrumenter and the server. They are 
console applications designed to run in an infinite loop. 4 times 
a second, the instrumenter queries the OS for the list of running 
processes, identifies Notepad instances that have not been processed
and injects them. As Notepads are opened and closed, the instrumentor 
shows its actions in its console window. Whenever one of the running
Notepads saves a file, the tracking server shows its pathname and 
size and saves them in Notepad_Save_Log.txt.





