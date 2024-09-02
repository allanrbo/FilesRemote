// Copyright 2020 Allan Riordan Boll

#ifndef SRC_HOSTDESC_H_
#define SRC_HOSTDESC_H_

#include <string>
#include <vector>

using std::string;
using std::vector;

class HostDesc {
public:
    string entered_;
    string host_;  // This is the DNS name or IP.
    string display_host_;  // This is what will be in the "Host"-line of ~/.ssh/config.
    string username_;
    int port_ = 22;
    bool is_ipv6_literal_ = false;
    vector<string> identity_files_;
    bool hostname_set = false;
    bool identityfile_set = false;
    bool username_given = false;
    bool user_set = false;
    bool port_given = false;
    bool port_set = false;

    HostDesc() {}

    explicit HostDesc(string host, string identity_file);

    void ParseConfigFile(string path);

    string ToString();

    string ToStringNoCol();

    string ToStringNoUser();

    string ToStringNoUserNoCol();
};

#endif  // SRC_HOSTDESC_H_
