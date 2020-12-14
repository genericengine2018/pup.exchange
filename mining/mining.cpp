#include <eosio/eosio.hpp>  
#include <eosio/singleton.hpp>
#include <eosio/print.hpp>
#include <eosio/asset.hpp>
#include <eosio/system.hpp>
#include "../common/utils.hpp"
#include "../common/eos_utils.hpp"
#include "../common/mconfig.hpp"

namespace puppy {
namespace mining {

using namespace eosio;
using std::string;

const uint64_t   DAY_MS = 24*3600*1000;

CONTRACT mining : public contract {
private:
  struct account {
    asset    balance;
    uint64_t primary_key()const { return balance.symbol.code().raw(); }
  };
  typedef multi_index<"accounts"_n,account> accounts;

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

  struct banker {
    uint64_t id;
    uint64_t uid;
    uint64_t tid;
    uint64_t vol;
    uint64_t pup_vol;
    uint64_t utime;

    uint64_t primary_key() const { return id; }
    uint128_t tuid_key() const {return uint128_t(tid)<<64|uid;} 
    uint128_t utime_key() const {return uint128_t(utime)<<64|id;}    
    uint64_t pupvol_key() const {return pup_vol;}
    uint128_t tvol_key() const {return uint128_t(tid)<<64|vol;}
  };
  typedef multi_index<"bankers"_n,banker,
    indexed_by<"bytuid"_n,const_mem_fun<banker,uint128_t,&banker::tuid_key>>,
    indexed_by<"byutime"_n, const_mem_fun<banker, uint128_t, &banker::utime_key>>,
    indexed_by<"bypupvol"_n,const_mem_fun<banker, uint64_t, &banker::pupvol_key>>,
    indexed_by<"bytvol"_n,const_mem_fun<banker, uint128_t, &banker::tvol_key>>  
  > bankers;
  
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
  typedef multi_index<"feepools"_n,feepool,
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
  typedef singleton<"mineral"_n,mineral> Mineral;

  struct [[eosio::table]] profit {
    uint64_t id;
    uint64_t tid;
    uint64_t day;
    uint64_t vol;
    uint64_t used_vol;
    uint64_t utime;

    uint64_t primary_key() const { return id; }
    uint128_t dtid_key() const {return (uint128_t(day)<<64)|tid;} 
    uint128_t utid_key() const {return (uint128_t(utime)<<64)|tid;}
  };
  typedef multi_index<"profits"_n,profit,
    indexed_by<"bydtid"_n,const_mem_fun<profit,uint128_t,&profit::dtid_key>>,
    indexed_by<"byutid"_n, const_mem_fun<profit, uint128_t, &profit::utid_key>>
  > profits;

  struct [[eosio::table]] income {
    name user;
    uint64_t vol;
    uint64_t primary_key() const { return user.value; }
  };
  typedef multi_index<"incomes"_n,income> incomes;

  struct [[eosio::table]] pool24 {
    uint64_t tid;    
    uint64_t day;
    uint64_t vol;
    uint64_t pup_vol;
    uint64_t primary_key() const {return tid;}
    uint64_t day_key() const {return day;}
  };
  typedef multi_index<"pool24s"_n,pool24,
    indexed_by<"byday"_n,const_mem_fun<pool24,uint64_t,&pool24::day_key>>
  > pool24s;

  struct [[eosio::table]] settlement {
    bool on;
    uint64_t day;
    int stage;
    uint64_t tid;
    uint64_t uid;
  };
  typedef singleton<"settlement"_n,settlement> Settlement;

  const uint64_t now;
  token pup;
  mconfig conf;

public:
  mining(name receiver, name code,  datastream<const char*> ds) : 
    contract::contract(receiver, code, ds),
    now( current_time() ),
    pup{ PUP_TOKEN_ID, extended_symbol(symbol(PUP_NAME,PUP_PREC),PUP_CONTRACT), 1, 1 },
    conf{ MConfHelper::Get(get_self()) } {
  }

  [[eosio::action]]
  void cleanup(uint64_t nonce){
    int i = 0;

    profits profits_table(get_self(),get_self().value);
    auto pitr = profits_table.begin();
    uint64_t day_bound = now/DAY_MS - 3;
    if(pitr!=profits_table.end()) {
      uint64_t lastid = profits_table.rbegin()->id;
      for(; i < conf.cleanup_limit &&
            pitr->id < lastid &&
            pitr->day <= day_bound; i++){
        pitr = profits_table.erase(pitr);
      }
      if(i==conf.cleanup_limit)return;      
    }   

    pool24s pool24s_table(get_self(),get_self().value);
    auto by_day = pool24s_table.get_index<"byday"_n>();
    auto p24itr = by_day.begin();
    for(; i < conf.cleanup_limit && 
          p24itr != by_day.end() && 
          p24itr->day <= day_bound; i++){
      pool24s_table.erase( pool24s_table.find(p24itr->tid) );
      p24itr = by_day.begin();
    }
  }

  [[eosio::on_notify("*::transfer")]]
  void ontransfer(name from, name to, asset quantity, string memo){      
    if(to!=get_self()) return;
    check(from!=to,"can't transfer to self.");
    check(quantity.amount>0,"must transfer positive quantity.");

    std::map<string,string> params = parse(memo);
    auto itr = params.find("action");
    string action = "";
    if(itr!=params.end()) action = itr->second;

    name contract = get_first_receiver();
    if(action=="fee") SaveFee(from,contract,quantity,params);
    else if(action=="mineral") IncMineral(from,contract,quantity,params);
  }

  [[eosio::action]]
  void withdraw(name user,double amount){
    require_auth(user);

    incomes incomes_table(get_self(),get_self().value);
    auto iitr = incomes_table.find(user.value);
    check( iitr!=incomes_table.end(), "income data not found.");

    uint64_t iamount = amount*PUP_TIMES;
    check(iamount>0,"withdraw amount too small.");
    check(iamount<=iitr->vol,"over withdrawn.");

    if(iamount==iitr->vol) incomes_table.erase(iitr);
    else incomes_table.modify(iitr,get_self(),[&](auto& i){
      i.vol -= iamount;
    });

    TransferOut(user,iamount,pup,"banker income withdrawal."); 
  }

  [[eosio::action]]
  void miningon(uint64_t nonce){
    Mineral mt(get_self(),get_self().value);
    mineral m = mt.get_or_default();      

    uint64_t start =  now/conf.round_period * conf.round_period;
    if(start>m.start) { // do mining at the begining of each round and leave fee jobs to next calls.
      Mine(m); 
      mt.set(m,get_self());
      return; 
    }

    feepools feepools_table(get_self(),get_self().value);
    auto by_rtid = feepools_table.get_index<"byrtid"_n>();
    auto fitr = by_rtid.begin();    
    uint64_t round = m.start/conf.round_period; //current round
    if(fitr == by_rtid.end() || fitr->round >= round) return; //nothing to process.

    // fee jobs - move fees of closed rounds to profits, copy last vols and erase processed records.
    uint64_t to_mining_pool = 0;
    for(int i=0; 
        i < conf.action_limit &&
        fitr != by_rtid.end() &&
        fitr->round < round; i++) {
      if(fitr->vol > 0) { 
        uint64_t fee_2_profit = fitr->vol * conf.fee_2_profit_p;
        uint64_t profit = fee_2_profit + fitr->mined;
        IncProfit(fitr->tid, profit);

        to_mining_pool += fitr->vol - fee_2_profit;

        auto next_fitr = by_rtid.find( (uint128_t)(fitr->round+1)|fitr->tid );
        if(next_fitr==by_rtid.end()) feepools_table.emplace(get_self(),[&](auto& p){
          p.id = AvailPrimKey(feepools_table);
          p.tid = fitr->tid;
          p.round = fitr->round+1;
          p.vol = 0;          
          p.last_vol = fitr->vol;
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

    if(to_mining_pool>0) {
      m.vol += to_mining_pool;
      m.vol_cap += to_mining_pool;
      mt.set(m,get_self());
    }
  }

  [[eosio::action]]
  void settleon(uint64_t nonce) {
    Settlement st(get_self(),get_self().value);
    settlement s = st.get_or_default();
    if(!s.on) { 
      uint64_t day =  now / DAY_MS;
      if(day > s.day) { //do settlement at the beginning of each new day.
        s.on = true;
        s.day = day;
        s.stage = 0;
        s.tid = 0;
        s.uid = 0;
      }
      else return;
    }

    uint64_t lastday = s.day-1;
    profits profits_table(get_self(),get_self().value);
    auto by_dtid = profits_table.get_index<"bydtid"_n>();
    auto pfitr = by_dtid.lower_bound( (uint128_t)lastday<<64 | s.tid );
    if(pfitr!=by_dtid.end()) s.tid = pfitr->tid;

    int count = 0;
    for(int t=0; t < conf.action_limit &&
          pfitr != by_dtid.end() &&
          pfitr->day == lastday; t++) {
      bool more = false;

      if(pfitr->vol==0) {
        count++;
        more = count<conf.count_limit;
      }
      else if(s.stage==0) {
        more = SettleStage1(s,count);
      }
      else if(s.stage==1) {
        profit pf = *pfitr;
        if(pf.vol <= pf.used_vol) more=true;
        else {        
          more = SettleStage2(s,pf,count);
          pf.utime = now;
          profits_table.modify(profits_table.find(pfitr->id),get_self(),[&](auto& p){p=pf;});
        }

        //extra processing when current token is done
        if(s.uid==0 || more) { 
          //clear p24 for this token.
          pool24s pool24s_table(get_self(),get_self().value);
          auto p24itr = pool24s_table.find(s.tid);
          if(p24itr!=pool24s_table.end()) pool24s_table.erase(p24itr);

          //move unused part to mining reserve or next day profit.
          bool moved = true;
          if( pf.used_vol == 0) { //meaning no valid bankers yet.
            IncMineral(pf.vol);
          }
          else if( pf.vol >= (pf.used_vol+10) ) { //with 10 as tolerance
            profit pf_nextday{};
            auto pfitr_nextday = by_dtid.find( (uint128_t)(lastday+1)<<64|pfitr->tid );
            if(pfitr_nextday!=by_dtid.end()) pf_nextday = *pfitr_nextday;
            else { 
              pf_nextday.id = AvailPrimKey(profits_table);
              pf_nextday.tid = pfitr->tid;
              pf_nextday.day = lastday+1;
            }

            pf_nextday.vol += pf.vol-pf.used_vol;
            pf_nextday.utime = now;

            if(pfitr_nextday==by_dtid.end()) profits_table.emplace(get_self(),[&](auto& p){p=pf_nextday;});
            else profits_table.modify(profits_table.find(pfitr_nextday->id),get_self(),[&](auto& p){p=pf_nextday;});
          }
          else moved = false;

          if(moved) {
            pf.used_vol = pf.vol;
            pf.utime = now;
            profits_table.modify(profits_table.find(pfitr->id),get_self(),[&](auto& p){p=pf;});
          }
        }
      }
      
      if(s.uid==0 || more) {
        pfitr++;
        s.tid = pfitr->tid;
        s.uid = 0;
      }
      
      if(!more) break;
    }

    if(pfitr==by_dtid.end() || pfitr->day!=lastday) { //move to next stage.
      s.stage++;
      s.tid = 0;
      s.uid = 0;
    }

    if(s.stage>1) s.on = false; //settlement is done.

    st.set(s,get_self());
  }

private:
  void SaveFee(const name& from,const name& contract,const asset& quantity,const std::map<string,string>& params){
    const extended_symbol src_sym = extended_symbol(quantity.symbol,contract);  
    check(src_sym == pup.symbol, "fee must be in PUP.");

    auto itr = params.find("src");
    check(itr!=params.end(),"missing param src");
    uint64_t src = std::stoull(string(itr->second));

    itr = params.find("dst");
    check(itr!=params.end(),"missing param dst");
    uint64_t dst = std::stoull(string(itr->second));

    uint64_t fee = quantity.amount;
    uint64_t src_fee = src==PUP_TOKEN_ID ? 0 : 
      ( (dst==PUP_TOKEN_ID) ? fee : fee/2 );
    SaveFee(src,src_fee);

    uint64_t dst_fee = fee - src_fee;
    SaveFee(dst,dst_fee);    
  }

  void SaveFee(uint64_t tid,uint64_t fee){
    if(fee==0) return;

    uint64_t round = now / conf.round_period;

    feepools feepools_table(get_self(),get_self().value);
    auto by_rtid = feepools_table.get_index<"byrtid"_n>();
    auto fitr = by_rtid.find( (uint128_t)round<<64 | tid );

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
    check(src_sym == pup.symbol, "mineral must be in pup.");

    IncMineral(quantity.amount);
  }

  void IncMineral(uint64_t amount){
    if(amount==0) return;

    Mineral mt(get_self(),get_self().value);
    mineral m = mt.get_or_default();
    m.vol_cap += amount;
    m.vol += amount;
    mt.set(m,get_self());
  }

  // Should be called once at the beginning of every new round.
  // the caller is in charge of saving the mineral object.
  void Mine(mineral& m){
    uint64_t round =  now/conf.round_period;
    uint64_t lastround = round-1;    
    if(round <= m.start/conf.round_period) return;

    m.start = round * conf.round_period;
    m.tvl = TVL();   
    m.limit = MineLimit(m);   
    if(m.vol==0 || m.limit==0) return;

    //find top tokens for last round and sum expected total mining rewards.
    feepools feepools_table(get_self(),get_self().value);
    if(feepools_table.begin()==feepools_table.end()) return;

    auto by_rvol = feepools_table.get_index<"byrvol"_n>();
    auto fitr = by_rvol.lower_bound( (uint128_t)round<<64 );
    if(fitr!=by_rvol.begin()) fitr--;
    if(fitr->round!=lastround) return; //no last-round record found.

    std::vector<feepool> topfps;
    std::vector<uint64_t> mined_expects;
    uint64_t mined_expect_sum = 0;
    double times = conf.top_x;
    tokens tokens_table(conf.core_account,conf.core_account.value);

    for(int i=0; i<conf.top_num && 
        fitr != by_rvol.end() && 
        fitr->round == lastround && 
        fitr->vol >= conf.top_vol_min; i++) {
      topfps.emplace_back(*fitr);

      const token tk = tokens_table.get(fitr->tid);

      uint64_t vol = std::min(fitr->vol, fitr->last_vol*conf.vol_lastvol_x);
      vol = vol*times;
      uint64_t mined_expect = std::min(vol, (uint64_t)(tk.pup_pool*conf.vol_pool_p));
      
      mined_expect_sum += mined_expect;
      mined_expects.emplace_back(mined_expect);

      times *= conf.top_x_dec;

      if(fitr==by_rvol.begin())break;
      else fitr--;
    }

    //cap total mining rewards 
    uint64_t mined_sum = std::min(mined_expect_sum, m.limit);
    double shrink_p = mined_expect_sum>0 ? mined_sum / (double)mined_expect_sum : 0;

    //write to feepools
    for(int i=0;i<topfps.size();i++) {
      const feepool& fp = topfps[i];
      uint64_t tk_mined = mined_expects[i] * shrink_p;
      feepools_table.modify(feepools_table.find(fp.id),get_self(),[&](auto& f){
        f.mined = tk_mined;
        //do not update utime here because 'mined' needs to be updated in real time 
        //on the client side, so updating utime here once as an ending shot does not work.
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

  void IncProfit(uint64_t tid,uint64_t amount){
    if(amount==0)return;
    
    uint64_t day = now/DAY_MS;
    profits profits_table(get_self(),get_self().value);
    auto by_dtid = profits_table.get_index<"bydtid"_n>();    
    auto pitr = by_dtid.find( (uint128_t)day<<64|tid );

    if(pitr==by_dtid.end()) profits_table.emplace(get_self(),[&](auto& p){
      p.id = AvailPrimKey(profits_table);
      p.tid = tid;
      p.day = day;
      p.vol = amount;
      p.utime = now;
    });
    else profits_table.modify(profits_table.find(pitr->id),get_self(),[&](auto& p){
      p.vol += amount;
      p.utime = now;
    });    
  }

  //return a boolean to indicate whether more processing is allowed.
  bool SettleStage1(settlement& s, int& count){
    bankers bankers_table(conf.core_account,conf.core_account.value);
    auto by_tuid = bankers_table.get_index<"bytuid"_n>();
    auto bitr = by_tuid.lower_bound( (uint128_t)s.tid<<64 | s.uid );
    if(bitr==by_tuid.end() || bitr->tid!=s.tid) return true;

    tokens tokens_table(conf.core_account,conf.core_account.value);
    token tk = tokens_table.get(s.tid);
    if(tk.pool==0 || tk.pup_pool==0) return true;

    uint64_t lastday = s.day-1;
    pool24s pool24s_table(get_self(),get_self().value);
    auto p24itr = pool24s_table.find(bitr->tid);
    pool24 p24{};
    if(p24itr!=pool24s_table.end() && p24itr->day==lastday){
      p24=*p24itr;    
      //correct previous p24 as the rate may change between calls.
      banker tfbk = *bitr;
      tfbk.vol = p24.vol;
      tfbk.pup_vol = p24.pup_vol;
      CorrectBankerVol(tfbk,tk);
      p24.vol = tfbk.vol;
      p24.pup_vol = tfbk.pup_vol;
    }
    else {
      p24.tid=tk.id;
      p24.day=lastday;
    }
    
    //count unqualified bankers into pool24
    uint64_t time_bound = (lastday+0.5)*DAY_MS;
    for(; count < conf.count_limit &&
          bitr != by_tuid.end() &&
          bitr->tid == tk.id; 
        count++, s.uid=bitr->uid+1, bitr++) {
      if(bitr->utime <= time_bound) continue;

      banker bk = *bitr;
      CorrectBankerVol(bk,tk);
      p24.vol += bk.vol;
      p24.pup_vol += bk.pup_vol;
    }

    if(p24.vol>0 && p24.pup_vol>0) {
      if(p24itr==pool24s_table.end()) pool24s_table.emplace(get_self(),[&](auto& p){p=p24;});
      else pool24s_table.modify(p24itr,get_self(),[&](auto& p){p=p24;}); 
    }

    if(bitr==by_tuid.end() || bitr->tid!=tk.id) s.uid=0;

    return count<conf.count_limit;
  }

  //return a boolean to indicate whether more processing is allowed.
  bool SettleStage2(settlement& s, profit& pf, int& count){
    bankers bankers_table(conf.core_account,conf.core_account.value);
    auto by_tuid = bankers_table.get_index<"bytuid"_n>();
    auto bitr = by_tuid.lower_bound( (uint128_t)s.tid<<64 | s.uid );
    if(bitr==by_tuid.end() || bitr->tid!=s.tid) return true;

    tokens tokens_table(conf.core_account,conf.core_account.value);
    token tk = tokens_table.get(s.tid);
    if(tk.pool==0 || tk.pup_pool==0) return true;

    uint64_t lastday = s.day-1;
    pool24s pool24s_table(get_self(),get_self().value);
    auto p24itr = pool24s_table.find(s.tid);
    pool24 p24{};
    if(p24itr!=pool24s_table.end() && p24itr->day==lastday){
      p24=*p24itr;    
      //correct p24 as the rate may change between calls.
      banker tfbk = *bitr;
      tfbk.vol = p24.vol;
      tfbk.pup_vol = p24.pup_vol;
      CorrectBankerVol(tfbk,tk);
      p24.vol = tfbk.vol;
      p24.pup_vol = tfbk.pup_vol;
    }
    else {
      p24.tid=tk.id;
      p24.day=lastday;
    }

    // cal effective token pool of last day (remove the p24 part)
    if(p24.vol>0 && p24.pup_vol>0) {
      tk.pool = tk.pool>p24.vol ? tk.pool-p24.vol : 0;
      tk.pup_pool = tk.pup_pool>p24.pup_vol ? tk.pup_pool-p24.pup_vol : 0;
    }
    if(tk.pool==0 || tk.pup_pool==0) return true;

    incomes incomes_table(get_self(),get_self().value);  
    uint64_t time_bound = (lastday+0.5)*DAY_MS;

    for(; count < conf.action_limit &&
          bitr != by_tuid.end() &&
          bitr->tid == tk.id; 
        count++, s.uid=bitr->uid+1, bitr++) {
      if(bitr->utime > time_bound) continue; //skip unqualified bankers.

      banker bk = *bitr;
      CorrectBankerVol(bk,tk);
      uint64_t share = pf.vol * (bk.pup_vol/(double)tk.pup_pool);
      if(share+pf.used_vol > pf.vol) share = pf.vol-pf.used_vol;

      if(share>0) {      
        pf.used_vol += share;
        pf.utime = now;

        auto iitr = incomes_table.find(bitr->uid);
        if(iitr==incomes_table.end()) incomes_table.emplace(get_self(),[&](auto& i){
          i.user = name(bitr->uid);
          i.vol = share;
        });
        else incomes_table.modify(iitr,get_self(),[&](auto& i){
          i.vol += share;
        });
      }
    }

    if(bitr==by_tuid.end() || bitr->tid!=tk.id) s.uid=0;

    return count<conf.action_limit;
  }

  void CorrectBankerVol(banker& bk,const token& tk){
    if(bk.vol==0 || bk.pup_vol==0) return;
    check( bk.tid == tk.id, "internal error.");
    check( bk.tid != PUP_TOKEN_ID, "PUP banker not allowed.");

    if(tk.pool==0 || tk.pup_pool==0){
      bk.vol=0;
      bk.pup_vol=0;
    }
    else {
      double volbase = sqrt(bk.vol) * sqrt(bk.pup_vol);
      double rate = tk.pool / (double)tk.pup_pool;
      rate = sqrt(rate);
      uint64_t vol = volbase * rate;
      uint64_t pup_vol = volbase / rate;
      bk.vol = vol;
      bk.pup_vol = pup_vol;
    }
  }

  void TransferOut(const name& to,uint64_t amount, const token& tk,const string& memo){
    print("transferring out ",amount," in ",tk.symbol.get_symbol().code().to_string(), "\n");
    
    if(amount==0 || tk.id==0)return;

    asset quantity(amount,tk.symbol.get_symbol());
    action(eosio::permission_level(get_self(), "active"_n),
      tk.symbol.get_contract(), "transfer"_n,
      std::make_tuple(get_self(), to, quantity, memo)
    ).send(); 
  }

};

} //namespace mining
} //namepsace puppy