#include <eosio/eosio.hpp>  
#include <eosio/singleton.hpp>
#include <eosio/print.hpp>
#include <eosio/asset.hpp>
#include <eosio/system.hpp>
#include "../common/utils.hpp"
#include "../common/eos_utils.hpp"
#include "../common/config.hpp"

namespace puppy {
namespace core {

using namespace eosio;
using std::string;

const uint64_t   INSTANT_FEE_TOLERANCE = PUP_TIMES;
constexpr double ORDER_PRICE_DOUBLE_2_INT = 1e10;
constexpr double ORDER_PRICE_MIN = 1e-9;
constexpr double ORDER_PRICE_MAX = 1e9;
constexpr double ORDER_DONE_AMOUNT_LIMIT_P = 0.001;  

const uint64_t   CLEANUP_LIFE_WINDOW = 1800 * 1000;

CONTRACT core : public contract {
private:
  struct [[eosio::table]] token {
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

  struct [[eosio::table]] banker {
    uint64_t id;
    uint64_t uid;
    uint64_t tid;
    uint64_t vol;
    uint64_t pup_vol;
    uint64_t utime;

    uint64_t primary_key() const { return id; }
    uint128_t tuid_key() const {return (uint128_t)tid<<64|uid;} 
    uint128_t utime_key() const {return (uint128_t)utime<<64|id;}    
    uint64_t pupvol_key() const {return pup_vol;}
    uint128_t tvol_key() const {return (uint128_t)tid<<64|vol;}
  };
  typedef multi_index<"bankers"_n,banker,
    indexed_by<"bytuid"_n,const_mem_fun<banker,uint128_t,&banker::tuid_key>>,
    indexed_by<"byutime"_n, const_mem_fun<banker, uint128_t, &banker::utime_key>>,
    indexed_by<"bypupvol"_n,const_mem_fun<banker, uint64_t, &banker::pupvol_key>>,
    indexed_by<"bytvol"_n,const_mem_fun<banker, uint128_t, &banker::tvol_key>>  
  > bankers;

  struct [[eosio::table]] order {
    uint64_t id;
    name     user;
    uint64_t src;
    uint64_t dst;
    double   p;
    double   int_p;
    uint64_t src_amount;
    uint64_t src_filled;
    uint64_t dst_filled;
    uint64_t ctime;
    uint64_t utime;
    uint64_t etime;
    int16_t  p_depth;
    bool     p_rev;
    bool     cancelled;

    uint64_t  primary_key() const {return id;}
    uint128_t pairprice_key() const {return IsDone() ? 0 : (uint128_t)Pid()<<64 | (uint64_t)(p*ORDER_PRICE_DOUBLE_2_INT);}
    uint128_t utime_key() const {return (uint128_t)utime<<64 | id;}
    uint64_t  etime_key() const {return etime;}

    uint64_t Pid() const {return src<<32 | dst;}
    uint64_t SrcAmount() const {return src_amount-src_filled;}
    uint64_t DstAmount() const {return src>dst ? SrcAmount()*int_p : SrcAmount()/int_p;}
    bool IsDone() const {return cancelled || SrcAmount()<src_amount*ORDER_DONE_AMOUNT_LIMIT_P;}
  };
  typedef multi_index<"orders"_n,order,
    indexed_by<"bypairprice"_n, const_mem_fun<order, uint128_t, &order::pairprice_key>>,
    indexed_by<"byutime"_n, const_mem_fun<order, uint128_t, &order::utime_key>>,
    indexed_by<"byetime"_n, const_mem_fun<order, uint64_t, &order::etime_key>>
  > orders;

  struct feehelper {
    uint64_t id;    
    uint64_t amount;
    uint64_t ctime;
    uint64_t primary_key() const {return id;}
  };
  typedef multi_index<"feehelpers"_n,feehelper> feehelpers;

  const uint64_t now;
  token pup;
  config conf;

public:
  core(name receiver, name code,  datastream<const char*> ds) : 
    contract::contract(receiver, code, ds), 
    now( current_time() ),
    pup{ PUP_TOKEN_ID, extended_symbol(symbol(PUP_NAME,PUP_PREC),PUP_CONTRACT), 1, 1 },
    conf{ ConfHelper::Get(get_self()) } {
  }

  [[eosio::action]]
  void cleanup(uint64_t nonce){
    int i=0;

    // clean orders
    orders orders_table(get_self(),get_self().value);
    uint64_t lastid = AvailPrimKey(orders_table)-1;
    auto by_pairprice = orders_table.get_index<"bypairprice"_n>();
    auto oitr = by_pairprice.begin();
    for(; i<conf.cleanup_limit &&
          oitr!=by_pairprice.end() &&
          oitr->IsDone() && 
          oitr->id!=lastid &&
          now - oitr->utime > CLEANUP_LIFE_WINDOW; i++){
      orders_table.erase(*oitr);
      oitr = by_pairprice.begin();
    }    
    if(i==conf.cleanup_limit)return;  

    // revoke expired open orders
    auto by_etime = orders_table.get_index<"byetime"_n>();
    auto eitr = by_etime.begin();
    for(; i<conf.cleanup_limit &&
          eitr!=by_etime.end() &&
          !eitr->IsDone() &&
          eitr->etime < now; i+=10){
      DoRevoke(*eitr,orders_table);
      eitr = by_etime.begin();
    }    
    if(i==conf.cleanup_limit)return;  

    // clean bankers.
    bankers bankers_table(get_self(),get_self().value);
    auto by_pupvol = bankers_table.get_index<"bypupvol"_n>();
    auto bitr = by_pupvol.begin();
    for(;i<conf.cleanup_limit && 
        bitr!=by_pupvol.end() && 
        bitr->pup_vol==0 &&
        now - bitr->utime > CLEANUP_LIFE_WINDOW; i++){
      bankers_table.erase( bankers_table.find(bitr->id) );
      bitr = by_pupvol.begin();
    }
    if(i==conf.cleanup_limit)return;    

    // clean feehelpers if any.
    feehelpers feehelpers_table(get_self(),get_self().value);
    auto fitr = feehelpers_table.begin();
    for(; i<conf.cleanup_limit&& fitr!=feehelpers_table.end(); i++){
      fitr = feehelpers_table.erase(fitr);
    }
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
    if(itr!=params.end()) action = itr->second;

    name contract = get_first_receiver();
    if(action=="fee") SaveFee(from,contract,quantity,params);
    else if(action=="convert") Convert(from,contract,quantity,params); 
    else if(action=="add") Deepen(from,contract,quantity,params);
    else if(action=="place") Place(from,contract,quantity,params);
    else if(action=="fill") Fill(from,contract,quantity,params); 
    else if(action=="topup") Topup(from,contract,quantity,params);    
  }

  [[eosio::action]]
  void withdraw(name user,uint64_t tid, double p) {
    require_auth(user);
    check(p>0&&p<=1,"invalid param p.");

    tokens tokens_table(get_self(),get_self().value);
    token tk = tokens_table.get(tid);  

    bankers bankers_table(get_self(),get_self().value);
    auto by_tuid = bankers_table.get_index<"bytuid"_n>();
    auto tuitr = by_tuid.find( uint128_t(tid)<<64|user.value );

    check(tuitr!=by_tuid.end(),"No deposit found.");
    
    banker bk = bankers_table.get(tuitr->id);
    check(user.value==bk.uid,"invalid user.");

    CorrectBankerVol(bk,tk);
    
    uint64_t dec = bk.vol*p;
    p = dec/(double)bk.vol;//correct p as the precision of the token may not be strong enough.
    uint64_t pup_dec = bk.pup_vol*p;

    check(dec>0&&pup_dec>0,"Withdraw amount is too small.");
    check(tk.pup_pool>conf.pup_pool_min,"Pool insufficient.");

    //correct withdraw amount
    uint64_t pup_dec_limit = tk.pup_pool - conf.pup_pool_min;
    if(pup_dec>pup_dec_limit) { //correct by the minimum reserve limit.
      pup_dec = pup_dec_limit;
      p = pup_dec / (double)bk.pup_vol;
      dec = bk.vol*p;
      p = dec/(double)bk.vol;
      pup_dec = bk.pup_vol*p;

      check(dec>0&&pup_dec>0,"Withdraw amount is too small.");      
      if(pup_dec>pup_dec_limit) pup_dec = pup_dec_limit;
    }
    else if(bk.pup_vol - pup_dec < conf.pup_pool_min) { //clear the banker if too little amount left.
      pup_dec = bk.pup_vol;
    }

    //clear both sides if one side is cleared.
    if(dec==bk.vol || pup_dec==bk.pup_vol){
      dec = bk.vol;
      pup_dec = bk.pup_vol;
    }
    
    bk.vol -= dec;
    bk.pup_vol -= pup_dec;
    bk.utime = now;
    auto bitr = bankers_table.find(bk.id);
    bankers_table.modify(bitr,get_self(),[&](auto& b){b=bk;});

    tk.pool -= dec;
    tk.pup_pool -= pup_dec;
    tk.utime = now;
    tokens_table.modify(tokens_table.find(tk.id),get_self(),[&](auto& t){t=tk;});

    TransferOut(user,dec,tk,"Fund withdrawal.");
    TransferOut(user,pup_dec,pup,"Fund withdrawal");
  }

  [[eosio::action]]
  void revoke(uint64_t oid){
    orders orders_table(get_self(),get_self().value);
    auto oitr = orders_table.find(oid);
    check(oitr!=orders_table.end(),"invalid order id.");
    check(!oitr->IsDone(),"revoke - order already closed.");
    require_auth(oitr->user);

    DoRevoke(*oitr,orders_table);
  }

  [[eosio::action]]
  void instant(uint64_t oid,uint64_t src_amount){
    orders orders_table(get_self(),get_self().value);
    order ord = orders_table.get(oid);
    check(!ord.IsDone(),"instant - order already closed.");
    check(src_amount>0,"src amount must not be zero.");
    check(src_amount<=ord.SrcAmount(),"invalid src amount.");

    tokens tokens_table(get_self(),get_self().value);
    token src_tk = tokens_table.get(ord.src);
    token dst_tk = tokens_table.get(ord.dst);

    uint64_t dst_amount = DoConvert(src_tk,dst_tk,src_amount,0,false);
    Fill(ord,src_amount,dst_amount,orders_table,src_tk,dst_tk);
  }

  [[eosio::action]]
  void match(uint64_t oid1,uint64_t oid2){
    orders orders_table(get_self(),get_self().value);
    order ord1 = orders_table.get(oid1);
    order ord2 = orders_table.get(oid2);

    check(!ord1.IsDone(), "match - order already closed. "+std::to_string(ord1.id));
    check(!ord2.IsDone(), "match - order already closed. "+std::to_string(ord2.id));
    check(ord1.src==ord2.dst && ord1.dst==ord2.src, "invalid order pair.");

    tokens tokens_table(get_self(),get_self().value);
    token src_tk = tokens_table.get(ord1.src);
    token dst_tk = tokens_table.get(ord1.dst);

    uint64_t srcFilled = std::min(ord1.SrcAmount(),ord2.DstAmount());
    uint64_t dstFilled = srcFilled==ord2.DstAmount() ? 
        ord2.SrcAmount() : 
        (uint64_t)(ord2.src>ord2.dst ? srcFilled/ord2.int_p : srcFilled*ord2.int_p);

    if(srcFilled>0 && dstFilled>0){
      Fill(ord1,srcFilled,dstFilled,orders_table,src_tk,dst_tk);
      Fill(ord2,dstFilled,srcFilled,orders_table,dst_tk,src_tk);
    }
  }

private:
  void SaveFee(const name& from,const name& contract,const asset& quantity,const std::map<string,string>& params){
    const extended_symbol src_sym = extended_symbol(quantity.symbol,contract);  
    check(src_sym == pup.symbol,"fee must be in PUP.");

    feehelper fh;
    fh.id = from.value;
    fh.ctime = now;
    fh.amount = quantity.amount;
    feehelpers feehelpers_table(get_self(),get_self().value);
    auto fitr = feehelpers_table.find(from.value);
    if(fitr==feehelpers_table.end()) feehelpers_table.emplace(get_self(),[&](auto& f){f=fh;});
    else feehelpers_table.modify(fitr,get_self(),[&](auto& f){f=fh;});
  }

  void Convert(const name& from,const name& contract,const asset& quantity,const std::map<string,string>& params){
    token src_tk = GetToken(contract,quantity.symbol,false);
    
    auto itr = params.find("user");
    name user = from;
    if(itr!=params.end()) user = name(string(itr->second));

    itr = params.find("to");
    check(itr!=params.end(),"missing param to");
    uint64_t dst = std::stoull(string(itr->second));
    check(dst!=src_tk.id,"can't convert to self.");

    itr = params.find("paid_fee");
    uint64_t paid_fee = 0;
    if(itr!=params.end()) paid_fee = std::stoull(string(itr->second));

    itr = params.find("cb");
    string ret_memo="";
    if(itr!=params.end()){
      ret_memo = itr->second;
      std::replace(ret_memo.begin(),ret_memo.end(),'-','=');
      std::replace(ret_memo.begin(),ret_memo.end(),',',';');
      ret_memo += ";src="+std::to_string(src_tk.id)+";src_amount="+std::to_string(quantity.amount);
    }

    //check paid_fee
    uint64_t recorded_fee = 0;
    feehelpers feehelpers_table(get_self(),get_self().value);
    auto fitr = feehelpers_table.find(user.value);
    if( fitr!=feehelpers_table.end()){
      if(fitr->ctime == now) recorded_fee = fitr->amount;
      feehelpers_table.erase(fitr);
    }
    check(paid_fee==recorded_fee,"paid fee unmatched.");

    //do convert.
    tokens tokens_table(get_self(),get_self().value);
    token dst_tk = tokens_table.get(dst);
    uint64_t convert = DoConvert(src_tk,dst_tk,quantity.amount,paid_fee,true);

    //transfer out convert result.
    if(user!=get_self()){
      if(ret_memo=="")
        ret_memo = "convert of " + amount_tostring(quantity.amount,src_tk.symbol.get_symbol());
      TransferOut(user,convert,dst_tk,ret_memo);    
    }
  }

  void Deepen(const name& from,const name& contract,const asset& quantity,const std::map<string,string>& params){
    token src_tk = GetToken(contract,quantity.symbol,true);
 
    auto itr = params.find("to");
    uint64_t to_id = 0;
    if(itr!=params.end() && src_tk.id==PUP_TOKEN_ID) 
      to_id = std::stoull(string(itr->second));
    else to_id = src_tk.id;

    itr = params.find("user");
    name user = itr==params.end() ? from : name(itr->second);

    token dst_tk{}; 
    tokens tokens_table(get_self(),get_self().value);

    if(to_id==PUP_TOKEN_ID ){ 
      dst_tk = src_tk;
      dst_tk.pool += quantity.amount;
      dst_tk.pup_pool += quantity.amount;
      tokens_table.modify(tokens_table.find(dst_tk.id),get_self(),[&](auto& tk){
        tk = dst_tk;
        tk.utime = now;
      });
      return; //the PUP token does not hold bankers.
    }

    if(to_id == src_tk.id) dst_tk = src_tk;
    else if(to_id>0) dst_tk = tokens_table.get(to_id);
    else { 
      //new token will get 0 as to_id on pup side so we assign new id here.
      //valid token listing composes two actions in one tx, so it works.
      to_id = dst_tk.id = AvailPrimKey(tokens_table);
    }

    bankers bankers_table(get_self(),get_self().value);
    auto by_tuids = bankers_table.get_index<"bytuid"_n>();
    auto tuitr = by_tuids.find((uint128_t)to_id<<64|user.value);

    uint64_t bid = 0;
    if(tuitr==by_tuids.end()){
      banker bk{};
      bk.id = AvailPrimKey(bankers_table);
      bk.tid = to_id;
      bk.uid = user.value;
      bankers_table.emplace( get_self(), [&](auto& b){b=bk;});
      bid = bk.id;
    }
    else bid = tuitr->id;

    feehelpers fhs_table(get_self(),get_self().value);
    auto fhitr = fhs_table.find(bid);

    if(fhitr==fhs_table.end() || fhitr->ctime!=now ){ 
      // the first call of add (pup side)
      check(src_tk.id==PUP_TOKEN_ID,"deposit pup token first.");
      check(quantity.amount >= conf.pup_pool_min, "Minimum "+amount_tostring(conf.pup_pool_min,pup.symbol.get_symbol())+" required.");

      feehelper fh{};
      fh.id = bid;
      fh.amount = quantity.amount;
      fh.ctime = now;

      if(fhitr==fhs_table.end()) fhs_table.emplace( get_self(),[&](auto& f){f=fh;});
      else fhs_table.modify(fhitr,get_self(),[&](auto& f){ f=fh;});
    }
    else if(fhitr->ctime==now){ 
      // the second call of add (token side)
      uint64_t inc = quantity.amount;
      uint64_t pup_inc = fhitr->amount;  

      //clear helper
      fhs_table.erase(fhitr);
      
      //adjust the incs if it's not an initial set-up.
      if(dst_tk.pool>0 && dst_tk.pup_pool>0) {
        uint64_t needed_pup_inc = dst_tk.pup_pool * ( inc/(double)dst_tk.pool );

        //correct(reduce) the primary token side first if needed
        if(needed_pup_inc > pup_inc) { 
          uint64_t needed_inc = dst_tk.pool * ( pup_inc/(double)dst_tk.pup_pool );
          uint64_t dinc = inc - needed_inc;
          if(dinc>0) TransferOut(user,dinc,dst_tk,"over deposit return.");
          inc = needed_inc;
        }

        // correct(reduce) the pup side.
        // the pup side will always have to go over correction, as the precision loss by the primary token side may be significant.
        needed_pup_inc = dst_tk.pup_pool * ( inc/(double)dst_tk.pool );
        if(needed_pup_inc < pup_inc){
          uint64_t dinc = pup_inc - needed_pup_inc ;
          if(dinc>0) TransferOut(user,dinc,pup,"over deposit return."); 
          pup_inc = needed_pup_inc;    
        } 
      }   

      // check the incs.
      check(inc>0, "deposit too small.");
      check(pup_inc >= conf.pup_pool_min, amount_tostring(conf.pup_pool_min,pup.symbol.get_symbol())+
        " equivalent deposit required. Try increase the deposit amount.");

      // inc the banker.
      auto bitr = bankers_table.find(bid);
      banker bk = *bitr;
      CorrectBankerVol(bk,dst_tk);
      bk.vol += inc;
      bk.pup_vol += pup_inc;
      bk.utime = now;
      bankers_table.modify( bitr, get_self(), [&](auto& b){b=bk;});   
      
      // deepen the token pools at last.
      dst_tk.pool += inc;
      dst_tk.pup_pool += pup_inc;
      dst_tk.utime = now;
      tokens_table.modify(tokens_table.find(dst_tk.id),get_self(),[&](auto& tk){tk=dst_tk;});
    }
    else check(false,"invalid branch reached.");
  }

  void Place(const name& from,const name& contract,const asset& quantity,const std::map<string,string>& params){
    auto itr = params.find("dst");
    check(itr!=params.end(),"missing param dst");
    uint64_t dst_id = std::stoull(string(itr->second));

    tokens tokens_table(get_self(),get_self().value);
    token src_tk = GetToken(contract,quantity.symbol,false);
    token dst_tk = tokens_table.get(dst_id);

    // minimum amount check
    check( Amount(quantity.amount,src_tk,pup) >= conf.order_amount_min,
      "Order amount must be >= "+amount_tostring(Amount(conf.order_amount_min,pup,src_tk),src_tk.symbol.get_symbol()));

    // cal expiry time by fee
    feehelpers feehelpers_table(get_self(),get_self().value);
    auto fitr = feehelpers_table.find(from.value);
    if( fitr==feehelpers_table.end() || fitr->ctime!=now || fitr->amount<conf.order_day_fee ){
      check(false,"minimum "+amount_tostring(conf.order_day_fee,pup.symbol.get_symbol())+" required.");
    }
    uint64_t etime = now + fitr->amount/conf.order_day_fee * 24*3600*1000;

    // transfer out the fee and clear the fee record.
    TransferOut(MINING_ACCOUNT,fitr->amount,pup,"action=fee;src="+std::to_string(src_tk.id)+";dst="+std::to_string(dst_tk.id));
    feehelpers_table.erase(fitr);

    itr= params.find("p");
    check(itr!=params.end(),"missing param p");
    double p = Str2Float(string(itr->second));
    check(p>ORDER_PRICE_MIN && p<ORDER_PRICE_MAX, "Price must be in the range of "+std::to_string(ORDER_PRICE_MIN)+" - "+std::to_string(ORDER_PRICE_MAX));

    itr= params.find("depth");
    check(itr!=params.end(),"missing param depth");
    int depth = std::stoi(string(itr->second));

    itr= params.find("rev");
    bool rev = false;
    if(itr!=params.end())rev = std::stoi(string(itr->second))==1 ? true:false;

    if(rev) p = 1/p;       
    uint64_t src_times = PrecTimes(src_tk);
    uint64_t dst_times = PrecTimes(dst_tk);
    double int_p = src_tk.id>dst_tk.id ? p/src_times*dst_times : p/dst_times*src_times;

    // build the order.
    orders orders_table(get_self(),get_self().value);
    auto oitr = orders_table.emplace(get_self(),[&](auto& o){
      o.id = AvailPrimKey(orders_table);
      o.user = from;
      o.src = src_tk.id;
      o.dst = dst_id;
      o.p = p;        
      o.int_p = int_p;
      o.p_rev = rev;
      o.p_depth = depth;
      o.src_amount = quantity.amount;
      o.ctime = now;
      o.utime = now;
      o.etime = etime;
    });

    // notify the wrapper account if any.
    itr = params.find("notify");
    if(itr!=params.end()) require_recipient(name(itr->second));
  }

  void Topup(const name& from,const name& contract,const asset& quantity,const std::map<string,string>& params){
    auto itr = params.find("oid");
    check(itr!=params.end(),"missing param oid");
    uint64_t oid = std::stoull(string(itr->second));

    orders orders_table(get_self(),get_self().value);
    auto oitr = orders_table.find(oid);
    check(oitr!=orders_table.end(), "invalid order id.");
    check(!oitr->IsDone(), "topup - order already closed.");
    check(oitr->etime>=now, "order already expired.");
    check(oitr->user==from, "the order belongs to "+oitr->user.to_string());

    check(quantity.amount>=conf.order_day_fee, "insufficient fee.");
    uint64_t add_etime = quantity.amount/conf.order_day_fee * 24*3600*1000;

    TransferOut(MINING_ACCOUNT,quantity.amount,pup,"action=fee;src="+std::to_string(oitr->src)+";dst="+std::to_string(oitr->dst));

    orders_table.modify(oitr,get_self(),[&](auto& o){
      o.etime += add_etime;
      o.utime = now;
    });
  }

  void Fill(const name& from,const name& contract,const asset& quantity,const std::map<string,string>& params){
    token dst_tk = GetToken(contract,quantity.symbol,false);

    auto itr = params.find("oid");
    check(itr!=params.end(),"missing param oid");
    uint64_t oid = std::stoull(string(itr->second));

    itr = params.find("user");
    name user = from;
    if(itr!=params.end()) user = name(itr->second);

    orders orders_table(get_self(),get_self().value);
    order ord = orders_table.get(oid);
    check(!ord.IsDone(),"fill - order already closed.");
    check(ord.dst == dst_tk.id,"invalid filling token received.");

    tokens tokens_table(get_self(),get_self().value);
    token src_tk = tokens_table.get(ord.src);
    uint64_t dstFilled = quantity.amount;
    check(dstFilled<=ord.DstAmount(), "dst amount overflown.");

    uint64_t srcFilled = 0;
    if(ord.DstAmount() == dstFilled) srcFilled = ord.SrcAmount();
    else {
      double d_srcFilled = ord.src>ord.dst ? dstFilled/ord.int_p : dstFilled*ord.int_p;
      srcFilled = std::min((uint64_t)ceil(d_srcFilled),ord.SrcAmount());
    }

    string memo = "convert of " + amount_tostring(dstFilled,dst_tk.symbol.get_symbol());
    TransferOut(user,srcFilled,src_tk,memo);

    Fill(ord,srcFilled,dstFilled,orders_table,src_tk,dst_tk);
  }

  void CorrectBankerVol(banker& bk,const token& tk){
    if(bk.vol==0||bk.pup_vol==0)return;
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

  void ChargeFee(uint64_t& amount,token& src_tk,token& dst_tk,int64_t paid_fee,bool strict){
    int64_t needed_fee = CalFee(src_tk,dst_tk.id,amount);
    if(needed_fee==0)return;

    if(needed_fee-paid_fee >= (int64_t)INSTANT_FEE_TOLERANCE){
      uint64_t amount_to_fee = A_2_B_by_B(src_tk.pool,src_tk.pup_pool,needed_fee-paid_fee);
      if(strict && amount_to_fee>amount) 
        check(false,"amount too small to cover the fee.");

      amount_to_fee = std::min(amount_to_fee,amount);

      amount -= amount_to_fee;
      paid_fee += DoConvert(src_tk,pup,amount_to_fee,0,false);
    }

    if(paid_fee>0)
      TransferOut(MINING_ACCOUNT,paid_fee,pup,"action=fee;src="+std::to_string(src_tk.id)+";dst="+std::to_string(dst_tk.id));
  }

  uint64_t DoConvert(token& src_tk,token& dst_tk,uint64_t amount,uint64_t paid_fee,bool strict){
    check(src_tk.pool>0&&src_tk.pup_pool>0,"src_tk token not ready.");
    check(dst_tk.pool>0&&dst_tk.pup_pool>0,"dst_tk token not ready");

    ChargeFee(amount,src_tk,dst_tk,paid_fee,strict);
    if(amount==0)return 0;

    uint64_t convert = 0;
    if(src_tk.id == PUP_TOKEN_ID){
       convert = Buy(dst_tk,amount);
    }
    else if(dst_tk.id == PUP_TOKEN_ID){
      convert = Sell(src_tk,amount);
    }
    else {
      convert = Sell(src_tk,amount);
      convert = Buy(dst_tk,convert);
    }

    return convert;
  }

  void DoRevoke(const order& ord,orders& orders_table){
    orders_table.modify(ord,get_self(),[&](auto& o){
      o.cancelled = true;
      o.utime = now;
      o.etime = 0xffffffffffffffff;
    });

    if(ord.SrcAmount() > 0){
      tokens tokens_table(get_self(),get_self().value);
      token tk = tokens_table.get(ord.src);
      TransferOut(ord.user,ord.SrcAmount(),tk,"revoked order refund.");
    }
  }

  void Fill(order& ord,uint64_t src_filled,uint64_t dst_filled,orders& orders_table,const token& src_tk,const token& dst_tk){
    if(src_filled==0 || dst_filled==0) return;

    //check execution price.
    bool tobuy = ord.src<ord.dst;
    double avg_p = tobuy ? src_filled/(double)dst_filled : dst_filled/(double)src_filled;
    check((tobuy && ord.int_p>=(avg_p*0.999)) || (!tobuy && avg_p>=(ord.int_p*0.999)), 
      "order price not met.");

    src_filled = std::min(src_filled,ord.SrcAmount());

    ord.src_filled += src_filled;
    ord.dst_filled += dst_filled;
    ord.utime = now;

    orders_table.modify(orders_table.find(ord.id),get_self(),[&](auto& o){o=ord;});
    
    int filledP = round(src_filled*100/(double)ord.src_amount);
    TransferOut(ord.user,dst_filled,dst_tk,"order fill return ("+std::to_string(filledP)+"%)");

    if(ord.IsDone() && ord.SrcAmount()>0) {
      TransferOut(ord.user,ord.SrcAmount(),src_tk,"order unfilled part refund.");
    }
  }

  uint64_t Sell(token& tk,uint64_t amount){
    if(amount==0)return 0;

    uint64_t conv = A_2_B(tk.pool,tk.pup_pool,amount);
    check(tk.pup_pool >= conv+conf.pup_pool_min, "Pool insufficient. Try smaller amount.");    

    tk.pool += amount;
    tk.pup_pool -= conv;

    tokens tokens_table(get_self(),get_self().value);
    tk.utime = now;
    tokens_table.modify(tokens_table.find(tk.id),get_self(),[&](auto& t){
      t = tk;
    });

    return conv;
  }

  uint64_t Buy(token& tk,uint64_t amount){
    if(amount==0)return 0;
    
    uint64_t conv = A_2_B(tk.pup_pool,tk.pool,amount);    
    tk.pup_pool += amount;
    tk.pool -= conv;    

    tokens tokens_table(get_self(),get_self().value);
    tk.utime = now;
    tokens_table.modify(tokens_table.find(tk.id),get_self(),[&](auto& t){
      t = tk;
    });

    return conv;
  }

  uint64_t CalFee(const token& src,uint64_t dst_id,uint64_t amount){
    uint64_t fee = 0;
    if(dst_id!=PUP_TOKEN_ID){
      fee = src.pup_pool/(double)src.pool*amount*conf.instant_fee_p;
      fee = std::max(fee,conf.instant_fee_min);
    }
    return fee;
  }

  uint64_t A_2_B(uint64_t a,uint64_t b,uint64_t da){
    return da / (double)(a+da) * b;
  }

  uint64_t A_2_B_by_B(uint64_t a,uint64_t b,uint64_t db){
    if(db>=b) return 0xffffffffffffffff;
    return ceil( db / (double)(b-db) * a );
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

  token GetToken(const name& contract,const symbol& sym,bool force){
    tokens tokens_table(get_self(),get_self().value);
    auto tokens_gid = tokens_table.get_index<"bygid"_n>();
    token tk{};
    tk.symbol = extended_symbol(sym,contract);    
    auto gtitr = tokens_gid.find(tk.global_key());

    if(gtitr == tokens_gid.end()){
      if(force){
        tk.id = AvailPrimKey(tokens_table);
        tk.utime = now;
        tokens_table.emplace(get_self(),[&](auto& t){t = tk;});      
      }
      else check(false,"token not ready.");
    }
    else tk = *gtitr;

    return tk;
  }

  double Rate(const token& src,const token& dst,double src_times,double dst_times){
    if(src.id==dst.id)return 1;
    else if(src.id==PUP_TOKEN_ID) return (dst.pool/dst_times ) / (dst.pup_pool/src_times);
    else if(dst.id==PUP_TOKEN_ID) return (src.pup_pool/dst_times ) / (src.pool/src_times);
    else return (src.pup_pool / (src.pool/src_times)) / (dst.pup_pool / (dst.pool/dst_times));
  }

  uint64_t Amount(uint64_t amount,const token& a,const token& b){
    double a_times = PrecTimes(a);
    double b_times = PrecTimes(b);
    return Rate(a,b,a_times,b_times) * amount / a_times * b_times;
  }

  uint64_t PrecTimes(const token& tk){
    return pow(10,tk.symbol.get_symbol().precision());
  }  
};

} //namespace core
} //namepsace puppy