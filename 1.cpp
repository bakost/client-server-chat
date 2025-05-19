// 1.cpp
// Compile with: /D_UNICODE /DUNICODE /DWIN32 /D_WINDOWS

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <tchar.h>
#include <wchar.h>
#include <vector>
#include <string>
#include <sstream>
#include <shlobj.h>

#pragma comment(lib, "ws2_32.lib")

using namespace std;

//*****************************************************************************
// Control IDs
#define IDC_BTN_SERVER      1001
#define IDC_BTN_CLIENT      1002
#define IDC_BTN_STOP_SERVER 1008
#define IDC_CHECK_BROADCAST 1009
#define IDC_EDIT_IP         1003
#define IDC_EDIT_PORT       1004
#define IDC_EDIT_CHAT       1005
#define IDC_EDIT_MSG        1006
#define IDC_BTN_SEND        1007
#define IDC_BTN_DISCONNECT  1010
#define IDC_EDIT_NICKNAME   1011
#define IDC_BTN_ATTACH      1012

// Custom message for updating chat log
#define WM_SOCKET_MESSAGE (WM_APP + 1)

//*****************************************************************************
// Global variables

// Standard window class and title.
static TCHAR szWindowClass[] = _T("DesktopApp");
static TCHAR szTitle[] = _T("Lab 8");

HINSTANCE hInst;
HWND hMainWnd = NULL;

// В клиентском режиме: сокет подключения.
SOCKET commSocket = INVALID_SOCKET;

// Для серверного режима:
SOCKET g_listenSocket = INVALID_SOCKET; // слушающий сокет

// Структура информации о клиенте (для нумерации)
struct ClientInfo {
    SOCKET sock;
    int id;
    std::wstring nickname;  // Add this line
};

vector<ClientInfo> g_clientInfos;

// Флаг управления пересылкой (broadcast): если true – сервер пересылает сообщения от клиента всем другим.
bool g_broadcastEnabled = false;

// Критическая секция для защиты доступа к списку клиентов.
CRITICAL_SECTION csClients;

// Режим: true – сервер, false – клиент.
bool isServerMode = false;

// Для нумерации клиентов
int g_nextClientId = 1;

//*****************************************************************************
// Function prototypes

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
DWORD WINAPI ServerThread(LPVOID lpParam);
DWORD WINAPI RecvThread(LPVOID lpParam);

