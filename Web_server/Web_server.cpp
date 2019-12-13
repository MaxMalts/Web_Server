#pragma comment(linker, "/STACK:1048576")

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <winsock2.h>
#pragma comment(lib, "Ws2_32.lib")

int Initialize() {
	WSADATA wsConf;
	int err = WSAStartup(MAKEWORD(1, 1), &wsConf);
	if (err != NO_ERROR) {
		fprintf(stderr, "(ERROR) WSAStartup() error: %d\n", err);
		return 1;
	}

	return 0;
}

hostent* GetCurHost() {
	char hostName[100] = "";
if (gethostname(hostName, 100) == SOCKET_ERROR) {
	fprintf(stderr, "(ERROR) gethostname() error: %d\n", WSAGetLastError());
	return NULL;
}
hostent* host = gethostbyname(hostName);
if (host == NULL) {
	fprintf(stderr, "(ERROR) gethostbyname() error: %d\n", WSAGetLastError());
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
		fprintf(stderr, "(ERROR) Error when creating the listen socket: %d\n", WSAGetLastError());
		return INVALID_SOCKET;
	}

	if (bind(listenSock, (sockaddr*)& listenAddr, sizeof(listenAddr)) == SOCKET_ERROR) {
		fprintf(stderr, "(ERROR) Bind listen socket error: %d\n", WSAGetLastError());
		return INVALID_SOCKET;
	}

	if (listen(listenSock, 1) == SOCKET_ERROR) {
		fprintf(stderr, "(ERROR) Listen socket connection error: %d\n", WSAGetLastError());
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
				fprintf(stderr, "(ERROR) Accepting connection timed out. Trying to reconnect...\n");
				continue;
			}
			fprintf(stderr, "(ERROR) Accepting connection error: %d\n", WSAGetLastError());
			return INVALID_SOCKET;
		}

		DWORD timeout = 3 * 1000;
		if (setsockopt(resSock, SOL_SOCKET, SO_RCVTIMEO, (char*)& timeout, sizeof(timeout)) == SOCKET_ERROR) {
			fprintf(stderr, "(ERROR) Socket timeout configuration error: %d\n", WSAGetLastError());
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
		fprintf(stderr, "(ERROR) Connection gracefully closed (while recv attemp)\n");
		return 0;
	}
	else if (recvLen == SOCKET_ERROR) {
		if (WSAGetLastError() == WSAETIMEDOUT) {
			fprintf(stderr, "(ERROR) Receive timed out\n");
			return 0;
		}
		else {
			fprintf(stderr, "(ERROR) Error while receiving attemp: %d\n", WSAGetLastError());
			return -1;
		}
	}

	return recvLen;
}

int SendData(SOCKET clientSock, char* buf, int len) {
	assert(clientSock != INVALID_SOCKET);
	assert(buf != NULL);
	assert(len > 0);

	if (send(clientSock, buf, len, 0) == SOCKET_ERROR) {
		fprintf(stderr, "(ERROR) Error while sending attemp: %d\n", WSAGetLastError());
		return 1;
	}

	return 0;
}

int DetContType(char* fName, char* contType) {
	assert(fName != NULL);
	assert(contType != NULL);

	int fNameLen = strlen(fName);
	char* lastCh = &fName[fNameLen - 1];
	if (fNameLen > 5) {
		if (strcmp(lastCh - 4, ".html") == 0) {
			strcpy(contType, "text/html; charset=utf-8");
			return 0;
		}
	}
	if (fNameLen > 4) {
		if (strcmp(lastCh - 4, ".css") == 0) {
			strcpy(contType, "text/css; charset=utf-8");
			return 0;
		}
		if (strcmp(lastCh - 3, ".ico") == 0) {
			strcpy(contType, "image/x-icon");
			return 0;
		}
		if (strcmp(lastCh - 3, ".jpg") == 0 || strcmp(lastCh - 3, ".jpegg") == 0) {
			strcpy(contType, "image/jpeg");
			return 0;
		}
		if (strcmp(lastCh - 3, ".png") == 0) {
			strcpy(contType, "image/png");
			return 0;
		}
		if (strcmp(lastCh - 4, ".gif") == 0) {
			strcpy(contType, "text/gif");
			return 0;
		}
	}

	return 1;
}

