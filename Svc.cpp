
#include <windows.h>

#include <strsafe.h>
#include <tchar.h>
#include <thread>
#include <tlhelp32.h>

#include <winhttp.h>

#include "Svc.h"
#include "UpdSvc.h"
#include "json.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

#include <Msi.h>
#include <msiquery.h>

#include <regex>

#include <filesystem>
#include <psapi.h>
#include <winbase.h>

#include <processthreadsapi.h>

#define uid TEXT("{028818E2-5DF4-414F-A1E4-2AA542DE4697}")

#define SVCNAME TEXT("UpdSvc")
static SERVICE_STATUS gSvcStatus;
static SERVICE_STATUS_HANDLE gSvcStatusHandle;
static HANDLE ghSvcStopEvent = NULL;

VOID SvcInstall(void);
VOID WINAPI SvcCtrlHandler(DWORD);
VOID WINAPI SvcMain(DWORD, LPTSTR *);

VOID ReportSvcStatus(DWORD, DWORD, DWORD);
VOID SvcInit(DWORD, LPTSTR *);
VOID SvcReportEvent(LPTSTR);

static std::vector<int> splitString(const std::string &str, char delimiter);
static bool matchFileRegex(const std::wstring &input, const std::wregex &pattern);

std::wstring getPathofComponent(wchar_t componentid[256]);
int ListProcessModules(DWORD dwPID);
bool isexe(std::wstring s);

/**
 * @brief Entry point for the process
 * @param argc Number of arguments
 * @param argv Argument contents
 * @return None, defaults to 0
 */

#ifndef SVC_TEST

int __cdecl _tmain(int argc, TCHAR *argv[]) {
    // If command-line parameter is "install", install the service.
    // Otherwise, the service is probably being started by the SCM.

    if (lstrcmpi(argv[1], TEXT("install")) == 0) {
        SvcInstall();
        return 0;
    }

    // TO_DO: Add any additional services for the process to this table.
    SERVICE_TABLE_ENTRY DispatchTable[] = {
            {SVCNAME, (LPSERVICE_MAIN_FUNCTION)SvcMain}, {NULL, NULL}};

    // This call returns when the service has stopped.
    // The process should simply terminate when the call returns.

    if (! StartServiceCtrlDispatcher(DispatchTable)) {
        SvcReportEvent(TEXT("StartServiceCtrlDispatcher"));
    }

    return 0;
}

#endif

//
// Purpose:
//   Installs a service in the SCM database
//
// Parameters:
//   None
//
// Return value:
//   None
//
VOID SvcInstall() {
    SC_HANDLE schSCManager;
    SC_HANDLE schService;
    TCHAR szUnquotedPath[MAX_PATH];

    if (! GetModuleFileName(NULL, szUnquotedPath, MAX_PATH)) {
        printf("Cannot install service (%lu)\n", GetLastError());
        return;
    }

    // In case the path contains a space, it must be quoted so that
    // it is correctly interpreted. For example,
    // "d:\my share\myservice.exe" should be specified as
    // ""d:\my share\myservice.exe"".
    TCHAR szPath[MAX_PATH];
    StringCbPrintf(szPath, MAX_PATH, TEXT("\"%s\""), szUnquotedPath);

    // Get a handle to the SCM database.

    schSCManager = OpenSCManager(NULL, // local computer
            NULL, // ServicesActive database
            SC_MANAGER_ALL_ACCESS); // full access rights

    if (NULL == schSCManager) {
        printf("OpenSCManager failed (%lu)\n", GetLastError());
        return;
    }

    // Create the service

    schService = CreateService(schSCManager, // SCM database
            SVCNAME, // name of service
            SVCNAME, // service name to display
            SERVICE_ALL_ACCESS, // desired access
            SERVICE_WIN32_OWN_PROCESS, // service type
            SERVICE_DEMAND_START, // start type
            SERVICE_ERROR_NORMAL, // error control type
            szPath, // path to service's binary
            NULL, // no load ordering group
            NULL, // no tag identifier
            NULL, // no dependencies
            NULL, // LocalSystem account
            NULL); // no password

    if (schService == NULL) {
        printf("CreateService failed (%lu)\n", GetLastError());
        CloseServiceHandle(schSCManager);
        return;
    }
    else {
        printf("Service installed successfully\n");
    }

    CloseServiceHandle(schService);
    CloseServiceHandle(schSCManager);
}

