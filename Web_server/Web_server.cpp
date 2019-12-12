#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <winsock2.h>
#pragma comment(lib, "Ws2_32.lib")

int Initialize() {
	WSADATA wsConf;
	int err = WSAStartup(MAKEWORD(1, 1), &wsConf);
	if (err != NO_ERROR) {
		fprintf(stderr, "WSAStartup() error: %d\n", err);
		return 1;
	}

	return 0;
}

hostent* GetCurHost() {
	char hostName[100] = "";
if (gethostname(hostName, 100) == SOCKET_ERROR) {
	fprintf(stderr, "gethostname() error: %d\n", WSAGetLastError());
	return NULL;
}
hostent* host = gethostbyname(hostName);
if (host == NULL) {
	fprintf(stderr, "gethostbyname() error: %d\n", WSAGetLastError());
	return NULL;
}

return host;
}

sockaddr_in GetListenAddr_in(hostent* host, int port) {
	assert(host != NULL);

	sockaddr_in listenAddr = {};
	listenAddr.sin_family = AF_INET;
	memcpy(&listenAddr.sin_addr, host->h_addr, host->h_length);
	listenAddr.sin_port = htons(port);

	return listenAddr;
}

SOCKET InitializeListenSock(sockaddr_in listenAddr) {
	SOCKET listenSock = socket(AF_INET, SOCK_STREAM, 0);

	if (listenSock == INVALID_SOCKET) {
		fprintf(stderr, "Error when creating the listen socket: %d\n", WSAGetLastError());
		return INVALID_SOCKET;
	}

	if (bind(listenSock, (sockaddr*)& listenAddr, sizeof(listenAddr)) == SOCKET_ERROR) {
		fprintf(stderr, "Bind listen socket error: %d\n", WSAGetLastError());
		return INVALID_SOCKET;
	}

	if (listen(listenSock, 1) == SOCKET_ERROR) {
		fprintf(stderr, "Listen socket connection error: %d\n", WSAGetLastError());
		return INVALID_SOCKET;
	}

	return listenSock;
}

SOCKET GetListenSock() {
	hostent* host = GetCurHost();
	if (host == NULL) {
		return INVALID_SOCKET;
	}

	sockaddr_in listenAddr = GetListenAddr_in(host, 80);

	//printf("%s", inet_ntoa(listenAddr.sin_addr));

	SOCKET listenSock = InitializeListenSock(listenAddr);

	return listenSock;
}

SOCKET AcceptConnection(SOCKET listenSock, sockaddr* clientAddr = NULL, int* addrLen = NULL) {
	assert(listenSock != INVALID_SOCKET);

	int sockaddrLen = sizeof(sockaddr);
	if (addrLen == NULL) {
		addrLen = &sockaddrLen;
	}

	SOCKET resSock = {};
	while (1) {
		resSock = accept(listenSock, clientAddr, addrLen);
		if (resSock == INVALID_SOCKET) {
			if (WSAGetLastError() == WSAETIMEDOUT) {
				fprintf(stderr, "Accepting connection timed out. Trying to reconnect...\n");
				continue;
			}
			fprintf(stderr, "Accepting connection error: %d\n", WSAGetLastError());
			return INVALID_SOCKET;
		}
		break;
	}

	return resSock;
}

int ReceiveData(SOCKET clientSock, char* buf, int len) {
	assert(clientSock != INVALID_SOCKET);
	assert(buf != NULL);
	assert(len > 0);

	int recvLen = recv(clientSock, buf, len, 0);
	assert(recvLen < len);
	if (recvLen == 0) {
		fprintf(stderr, "Connection gracefully closed (while recv attemp)\n");
	}
	else if (recvLen == SOCKET_ERROR) {
		fprintf(stderr, "Error while receiving attemp: %d\n", WSAGetLastError());
		return -1;
	}

	return recvLen;
}

int SendData(SOCKET clientSock, char* buf, int len) {
	assert(clientSock != INVALID_SOCKET);
	assert(buf != NULL);
	assert(len > 0);

	if (send(clientSock, buf, len, 0) == SOCKET_ERROR) {
		fprintf(stderr, "Error while sending attemp: %d\n", WSAGetLastError());
		return 1;
	}

	return 0;
}

