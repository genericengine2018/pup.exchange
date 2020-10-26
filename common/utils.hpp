#pragma once
#include <math.h>
#include <string>

namespace puppy {
    using std::string;

    std::vector<string> split(const string& s, const char* delim = " ") {
        std::vector<string> sv;
        char* buffer = new char[s.size() + 1];     
        std::copy(s.begin(), s.end(), buffer);  
        char* p = std::strtok(buffer, delim);
        do {sv.emplace_back(p);} 
        while ((p = std::strtok(NULL, delim)));
        delete[] buffer;
        return sv;
    }

    std::map<string,string> parse(const string& str,const char* delim1=";",const char* delim2 ="="){
        std::map<string,string> params;
        std::vector<string> strs = split(str,delim1);
        for(int i=0;i<strs.size();i++){
            std::vector<string> substrs = split(strs[i],delim2);
            if(substrs.size()>=2)params.emplace(substrs[0],substrs[1]);
        }
        return params;
    }

    template <typename T>
    string ToFixed(const T& t,int n){
        int sum = 1;
        for(int i = 0; i < n; i++) sum *=10;
        string str = std::to_string(round(t*sum)/sum);
        return str.substr(0,std::min(str.length(),str.find_first_of(".")+n+1));
    }

    uint32_t Rem64(uint64_t big,uint32_t n){
        uint32_t tmp = (big>>32) + (uint32_t)big;
        return tmp%n;
    }

    // no scientific notation accepted.
    double Str2Float(string str){
        std::vector<string> parts = split(str,".");
        int64_t intPart = std::stoll(parts[0]);
        bool neg = parts[0][0]=='-' ? true : false;

        double digitPart = 0;
        if(parts.size()>1){
            uint64_t tmp = std::stoll(parts[1]);
            if(tmp>0){
                digitPart = tmp / pow(10,parts[1].size());
                if(neg)digitPart = -digitPart;
            }
        }

        return intPart+digitPart;
    }

    string to_hex(const uint8_t* d, uint32_t s) {
        string r;
        const char* to_hex = "0123456789abcdef";
        uint8_t* c = (uint8_t*)d;
        for (uint32_t i = 0; i < s; ++i)
            (r += to_hex[(c[i] >> 4)]) += to_hex[(c[i] & 0x0f)];
        return r;
    }
}