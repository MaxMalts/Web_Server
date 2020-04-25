#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "Content_types.h"
#include <winsock2.h>
#pragma comment(lib, "Ws2_32.lib")

#ifdef _DEBUG
#define DEBUG_CODE(code) code
#else
#define DEBUG_CODE(code) 
#endif


struct server_properties {
	char ipAddr[100] = "auto";
	int port = 80;

	char siteRootFolder[10000] = "Site-Root";
	char homePage[10000] = "index.html";

	int soundConnection = 0;
};


long long FileSize(FILE* file) {
	assert(file != NULL);

	long long backupPos = ftell(file);

	fseek(file, 0, SEEK_END);
	long long res = ftell(file);
	fseek(file, backupPos, SEEK_SET);

	return res;
}



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

sockaddr_in GetListenAddrFromHost(hostent* host, int port) {
	assert(host != NULL);

	sockaddr_in listenAddr = {};
	listenAddr.sin_family = AF_INET;
	memcpy(&listenAddr.sin_addr, host->h_addr, host->h_length);
	//listenAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
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

SOCKET GetListenSock(server_properties props) {
	sockaddr_in listenAddr = {};

	if (strcmp(props.ipAddr, "auto") == 0) {
		hostent* host = GetCurHost();
		if (host == NULL) {
			return INVALID_SOCKET;
		}

		listenAddr = GetListenAddrFromHost(host, props.port);
	}
	else {
		listenAddr = {};
		listenAddr.sin_family = AF_INET;
		listenAddr.sin_addr.s_addr = inet_addr(props.ipAddr);
		listenAddr.sin_port = htons(props.port);
	}

	DEBUG_CODE(printf("Server IP: %s; port: %d\n", inet_ntoa(listenAddr.sin_addr), ntohs(listenAddr.sin_port)));

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

	const int chunkSize = 2 * 1024;

	int NChunks = len / chunkSize + 1;
	int i = 0;
	char* cursor = buf;
	while (len >= chunkSize) {
		if (send(clientSock, cursor, chunkSize, 0) == SOCKET_ERROR) {
			fprintf(stderr, "(ERROR) Error while sending attemp: %d\n", WSAGetLastError());
			return 1;
		}
		len -= chunkSize;
		cursor += chunkSize;

		++i;
		if (i % 300 == 0) {
			printf("%.1f%% sent\n", (float)i / NChunks * 100);
		}
	}
	if (send(clientSock, cursor, len, 0) == SOCKET_ERROR) {
		fprintf(stderr, "(ERROR) Error while sending attemp: %d\n", WSAGetLastError());
		return 1;
	}
	printf("100.0%% sent\n");


	return 0;
}

int DetContType(char* fName, char* contType) {
	assert(fName != NULL);
	assert(contType != NULL);

	char tFName[10000] = "";
	strcpy(tFName, fName);
	strlwr(tFName);

	int fNameLen = strlen(tFName);
	char* lastCh = &tFName[fNameLen - 1];
	for (int i = 0; i < content_matches.size; ++i) {
		int curExtensionSize = strlen(content_matches.contents[i].fExtension);

		if (fNameLen >= curExtensionSize &&
			strcmp(lastCh - curExtensionSize + 1, content_matches.contents[i].fExtension) == 0) {

			strcpy(contType, content_matches.contents[i].contentType);
			return 0;
		}
	}

	return 1;
}

int CreateSendBuf(char* fSendName, char* buf, int bufLen) {
	assert(fSendName != NULL);
	assert(buf != NULL);
	assert(bufLen > 0);

	char headBuf[10000] = "";
	int headLen = 0;

	FILE* fSend = fopen(fSendName, "rb");
	if (fSend == NULL) {
		headLen = sprintf(buf, "HTTP/1.1 404 Not Found");
		fprintf(stderr, "(WARNING) \"%s\" file open error: %d (%s)\n", fSendName, errno, strerror(errno));
		return headLen;
	}
	else {
		char contType[100] = "";
		if (DetContType(fSendName, contType) == 1) {
			fprintf(stderr, "(ERROR) Didn't determine content type of file %s\n", fSendName);

			fclose(fSend);
			return -1;
		}

		long long fBufLen = FileSize(fSend);
		headLen = sprintf(headBuf, "HTTP/1.1 200 OK\r\nContent-type: %s\r\nContent-length: %d", contType, fBufLen);

		if (headLen + fBufLen + 4 >= bufLen) {
			fprintf(stderr, "(ERROR) Buffer length too small: header length: %d, "
				"file length: %d, buffer length: %d\n", headLen, fBufLen, bufLen);
			fclose(fSend);
			return -1;
		}
		sprintf(buf, "%s\r\n\r\n", headBuf);
		fread(&buf[headLen + 4], sizeof(char), bufLen - 4, fSend);

		fclose(fSend);
		return headLen + fBufLen + 4;
	}
}

int InteractClient(SOCKET clientSock, server_properties props) {
	assert(clientSock != INVALID_SOCKET);

	const int fReqNameMaxSize = 10000;

	char buf[1500 * 1024 * 1024] = "";
	printf("\t\tReceiving data...\n");
	int bufLen = ReceiveData(clientSock, buf, sizeof(buf) - 1);
	if (bufLen <= 0) {
		return 1;
	}
	printf("\t\tData received successfully:\n");
	DEBUG_CODE(fwrite(buf, sizeof(char), bufLen, stdout));
	
	if (strncmp(buf, "GET ", 4) == 0) {
		bufLen = 0;
		assert(buf[4] == '/');

		char fReqName[fReqNameMaxSize] = "";
		if (buf[5] == ' ') {
			sprintf(fReqName, "%s/%s", props.siteRootFolder, props.homePage);
		}
		else {
			char* space = strchr(&buf[4], ' ');
			int fReqNameSize = space - &buf[5];
			assert(fReqNameSize > 0 && fReqNameSize < fReqNameMaxSize);

			sprintf(fReqName, "%s/", props.siteRootFolder);
			strncpy(&fReqName[strlen(props.siteRootFolder) + 1], &buf[5], fReqNameSize);
		}

		bufLen = CreateSendBuf(fReqName, buf, sizeof(buf) - 1);
		if (bufLen == -1) {
			return 1;
		}

		printf("\n\t\tSending data:\n");
		DEBUG_CODE(fwrite(buf, sizeof(char), bufLen, stdout); printf("\n"););
		if (SendData(clientSock, buf, bufLen) == 1) {
			return 1;
		}
		printf("\n\t\tData sent.\n");
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

int StartServer(server_properties props) {
	printf("Initializing...\n");
	int err = Initialize();
	if (err != 0) {
		return 1;
	}
	(printf("Initialized.\n\n"));

	printf("Creating listening socket...\n");
	SOCKET listenSock = GetListenSock(props);
	if (listenSock == INVALID_SOCKET) {
		closesocket(listenSock);
		return 1;
	}
	printf("Listening socket successfully created.\n\n\n");

	while (1) {
		sockaddr_in clientAddr = {AF_INET, 0};
		printf("\tWaiting for incoming connection...\n");
		SOCKET clientSock = AcceptConnection(listenSock, (sockaddr*)&clientAddr);
		if (clientSock == INVALID_SOCKET) {
			closesocket(listenSock);
			return 1;
		}
		printf("\tConnected to %s\n\n", inet_ntoa(clientAddr.sin_addr));
		if (props.soundConnection) {
			printf("\a");
		}

		printf("\tInteracting with client:\n");
		InteractClient(clientSock, props);
		printf("\tEnded interaction.\n\n");

		printf("\tClosing connection...\n");
		if (EndConnection(clientSock) == SOCKET_ERROR) {
			closesocket(listenSock);
			return 1;
		}
		printf("\tConnection closed successfully.\n\n\n\n");
	}
}


server_properties PropertiesInput() {
	server_properties props = {};

	char ipInput[100] = "";
	printf("Please, configure server properties. To use all default settings, type \"-\":\n");

	printf("Enter server IP address (IPv4). To set automatically type \"auto\": ");
	scanf("%99s", ipInput);
	while (ipInput[98] != '\0') {
		printf("Invalid input\n");

		fseek(stdin, 0, SEEK_END);
		ipInput[98] = '\0';

		printf("Enter server IP address (IPv4). To set automatically type \"auto\": ");
		scanf("%99s", ipInput);
	}
	if (ipInput[0] == '-') {
		printf("\n");
		return props;
	}

	strcpy(props.ipAddr, ipInput);
	printf("Enter port: ");
	scanf("%d", &props.port);
	fseek(stdin, 0, SEEK_END);

	printf("Enter site root folder: ");
	scanf("%9999s", props.siteRootFolder);
	if(props.siteRootFolder[9998] != '\0') {
		fprintf(stderr, "(ERROR) Site root folder input overflow\n");
	}
	fseek(stdin, 0, SEEK_END);

	printf("Enter home page file (relative to site root folder): ");
	scanf("%9999s", props.homePage);
	if (props.homePage[9998] != '\0') {
		fprintf(stderr, "(ERROR) Home page file input overflow\n");
	}
	fseek(stdin, 0, SEEK_END);

	char alert = 0;
	while (alert != 'y' && alert != 'n') {
		printf("Do you want server to alert you with sound when someone connects? [y/n]: ");
		scanf("%c", &alert);
		alert = tolower(alert);

		if (alert == 'y') {
			props.soundConnection = 1;
		} else if (alert == 'n') {
			props.soundConnection = 0;
		} else {
			printf("Enter \"y\" or \"n\"\n");
		}
		fseek(stdin, 0, SEEK_END);
	}

	printf("\n");
	return props;
}


int main() {
	server_properties props = PropertiesInput();

	int err = 0;
	while (1) {
		err = StartServer(props);
		if (err != 0) {
			printf("Restarting...\n");
		}
	}

	return err;
}