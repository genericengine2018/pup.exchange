#pragma once
#include <eosio/eosio.hpp>  
#include <eosio/system.hpp>
#include <eosio/permission.hpp>
#include <string>

namespace puppy {
    using namespace eosio;    
    using std::string;

    template <typename T> 
    uint64_t AvailPrimKey(T& table){ 
        uint64_t id = table.available_primary_key();
        return id==0?1:id;
    }

    uint64_t current_time(){
        return current_time_point().time_since_epoch().count()/1000;
    }   

    checksum256 get_tx_id(){
        auto size = transaction_size();
        char buf[size];
        uint32_t read = read_transaction( buf, size );
        check( size == read, "read_transaction failed");
        return sha256(buf, read);
    }

    string checksum256_to_string (const checksum256& checksum){
        return to_hex( &checksum.extract_as_byte_array()[0], 32);
    }     

    string amount_tostring(uint64_t amount,const symbol& sym){
        uint8_t precision = sym.precision();

        string str = std::to_string(amount);
        int tofix = precision+1-str.length();
        for(int i=0;i<tofix;i++)str = "0"+str;
        str.insert(str.length()-precision,".");
        
        return str + " " + sym.code().to_string();
    }
}