//------------------------------------------------------------
// @file        nat_checker.h
// @brief       
//------------------------------------------------------------
#ifndef _NAT_CHECKER_H_
#define _NAT_CHECKER_H_

#include"includemanager.h"

struct ExternalAddress {
    std::string ip;
    int port;
};

class NATChecker {
public:
    NATChecker();
    ~NATChecker();

    std::string detectNATType();
    ExternalAddress getExternalAddress(const std::string& stunIP, int stunPort);

private:
    void generateTransactionID(unsigned char* id);
    std::vector<unsigned char> createStunRequest();
    ExternalAddress parseStunResponse(char* response, int len);
    std::string classify(const ExternalAddress& addr1, const ExternalAddress& addr2, const ExternalAddress& addr3);
};

#endif