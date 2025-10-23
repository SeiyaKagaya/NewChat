//------------------------------------------------------------
// @file        stun_client.h
// @brief       STUNを用いて外部IPとポートを取得する
//------------------------------------------------------------
#ifndef _STUN_CLIENT_H_
#define _STUN_CLIENT_H_

#include "includemanager.h"

//#include <iostream>

// 成功した場合 true を返し、外部IPとポートを outIP, outPort に格納する
bool GetExternalAddress(std::string& outIP, unsigned short& outPort);


#endif