//
// Purpose:
//   Entry point for the service
//
// Parameters:
//   dwArgc - Number of arguments in the lpszArgv array
//   lpszArgv - Array of strings. The first string is the name of
//     the service and subsequent strings are passed by the process
//     that called the StartService function to start the service.
//
// Return value:
//   None.
//
VOID WINAPI SvcMain(DWORD dwArgc, LPTSTR *lpszArgv) {
    // Register the handler function for the service

    gSvcStatusHandle = RegisterServiceCtrlHandler(SVCNAME, SvcCtrlHandler);

    if (! gSvcStatusHandle) {
        SvcReportEvent(TEXT("RegisterServiceCtrlHandler"));
        return;
    }

    // These SERVICE_STATUS members remain as set here

    gSvcStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    gSvcStatus.dwServiceSpecificExitCode = 0;

    // Report initial status to the SCM

    ReportSvcStatus(SERVICE_START_PENDING, NO_ERROR, 3000);

    // Perform service-specific initialization and work.

    SvcInit(dwArgc, lpszArgv);
}

//
// Purpose:
//   The service code
//
// Parameters:
//   dwArgc - Number of arguments in the lpszArgv array
//   lpszArgv - Array of strings. The first string is the name of
//     the service and subsequent strings are passed by the process
//     that called the StartService function to start the service.
//
// Return value:
//   None
//
VOID SvcInit(DWORD dwArgc, LPTSTR *lpszArgv) {
    // TO_DO: Declare and set any required variables.
    //   Be sure to periodically call ReportSvcStatus() with
    //   SERVICE_START_PENDING. If initialization fails, call
    //   ReportSvcStatus with SERVICE_STOPPED.

    // Create an event. The control handler function, SvcCtrlHandler,
    // signals this event when it receives the stop control code.

    ghSvcStopEvent = CreateEvent(NULL, // default security attributes
            TRUE, // manual reset event
            FALSE, // not signaled
            NULL); // no name

    if (ghSvcStopEvent == NULL) {
        ReportSvcStatus(SERVICE_STOPPED, GetLastError(), 0);
        return;
    }

    // Report running status when initialization is complete.

    ReportSvcStatus(SERVICE_RUNNING, NO_ERROR, 0);

    // TO_DO: Perform work until service stops.

    while (1) {
        // Check whether to stop the service.

        WaitForSingleObject(ghSvcStopEvent, INFINITE);

        ReportSvcStatus(SERVICE_STOPPED, NO_ERROR, 0);

        // CreateRequest();

        return;
    }
}

//
// Purpose:
//   Sets the current service status and reports it to the SCM.
//
// Parameters:
//   dwCurrentState - The current state (see SERVICE_STATUS)
//   dwWin32ExitCode - The system error code
//   dwWaitHint - Estimated time for pending operation,
//     in milliseconds
//
// Return value:
//   None
//
VOID ReportSvcStatus(DWORD dwCurrentState, DWORD dwWin32ExitCode, DWORD dwWaitHint) {
    static DWORD dwCheckPoint = 1;

    // Fill in the SERVICE_STATUS structure.

    gSvcStatus.dwCurrentState = dwCurrentState;
    gSvcStatus.dwWin32ExitCode = dwWin32ExitCode;
    gSvcStatus.dwWaitHint = dwWaitHint;

    if (dwCurrentState == SERVICE_START_PENDING) {
        gSvcStatus.dwControlsAccepted = 0;
    }
    else {
        gSvcStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    }

    if ((dwCurrentState == SERVICE_RUNNING) || (dwCurrentState == SERVICE_STOPPED)) {
        gSvcStatus.dwCheckPoint = 0;
    }
    else {
        gSvcStatus.dwCheckPoint = dwCheckPoint++;
    }

    // Report the status of the service to the SCM.
    SetServiceStatus(gSvcStatusHandle, &gSvcStatus);
}

