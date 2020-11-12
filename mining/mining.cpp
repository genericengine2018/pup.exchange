#include <eosio/eosio.hpp>  
#include <eosio/singleton.hpp>
#include <eosio/print.hpp>
#include <eosio/asset.hpp>
#include <eosio/system.hpp>
#include "../common/utils.hpp"
#include "../common/eos_utils.hpp"
#include "../common/config.hpp"
#include "../common/mconfig.hpp"

namespace puppy {

using namespace eosio;
using std::string;

constexpr name   WRAPPER_ACCOUNT = "puppynetwork"_n;

CONTRACT mining : public eosio::contract {
private:
  struct account {
    asset    balance;
    uint64_t primary_key()const { return balance.symbol.code().raw(); }
  };
  typedef eosio::multi_index<"accounts"_n,account> accounts;

  struct token {
    uint64_t id;
    extended_symbol symbol; 
    uint64_t pool;
    uint64_t pup_pool;
    uint64_t utime;

    uint64_t primary_key() const {return id;}
    uint128_t global_key() const { return (uint128_t)symbol.get_contract().value<<64|symbol.get_symbol().code().raw();}
    uint128_t utime_key() const {return (uint128_t)utime<<64|id;} 
  };
  typedef multi_index<"tokens"_n,token,
      indexed_by<"bygid"_n, const_mem_fun<token, uint128_t, &token::global_key>>,
      indexed_by<"byutime"_n, const_mem_fun<token, uint128_t, &token::utime_key>>
  > tokens;

  struct [[eosio::table]] feepool {
    uint64_t id;
    uint64_t tid;
    uint64_t round;
    uint64_t last_vol;
    uint64_t vol;
    uint64_t mined;
    uint64_t utime;

    uint64_t  primary_key() const { return id; }
    uint128_t rtid_key() const {return (uint128_t(round)<<64)|tid;} 
    uint128_t utid_key() const {return (uint128_t(utime)<<64)|tid;}
    uint128_t rvol_key() const {return (uint128_t(round)<<64)|vol;}
  };
  typedef eosio::multi_index<"feepools"_n,feepool,
    indexed_by<"byrtid"_n,const_mem_fun<feepool,uint128_t,&feepool::rtid_key>>,
    indexed_by<"byrvol"_n, const_mem_fun<feepool, uint128_t, &feepool::rvol_key>>,    
    indexed_by<"byutid"_n, const_mem_fun<feepool, uint128_t, &feepool::utid_key>>
  > feepools;

  struct [[eosio::table]] mineral {
    uint64_t vol;
    uint64_t vol_cap;
    uint64_t start;
    uint64_t limit;
    uint64_t tvl;
  };
  typedef eosio::singleton<"mineral"_n,mineral> Mineral;

  const uint64_t now;
  token pup;
  mconfig conf;

public:
  mining(name receiver, name code,  datastream<const char*> ds) : 
    contract::contract(receiver, code, ds),
    now(current_time()),
    pup{PUP_TOKEN_ID,extended_symbol(symbol(PUP_NAME,PUP_PREC),PUP_CONTRACT),1,1},
    conf{MConfHelper::Get(WRAPPER_ACCOUNT,WRAPPER_ACCOUNT)} {
  }

  [[eosio::on_notify("*::transfer")]]
  void ontransfer(name from, name to, asset quantity, string memo)
  {      
    if(to!=get_self())return;
    check(from!=to,"can't transfer to self.");
    check(quantity.amount>0,"must transfer positive quantity.");

    std::map<string,string> params = parse(memo);
    auto itr = params.find("action");
    string action = "";
    if(itr!=params.end())action = itr->second;

    name contract = get_first_receiver();
    if(action=="fee")SaveFee(from,contract,quantity,params);
    else if(action=="mineral")IncMineral(from,contract,quantity,params);
  }

  [[eosio::action]]
  void miningon(uint64_t nonce){
    Mineral mt(get_self(),get_self().value);
    mineral m = mt.get_or_default();      

    uint64_t start =  now/conf.round_period * conf.round_period;
    if(start>m.start) {
      Mine(m);
      mt.set(m,get_self());
      return;
    }

    // move past round fees to profits, copy last fee vol and erase processed feepool records.
    feepools feepools_table(get_self(),get_self().value);
    auto by_rtid = feepools_table.get_index<"byrtid"_n>();
    auto fitr = by_rtid.begin();    
    uint64_t round = m.start/conf.round_period;
    uint64_t to_mining_pool = 0;

    for(int i=0;i<conf.step_limit && fitr!=by_rtid.end() && fitr->round<round; i++){
      if(fitr->vol > 0){ 
        uint64_t fee_2_profit = fitr->vol*conf.fee_2_profit_p;
        uint64_t mined = fee_2_profit + fitr->mined;
        string memo = "action=profit;tid="+std::to_string(fitr->tid);
        TransferOut(PROFIT_ACCOUNT,mined,pup,memo);

        to_mining_pool += fitr->vol - fee_2_profit;

        auto next_fitr = by_rtid.find((uint128_t)(fitr->round+1)|fitr->tid);
        if(next_fitr==by_rtid.end()) feepools_table.emplace(get_self(),[&](auto& p){
          p.id = AvailPrimKey(feepools_table);
          p.tid = fitr->tid;
          p.round = fitr->round+1;
          p.last_vol = fitr->vol;
          p.vol = 0;
          p.utime = now;
        });
        else feepools_table.modify(feepools_table.find(next_fitr->id),get_self(),[&](auto& p){
          p.last_vol = fitr->vol;
          p.utime = now;
        });
      }

      feepools_table.erase(feepools_table.find(fitr->id));
      fitr = by_rtid.begin();
    }

    m.vol += to_mining_pool;
    m.vol_cap += to_mining_pool;
    mt.set(m,get_self());
  }

private:
  void SaveFee(const name& from,const name& contract,const asset& quantity,const std::map<string,string>& params){
    const extended_symbol src_sym = extended_symbol(quantity.symbol,contract);  
    check(src_sym == pup.symbol,"fee must be in PUP.");

    auto itr = params.find("src");
    check(itr!=params.end(),"missing param src");
    uint64_t src = std::stoull(string(itr->second));

    itr = params.find("dst");
    check(itr!=params.end(),"missing param dst");
    uint64_t dst = std::stoull(string(itr->second));

    uint64_t fee = quantity.amount;
    uint64_t src_fee = src==PUP_TOKEN_ID ? 0 : ( (dst==PUP_TOKEN_ID) ? 0 : fee/2 );
    if(src_fee>0)SaveFee(src,src_fee);

    uint64_t dst_fee = fee - src_fee;
    if(dst_fee>0)SaveFee(dst,dst_fee);    
  }

  void SaveFee(uint64_t tid,uint64_t fee){
    uint64_t round = now / conf.round_period;

    feepools feepools_table(get_self(),get_self().value);
    auto by_rtid = feepools_table.get_index<"byrtid"_n>();
    auto fitr = by_rtid.find( (uint128_t)round<<64|tid );

    if(fitr==by_rtid.end()) feepools_table.emplace(get_self(),[&](auto& p){
      p.id = AvailPrimKey(feepools_table);
      p.tid = tid;
      p.round = round;
      p.vol = fee;
      p.utime = now;
    });
    else feepools_table.modify(feepools_table.find(fitr->id),get_self(),[&](auto& p){
      p.vol += fee;
      p.utime = now;
    });
  }

  void IncMineral(const name& from,const name& contract,const asset& quantity,const std::map<string,string>& params){
    const extended_symbol src_sym = extended_symbol(quantity.symbol,contract);  
    check(src_sym == pup.symbol,"mineral must be in pup.");

    Mineral mt(get_self(),get_self().value);
    mineral m = mt.get_or_default();
    m.vol_cap += quantity.amount;
    m.vol += quantity.amount;
    mt.set(m,get_self());
  }

  void Mine(mineral& m){
    // Mine should be called once at the begining of new round by design.
    // the change on mineral instance should be written to db by the caller.
    uint64_t round =  now/conf.round_period;
    if(round<=m.start/conf.round_period) return;
    uint64_t lastround = round-1;

    m.start = round * conf.round_period;
    m.tvl = TVL();   
    m.limit = MineLimit(m);   

    if(m.vol==0 || m.limit==0)return;

    //find top tokens for last round and sum expected total mining reward.
    feepools feepools_table(get_self(),get_self().value);
    if(feepools_table.begin()==feepools_table.end()) return;

    auto by_rvol = feepools_table.get_index<"byrvol"_n>();
    auto fitr = by_rvol.lower_bound((uint128_t)round<<64);
    if(fitr->round==round && fitr!=by_rvol.begin()) fitr--;
    if(fitr->round!=lastround) return; //no last round record exists.

    std::vector<feepool> topfps;
    std::vector<uint64_t> mined_expects;
    uint64_t mined_expect_sum = 0;
    double times = conf.top_x;
    tokens tokens_table(conf.core_account,conf.core_account.value);

    for(int i=0;i<conf.top_num && 
        fitr!=by_rvol.end() && 
        fitr->round==lastround && 
        fitr->vol >= conf.top_vol_min; i++){
      topfps.emplace_back(*fitr);

      const token tk = tokens_table.get(fitr->tid);

      uint64_t vol = std::min(fitr->last_vol*conf.vol_lastvol_x, fitr->vol);
      vol = vol*times;
      uint64_t mined_expect = std::min(vol,(uint64_t)(tk.pup_pool*conf.vol_pool_p));
      
      mined_expect_sum += mined_expect;
      mined_expects.emplace_back(mined_expect);

      times *= conf.top_x_dec;

      if(fitr==by_rvol.begin())break;
      else fitr--;
    }

    //cap total mining reward 
    uint64_t mined_sum = std::min(mined_expect_sum, m.limit);
    double shrink_p = mined_expect_sum>0 ? mined_sum / (double)mined_expect_sum : 0;

    //write to feepools
    for(int i=0;i<topfps.size();i++){
      const feepool& fp = topfps[i];
      uint64_t tk_mined = mined_expects[i] * shrink_p;
      feepools_table.modify(feepools_table.find(fp.id),get_self(),[&](auto& f){
        f.mined = tk_mined;
        //do not update utime here because 'mined' need to be updated in real time 
        //on the client side, so updating utime here for once does not work.
        //basically utime is for the updating of 'vol'.
      });
    }

    m.vol -= mined_sum;
  }

  uint64_t MineLimit(const mineral& m){
    uint64_t limit = m.vol*conf.round_p;
    limit = std::min(limit,(uint64_t)(m.tvl*conf.round_tvl_p));
    limit = std::min(limit,conf.round_max);
    return limit;  
  }

  uint64_t TVL(){
    accounts accounts_table(pup.symbol.get_contract(),conf.core_account.value);
    auto aitr = accounts_table.find(pup.symbol.get_symbol().code().raw());
    return aitr->balance.amount;
  }

  void TransferOut(name to,uint64_t amount, const token& tk,string memo){
    print("transferring out ",amount," in ",tk.symbol.get_symbol().code().to_string(), "\n");
    
    if(amount==0)return;
    if(tk.id==0)return;
    asset quantity(amount,tk.symbol.get_symbol());
    action(eosio::permission_level(get_self(), "active"_n),
      tk.symbol.get_contract(), "transfer"_n,
      std::make_tuple(get_self(), to, quantity, memo)
    ).send(); 
  }

};

} //namepsace puppy