// Функция для добавления сообщений в лог (read-only multi-line edit)
void AppendChatMessage(HWND hEdit, LPCTSTR msg)
{
    int len = GetWindowTextLength(hEdit);
    SendMessage(hEdit, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessage(hEdit, EM_REPLACESEL, FALSE, (LPARAM)msg);
}

//*****************************************************************************
// WinMain

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    // Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        MessageBox(NULL, _T("WSAStartup failed."), _T("Error"), MB_ICONERROR);
        return 1;
    }
    InitializeCriticalSection(&csClients);

    WNDCLASSEX wcex = {0};
    wcex.cbSize        = sizeof(WNDCLASSEX);
    wcex.style         = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc   = WndProc;
    wcex.hInstance     = hInstance;
    wcex.hIcon         = LoadIcon(hInstance, IDI_APPLICATION);
    wcex.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszClassName = szWindowClass;
    wcex.hIconSm       = LoadIcon(hInstance, IDI_APPLICATION);

    if (!RegisterClassEx(&wcex))
    {
        MessageBox(NULL, _T("Failed to register window class!"), _T("Error"), MB_ICONERROR);
        return 1;
    }

    hInst = hInstance;
    hMainWnd = CreateWindowEx(
        WS_EX_OVERLAPPEDWINDOW,
        szWindowClass,
        szTitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        800, 600,
        NULL, NULL,
        hInstance,
        NULL
    );

    if (!hMainWnd)
    {
        MessageBox(NULL, _T("Failed to create window!"), _T("Error"), MB_ICONERROR);
        return 1;
    }

    ShowWindow(hMainWnd, nCmdShow);
    UpdateWindow(hMainWnd);

    // Main message loop
    MSG msg;
    while (GetMessage(&msg,NULL,0,0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    DeleteCriticalSection(&csClients);
    if(commSocket != INVALID_SOCKET)
        closesocket(commSocket);
    WSACleanup();

    return (int) msg.wParam;
}

//*****************************************************************************
// WndProc

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    static HFONT hFont = NULL;
    switch (message)
    {
    case WM_CREATE:
    {
        // Создаем кнопки, поля ввода, чекбокс и кнопку Disconnect
        CreateWindow(_T("BUTTON"), _T("Create Server"),
                     WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                     10, 10, 120, 30,
                     hWnd, (HMENU)IDC_BTN_SERVER, hInst, NULL);

        CreateWindow(_T("BUTTON"), _T("Connect to Server"),
                     WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                     150, 10, 150, 30,
                     hWnd, (HMENU)IDC_BTN_CLIENT, hInst, NULL);

        CreateWindow(_T("BUTTON"), _T("Stop Server"),
                     WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                     310, 10, 120, 30,
                     hWnd, (HMENU)IDC_BTN_STOP_SERVER, hInst, NULL);

        CreateWindow(_T("BUTTON"), _T("Disconnect"),
                     WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                     450, 10, 120, 30,
                     hWnd, (HMENU)IDC_BTN_DISCONNECT, hInst, NULL);


        CreateWindow(_T("BUTTON"), _T("Attach File"),
                     WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                     570, 50, 120, 25,
                     hWnd, (HMENU)IDC_BTN_ATTACH, hInst, NULL);


        CreateWindow(_T("EDIT"), _T(""), WS_CHILD | WS_VISIBLE | WS_BORDER,
                     370, 50, 150, 25, hWnd, (HMENU)IDC_EDIT_NICKNAME, hInst, NULL);
        CreateWindow(_T("STATIC"), _T("Nickname:"), WS_CHILD | WS_VISIBLE,
                     270, 50, 80, 25, hWnd, NULL, hInst, NULL);

        // Чекбокс для управления broadcast
        CreateWindow(_T("BUTTON"), _T("Messages for all"),
                     WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                     580, 10, 150, 30,
                     hWnd, (HMENU)IDC_CHECK_BROADCAST, hInst, NULL);

        CreateWindow(_T("EDIT"), _T("127.0.0.1"),
                     WS_CHILD | WS_VISIBLE | WS_BORDER,
                     10, 50, 120, 25,
                     hWnd, (HMENU)IDC_EDIT_IP, hInst, NULL);

        CreateWindow(_T("EDIT"), _T("4444"),
                     WS_CHILD | WS_VISIBLE | WS_BORDER,
                     150, 50, 100, 25,
                     hWnd, (HMENU)IDC_EDIT_PORT, hInst, NULL);

        // Многострочный edit для лога (read-only)
        HWND hChatEdit = CreateWindow(_T("EDIT"), _T(""),
                     WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
                     10, 90, 760, 350,
                     hWnd, (HMENU)IDC_EDIT_CHAT, hInst, NULL);
        SendMessage(hChatEdit, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
        SendMessage(hChatEdit, EM_SETREADONLY, TRUE, 0);

        // Поле ввода сообщения
        HWND hMsgEdit = CreateWindow(_T("EDIT"), _T(""),
                     WS_CHILD | WS_VISIBLE | WS_BORDER,
                     10, 450, 400, 25,
                     hWnd, (HMENU)IDC_EDIT_MSG, hInst, NULL);

        EnableWindow(GetDlgItem(hWnd, IDC_EDIT_MSG), FALSE);
        SendMessage(hMsgEdit, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
        
        CreateWindow(_T("BUTTON"), _T("Send"),
                     WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                     420, 450, 100, 25,
                     hWnd, (HMENU)IDC_BTN_SEND, hInst, NULL);
        EnableWindow(GetDlgItem(hWnd, IDC_BTN_SEND), FALSE);
    }
    break;

    case WM_COMMAND:
    {
        int wmId = LOWORD(wParam);
        switch (wmId)
        {
        case IDC_BTN_SERVER:
        {
            // Если сервер уже запущен или имеется активное соединение, предупреждаем.
            if (g_listenSocket != INVALID_SOCKET || !g_clientInfos.empty() || commSocket != INVALID_SOCKET)
            {
                MessageBox(hWnd, _T("Server is already running."), _T("Info"), MB_ICONINFORMATION);
                break;
            }
            isServerMode = true;
            TCHAR portText[16];
            GetWindowText(GetDlgItem(hWnd, IDC_EDIT_PORT), portText, 16);
            int port = _ttoi(portText);
            if (port <= 0)
            {
                MessageBox(hWnd, _T("Invalid port number."), _T("Error"), MB_ICONERROR);
                break;
            }
            int* pPort = (int*)malloc(sizeof(int));
            *pPort = port;
            HANDLE hThread = CreateThread(NULL, 0, ServerThread, (LPVOID)pPort, 0, NULL);
            if (hThread)
                CloseHandle(hThread);
            // Сбрасываем флаг broadcast и обновляем чекбокс
            g_broadcastEnabled = false;
            CheckDlgButton(hWnd, IDC_CHECK_BROADCAST, BST_UNCHECKED);
            // Разблокируем поле ввода и кнопку Send (сервер может сразу отправлять сообщения)
            EnableWindow(GetDlgItem(hWnd, IDC_EDIT_MSG), TRUE);
            EnableWindow(GetDlgItem(hWnd, IDC_BTN_SEND), TRUE);
            AppendChatMessage(GetDlgItem(hWnd, IDC_EDIT_CHAT), _T("Server running, waiting for connection...\r\n"));
        }
        break;

        case IDC_BTN_CLIENT:
        {
            // Если в серверном режиме нельзя подключаться как клиент.
            if (isServerMode)
            {
                MessageBox(hWnd, _T("Cannot connect as client while in server mode."), _T("Info"), MB_ICONINFORMATION);
                break;
            }
            if (commSocket != INVALID_SOCKET)
            {
                MessageBox(hWnd, _T("Already connected to a server."), _T("Info"), MB_ICONINFORMATION);
                break;
            }
            isServerMode = false; // клиентский режим
            TCHAR ip[64], portText[16], nick[64];
            GetWindowText(GetDlgItem(hWnd, IDC_EDIT_IP), ip, 64);
            GetWindowText(GetDlgItem(hWnd, IDC_EDIT_PORT), portText, 16);
            GetWindowText(GetDlgItem(hWnd, IDC_EDIT_NICKNAME), nick, 64);
            int port = _ttoi(portText);
            if (port <= 0)
            {
                MessageBox(hWnd, _T("Invalid port number."), _T("Error"), MB_ICONERROR);
                break;
            }
            SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (sock == INVALID_SOCKET)
            {
                MessageBox(hWnd, _T("Failed to create socket."), _T("Error"), MB_ICONERROR);
                break;
            }
            sockaddr_in serverAddr;
            serverAddr.sin_family = AF_INET;
            serverAddr.sin_port = htons(port);
            if (InetPton(AF_INET, ip, &serverAddr.sin_addr) != 1)
            {
                MessageBox(hWnd, _T("Invalid IP address."), _T("Error"), MB_ICONERROR);
                closesocket(sock);
                break;
            }
            if (connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
            {
                MessageBox(hWnd, _T("Failed to connect to server."), _T("Error"), MB_ICONERROR);
                closesocket(sock);
                break;
            }
            // Если никнейм задан, отправляем серверу сразу сообщение для установки никнейма.
            if (_tcslen(nick) > 0)
            {
                ostringstream oss;
                oss << "NICK:" << string(nick, nick + _tcslen(nick));
                string nickMsg = oss.str();
                send(sock, nickMsg.c_str(), (int)nickMsg.length(), 0);
            }
            commSocket = sock;
            // Разблокируем поле ввода и кнопку Send
            EnableWindow(GetDlgItem(hWnd, IDC_EDIT_MSG), TRUE);
            EnableWindow(GetDlgItem(hWnd, IDC_BTN_SEND), TRUE);
            HANDLE hThread = CreateThread(NULL, 0, RecvThread, (LPVOID)sock, 0, NULL);
            if (hThread)
                CloseHandle(hThread);
            AppendChatMessage(GetDlgItem(hWnd, IDC_EDIT_CHAT), _T("Connected to server.\r\n"));
        }
        break;

        case IDC_BTN_STOP_SERVER:
        {
            if (!isServerMode)
            {
                MessageBox(hWnd, _T("Not running in server mode."), _T("Info"), MB_ICONINFORMATION);
                break;
            }
            if (g_listenSocket != INVALID_SOCKET)
            {
                closesocket(g_listenSocket);
                g_listenSocket = INVALID_SOCKET;
            }
            EnterCriticalSection(&csClients);
            for (size_t i = 0; i < g_clientInfos.size(); i++)
            {
                const char* stop_msg = "Server stopped.";
                send(g_clientInfos[i].sock, stop_msg, (int)strlen(stop_msg), 0);
                closesocket(g_clientInfos[i].sock);
            }
            g_clientInfos.clear();
            LeaveCriticalSection(&csClients);
            EnableWindow(GetDlgItem(hWnd, IDC_EDIT_MSG), FALSE);
            EnableWindow(GetDlgItem(hWnd, IDC_BTN_SEND), FALSE);
            AppendChatMessage(GetDlgItem(hWnd, IDC_EDIT_CHAT), _T("Server stopped.\r\n"));
            isServerMode = false;
            g_nextClientId = 1;
        }
        break;

        case IDC_BTN_DISCONNECT:
        {
            if (isServerMode)
            {
                MessageBox(hWnd, _T("Server cannot disconnect using this button."), _T("Info"), MB_ICONINFORMATION);
                break;
            }
            if (commSocket == INVALID_SOCKET)
            {
                MessageBox(hWnd, _T("Not connected to a server."), _T("Info"), MB_ICONINFORMATION);
                break;
            }
            closesocket(commSocket);
            commSocket = INVALID_SOCKET;
            AppendChatMessage(GetDlgItem(hWnd, IDC_EDIT_CHAT), _T("Disconnected from server.\r\n"));
            EnableWindow(GetDlgItem(hWnd, IDC_EDIT_MSG), FALSE);
            EnableWindow(GetDlgItem(hWnd, IDC_BTN_SEND), FALSE);
        }
        break;

        case IDC_CHECK_BROADCAST:
        {
            if (HIWORD(wParam) == BN_CLICKED)
            {
                if (isServerMode)
                {
                    BOOL checked = IsDlgButtonChecked(hWnd, IDC_CHECK_BROADCAST);
                    g_broadcastEnabled = (checked == BST_CHECKED);
                    if (g_broadcastEnabled)
                        AppendChatMessage(GetDlgItem(hWnd, IDC_EDIT_CHAT), _T("Broadcast enabled: clients see each other's messages.\r\n"));
                    else
                        AppendChatMessage(GetDlgItem(hWnd, IDC_EDIT_CHAT), _T("Broadcast disabled: only server sees client messages.\r\n"));
                }
                else
                {
                    CheckDlgButton(hWnd, IDC_CHECK_BROADCAST, BST_UNCHECKED);
                }
            }
        }
        break;

        case IDC_BTN_SEND:
        {
            TCHAR msg[256];
            GetWindowText(GetDlgItem(hWnd, IDC_EDIT_MSG), msg, 256);
            if (_tcslen(msg) == 0)
                break;

            // Преобразуем сообщение из Unicode в multibyte
            int len = WideCharToMultiByte(CP_ACP, 0, msg, -1, NULL, 0, NULL, NULL);
            char* sendBuffer = (char*)malloc(len);
            WideCharToMultiByte(CP_ACP, 0, msg, -1, sendBuffer, len, NULL, NULL);

            if (isServerMode)
            {
                // Формируем строку "Server: " + <сообщение>
                const char* serverPrefix = "Server: ";
                size_t prefixLen = strlen(serverPrefix);
                size_t messageLen = strlen(sendBuffer);
                size_t totalLen = prefixLen + messageLen + 1;
                char* fullMsg = (char*)malloc(totalLen);
                strcpy(fullMsg, serverPrefix);
                strcat(fullMsg, sendBuffer);

                // Отправляем всем подключённым клиентам эту строку
                EnterCriticalSection(&csClients);
                for (size_t i = 0; i < g_clientInfos.size(); i++)
                {
                    send(g_clientInfos[i].sock, fullMsg, (int)strlen(fullMsg), 0);
                }
                LeaveCriticalSection(&csClients);
                // На сервере показываем сообщение с префиксом "Server: "
                AppendChatMessage(GetDlgItem(hWnd, IDC_EDIT_CHAT), _T("Server: "));
                free(fullMsg);
            }
            else
            {
                if (commSocket != INVALID_SOCKET)
                {
                    send(commSocket, sendBuffer, (int)strlen(sendBuffer), 0);
                    AppendChatMessage(GetDlgItem(hWnd, IDC_EDIT_CHAT), _T("You: "));
                }
            }
            free(sendBuffer);

            // Добавляем само сообщение и перевод строки
            AppendChatMessage(GetDlgItem(hWnd, IDC_EDIT_CHAT), msg);
            AppendChatMessage(GetDlgItem(hWnd, IDC_EDIT_CHAT), _T("\r\n"));
            SetWindowText(GetDlgItem(hWnd, IDC_EDIT_MSG), _T(""));
        }
        break;

        case IDC_BTN_ATTACH:
        {
            // Проверяем, существует ли подключение.
            if ((!isServerMode && commSocket == INVALID_SOCKET) ||
                (isServerMode && g_clientInfos.empty())) {
                MessageBox(hWnd, _T("No connection established."), _T("Error"), MB_ICONERROR);
                break;
            }
            
            // Используем стандартный диалог выбора файла.
            TCHAR filePath[MAX_PATH] = _T("");
            OPENFILENAME ofn = {0};
            ofn.lStructSize = sizeof(OPENFILENAME);
            ofn.hwndOwner = hWnd;
            ofn.lpstrFile = filePath;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrFilter = _T("All Files (*.*)\0*.*\0");
            ofn.lpstrDefExt = _T("*");
            
            if (GetOpenFileName(&ofn)) {
                // Получаем размер файла.
                WIN32_FILE_ATTRIBUTE_DATA fad;
                if (!GetFileAttributesEx(filePath, GetFileExInfoStandard, &fad)) {
                    MessageBox(hWnd, _T("Unable to get file attributes."), _T("Error"), MB_ICONERROR);
                    break;
                }
                LARGE_INTEGER fileSize;
                fileSize.LowPart = fad.nFileSizeLow;
                fileSize.HighPart = fad.nFileSizeHigh;
                
                // Формируем строку с информацией о файле для подтверждения.
                TCHAR infoMsg[512];
                _stprintf(infoMsg, _T("Are you sure you want to send the file?\nName: %ls\nSize: %llu bytes"),
                        filePath, fileSize.QuadPart);
                int confirm = MessageBox(hWnd, infoMsg, _T("Attach File"), MB_YESNO | MB_ICONQUESTION);
                if (confirm == IDYES) {
                    // Дополнительное подтверждение на английском (по желанию):
                    int confirm2 = MessageBox(hWnd, _T("Are you sure you want to send the file?"), _T("Confirmation"), MB_YESNO | MB_ICONQUESTION);
                    if (confirm2 != IDYES)
                        break;
                    
                    // Читаем файл в память.
                    HANDLE hFile = CreateFile(filePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
                    if (hFile == INVALID_HANDLE_VALUE) {
                        MessageBox(hWnd, _T("Failed to open file."), _T("Error"), MB_ICONERROR);
                        break;
                    }
                    DWORD dwFileSize = GetFileSize(hFile, NULL);
                    char* fileBuffer = new char[dwFileSize];
                    DWORD bytesRead;
                    if (!ReadFile(hFile, fileBuffer, dwFileSize, &bytesRead, NULL)) {
                        MessageBox(hWnd, _T("Failed to read file."), _T("Error"), MB_ICONERROR);
                        CloseHandle(hFile);
                        delete[] fileBuffer;
                        break;
                    }
                    CloseHandle(hFile);
                    
                    // Извлекаем базовое имя файла из полного пути.
                    // filePath – это TCHAR*, для UNICODE сборки это wide-строка.
                    std::wstring fullPath(filePath);
                    std::wstring baseName;
                    size_t pos = fullPath.find_last_of(L"\\/");
                    if (pos != std::wstring::npos)
                        baseName = fullPath.substr(pos + 1);
                    else
                        baseName = fullPath;
                    
                    // Подготавливаем заголовок файла.
                    // Новый формат: "FILE_REQ|<basename>|<filesize>"
                    char baseNameA[MAX_PATH];
                    WideCharToMultiByte(CP_ACP, 0, baseName.c_str(), -1, baseNameA, MAX_PATH, NULL, NULL);
                    std::ostringstream oss;
                    oss << "FILE_REQ|" << baseNameA << "|" << dwFileSize;
                    std::string header = oss.str();
                    
                    // Отправляем заголовок всем участникам.
                    if (isServerMode) {
                        EnterCriticalSection(&csClients);
                        for (size_t i = 0; i < g_clientInfos.size(); i++) {
                            send(g_clientInfos[i].sock, header.c_str(), (int)header.length(), 0);
                        }
                        LeaveCriticalSection(&csClients);
                    } else {
                        send(commSocket, header.c_str(), (int)header.length(), 0);
                    }
                    
                    // Здесь можно добавить ожидание подтверждения от получателя(ей)
                    // Для простоты отправляем содержимое файла сразу.
                    if (isServerMode) {
                        EnterCriticalSection(&csClients);
                        for (size_t i = 0; i < g_clientInfos.size(); i++) {
                            send(g_clientInfos[i].sock, fileBuffer, dwFileSize, 0);
                        }
                        LeaveCriticalSection(&csClients);
                    } else {
                        send(commSocket, fileBuffer, dwFileSize, 0);
                    }
                    
                    delete[] fileBuffer;
                }
            }
        }
        break;


        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
            }
    }
    break;

    case WM_SOCKET_MESSAGE:
    {
        TCHAR* newMsg = (TCHAR*)lParam;
        AppendChatMessage(GetDlgItem(hWnd, IDC_EDIT_CHAT), newMsg);
        free(newMsg);
    }
    break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

//*****************************************************************************
// ServerThread: запускает слушающий сокет и принимает подключения

DWORD WINAPI ServerThread(LPVOID lpParam)
{
    int port = *(int*)lpParam;
    free(lpParam);

    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET)
        return 1;

    int optVal = 1;
    setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&optVal, sizeof(optVal));

    g_listenSocket = listenSocket;

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);

    if (bind(listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
    {
        closesocket(listenSocket);
        g_listenSocket = INVALID_SOCKET;
        MessageBox(hMainWnd, _T("Bind failed."), _T("Error"), MB_ICONERROR);
        return 1;
    }

    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR)
    {
        closesocket(listenSocket);
        g_listenSocket = INVALID_SOCKET;
        MessageBox(hMainWnd, _T("Listen failed."), _T("Error"), MB_ICONERROR);
        return 1;
    }

    // Цикл принятия клиентов
    while (true)
    {
        SOCKET client = accept(listenSocket, NULL, NULL);
        if (client == INVALID_SOCKET)
            break; // если сокет закрыт (например, при Stop Server)

        // Создаем новую запись для клиента
        ClientInfo ci;
        ci.sock = client;
        ci.id = g_nextClientId++;
        EnterCriticalSection(&csClients);
        g_clientInfos.push_back(ci);
        LeaveCriticalSection(&csClients);

        // Информируем сервер о подключении клиента
        {
            wstring s = L"Client #" + to_wstring(ci.id) + L" connected.\r\n";
            PostMessage(hMainWnd, WM_SOCKET_MESSAGE, 0, (LPARAM)_tcsdup(s.c_str()));
        }

        HANDLE hThread = CreateThread(NULL, 0, RecvThread, (LPVOID)client, 0, NULL);
        if (hThread)
            CloseHandle(hThread);
    }
    g_listenSocket = INVALID_SOCKET;
    return 0;
}

std::wstring ConvertToWide(const std::string &s) {
    int size_needed = MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, nullptr, 0);
    if (size_needed <= 0) {
        return std::wstring();
    }
    std::wstring wstr(size_needed, 0);
    MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, &wstr[0], size_needed);
    wstr.resize(size_needed - 1);
    return wstr;
}

//*****************************************************************************
// RecvThread: обрабатывает входящие сообщения
// Для серверного режима – получает сообщения от клиента, формирует сообщение с префиксом и при необходимости пересылает его.
DWORD WINAPI RecvThread(LPVOID lpParam)
{
    SOCKET sock = (SOCKET)lpParam;
    char buffer[512];
    int bytesReceived;

    while ((bytesReceived = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytesReceived] = '\0';

        // В начале цикла обработки входящих сообщений в RecvThread:
        std::wstring wBuffer = ConvertToWide(buffer);
        if (wcsncmp(wBuffer.c_str(), L"FILE_REQ|", 9) == 0) {  // если данные заголовка в wide‑строке
            // Разбираем header из исходного ANSI‑буфера
            char* token = strtok(buffer, "|");       // должен быть "FILE_REQ"
            token = strtok(nullptr, "|");            // базовое имя файла, например "2.cpp"
            std::string fileName(token ? token : "");
            token = strtok(nullptr, "|");            // размер файла
            long fileSize = token ? atol(token) : 0;
            
            // Формируем запрос для пользователя
            std::wostringstream woss;
            woss << L"Do you want to receive file " << ConvertToWide(fileName)
                << L" (" << fileSize << L" bytes)?";
            int answer = MessageBoxW(hMainWnd, woss.str().c_str(),
                                    L"File Transfer Request",
                                    MB_YESNO | MB_ICONQUESTION);
            
            if (answer == IDYES) {
                // Пользователь согласен принимать файл.
                // Выбор папки для сохранения
                BROWSEINFO bi = {0};
                bi.hwndOwner = hMainWnd;
                bi.lpszTitle = L"Select destination folder";
                LPITEMIDLIST pidl = SHBrowseForFolder(&bi);
                if (pidl != nullptr) {
                    // Пользователь выбрал папку
                    wchar_t folderPath[MAX_PATH];
                    SHGetPathFromIDListW(pidl, folderPath);
                    CoTaskMemFree(pidl);
                    
                    int len = (int)wcslen(folderPath);
                    if (len == 1 || wcschr(folderPath, L':') == nullptr) {
                        wchar_t tmp[MAX_PATH];
                        _snwprintf(tmp, MAX_PATH, L"%s:\\", folderPath);
                        wcscpy_s(folderPath, tmp);
                    } else {
                        if (folderPath[len - 1] != L'\\')
                            wcscat_s(folderPath, L"\\");
                    }
                    
                    // Преобразуем fileName в wide‑строку
                    std::wstring wFileName = ConvertToWide(fileName);
                    
                    // Формируем итоговый путь к файлу
                    wchar_t targetFilePath[MAX_PATH];
                    _snwprintf(targetFilePath, MAX_PATH, L"%s%s", folderPath, wFileName.c_str());
                    
                    // Открываем файл для записи
                    HANDLE hFile = CreateFileW(targetFilePath, GENERIC_WRITE, 0, nullptr,
                                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                    if (hFile == INVALID_HANDLE_VALUE) {
                        DWORD err = GetLastError();
                        wchar_t errMsg[256];
                        _snwprintf(errMsg, 256, L"Failed to create file for saving. Error code: %lu", err);
                        MessageBoxW(hMainWnd, errMsg, L"Error", MB_ICONERROR);
                        
                        // Если не удалось создать файл, сбрасываем оставшиеся fileSize байт
                        long remaining = fileSize;
                        char dummyBuffer[1024];
                        while (remaining > 0) {
                            int toRead = (int)std::min((long)1024, remaining);
                            int r = recv(sock, dummyBuffer, toRead, 0);
                            if (r <= 0)
                                break;
                            remaining -= r;
                        }
                        continue;
                    }
                    
                    // Цикл приёма fileSize байт из сокета и запись в файл
                    long totalReceived = 0;
                    char fileBuffer[1024];
                    DWORD bytesWritten = 0;
                    while (totalReceived < fileSize) {
                        int toRecv = (int)std::min((long)1024, fileSize - totalReceived);
                        int recvLen = recv(sock, fileBuffer, toRecv, 0);
                        if (recvLen <= 0) {
                            MessageBoxW(hMainWnd, L"Connection interrupted during file transfer.",
                                        L"Error", MB_ICONERROR);
                            break;
                        }
                        if (!WriteFile(hFile, fileBuffer, recvLen, &bytesWritten, nullptr)) {
                            MessageBoxW(hMainWnd, L"Error writing file.", L"Error", MB_ICONERROR);
                            break;
                        }
                        totalReceived += recvLen;
                    }
                    CloseHandle(hFile);
                    
                    if (totalReceived == fileSize)
                        MessageBoxW(hMainWnd, L"File received and saved successfully.",
                                    L"Info", MB_ICONINFORMATION);
                    else
                        MessageBoxW(hMainWnd, L"File transfer incomplete.",
                                    L"Error", MB_ICONERROR);
                }
                else {
                    // Пользователь отменил выбор папки
                    MessageBoxW(hMainWnd, L"Folder selection cancelled. File transfer aborted.",
                                L"Info", MB_ICONINFORMATION);
                    
                    // Сбрасываем оставшиеся fileSize байт, чтобы данные из файла не попали в общий обработчик
                    long remaining = fileSize;
                    char dummyBuffer[1024];
                    while (remaining > 0) {
                        int toRead = (int)std::min((long)1024, remaining);
                        int r = recv(sock, dummyBuffer, toRead, 0);
                        if (r <= 0)
                            break;
                        remaining -= r;
                    }
                }
            }
            else {
                // Пользователь отклонил запрос приема файла
                MessageBoxW(hMainWnd, L"File transfer declined.", L"Info", MB_ICONINFORMATION);
                
                // Сбрасываем оставшиеся fileSize байт, чтобы данные не попали в общий обработчик
                long remaining = fileSize;
                char dummyBuffer[1024];
                while (remaining > 0) {
                    int toRead = (int)std::min((long)1024, remaining);
                    int r = recv(sock, dummyBuffer, toRead, 0);
                    if (r <= 0)
                        break;
                    remaining -= r;
                }
            }
            continue;  // Пропускаем дальнейшую обработку этого сообщения
        }
            
        // Если сообщение начинается с "NICK:" и работаем в серверном режиме, обновляем никнейм
        if (isServerMode && strncmp(buffer, "NICK:", 5) == 0) {
            const char* nickStr = buffer + 5; // пропускаем "NICK:"
            int nickWSize = MultiByteToWideChar(CP_ACP, 0, nickStr, -1, NULL, 0);
            wchar_t* nickW = new wchar_t[nickWSize];
            MultiByteToWideChar(CP_ACP, 0, nickStr, -1, nickW, nickWSize);
            EnterCriticalSection(&csClients);
            for (size_t i = 0; i < g_clientInfos.size(); i++) {
                if (g_clientInfos[i].sock == sock) {
                    g_clientInfos[i].nickname = nickW;
                    // Для наглядности можно оповестить сервер об изменении
                    wstring updateMsg = L"Client #" + to_wstring(g_clientInfos[i].id) +
                                        L" is now known as " + g_clientInfos[i].nickname + L".\r\n";
                    PostMessage(hMainWnd, WM_SOCKET_MESSAGE, 0, (LPARAM)_tcsdup(updateMsg.c_str()));
                    break;
                }
            }
            LeaveCriticalSection(&csClients);
            delete[] nickW;
            continue;  // этот пакет не обрабатываем как обычное сообщение
        }

        if (isServerMode) {
            // Перед каждым сообщением перечитываем актуальные данные для данного клиента
            int clientId = -1;
            wstring clientNick = L"";
            EnterCriticalSection(&csClients);
            for (size_t i = 0; i < g_clientInfos.size(); i++) {
                if (g_clientInfos[i].sock == sock) {
                    clientId = g_clientInfos[i].id;
                    clientNick = g_clientInfos[i].nickname;
                    break;
                }
            }
            LeaveCriticalSection(&csClients);

            // Формируем префикс:
            // Если ник задан, используем "Client #<id> (<nickname>): ", иначе "Client #<id>: "
            wstring prefix;
            if (clientNick.empty())
                prefix = L"Client #" + to_wstring(clientId) + L": ";
            else
                prefix = L"Client #" + to_wstring(clientId) + L" (" + clientNick + L"): ";

            int wsize = MultiByteToWideChar(CP_ACP, 0, buffer, -1, NULL, 0);
            wchar_t* wMsg = new wchar_t[wsize];
            MultiByteToWideChar(CP_ACP, 0, buffer, -1, wMsg, wsize);
            wstring displayMsg = prefix + wMsg;
            // Гарантируем наличие перевода строки в конце
            if (displayMsg.back() != L'\n')
                displayMsg += L"\r\n";
            delete[] wMsg;

            // Выводим сообщение на сервере
            PostMessage(hMainWnd, WM_SOCKET_MESSAGE, 0, (LPARAM)_tcsdup(displayMsg.c_str()));

            // При включённом режиме broadcast рассылаем это сообщение другим клиентам
            if (g_broadcastEnabled) {
                ostringstream oss;
                string senderMb(prefix.begin(), prefix.end());
                oss << senderMb << buffer << "\r\n";
                string broadcastMsg = oss.str();
                EnterCriticalSection(&csClients);
                for (size_t i = 0; i < g_clientInfos.size(); i++) {
                    if (g_clientInfos[i].sock != sock) {
                        send(g_clientInfos[i].sock, broadcastMsg.c_str(), (int)broadcastMsg.length(), 0);
                    }
                }
                LeaveCriticalSection(&csClients);
            }
        }
        else { // клиентский режим
            int wlen = MultiByteToWideChar(CP_ACP, 0, buffer, -1, NULL, 0);
            wchar_t* wStr = new wchar_t[wlen];
            MultiByteToWideChar(CP_ACP, 0, buffer, -1, wStr, wlen);
            // Если сообщение уже начинается с "Client #", считаем, что нужно его выводить как есть
            if (wcsncmp(wStr, L"Client #", 8) == 0)
            {
                PostMessage(hMainWnd, WM_SOCKET_MESSAGE, 0, (LPARAM)wStr);
            }
            else {
                // Иначе — это сообщение от сервера; не добавляем лишний префикс "Server:"
                wstring newMsg = wStr;
                if (newMsg.back() != L'\n')
                    newMsg += L"\r\n";
                PostMessage(hMainWnd, WM_SOCKET_MESSAGE, 0, (LPARAM)_tcsdup(newMsg.c_str()));
                delete[] wStr;
            }
        }
    }
    
    // Обработка разрыва соединения
    if (!isServerMode) {
        PostMessage(hMainWnd, WM_SOCKET_MESSAGE, 0, (LPARAM)_tcsdup(_T("Disconnected from server.\r\n")));
        commSocket = INVALID_SOCKET;
        EnableWindow(GetDlgItem(hMainWnd, IDC_EDIT_MSG), FALSE);
        EnableWindow(GetDlgItem(hMainWnd, IDC_BTN_SEND), FALSE);
    }
    else {
        int disconnectedId = -1;
        EnterCriticalSection(&csClients);
        for (vector<ClientInfo>::iterator it = g_clientInfos.begin(); it != g_clientInfos.end(); ++it) {
            if (it->sock == sock) {
                disconnectedId = it->id;
                g_clientInfos.erase(it);
                break;
            }
        }
        LeaveCriticalSection(&csClients);
        wstring discMsg = L"Client #" + to_wstring(disconnectedId) + L" disconnected.\r\n";
        PostMessage(hMainWnd, WM_SOCKET_MESSAGE, 0, (LPARAM)_tcsdup(discMsg.c_str()));
    }
    
    closesocket(sock);
    return 0;
}