//
// Purpose:
//   Called by SCM whenever a control code is sent to the service
//   using the ControlService function.
//
// Parameters:
//   dwCtrl - control code
//
// Return value:
//   None
//
VOID WINAPI SvcCtrlHandler(DWORD dwCtrl) {
    // Handle the requested control code.

    switch (dwCtrl) {
    case SERVICE_CONTROL_STOP:
        ReportSvcStatus(SERVICE_STOP_PENDING, NO_ERROR, 0);

        // Signal the service to stop.

        SetEvent(ghSvcStopEvent);
        ReportSvcStatus(gSvcStatus.dwCurrentState, NO_ERROR, 0);

        return;

    case SERVICE_CONTROL_INTERROGATE:
        break;

    default:
        break;
    }
}

//
// Purpose:
//   Logs messages to the event log
//
// Parameters:
//   szFunction - name of function that failed
//
// Return value:
//   None
//
// Remarks:
//   The service must have an entry in the Application event log.
//
VOID SvcReportEvent(LPTSTR szFunction) {
    HANDLE hEventSource;
    LPCTSTR lpszStrings[2];
    TCHAR Buffer[80];

    hEventSource = RegisterEventSource(NULL, SVCNAME);

    if (NULL != hEventSource) {
        StringCchPrintf(Buffer, 80, TEXT("%s failed with %d"), szFunction, GetLastError());

        lpszStrings[0] = SVCNAME;
        lpszStrings[1] = Buffer;

        ReportEvent(hEventSource, // event log handle
                EVENTLOG_ERROR_TYPE, // event type
                0, // event category
                SVC_ERROR, // event identifier
                NULL, // no security identifier
                2, // size of lpszStrings array
                0, // no binary data
                lpszStrings, // array of strings
                NULL); // no binary data

        DeregisterEventSource(hEventSource);
    }
}

