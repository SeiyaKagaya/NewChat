//------------------------------------------------------------
// @file        stun_client.h
// @brief       STUN��p���ĊO��IP�ƃ|�[�g���擾����
//------------------------------------------------------------
#ifndef _STUN_CLIENT_H_
#define _STUN_CLIENT_H_

#include <string>
#pragma comment(lib, "ws2_32.lib")
//#include <iostream>

// ���������ꍇ true ��Ԃ��A�O��IP�ƃ|�[�g�� outIP, outPort �Ɋi�[����
bool GetExternalAddress(std::string& outIP, unsigned short& outPort);

#endif