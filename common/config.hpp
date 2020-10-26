#pragma once

#include <eosio/eosio.hpp>  
#include <math.h>
#include <string>

namespace puppy {

using namespace eosio;
using std::string;

const uint64_t   SYS_TOKEN_ID = 1;
const uint64_t   PUP_TOKEN_ID = 2;
const string     PUP_NAME = "PUP";
const uint8_t    PUP_PREC = 4;
constexpr name   PUP_CONTRACT = "puppygotoken"_n;
const uint64_t   PUP_TIMES = 10000;

const uint64_t   PUP_POOL_MIN = 10000 * PUP_TIMES;
const uint64_t   CLEANUP_WINDOW = 1800*1000;
const uint64_t   CLEANUP_LIMIT = 500;

constexpr name   INSTANT_FEE_ACCOUNT = "puppynetwork"_n;
constexpr double INSTANT_FEE_P = 0.005;
const uint64_t   INSTANT_FEE_MIN = 10 * PUP_TIMES;  

constexpr name   ORDER_FEE_ACCOUNT = "111222333pup"_n;
const uint64_t   ORDER_DAY_FEE = 20 * PUP_TIMES;
const uint64_t   ORDER_AMOUNT_MIN = 100 * PUP_TIMES;

struct config {
    uint64_t  pup_pool_min = PUP_POOL_MIN;
    name      instant_fee_account = INSTANT_FEE_ACCOUNT;
    double    instant_fee_p = INSTANT_FEE_P;
    uint64_t  instant_fee_min = INSTANT_FEE_MIN;
    name      order_fee_account = ORDER_FEE_ACCOUNT;
    uint64_t  order_day_fee = ORDER_DAY_FEE;
    uint64_t  order_amount_min = ORDER_AMOUNT_MIN;
    uint64_t  cleanup_window = CLEANUP_WINDOW;
    uint64_t  cleanup_limit = CLEANUP_LIMIT;
};
typedef singleton<"config"_n,config> Config;

class ConfHelper{
public:
    static config Get(const name& contract,const name& scope){
        Config conf_table(contract,scope.value);   
        config conf = conf_table.get_or_default();

        //clamp core params that may become evil.
        conf.pup_pool_min = std::min(conf.pup_pool_min, PUP_POOL_MIN);
        conf.instant_fee_p = std::min(conf.instant_fee_p, INSTANT_FEE_P);
        conf.instant_fee_min = std::min(conf.instant_fee_min, INSTANT_FEE_MIN);
        conf.order_day_fee = std::min(conf.order_day_fee, ORDER_DAY_FEE);
        conf.order_amount_min = std::min(conf.order_amount_min, ORDER_AMOUNT_MIN);

        return conf;
    }

    static void Set(const config& conf,const name& contract,const name& scope){
        Config conf_table(contract,scope.value);  
        conf_table.set(conf,contract);
    }
};

}