std::string CreateRequest(bool file, const std::wstring &domain, const std::wstring &path) {
    DWORD dwSize = 0;
    DWORD dwDownloaded = 0;

    std::stringstream sstr;
    std::ofstream ostr;

    LPCWSTR lpcwstrDomain = domain.c_str();
    LPCWSTR lpcwstrPath = path.c_str();

    // Get file name from path
    std::size_t lastSlashPos = path.find_last_of(L"/");
    auto filename = path.substr(lastSlashPos + 1);

    if (file) {
        // Control if file name is valid
        std::wregex acceptedRegex(L"^[A-Za-z0-9._-]+$");
        if (! (matchFileRegex(filename, acceptedRegex))) {
            std::wcerr << "Invalid file, this file cannot be downloaded" << std::endl;
            return {};
        }

        // Get path of default location for temporary files (%userprofile%\AppData\Local\Temp)

        std::wstring tempDir;
        if (auto ret = std::getenv("TEMP"); (! ret)) {
            return {};
        }
        else {
            tempDir = s2ws(std::string_view{ret});
        }
        if (tempDir.empty()) {
            std::wcerr << "Failed to retrieve the temporary directory path." << std::endl;
            return {};
        }

        // Open file at %userprofile%\AppData\Local\Temp
        auto tempFilePath = std::wstring(tempDir) + L"\\updsvc";
        if (! checkandCreateDirectory(tempFilePath)) {
            tempFilePath = std::wstring(tempDir) + L"\\" + filename;
        }
        tempFilePath = std::wstring(tempDir) + L"\\updsvc\\" + filename;
        ostr.open(tempFilePath, std::ios::trunc | std::ios::binary);
        if (! ostr.is_open()) {
            std::wcerr << "Failed to open file." << std::endl;
            return {};
        }
    }

    BOOL bResults = FALSE;
    HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL;

    // Use WinHttpOpen to obtain a session handle.
    hSession = WinHttpOpen(L"Http Request Attempt", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);

    // Specify an HTTP server.
    if (hSession) {
        hConnect = WinHttpConnect(hSession, lpcwstrDomain, INTERNET_DEFAULT_PORT, 0);
    }
    // Create an HTTP Request handle.
    if (hConnect) {
        hRequest = WinHttpOpenRequest(hConnect, L"GET", lpcwstrPath, NULL, WINHTTP_NO_REFERER,
                WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    }
    // Send a Request.
    if (hRequest) {
        bResults = WinHttpSendRequest(
                hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
        SvcReportEvent(L"request sent");
    }

    // End the request.
    if (bResults) {
        bResults = WinHttpReceiveResponse(hRequest, NULL);
    }

    // Keep checking for data until there is nothing left.
    if (bResults) {
        do {
            // Check for available data.
            dwSize = 0;
            if (! WinHttpQueryDataAvailable(hRequest, &dwSize)) {
                printf("Error %lu in WinHttpQueryDataAvailable.\n", GetLastError());
                break;
            }

            // No more available data.
            if (! dwSize) {
                break;
            }

            // Allocate space for the buffer.
            auto pszOutBuffer = new char[dwSize + 1];
            if (! pszOutBuffer) {
                printf("Out of memory\n");
                break;
            }

            // Read the Data.
            ZeroMemory(pszOutBuffer, dwSize + 1);

            if (! WinHttpReadData(hRequest, (LPVOID)pszOutBuffer, dwSize, &dwDownloaded)) {
                printf("Error %lu in WinHttpReadData.\n", GetLastError());
                SvcReportEvent(L"failed to read");
                break;
            }

            assert(dwSize == dwDownloaded);
            if (file) {
                ostr << std::string_view(pszOutBuffer, dwSize);
            }
            else {
                sstr << std::string_view(pszOutBuffer, dwSize);
            }
            delete[] pszOutBuffer;
            SvcReportEvent(L"data read");

            // This condition should never be reached since WinHttpQueryDataAvailable
            // reported that there are bits to read.
            if (dwDownloaded == 0) {
                break;
            }

        } while (dwSize > 0);
    }
    else {
        // Report any errors.
        printf("Error %lu has occurred.\n", GetLastError());
    }

    // Close any open handles.
    if (hRequest) {
        WinHttpCloseHandle(hRequest);
    }
    if (hConnect) {
        WinHttpCloseHandle(hConnect);
    }
    if (hSession) {
        WinHttpCloseHandle(hSession);
    }
    if (file) {
        ostr.close();
        return ws2s(filename);
    }
    else {
        return sstr.str();
    }
}

std::wstring UpdateDetector(const std::string &str) {
    using json = nlohmann::json;
    json j_complete;
    const auto wversion = GetProgramVersion();
    const auto version = ws2s(wversion);

    try {
        // parsing input with a syntax error
        j_complete = json::parse(str);
    }
    catch (json::parse_error &e) {
        // output exception information
        std::wcout << "message: " << e.what() << '\n'
                   << "exception id: " << e.id << '\n'
                   << "byte position of error: " << e.byte << std::endl;
        return {};
    }

    const auto &windows = j_complete["mgui-wgt"]["exe"];

    for (auto it = windows.rbegin(); it != windows.rend(); ++it) {
        std::string key = it.key();
        // check whether current version is smaller than the version at hand
        if (compareVersions(it.key(), wversion) != 1) {
            std::cout << "Version " << it.key() << " skipped" << std::endl;
            continue;
        }

        auto &val = it.value();
        for (auto jt = val.begin(); jt != val.end(); ++jt) {
            if (jt.key() == version && jt.value()["channel"] == "Stable") {
                const auto &url = jt.value()["url"];
                std::cout << "Patch update from " << jt.key() << " to " << it.key()
                          << " url: " << url << std::endl;
                return s2ws(std::string_view(url));
            }
        }

        if (val["null"]["channel"] == "Stable") {
            const auto &url = val["null"]["url"];
            std::cout << "Full update from " << version << " to " << it.key() << " url: " << url
                      << std::endl;
            return s2ws(std::string_view(url));
        }

        std::cout << "Package version " << it.key() << " channel " << val["null"]["channel"]
                  << " was skipped" << std::endl;
    }

    return {};
}

std::vector<int> splitString(const std::string &str, char delimiter) {
    std::vector<int> nums;
    std::stringstream ss(str);
    std::string num;

    while (getline(ss, num, delimiter)) {
        nums.push_back(std::stoi(num));
    }
    return nums;
}
std::vector<int> splitString(const std::wstring &str, wchar_t delimiter) {
    std::vector<int> nums;
    std::wstringstream ss(str);
    std::wstring num;

    while (std::getline(ss, num, delimiter)) {
        nums.push_back(std::stoi(num));
    }
    return nums;
}

int compareVersions(const std::string &version1, const std::wstring &version2) {
    std::vector<int> v1 = splitString(version1, '.');
    std::vector<int> v2 = splitString(version2, '.');

    // Compare major version
    if (v1[0] < v2[0]) {
        return -1;
    }
    else if (v1[0] > v2[0]) {
        return 1;
    }

    // Compare minor version
    if (v1[1] < v2[1]) {
        return -1;
    }
    else if (v1[1] > v2[1]) {
        return 1;
    }

    // Compare patch version
    if (v1[2] < v2[2]) {
        return -1;
    }
    else if (v1[2] > v2[2]) {
        return 1;
    }

    // Versions are equal
    return 0;
}

void urlSplit(const std::wstring &url, std::wstring &domain, std::wstring &path) {
    auto modifiedUrl = url;
    if (modifiedUrl.find("https://" == 0)) {
        modifiedUrl.erase(0, 8);
    }

    size_t firstSlashPos = modifiedUrl.find(wchar_t{'/'});

    if (firstSlashPos == std::wstring::npos) {
        return;
    }

    domain = modifiedUrl.substr(0, firstSlashPos);
    path = modifiedUrl.substr(firstSlashPos);
}

// Function to retrieve the version of a program
std::wstring GetProgramVersion() {
    wchar_t versionBuffer[256];
    DWORD bufferSize = sizeof(versionBuffer);

    // Use MsiGetProductInfo for get the version
    UINT result = MsiGetProductInfo(uid, INSTALLPROPERTY_VERSIONSTRING, versionBuffer, &bufferSize);
    if (result == ERROR_SUCCESS) {
        return std::wstring(versionBuffer);
    }

    return {};
}

std::wstring GetSourcePath() {
    wchar_t versionBuffer[1024];
    DWORD bufferSize = sizeof(versionBuffer);

    // Use MsiGetProductInfo for get the source path
    UINT result = MsiGetProductInfo(uid, INSTALLPROPERTY_INSTALLSOURCE, versionBuffer, &bufferSize);
    if (result == ERROR_SUCCESS) {
        return std::wstring(versionBuffer);
    }

    return {};
}

/*std::wstring GetSourceFilePath() {
    std::filesystem::path path = GetSourcePath();
    std::wstring filename, fullPath;
    if (std::filesystem::is_directory(path) && std::filesystem::exists(path)) {
        // Get the first file entry in the directory
        std::filesystem::directory_entry entry(path);

        if (entry.path().extension() == ".msi") {
            std::wcout << GetSourcePath() + filename << std::endl;
            return GetSourcePath() + filename;
        }
        else {
            std::wcerr << "No file found in the directory." << std::endl;
        }
    }
    else {
        std::wcerr << "The given path is not a directory or does not exist." << std::endl;
    }
}*/

std::wstring GetFirstFileNameInDirectory(const std::wstring &directoryPath) {
    DWORD fileAttributes = GetFileAttributes(directoryPath.c_str());
    if (fileAttributes != INVALID_FILE_ATTRIBUTES && (fileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        WIN32_FIND_DATA findFileData;
        HANDLE hFind = FindFirstFile((directoryPath + L"*").c_str(), &findFileData);

        if (hFind == INVALID_HANDLE_VALUE) {
            std::wcerr << L"Failed to find the first file." << std::endl;
            return L"";
        }

        do {
            if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                // Skip directories "." and ".."
                if (wcscmp(findFileData.cFileName, L".") != 0
                        && wcscmp(findFileData.cFileName, L"..") != 0) {
                    // Handle subdirectories if needed
                    // ...
                }
            }
            else {
                // Return the first file name
                FindClose(hFind);
                return findFileData.cFileName;
            }
        } while (FindNextFile(hFind, &findFileData) != 0);

        FindClose(hFind);
    }
    // Return an empty string if the directory does not exist or contains no files.
    return L"";
}

// Convert std::string to wstring
std::wstring s2ws(std::string_view s) {
    int len;
    int slength = (int)s.length();
    len = MultiByteToWideChar(CP_UTF8, 0, s.data(), slength, 0, 0);
    std::wstring buf;
    buf.resize(len);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), slength, const_cast<wchar_t *>(buf.c_str()), len);
    return buf;
}
// Convert std::string to wstring
std::string ws2s(std::wstring_view s) {
    int len;
    int slength = (int)s.length();
    len = WideCharToMultiByte(CP_UTF8, 0, s.data(), slength, 0, 0, 0, 0);
    std::string buf;
    buf.resize(len);
    WideCharToMultiByte(CP_UTF8, 0, s.data(), slength, const_cast<char *>(buf.data()), len, 0, 0);
    return buf;
}

