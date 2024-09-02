// Copyright 2020 Allan Riordan Boll

#include "src/hostdesc.h"

#include <wx/wx.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <regex>  // NOLINT
#include <sstream>
#include <string>

#ifndef __WXOSX__

#include <filesystem>

#endif

using std::ifstream;
using std::invalid_argument;
using std::istringstream;
using std::regex;
using std::string;
using std::to_string;

#ifndef __WXOSX__
using std::filesystem::exists;
#else
#include "src/filesystem.osx.polyfills.h"
#endif

static string strip_quotes(string s) {
    if (s[0] == '"') {
        s = s.substr(1, s.length() - 2);
    }

    return s;
}

HostDesc::HostDesc(string host, string identity_file) {
    this->entered_ = host;
    this->username_ = wxGetUserId();
    this->host_ = host;
    this->port_ = 22;

#ifdef __WXMSW__
    // Windows usually title-cases usernames, but the hosts we will likely be SSH'ing to are usually lower cased.
    transform(this->username_.begin(), this->username_.end(), this->username_.begin(), ::tolower);
#endif

    // Check if there's a username given.
    if (this->host_.find("@") != string::npos) {
        this->username_given = true;
        int i = this->host_.find("@");
        this->username_ = this->host_.substr(0, i);
        this->host_ = this->host_.substr(i + 1);
    }

    int colon_count = 0;
    string::size_type pos = 0;
    while ((pos = this->host_.find(":", pos )) != string::npos) {
        colon_count++;
        pos++;
    }

    string port_string = "";
    if (colon_count == 1) {
        // If there is only one colon, this is probably a port delimiter, and not port of an IPv6 address.
        string::size_type i = this->host_.rfind(":");
        port_string = string(this->host_.substr(i + 1));
        this->host_ = this->host_.substr(0, i);
    } else if (colon_count > 1) {
        this->is_ipv6_literal_ = true;

        // Is this an IPv6 address like "[2001:db8::1]"?
        if (this->host_[0] == '[') {
            string::size_type i = this->host_.rfind("]");
            if (i != string::npos) {
                string rest = this->host_.substr(i);
                this->host_ = this->host_.substr(1, i - 1);

                // Is this an IPv6 address like "[2001:db8::1]:22"?
                i = rest.rfind(":");
                if (i != string::npos) {
                    port_string = string(rest.substr(i + 1));
                }
            }
        }
    }

    if (!port_string.empty()) {
        this->port_given = true;

        if (!all_of(port_string.begin(), port_string.end(), ::isdigit)) {
            throw invalid_argument("non-digit port number");
        }
        this->port_ = stoi(string(port_string));
        if (!(0 < this->port_ && this->port_ < 65536)) {
            throw invalid_argument("invalid port number");
        }
    }

    // The "Host"-lines in ~/.ssh/config may differ from the actual DNS name or IP in the "HostName" field.
    // The display_host_ field represents what will be in the "Host"-line of ~/.ssh/config.
    this->display_host_ = this->host_;

    // Prepare where to look for ssh config files.
    vector<string> try_ssh_config_paths;
#ifdef __WXMSW__
    string home = getenv("HOMEPATH");
    try_ssh_config_paths.push_back("C:\\Program Files\\Git\\etc\\ssh\\ssh_config");
#else
    string home = getenv("HOME");
#endif
    try_ssh_config_paths.push_back(home + "/.ssh/config");

    // Look through each potential ssh config file.
    for (auto path : try_ssh_config_paths) {
        try {
            if (exists(path)) {
                ParseConfigFile(path);
            }
        } catch (invalid_argument) {
            throw;
        } catch (...) {
            // Probably permission error. Continue to try the next config path.
            continue;
        }
    }

    // Additional standard paths to load the key from.
    this->identity_files_.push_back(home + "/.ssh/id_rsa_" + this->host_);  // Observed openssh client use this.
    this->identity_files_.push_back(home + "/.ssh/id_dsa_" + this->host_);
    this->identity_files_.push_back(home + "/.ssh/id_ed25519_" + this->host_);
    this->identity_files_.push_back(home + "/.ssh/id_rsa");
    this->identity_files_.push_back(home + "/.ssh/id_dsa");
    this->identity_files_.push_back(home + "/.ssh/id_ed25519");

    // If an identify file was explicitly given as param, then use that instead.
    if (!identity_file.empty()) {
        this->identity_files_.clear();
        this->identity_files_.push_back(identity_file);
    }

    // An allow-list regex would be better, but too tricky due to internationalized domain names.
    if (regex_search(this->host_, regex("[/\\\\]"))) {
        throw invalid_argument("invalid host name");
    }
}