int CreateHtmlBuf(char* htmlFName, char* buf, int bufLen) {
	assert(htmlFName != NULL);
	assert(buf != NULL);
	assert(bufLen > 0);

	const int htmlFMaxSize = 5000;

	FILE* htmlF = fopen(htmlFName, "rb");
	if (htmlF == NULL) {
		fprintf(stderr, "Open %s file error: %d (%s)\n", htmlFName, errno, strerror(errno));
		return 1;
	}

	char headBuf[] = "HTTP/1.1 200 OK\r\nContent-type: text/html; charset=utf-8";

	char htmlBuf[htmlFMaxSize] = "";
	int htmlLen = fread(htmlBuf, sizeof(char), htmlFMaxSize, htmlF);
	assert(htmlLen < htmlFMaxSize);
	fclose(htmlF);

	if (sizeof(headBuf) + htmlLen >= bufLen) {
		fprintf(stderr, "Buffer length too small: header length: %d, "
			    "html file length: %d, buffer length: %d\n", sizeof(headBuf), htmlLen, bufLen);
		return 1;
	}

	sprintf(buf, "%s\r\n\r\n", headBuf);
	memcpy(&buf[(sizeof(headBuf) - 1) + 4], htmlBuf, htmlLen);

	return (sizeof(headBuf) - 1) + 4 + htmlLen;
}

int CreateFaviconBuf(char* favicFName, char* buf, int bufLen) {
	assert(favicFName != NULL);
	assert(buf != NULL);
	assert(bufLen > 0);

	const int favicFMaxSize = 1000;

	FILE* favicF = fopen(favicFName, "rb");
	if (favicF == NULL) {
		fprintf(stderr, "Open %s file error: %d (%s)\n", favicFName, errno, strerror(errno));
		return -1;
	}

	char headBuf[] = "HTTP/1.1 200 OK\r\nContent-type: image/x-icon";

	char favicBuf[favicFMaxSize] = "";
	int favicLen = fread(favicBuf, sizeof(char), favicFMaxSize, favicF);
	assert(favicLen < favicFMaxSize);
	fclose(favicF);

	if (sizeof(headBuf) + favicLen >= bufLen) {
		fprintf(stderr, "Buffer length too small: header length: %d, "
			"favicon file length: %d, buffer length: %d\n", sizeof(headBuf), favicLen, bufLen);
		return -1;
	}

	sprintf(buf, "%s\r\n\r\n", headBuf);
	memcpy(&buf[(sizeof(headBuf) - 1) + 4], favicBuf, favicLen);

	return (sizeof(headBuf) - 1) + 4 + favicLen;
}

int InteractClient(SOCKET clientSock) {
	assert(clientSock != INVALID_SOCKET);

	char buf[5000] = "";
	printf("Receiving data...\n");
	int bufLen = ReceiveData(clientSock, buf, sizeof(buf) - 1);
	if (bufLen <= 0) {
		return 1;
	}
	printf("Data received successfully:\n");
	fwrite(buf, sizeof(char), bufLen, stdout);
	
	if (strncmp(buf, "GET ", 4) == 0) {
		bufLen = 0;
		if (strncmp(&buf[4], "/favicon.ico", 12) == 0) {
			bufLen = CreateFaviconBuf((char*)"favicon.ico", buf, sizeof(buf) - 1);
			if (bufLen == -1) {
				return 1;
			}
		}
		else {
			bufLen = CreateHtmlBuf((char*)"Page.html", buf, sizeof(buf) - 1);
			if (bufLen == -1) {
				return 1;
			}
		}

		printf("\nSending data:\n");
		fwrite(buf, sizeof(char), bufLen, stdout);
		if (SendData(clientSock, buf, bufLen) == 1) {
			return 1;
		}
		printf("\nData sent.\n");
	}
	else {
		fprintf(stderr, "Didn't receive GET method\n");
	}

	return 0;
}

int StartServer() {
	printf("Initializing...\n");
	int err = Initialize();
	if (err != 0) {
		return 1;
	}
	printf("Initialized.\n\n");

	printf("Creating listening socket...\n");
	SOCKET listenSock = GetListenSock();
	if (listenSock == INVALID_SOCKET) {
		return 1;
	}
	printf("Listening socket successfully created.\n\n\n");

	while (1) {
		sockaddr_in clientAddr = {AF_INET, 0};
		printf("Waiting for incoming connection...\n");
		SOCKET clientSock = AcceptConnection(listenSock, (sockaddr*)&clientAddr);
		if (clientSock == INVALID_SOCKET) {
			return 1;
		}
		printf("Connected to %s.\n\n", inet_ntoa(clientAddr.sin_addr));

		printf("Interacting with client:\n");
		InteractClient(clientSock);
		printf("Ended interaction.\n\n");

		printf("Closing connection...\n");
		if (closesocket(clientSock) == SOCKET_ERROR) {
			fprintf(stderr, "closesocket() error: %d", WSAGetLastError());
			return 1;
		}
		printf("Connection closed successfully.\n\n");
	}
}

int main() {

	int err = StartServer();

	getchar();
	return err;
}