bool matchFileRegex(const std::wstring &input, const std::wregex &pattern) {
    if (std::regex_match(input, pattern)) {
        std::wcout << "Valid file name " << input << std::endl;
        return true;
    }
    else {
        std::wcout << "Invalid file name" << std::endl;
        return false;
    }
}

bool checkandCreateDirectory(std::wstring path) {
    DWORD attributes = GetFileAttributes(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || ! (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        if (CreateDirectory(path.data(), NULL)) {
            std::wcout << "Directory created: " << path << std::endl;
            return true;
        }
        else {
            std::wcerr << "Failed to create directory: " << path << std::endl;
            return false;
        }
    }
    else {
        std::wcout << "Directory already exists: " << path << std::endl;
        return true;
    }
}

std::wstring ReadMSI(const wchar_t *msiPath) {

    // Open the MSI package
    MSIHANDLE hDatabase = 0;
    if (MsiOpenDatabase(msiPath, MSIDBOPEN_READONLY, &hDatabase) != ERROR_SUCCESS) {
        std::wcerr << "Open MSI package failed" << std::endl;
        return L"";
    }

    // Prepare the query to fetch all files from the MSI package
    PMSIHANDLE hView = 0;
    if (MsiDatabaseOpenView(hDatabase, L"SELECT ComponentId FROM Component", &hView)
            != ERROR_SUCCESS) {
        MsiCloseHandle(hDatabase);
        std::wcerr << "Error preparing query" << std::endl;
        return L"";
    }

    // Execute the query
    if (MsiViewExecute(hView, 0) != ERROR_SUCCESS) {
        MsiCloseHandle(hDatabase);
        std::wcerr << "Execute query failed" << std::endl;
        return L"";
    }

    wchar_t componentId[1024];
    DWORD dirparentBufferSize = sizeof(componentId) / sizeof(wchar_t);
    std::wstring path;

    // Fetch and extract each file from the MSI package
    PMSIHANDLE hRecord = 0;

    while (MsiViewFetch(hView, &hRecord) == ERROR_SUCCESS) {
        UINT res = MsiRecordGetString(hRecord, 1, componentId, &dirparentBufferSize);

        if (res != ERROR_SUCCESS) {
            std::wcerr << "Read data failed" << std::endl;
            return L"";
        }

        // Get the information from the record
        path = getPathofComponent(componentId);
        // std::wcout << path << std::endl;
        if (isexe(path)) {
            std::wcout << path << std::endl;
            return path;
        }
        dirparentBufferSize = sizeof(componentId) / sizeof(wchar_t);
    }

    MsiCloseHandle(hRecord);
    MsiCloseHandle(hView);
    MsiCloseHandle(hDatabase);
    return L"";
}

std::wstring getPathofComponent(wchar_t componentid[256]) {
    wchar_t install[1024];
    DWORD installsize = sizeof(install);
    MsiGetComponentPath(uid, componentid, install, &installsize);
    std::wstring path = install;
    return path;
}

bool isexe(std::wstring s) {
    std::wstring lastFourChars = s.substr(s.length() - 4);
    return lastFourChars == L".exe";
}

int isRunning() {
    HANDLE hProcessSnap;
    PROCESSENTRY32 pe32;

    // Take a snapshot of all processes in the system.
    hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hProcessSnap == INVALID_HANDLE_VALUE) {
        std::wcerr << "Error taking snapshot of processes" << std::endl;
        return -1;
    }

    // Set the size of the structure before using it.
    pe32.dwSize = sizeof(PROCESSENTRY32);

    // Retrieve information about the first process,
    // and exit if unsuccessful
    if (! Process32First(hProcessSnap, &pe32)) {
        std::wcerr << "Cant retrieve information about firs process" << std::endl;
        CloseHandle(hProcessSnap); // clean the snapshot object
        return -1;
    }

    auto source = GetSourcePath();
    auto a = GetFirstFileNameInDirectory(source);
    auto fullpath = source + a;
    const wchar_t *msiPath = fullpath.c_str();
    auto exepath = ReadMSI(msiPath);
    std::size_t lastSlashPos = exepath.find_last_of(L"\\");
    auto exename = exepath.substr(lastSlashPos + 1);
    // Now walk the snapshot of processes, and
    // compare programs name with all processes
    do {
        if (! (wcscmp(pe32.szExeFile, exename.c_str()))) { // TODO get name of exe dynamically
            std::wcout << "\nPROCESS NAME:" << pe32.szExeFile << std::endl;
            auto programsituation = ListProcessModules(pe32.th32ProcessID);
            if (programsituation == 1) {
                return 1;
            }
            else if (programsituation == -1) {
                return -1;
            }
        }

    } while (Process32Next(hProcessSnap, &pe32));

    CloseHandle(hProcessSnap);
    return 0;
}

