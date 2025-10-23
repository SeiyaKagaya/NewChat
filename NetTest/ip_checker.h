//------------------------------------------------------------
// @file        ip_checker.h
// @brief       Network�v���g�R���`�F�b�N
//------------------------------------------------------------
#ifndef _IP_TEST_H_
#define _IP_TEST_H_

#include <string>

class IpChecker
{
public:

    IpChecker();
    ~IpChecker();

    //----------------------------------------------
    // �T�[�o�[�ɐڑ����� IPv4/IPv6 �̓��B�ۂ��擾
    // host: �T�[�o�[�̃z�X�g��/IP
    // path: HTTP GET �̃p�X
    // port: �|�[�g�ԍ�
    // �߂�l: "OK4", "OK6", "BOTH", "NONE"
    //----------------------------------------------
    std::string CheckServerIP(const std::string& host, const std::string& path, unsigned short port);

private:
    //----------------------------------------------
    // HTTP GET ���s
    // response: �擾�������X�|���X���i�[
    // �^�C���A�E�g5�b
    //----------------------------------------------
    bool HttpGet(const std::string& host, const std::string& path, unsigned short port, std::string& response);
};

#endif