int CreateSendBuf(char* fSendName, char* buf, int bufLen) {
	assert(fSendName != NULL);
	assert(buf != NULL);
	assert(bufLen > 0);

	const int fBufMaxSize = 50000;

	FILE* fSend = fopen(fSendName, "rb");
	if (fSend == NULL) {
		fprintf(stderr, "(ERROR) Open %s file error: %d (%s)\n", fSendName, errno, strerror(errno));
		return -1;
	}

	char contType[100] = "";
	if (DetContType(fSendName, contType) == 1) {
		fprintf(stderr, "(ERROR) Didn't determine content type of file %s\n", fSendName);
		return -1;
	}

	char headBuf[300] = "";
	int headLen = sprintf(headBuf, "HTTP/1.1 200 OK\r\nContent-type: %s", contType);

	char fBuf[fBufMaxSize] = "";
	int fBufLen = fread(fBuf, sizeof(char), fBufMaxSize, fSend);
	assert(fBufLen < fBufMaxSize);
	fclose(fSend);

	if (headLen + fBufLen >= bufLen) {
		fprintf(stderr, "(ERROR) Buffer length too small: header length: %d, "
			    "file length: %d, buffer length: %d\n", headLen, fBufLen, bufLen);
		return -1;
	}

	sprintf(buf, "%s\r\n\r\n", headBuf);
	memcpy(&buf[headLen  + 4], fBuf, fBufLen);

	return headLen + 4 + fBufLen;
}

int InteractClient(SOCKET clientSock) {
	assert(clientSock != INVALID_SOCKET);

	const int fReqNameMaxSize = 300;

	char buf[50000] = "";
	printf("Receiving data...\n");
	int bufLen = ReceiveData(clientSock, buf, sizeof(buf) - 1);
	if (bufLen <= 0) {
		return 1;
	}
	printf("Data received successfully:\n");
	fwrite(buf, sizeof(char), bufLen, stdout);
	
	if (strncmp(buf, "GET ", 4) == 0) {
		bufLen = 0;
		assert(buf[4] == '/');

		char fReqName[fReqNameMaxSize] = "";
		if (buf[5] == ' ') {
			strcpy(fReqName, "Page.html");
		}
		else {
			char* space = strchr(&buf[4], ' ');
			int fReqNameSize = space - &buf[5];
			assert(fReqNameSize > 0 && fReqNameSize < fReqNameMaxSize);

			strncpy(fReqName, &buf[5], fReqNameSize);
		}

		bufLen = CreateSendBuf(fReqName, buf, sizeof(buf) - 1);
		if (bufLen == -1) {
			return 1;
		}

		printf("\nSending data:\n");
		fwrite(buf, sizeof(char), bufLen, stdout);
		if (SendData(clientSock, buf, bufLen) == 1) {
			return 1;
		}
		printf("\nData sent.\n");
	}
	else {
		fprintf(stderr, "(ERROR) Didn't receive GET method\n");
	}

	return 0;
}

int EndConnection(SOCKET clientSock) {
	assert(clientSock != INVALID_SOCKET);

	if (shutdown(clientSock, SD_BOTH) == SOCKET_ERROR) {
		fprintf(stderr, "(ERROR) shutdown() error: %d", WSAGetLastError());
		return SOCKET_ERROR;
	}
	if (closesocket(clientSock) == SOCKET_ERROR) {
		fprintf(stderr, "(ERROR) closesocket() error: %d", WSAGetLastError());
		return SOCKET_ERROR;
	}
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
		closesocket(listenSock);
		return 1;
	}
	printf("Listening socket successfully created.\n\n\n");

	while (1) {
		sockaddr_in clientAddr = {AF_INET, 0};
		printf("Waiting for incoming connection...\n");
		SOCKET clientSock = AcceptConnection(listenSock, (sockaddr*)&clientAddr);
		if (clientSock == INVALID_SOCKET) {
			closesocket(listenSock);
			return 1;
		}
		printf("Connected to %s.\n\n", inet_ntoa(clientAddr.sin_addr));

		printf("Interacting with client:\n");
		InteractClient(clientSock);
		printf("Ended interaction.\n\n");

		printf("Closing connection...\n");
		if (EndConnection(clientSock) == SOCKET_ERROR) {
			closesocket(listenSock);
			return 1;
		}
		printf("Connection closed successfully.\n\n");
	}
}

int main() {

	int err = 0;
	while (1) {
		err = StartServer();
		if (err != 0) {
			printf("Restarting...\n");
		}
	}

	return err;
}