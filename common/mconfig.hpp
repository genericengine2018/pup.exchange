#pragma once

#include <eosio/eosio.hpp>  
#include <math.h>
#include <string>

namespace puppy {

using namespace eosio;
using std::string;

constexpr name   CORE_ACCOUNT = "puppycore124"_n;
constexpr name   MCONFIG_ACCOUNT = "puppynetwork"_n;

const uint64_t   SYS_TOKEN_ID = 1;
const uint64_t   PUP_TOKEN_ID = 2;
const string     PUP_NAME = "PUP";
const uint8_t    PUP_PREC = 4;
constexpr name   PUP_CONTRACT = "puppygotoken"_n;
const uint64_t   PUP_TIMES = 10000;

const uint64_t   ROUND_PERIOD = 10*3600*1000;
const uint64_t   ROUND_PERIOD_L = 8*3600*1000;
const uint64_t   ROUND_PERIOD_U = 24*3600*1000;

constexpr double ROUND_P = 0.001;
constexpr double ROUND_P_L = 0.0001;
constexpr double ROUND_P_U = 0.001;

constexpr double ROUND_TVL_P = 0.05;
constexpr double ROUND_TVL_P_L = 0.005;
constexpr double ROUND_TVL_P_U = 0.05;

const uint64_t   ROUND_MAX = 2000000 * PUP_TIMES;
const uint64_t   ROUND_MAX_L = 1000000 * PUP_TIMES;
const uint64_t   ROUND_MAX_U = 5000000 * PUP_TIMES;

const uint64_t   TOP_NUM = 10;
const uint64_t   TOP_NUM_L = 10;
const uint64_t   TOP_NUM_U = 100;

const uint64_t   TOP_VOL_MIN = 1000 * PUP_TIMES;
const uint64_t   TOP_VOL_MIN_L = 1 * PUP_TIMES;
const uint64_t   TOP_VOL_MIN_U = 1000 * PUP_TIMES;

const uint64_t   TOP_X = 100;
const uint64_t   TOP_X_L = 10;
const uint64_t   TOP_X_U = 100;

constexpr double TOP_X_DEC = 0.6;
constexpr double TOP_X_DEC_L = 0.6;
constexpr double TOP_X_DEC_U = 0.9;

constexpr double VOL_POOL_P = 0.01;     
constexpr double VOL_POOL_P_L = 0.001;
constexpr double VOL_POOL_P_U = 0.01;

const uint64_t   VOL_LASTVOL_X = 10;
const uint64_t   VOL_LASTVOL_X_L = 2;
const uint64_t   VOL_LASTVOL_X_U = 50;

constexpr double FEE_2_PROFIT_P = 0.5;
constexpr double FEE_2_PROFIT_P_L = 0.4;
constexpr double FEE_2_PROFIT_P_U = 0.8;

const uint64_t   COUNT_LIMIT = 300;
const uint64_t   COUNT_LIMIT_L = 100;
const uint64_t   COUNT_LIMIT_U = 500;

const uint64_t   ACTION_LIMIT = 50;
const uint64_t   ACTION_LIMIT_L = 10;
const uint64_t   ACTION_LIMIT_U = 200;

const uint64_t   CLEANUP_LIMIT = 500;
const uint64_t   CLEANUP_LIMIT_L = 100;
const uint64_t   CLEANUP_LIMIT_U = 500;

struct mconfig {
    name        core_account = CORE_ACCOUNT;
    uint64_t    round_period = ROUND_PERIOD;
    double      round_p = ROUND_P;
    double      round_tvl_p = ROUND_TVL_P;
    uint64_t    round_max = ROUND_MAX;
    uint64_t    top_num = TOP_NUM;
    uint64_t    top_vol_min = TOP_VOL_MIN;
    uint64_t    top_x = TOP_X;
    double      top_x_dec = TOP_X_DEC;
    double      vol_pool_p = VOL_POOL_P;
    uint64_t    vol_lastvol_x = VOL_LASTVOL_X;
    double      fee_2_profit_p = FEE_2_PROFIT_P;    
    uint64_t    count_limit = COUNT_LIMIT;
    uint64_t    action_limit = ACTION_LIMIT;
    uint64_t    cleanup_limit = CLEANUP_LIMIT;    
};
typedef singleton<"mconfig"_n,mconfig> MConfig;

class MConfHelper{
public:
    static mconfig Get(const name& scope){
        MConfig conf_table(MCONFIG_ACCOUNT,scope.value);   
        mconfig conf = conf_table.get_or_default();

        conf.round_period = std::clamp(conf.round_period,ROUND_PERIOD_L,ROUND_PERIOD_U);
        conf.round_p = std::clamp(conf.round_p,ROUND_P_L,ROUND_P_U);
        conf.round_tvl_p = std::clamp(conf.round_tvl_p,ROUND_TVL_P_L,ROUND_TVL_P_U);
        conf.round_max = std::clamp(conf.round_max,ROUND_MAX_L,ROUND_MAX_U);
        conf.top_num = std::clamp(conf.top_num,TOP_NUM_L,TOP_NUM_U);
        conf.top_vol_min = std::clamp(conf.top_vol_min,TOP_VOL_MIN_L,TOP_VOL_MIN_U);
        conf.top_x = std::clamp(conf.top_x,TOP_X_L,TOP_X_U);
        conf.top_x_dec = std::clamp(conf.top_x_dec,TOP_X_DEC_L,TOP_X_DEC_U);
        conf.vol_pool_p = std::clamp(conf.vol_pool_p,VOL_POOL_P_L,VOL_POOL_P_U);
        conf.vol_lastvol_x = std::clamp(conf.vol_lastvol_x,VOL_LASTVOL_X_L,VOL_LASTVOL_X_U);
        conf.fee_2_profit_p = std::clamp(conf.fee_2_profit_p,FEE_2_PROFIT_P_L,FEE_2_PROFIT_P_U);
        conf.count_limit = std::clamp(conf.count_limit,COUNT_LIMIT_L,COUNT_LIMIT_U);        
        conf.action_limit = std::clamp(conf.action_limit,ACTION_LIMIT_L,ACTION_LIMIT_U);
        conf.cleanup_limit = std::clamp(conf.cleanup_limit,CLEANUP_LIMIT_L,CLEANUP_LIMIT_U);

        return conf;
    }

    static void Set(const mconfig& conf, const name& scope){
        MConfig conf_table(MCONFIG_ACCOUNT,scope.value);  
        conf_table.set(conf,MCONFIG_ACCOUNT);
    }
};

}