void HostDesc::ParseConfigFile(string path) {
    // Copy-paste :(
#ifdef __WXMSW__
    string home = getenv("HOMEPATH");
#else
    string home = getenv("HOME");
#endif
    ifstream infile(path);
    string line, cur_host;
    while (getline(infile, line)) {
        istringstream iss(line);
        string cmd;
        iss >> cmd;
        transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

        if (cmd == "include") {
            string include_path;
            iss >> include_path;
            include_path = strip_quotes(include_path);

            // Expand homedir tilde.
            if (include_path[0] == '~') {
                include_path = home + include_path.substr(1);
            }
            // If the include path is relative, make it absolute based on the current config file's location.
            if (include_path[0] != '/') {
                size_t last_slash_idx = path.find_last_of("/\\");
                if (last_slash_idx != std::string::npos) {
                    include_path = path.substr(0, last_slash_idx + 1) + include_path;
                }
            } 

            // Recursively parse the included file.
            if (exists(include_path)) {
                ParseConfigFile(include_path);
            }
            continue;
        }

        if (cmd == "host") {
            iss >> cur_host;
            cur_host = strip_quotes(cur_host);
            transform(cur_host.begin(), cur_host.end(), cur_host.begin(), ::tolower);
        }

        if (cur_host == this->display_host_ || cur_host.empty() || cur_host == "*") {
            if (cmd == "hostname" && !this->hostname_set) {
                this->hostname_set = true;
                string hostname;
                iss >> hostname;
                hostname = strip_quotes(hostname);
                this->host_ = hostname;
            } else if (cmd == "identityfile" && !this->identityfile_set) {
                this->identityfile_set = true;
                string identity_file;
                iss >> identity_file;
                identity_file = strip_quotes(identity_file);

                // Expand homedir tilde.
                if (identity_file[0] == '~') {
                    identity_file = home + identity_file.substr(1);
                }

                this->identity_files_.push_back(identity_file);
            } else if (cmd == "user" && !this->username_given && !this->user_set) {
                this->user_set = true;
                string user;
                iss >> user;
                user = strip_quotes(user);
                this->username_ = user;
            } else if (cmd == "port" && !this->port_given && !this->port_set) {
                this->port_set = true;
                string ps;
                iss >> ps;
                ps = strip_quotes(ps);

                if (!all_of(ps.begin(), ps.end(), ::isdigit)) {
                    throw invalid_argument("non-digit port number in ssh config");
                }
                this->port_ = stoi(string(ps));
                if (!(0 < this->port_ && this->port_ < 65536)) {
                    throw invalid_argument("invalid port number ssh config");
                }
            }
        }
    }
}

string HostDesc::ToString() {
    string s = this->username_ + "@" + this->host_ + ":" + to_string(this->port_);

    if (this->is_ipv6_literal_) {
        s = this->username_ + "@[" + this->host_ + "]:" + to_string(this->port_);
    }

    if (this->host_ != this->display_host_) {
        s += " (" + this->display_host_ + ")";
    }
    return s;
}

string HostDesc::ToStringNoCol() {
    if (this->is_ipv6_literal_) {
        string h = std::regex_replace(this->host_, std::regex(":"), ".");
        return this->username_ + "@[" + h + "]_" + to_string(this->port_);
    }

    return this->username_ + "@" + this->host_ + "_" + to_string(this->port_);
}

string HostDesc::ToStringNoUser() {
    if (this->is_ipv6_literal_) {
        return "[" + this->host_ + "]:" + to_string(this->port_);
    }

    return this->host_ + ":" + to_string(this->port_);
}

string HostDesc::ToStringNoUserNoCol() {
    if (this->is_ipv6_literal_) {
        string h = std::regex_replace(this->host_, std::regex(":"), ".");
        return "[" + h + "]_" + to_string(this->port_);
    }

    return this->host_ + "_" + to_string(this->port_);
}