// Purpose:
//   First we get list of current processes(in isRunning)
//   If our exe's name is inside of it, calls this function
//   Compare our path with exe files module to be sure its our program

// if process gets an error
//   function returns -1
// if module of mgui-wgt.exe contains our path
//   function returns 1
// if module of mgu-wgt.exe dont contains our path
//   function returns 0
int ListProcessModules(DWORD dwPID) {
    HANDLE hModuleSnap = INVALID_HANDLE_VALUE;
    MODULEENTRY32 me32;

    // Take a snapshot of all modules in the specified process.
    hModuleSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, dwPID);
    if (hModuleSnap == INVALID_HANDLE_VALUE) {
        std::wcerr << "Create tool help error" << std::endl;
        return -1;
    }

    // Set the size of the structure before using it.
    me32.dwSize = sizeof(MODULEENTRY32);

    // Retrieve information about the first module,
    // and exit if unsuccessful
    if (! Module32First(hModuleSnap, &me32)) {
        std::wcerr << "Cant retrieve information about firs module" << std::endl;
        CloseHandle(hModuleSnap); // clean the snapshot object
        return -1;
    }

    // getting path of exe dynamically
    auto source = GetSourcePath();
    auto a = GetFirstFileNameInDirectory(source);
    auto fullpath = source + a;
    const wchar_t *msiPath = fullpath.c_str();
    auto exepath = ReadMSI(msiPath);

    // Now walk the module list of the process,
    // and compare the paths with our path
    do {
        if (! (wcscmp(me32.szExePath, exepath.c_str()))) {
            std::wcout << me32.szExePath << std::endl;
            return 1;
        }
    } while (Module32Next(hModuleSnap, &me32));

    CloseHandle(hModuleSnap);
    return 0;
}

void Update() {
    auto a = isRunning();
    if (a == -1) {
        std::wcerr << "Error at getting process list cant update" << std::endl;
        return;
    }
    while (a == 1) {
        a = isRunning();
        std::wcerr << "Program is running cant update" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }
    std::wcout << "Program is closed update can start" << std::endl;
}
