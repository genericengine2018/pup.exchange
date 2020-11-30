#pragma once

#include <eosio/eosio.hpp>  
#include <math.h>
#include <string>

namespace puppy {

using namespace eosio;
using std::string;

constexpr name   CORE_ACCOUNT = "puppycore124"_n;
constexpr name   MCONFIG_ACCOUNT = "puppynetwork"_n;

const uint64_t   MINING_ROUND_PERIOD = 10*3600*1000;
const uint64_t   MINING_ROUND_PERIOD_L = 8*3600*1000;
const uint64_t   MINING_ROUND_PERIOD_U = 24*3600*1000;

constexpr double MINING_ROUND_P = 0.001;
constexpr double MINING_ROUND_P_L = 0.0001;
constexpr double MINING_ROUND_P_U = 0.001;

constexpr double MINING_ROUND_TVL_P = 0.05;
constexpr double MINING_ROUND_TVL_P_L = 0.005;
constexpr double MINING_ROUND_TVL_P_U = 0.05;

const uint64_t   MINING_ROUND_MAX = 2000000 * PUP_TIMES;
const uint64_t   MINING_ROUND_MAX_L = 1000000 * PUP_TIMES;
const uint64_t   MINING_ROUND_MAX_U = 5000000 * PUP_TIMES;

const uint64_t   MINING_TOP_NUM = 10;
const uint64_t   MINING_TOP_NUM_L = 10;
const uint64_t   MINING_TOP_NUM_U = 100;

const uint64_t   MINING_TOP_VOL_MIN = 1000 * PUP_TIMES;
const uint64_t   MINING_TOP_VOL_MIN_L = 1 * PUP_TIMES;
const uint64_t   MINING_TOP_VOL_MIN_U = 1000 * PUP_TIMES;

const uint64_t   MINING_TOP_X = 100;
const uint64_t   MINING_TOP_X_L = 10;
const uint64_t   MINING_TOP_X_U = 100;

constexpr double MINING_TOP_X_DEC = 0.6;
constexpr double MINING_TOP_X_DEC_L = 0.6;
constexpr double MINING_TOP_X_DEC_U = 0.9;

constexpr double MINING_VOL_POOL_P = 0.01;     
constexpr double MINING_VOL_POOL_P_L = 0.001;
constexpr double MINING_VOL_POOL_P_U = 0.01;

const uint64_t   MINING_VOL_LASTVOL_X = 10;
const uint64_t   MINING_VOL_LASTVOL_X_L = 2;
const uint64_t   MINING_VOL_LASTVOL_X_U = 50;

const uint64_t   MINING_COUNT_LIMIT = 300;
const uint64_t   MINING_COUNT_LIMIT_L = 100;
const uint64_t   MINING_COUNT_LIMIT_U = 500;

const uint64_t   MINING_ACTION_LIMIT = 50;
const uint64_t   MINING_ACTION_LIMIT_L = 10;
const uint64_t   MINING_ACTION_LIMIT_U = 200;

constexpr double MINING_FEE_2_PROFIT_P = 0.5;
constexpr double MINING_FEE_2_PROFIT_P_L = 0.4;
constexpr double MINING_FEE_2_PROFIT_P_U = 0.8;

struct mconfig {
    name        core_account = CORE_ACCOUNT;
    uint64_t    round_period = MINING_ROUND_PERIOD;
    double      round_p = MINING_ROUND_P;
    double      round_tvl_p = MINING_ROUND_TVL_P;
    uint64_t    round_max = MINING_ROUND_MAX;
    uint64_t    top_num = MINING_TOP_NUM;
    uint64_t    top_vol_min = MINING_TOP_VOL_MIN;
    uint64_t    top_x = MINING_TOP_X;
    double      top_x_dec = MINING_TOP_X_DEC;
    double      vol_pool_p = MINING_VOL_POOL_P;
    uint64_t    vol_lastvol_x = MINING_VOL_LASTVOL_X;
    uint64_t    count_limit = MINING_COUNT_LIMIT;
    uint64_t    action_limit = MINING_ACTION_LIMIT;
    double      fee_2_profit_p = MINING_FEE_2_PROFIT_P;
};
typedef singleton<"mconfig"_n,mconfig> MConfig;

class MConfHelper{
public:
    static mconfig Get(){
        MConfig conf_table(MCONFIG_ACCOUNT,MCONFIG_ACCOUNT.value);   
        mconfig conf = conf_table.get_or_default();

        conf.round_period = std::clamp(conf.round_period,MINING_ROUND_PERIOD_L,MINING_ROUND_PERIOD_U);
        conf.round_p = std::clamp(conf.round_p,MINING_ROUND_P_L,MINING_ROUND_P_U);
        conf.round_tvl_p = std::clamp(conf.round_tvl_p,MINING_ROUND_TVL_P_L,MINING_ROUND_TVL_P_U);
        conf.round_max = std::clamp(conf.round_max,MINING_ROUND_MAX_L,MINING_ROUND_MAX_U);
        conf.top_num = std::clamp(conf.top_num,MINING_TOP_NUM_L,MINING_TOP_NUM_U);
        conf.top_vol_min = std::clamp(conf.top_vol_min,MINING_TOP_VOL_MIN_L,MINING_TOP_VOL_MIN_U);
        conf.top_x = std::clamp(conf.top_x,MINING_TOP_X_L,MINING_TOP_X_U);
        conf.top_x_dec = std::clamp(conf.top_x_dec,MINING_TOP_X_DEC_L,MINING_TOP_X_DEC_U);
        conf.vol_pool_p = std::clamp(conf.vol_pool_p,MINING_VOL_POOL_P_L,MINING_VOL_POOL_P_U);
        conf.vol_lastvol_x = std::clamp(conf.vol_lastvol_x,MINING_VOL_LASTVOL_X_L,MINING_VOL_LASTVOL_X_U);
        conf.count_limit = std::clamp(conf.count_limit,MINING_COUNT_LIMIT_L,MINING_COUNT_LIMIT_U);        
        conf.action_limit = std::clamp(conf.action_limit,MINING_ACTION_LIMIT_L,MINING_ACTION_LIMIT_U);
        conf.fee_2_profit_p = std::clamp(conf.fee_2_profit_p,MINING_FEE_2_PROFIT_P_L,MINING_FEE_2_PROFIT_P_U);

        return conf;
    }

    static void Set(const mconfig& conf){
        MConfig conf_table(MCONFIG_ACCOUNT,MCONFIG_ACCOUNT.value);  
        conf_table.set(conf,MCONFIG_ACCOUNT);
    }
};

}