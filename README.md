# client-server-chat

Реализована передача по локальной сети текста или же клиент-серверной чат. В программе есть кнопка с созданием сервера, также кнопка с подключением к серверу,  строки для ввода ip-адреса и порта.
<br><br>
Компиляция:
<br><br>
g++ -DUNICODE -D_UNICODE -DWIN32 -D_WINDOWS -D_WIN32_WINNT=0x0600 -o 1.exe 1.cpp -lshell32 -lole32 -lws2_32 -mwindows
<br><br>
1.exe
<br><br>
PAUSE