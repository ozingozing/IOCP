#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <ws2tcpip.h>  // inet_pton() 사용을 위해 필요

#pragma comment(lib, "Ws2_32.lib")

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 5150

int main(int argc, char** argv)
{
    WSADATA wsaData;
    SOCKET clientSocket;
    SOCKADDR_IN serverAddr;
    char buffer[1024];
    int bytesReceived;

    if (argc != 3)
    {
        printf("Usage: %s <ServerIp> <Port>\n", argv[0]);
        return 1;
    }
    // IP 주소와 포트 번호 가져오기
    const char* serverIP = argv[1];         // 첫 번째 인자: 서버 IP
    int serverPort = atoi(argv[2]);         // 두 번째 인자: 포트 (문자열 → 정수 변환)

    // WinSock 초기화
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        printf("WSAStartup failed: %d\n", WSAGetLastError());
        return 1;
    }

    // 소켓 생성
    clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket == INVALID_SOCKET)
    {
        printf("Socket creation failed: %d\n", WSAGetLastError());
        return 1;
    }

    // 서버 정보 설정
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(serverPort);
    if (inet_pton(AF_INET, serverIP, &serverAddr.sin_addr) <= 0)
    {
        printf("Invalid address/ Address not supported\n");
        return 1;
    }

    // 서버에 연결 요청
    if (connect(clientSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
    {
        printf("Connection failed: %d\n", WSAGetLastError());
        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }

    printf("Connected to server!\n");

    // 데이터 전송
    const char* message = "Hello World Server!!!!!!!";
    send(clientSocket, message, (int)strlen(message), 0);

    // 서버 응답 받기
    bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    if (bytesReceived > 0)
    {
        buffer[bytesReceived] = '\0';
        printf("Server Response: %s\n", buffer);
    }

    // 소켓 닫기
    closesocket(clientSocket);
    WSACleanup();

    return 0;
}