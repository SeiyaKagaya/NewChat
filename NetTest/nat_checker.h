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

enum class ConnectionMode {
    P2P,
    LocalP2P,
    Relay
};


class NATChecker {
public:
    NATChecker();
    ~NATChecker();

    ConnectionMode decideConnectionMode(const std::string& natType);

    std::string detectNATType();
    ExternalAddress getExternalAddress(const std::string& stunIP, int stunPort);

private:
    void generateTransactionID(unsigned char* id);
    std::vector<unsigned char> createStunRequest();
    ExternalAddress parseStunResponse(char* response, int len);
    std::string classify(const ExternalAddress& addr1, const ExternalAddress& addr2, const ExternalAddress& addr3);
};

inline ConnectionMode StringToConnectionMode(const std::string& str)
{
    if (str == "Relay")
    {
        return ConnectionMode::Relay;
    }
    else if (str == "LocalP2P")
    {
        return ConnectionMode::LocalP2P;
    }
    else
    {
        return ConnectionMode::P2P;
    }
}

#endif