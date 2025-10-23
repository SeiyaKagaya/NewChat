//------------------------------------------------------------
// @file        ip_checker.cpp
// @brief       Network�v���g�R���`�F�b�J�[
//------------------------------------------------------------
#include "ip_checker.h"

//----------------------------------------------
// �R���X�g���N�^
//----------------------------------------------
IpChecker::IpChecker()
{
}

//----------------------------------------------
// �f�X�g���N�^
//----------------------------------------------
IpChecker::~IpChecker()
{
}

//----------------------------------------------
// HTTP GET �����s�i�^�C���A�E�g 5�b / �G���[�\���t���j
// host: �T�[�o�[��
// path: �p�X
// port: �|�[�g�ԍ�
// response: �擾�������X�|���X������
//----------------------------------------------
bool IpChecker::HttpGet(const std::string& host, const std::string& path, unsigned short port, std::string& response)
{
    response.clear();

    HINTERNET hSession = WinHttpOpen(L"CityCleaners HTTP Client/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        std::cerr << "[WinHTTP] WinHttpOpen failed: " << GetLastError() << std::endl;
        return false;
    }

    WinHttpSetTimeouts(hSession, 5000, 5000, 5000, 5000);

    std::wstring wHost(host.begin(), host.end());
    std::wstring wPath(path.begin(), path.end());

    HINTERNET hConnect = WinHttpConnect(hSession, wHost.c_str(), port, 0);
    if (!hConnect) {
        std::cerr << "[WinHTTP] WinHttpConnect failed: " << GetLastError() << std::endl;
        WinHttpCloseHandle(hSession);
        return false;
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", wPath.c_str(),
        NULL, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!hRequest) {
        std::cerr << "[WinHTTP] WinHttpOpenRequest failed: " << GetLastError() << std::endl;
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(hRequest, NULL)) {
        std::cerr << "[WinHTTP] WinHttpSendRequest/ReceiveResponse failed: " << GetLastError() << std::endl;
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD bytesAvailable = 0;
    while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
        char* buffer = new char[bytesAvailable + 1];
        DWORD bytesRead = 0;
        if (WinHttpReadData(hRequest, buffer, bytesAvailable, &bytesRead)) {
            buffer[bytesRead] = 0;
            response += buffer;
        }
        delete[] buffer;
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return true;
}

//----------------------------------------------
// �T�[�o�[�ɃA�N�Z�X���� IPv4/IPv6 ����
// host: �T�[�o�[��
// path: ����p�X�N���v�g�p�X
// port: �|�[�g�ԍ�
// �߂�l: "OK4", "OK6", "BOTH", "NONE"
//----------------------------------------------
std::string IpChecker::CheckServerIP(const std::string& host, const std::string& path, unsigned short port)
{
    std::string result;

    if (HttpGet(host, path, port, result)) {
        // �O��̋󔒁E���s����
        result.erase(result.find_last_not_of(" \r\n") + 1);
        result.erase(0, result.find_first_not_of(" \r\n"));
        return result;
    }

    return "NONE";
}
