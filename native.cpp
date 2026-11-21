// Native bounded-column pipeline. No dataset IDs or expected rows are embedded.
#define main legacy_main
#include "fastio.cpp"
#undef main
#include <cstring>
#include <memory>
#include <array>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
using SV=string_view;
constexpr int B=64;
int workers(){return max(1,min(8,getenv("NCPU")?atoi(getenv("NCPU")):4));}
uint64_t hash_id(SV s){uint64_t h=14695981039346656037ULL;for(unsigned char c:s){h^=c;h*=1099511628211ULL;}h^=h>>33;h*=0xff51afd7ed558ccdULL;h^=h>>33;h*=0xc4ceb9fe1a85ec53ULL;return h^(h>>33);}
int bucket(uint64_t hash){return hash>>58;}
template<class F>void parallel(size_t count,F f){atomic<size_t> next{0};exception_ptr error;mutex lock;vector<thread> threads;
    for(int w=0;w<workers();w++)threads.emplace_back([&,w]{try{for(;;){auto i=next.fetch_add(1);if(i>=count)break;f(i,w);}}catch(...){lock_guard<mutex> guard(lock);if(!error)error=current_exception();next=count;}});
    for(auto&t:threads)t.join();if(error)rethrow_exception(error);
}
struct MapFile{int fd=-1;size_t size=0;const char*data=nullptr;explicit MapFile(const filesystem::path&p){fd=open(p.c_str(),O_RDONLY);if(fd<0)throw runtime_error("open failed");struct stat s;if(fstat(fd,&s))throw runtime_error("stat failed");size=s.st_size;if(size){void*v=mmap(nullptr,size,PROT_READ,MAP_PRIVATE,fd,0);if(v==MAP_FAILED)throw runtime_error("mmap failed");data=(const char*)v;madvise((void*)data,size,MADV_SEQUENTIAL);}}~MapFile(){if(data)munmap((void*)data,size);if(fd>=0)close(fd);}};
struct Task{MapFile*file;size_t begin,end;};
struct Inputs{vector<unique_ptr<MapFile>> files;vector<Task> tasks;explicit Inputs(const filesystem::path&p){for(auto&path:csvs(p)){auto f=make_unique<MapFile>(path);auto*ptr=f.get();for(size_t off=0;off<ptr->size;off+=(4<<20))tasks.push_back({ptr,off,min(ptr->size,off+(4<<20))});files.push_back(move(f));}}};
bool view_fields(SV line,vector<SV>&out,size_t n){out.clear();size_t start=0;while(true){size_t end=line.find(',',start);if(end==SV::npos)end=line.size();SV v=line.substr(start,end-start);if(!v.empty()&&v.front()=='"'){if(v.size()<2||v.back()!='"')return false;v.remove_prefix(1);v.remove_suffix(1);}if(v.find('"')!=SV::npos)return false;out.push_back(v);if(end==line.size())break;start=end+1;}return out.size()==n;}
template<class F>void task_rows(const Task&task,size_t n,F f){auto*base=task.file->data;auto*limit=base+task.file->size;const char*p=base+task.begin;vector<SV> values;values.reserve(n);
    if(!task.begin||p[-1]!='\n'){const char*q=(const char*)memchr(p,'\n',limit-p);if(!q)return;p=q+1;}
    while(p<limit&&p<base+task.end){const char*q=(const char*)memchr(p,'\n',limit-p);if(!q)q=limit;if(view_fields(SV(p,q-p),values,n))f(values);p=q==limit?limit:q+1;}
}

// Built only after all unions complete. The UF owns every referenced byte string
// and is immutable for the rest of execution, including all worker threads.
struct AliasFlat{
    struct Entry{SV key,value;uint64_t hash;};
    struct Slot{uint32_t fingerprint=0,index=UINT32_MAX;};
    vector<Entry>entries;vector<Slot>slots;
    void build(const Union&uf){
        size_t cap=8;while(cap<uf.index.size()+uf.index.size()/3+1)cap*=2;
        slots.resize(cap);entries.reserve(uf.index.size());
        for(auto&[key,index]:uf.index){int root=index;while(uf.parent[root]!=root)root=uf.parent[root];
            uint64_t hash=std::hash<SV>{}(key);size_t k=hash&(slots.size()-1);
            while(slots[k].index!=UINT32_MAX)k=(k+1)&(slots.size()-1);
            if(entries.size()>=UINT32_MAX)throw runtime_error("alias table too large");
            slots[k]={uint32_t(hash>>32),uint32_t(entries.size())};entries.push_back({key,uf.minimum[root],hash});
        }
    }
    SV name(SV key)const{
        uint64_t hash=std::hash<SV>{}(key);uint32_t fp=hash>>32;size_t k=hash&(slots.size()-1);
        while(slots[k].index!=UINT32_MAX){if(slots[k].fingerprint==fp){const auto&e=entries[slots[k].index];if(e.hash==hash&&e.key==key)return e.value;}k=(k+1)&(slots.size()-1);}
        return key;
    }
};
struct Meta{Union uf;AliasFlat aliases;unordered_map<string,uint32_t> products,campaign;unordered_map<string,uint32_t> category_ids;vector<string>categories,campaigns,channels;uint32_t unknown,organic;
    uint32_t category(SV s){auto found=category_ids.find(string(s));if(found!=category_ids.end())return found->second;uint32_t id=categories.size();categories.emplace_back(s);category_ids.emplace(s,id);return id;}
    explicit Meta(const filesystem::path&data){for(auto&p:csvs(data/"links"))rows(p,2,[&](auto&v){if(!v[0].empty()&&!v[1].empty())uf.join(v[0],v[1]);});
        aliases.build(uf);
        rows(data/"products.csv",2,[&](auto&v){products[string(v[0])]=category(v[1]);});unknown=category("unknown");
        rows(data/"campaigns.csv",2,[&](auto&v){uint32_t id=campaigns.size();campaign.emplace(v[0],id);campaigns.emplace_back(v[0]);channels.emplace_back(v[1]);});organic=campaigns.size();campaigns.emplace_back("organic");channels.emplace_back("organic");
    }
};
struct RawEvent{SV id,u;I t=0,ingest=0;uint64_t hash=0;uint32_t price=0,cat=0;bool valid=false;};
struct Event{SV u;I t;uint32_t price,cat;};
struct Click{SV id,u;I t;uint32_t campaign;};
template<class T>using Buckets=array<vector<T>,B>;
template<class T>void add(vector<T>&v,const T&value){if(v.empty()&&!v.capacity())v.reserve(4096);v.push_back(value);}
size_t slot_count(size_t n){size_t cap=8;while(cap<n+n/3+1)cap*=2;return cap;}
vector<Event> combine_events(vector<Buckets<Event>>&from,int b){size_t n=0;for(auto&group:from)n+=group[b].size();vector<Event> result;result.reserve(n);for(auto&group:from){result.insert(result.end(),group[b].begin(),group[b].end());vector<Event>().swap(group[b]);}return result;}
vector<Click> combine_clicks(vector<Buckets<Click>>&from,int b){size_t n=0;for(auto&group:from)n+=group[b].size();vector<Click> result;result.reserve(n);for(auto&group:from){result.insert(result.end(),group[b].begin(),group[b].end());vector<Click>().swap(group[b]);}return result;}
bool user_less(uint64_t ah,SV a,uint64_t bh,SV b){return ah!=bh?ah<bh:a<b;}
struct DayStats{I day,sessions=0,events=0,revenue=0;vector<I>category;};
struct UserIndex{
    struct Name{SV u;uint64_t hash;};vector<Name> names;vector<uint32_t> slots=vector<uint32_t>(2048,UINT32_MAX);
    uint32_t get(SV u){uint64_t hash=hash_id(u);if(names.size()*2>=slots.size()){vector<uint32_t>next(slots.size()*2,UINT32_MAX);for(uint32_t i=0;i<names.size();i++){size_t k=names[i].hash&(next.size()-1);while(next[k]!=UINT32_MAX)k=(k+1)&(next.size()-1);next[k]=i;}slots.swap(next);}
        size_t k=hash&(slots.size()-1);while(slots[k]!=UINT32_MAX){auto&n=names[slots[k]];if(n.hash==hash&&n.u==u)return slots[k];k=(k+1)&(slots.size()-1);}uint32_t i=names.size();slots[k]=i;names.push_back({u,hash});return i;
    }
};
template<class Time>struct PackedEvent{Time t;uint32_t user,price,cat;};
struct PackedClick{I t;uint32_t user,campaign;SV id;};
template<class Row,class Key>void radix_order(vector<Row>&items,Key key){
    if(items.size()<2)return;uint64_t maximum=0;for(auto&r:items)maximum=max(maximum,uint64_t(key(r)));if(!maximum)return;
    vector<Row> buffer(items.size());
    for(unsigned shift=0;shift<64&&(maximum>>shift);shift+=8){
        array<size_t,256> offset{};for(auto&r:items)offset[(uint64_t(key(r))>>shift)&255]++;
        size_t prefix=0;for(auto&count:offset){size_t old=count;count=prefix;prefix+=old;}
        for(auto&r:items)buffer[offset[(uint64_t(key(r))>>shift)&255]++]=r;
        items.swap(buffer);
    }
}
template<class Time>void process_bucket(size_t b,int worker,vector<Buckets<Event>>&events,vector<Buckets<Click>>&clicks,
 const Meta&meta,const filesystem::path&work,I begin,I first_day,size_t nc,vector<vector<Stats>>&totals){
    UserIndex users;auto source=combine_events(events,b);vector<PackedEvent<Time>> ev;ev.reserve(source.size());
    for(auto&r:source)ev.push_back({Time(r.t-begin),users.get(r.u),r.price,r.cat});vector<Event>().swap(source);
    auto clraw=combine_clicks(clicks,b);vector<uint32_t>slots(slot_count(clraw.size()),UINT32_MAX);vector<uint32_t>unique;unique.reserve(clraw.size());
    for(uint32_t i=0;i<clraw.size();i++){auto&r=clraw[i];size_t k=hash_id(r.id)&(slots.size()-1);while(slots[k]!=UINT32_MAX&&clraw[unique[slots[k]]].id!=r.id)k=(k+1)&(slots.size()-1);if(slots[k]==UINT32_MAX){slots[k]=unique.size();unique.push_back(i);}}
    vector<PackedClick>cl;cl.reserve(unique.size());for(uint32_t i:unique){auto&r=clraw[i];cl.push_back({r.t,users.get(r.u),r.campaign,r.id});}vector<Click>().swap(clraw);vector<uint32_t>().swap(slots);vector<uint32_t>().swap(unique);
    if constexpr(sizeof(Time)<=4){radix_order(ev,[](auto&r){return (uint64_t(r.user)<<32)|uint64_t(r.t);});}
    else{radix_order(ev,[](auto&r){return uint64_t(r.t);});radix_order(ev,[](auto&r){return uint64_t(r.user);});}
    I click_base=begin-604800000;uint64_t click_span=0;for(auto&r:cl)click_span=max(click_span,uint64_t(r.t-click_base));
    if(click_span<=UINT32_MAX){radix_order(cl,[&](auto&r){return (uint64_t(r.user)<<32)|uint64_t(r.t-click_base);});}
    else{radix_order(cl,[&](auto&r){return uint64_t(r.t-click_base);});radix_order(cl,[](auto&r){return uint64_t(r.user);});}
    Output out(work/("users-"+to_string(b)+".csv"));out<<"user_id,day,sessions,events,revenue_cents,top_category\n";size_t e=0,c=0;
    while(e<ev.size()){size_t ee=e+1;while(ee<ev.size()&&ev[ee].user==ev[e].user)ee++;uint32_t user=ev[e].user;SV u=users.names[user].u;
        while(c<cl.size()&&cl[c].user<user)c++;size_t ce=c;while(ce<cl.size()&&cl[ce].user==user)ce++;
        size_t lo=c,hi=c;I latest=-1;SV latest_id;uint32_t credited=meta.organic;vector<DayStats>daily;unordered_map<uint32_t,uint32_t>active;
        while(e<ee){size_t stop=e+1;while(stop<ee&&ev[stop].t-ev[stop-1].t<=1800000)stop++;I start=begin+ev[e].t,day=start/86400000;
            if(daily.empty()||daily.back().day!=day)daily.push_back(DayStats{day});auto&d=daily.back();d.sessions++;d.events+=stop-e;I revenue=0;
            for(size_t i=e;i<stop;i++){revenue+=ev[i].price;if(ev[i].price){if(d.category.empty())d.category.resize(meta.categories.size(),0);d.category[ev[i].cat]+=ev[i].price;}}d.revenue+=revenue;
            while(hi<ce&&cl[hi].t<=start){auto&r=cl[hi];active[r.campaign]++;if(r.t>latest||(r.t==latest&&r.id<latest_id)){latest=r.t;latest_id=r.id;credited=r.campaign;}hi++;}
            I lower=start-604800000;while(lo<hi&&cl[lo].t<lower){auto it=active.find(cl[lo].campaign);if(--it->second==0)active.erase(it);lo++;}
            uint32_t credit=latest>=lower?credited:meta.organic;auto&stats=totals[worker][(day-first_day)*nc+credit];stats.sessions++;stats.purchasing+=revenue>0;stats.revenue+=revenue;
            for(auto&[campaign,count]:active)totals[worker][(day-first_day)*nc+campaign].assists++;e=stop;
        }
        for(auto&d:daily){text(out,u);out<<','<<date(d.day)<<','<<d.sessions<<','<<d.events<<','<<d.revenue<<',';I best=0;uint32_t cat=0;for(uint32_t i=0;i<d.category.size();i++)if(d.category[i]>best||(d.category[i]==best&&best&&meta.categories[i]<meta.categories[cat])){best=d.category[i];cat=i;}if(best)text(out,meta.categories[cat]);out<<'\n';}c=ce;
    }
}
void engine(const filesystem::path&data,const filesystem::path&work,I begin,I end){
    const auto started=chrono::steady_clock::now();auto seconds=[&](){return chrono::duration<double>(chrono::steady_clock::now()-started).count();};
    const Meta meta(data);Inputs event_input(data/"events");vector<Buckets<RawEvent>> raw(workers());
    parallel(event_input.tasks.size(),[&](size_t n,int worker){task_rows(event_input.tasks[n],7,[&](auto&v){
        if(v[0].empty())return;RawEvent r;r.id=v[0];r.hash=hash_id(r.id);bool good=timestamp(v[2],r.t)&&r.t>=begin&&r.t<end;
        if(!timestamp(v[3],r.ingest))throw runtime_error("ingest_ts violates documented valid F1");
        if(v[1].empty()||v[5].empty()||(v[4]!="view"&&v[4]!="cart"&&v[4]!="purchase"))good=false;
        if(v[4]=="purchase"){auto p=v[6];bool ok=digits(p);while(p.size()>1&&p.front()=='0')p.remove_prefix(1);if(p.size()>8)ok=false;if(ok){r.price=number(p);ok=r.price>=1&&r.price<=10000000;}if(!ok)good=false;}
        if(good&&r.ingest-r.t>86400000)good=false;
        r.valid=good;if(good){r.u=meta.aliases.name(v[1]);auto cat=meta.products.find(string(v[5]));r.cat=cat==meta.products.end()?meta.unknown:cat->second;}
        add(raw[worker][bucket(r.hash)],r);
    });});
    cerr<<"native parsed "<<seconds()<<"s\n";
    vector<Buckets<Event>> events(workers());
    parallel(B,[&](size_t b,int worker){size_t n=0;for(auto&group:raw)n+=group[b].size();if(n>=UINT32_MAX)throw runtime_error("dedup partition too large");struct Slot{uint32_t fingerprint=0,index=UINT32_MAX;};vector<Slot>slots(slot_count(n));vector<RawEvent*> unique;unique.reserve(n);
        for(auto&group:raw)for(auto&r:group[b]){uint32_t fingerprint=r.hash>>32;size_t k=r.hash&(slots.size()-1);while(slots[k].index!=UINT32_MAX){if(slots[k].fingerprint==fingerprint){auto*old=unique[slots[k].index];if(old->hash==r.hash&&old->id==r.id){if(r.ingest<old->ingest)unique[slots[k].index]=&r;break;}}k=(k+1)&(slots.size()-1);}if(slots[k].index==UINT32_MAX){slots[k]={fingerprint,uint32_t(unique.size())};unique.push_back(&r);}}
        for(auto*r:unique)if(r->valid){Event e{r->u,r->t,r->price,r->cat};add(events[worker][bucket(hash_id(e.u))],e);}
        for(auto&group:raw)vector<RawEvent>().swap(group[b]);
    });
    vector<Buckets<RawEvent>>().swap(raw);cerr<<"native dedup "<<seconds()<<"s\n";
    Inputs click_input(data/"clicks");vector<Buckets<Click>> clicks(workers());
    parallel(click_input.tasks.size(),[&](size_t n,int worker){task_rows(click_input.tasks[n],4,[&](auto&v){I ts;auto campaign=meta.campaign.find(string(v[2]));if(v[0].empty()||v[1].empty()||campaign==meta.campaign.end()||!timestamp(v[3],ts)||ts<begin-604800000||ts>=end)return;SV u=meta.aliases.name(v[1]);Click c{v[0],u,ts,campaign->second};add(clicks[worker][bucket(hash_id(u))],c);});});
    const I first_day=begin/86400000,last_day=(end-1)/86400000;const size_t nc=meta.campaigns.size();vector<vector<Stats>> totals(workers(),vector<Stats>((last_day-first_day+1)*nc));
    parallel(B,[&](size_t b,int worker){if(uint64_t(end-begin)<=UINT32_MAX)process_bucket<uint32_t>(b,worker,events,clicks,meta,work,begin,first_day,nc,totals);else process_bucket<uint64_t>(b,worker,events,clicks,meta,work,begin,first_day,nc,totals);});
    Output out(work/"campaign_daily.csv");out<<"campaign_id,channel,day,sessions,purchasing_sessions,revenue_cents,assisted_sessions\n";
    for(I day=first_day;day<=last_day;day++)for(uint32_t c=0;c<nc;c++){Stats sum;for(auto&all:totals){auto&s=all[(day-first_day)*nc+c];sum.sessions+=s.sessions;sum.purchasing+=s.purchasing;sum.revenue+=s.revenue;sum.assists+=s.assists;}if(sum.sessions||sum.assists){text(out,meta.campaigns[c]);out<<',';text(out,meta.channels[c]);out<<','<<date(day)<<','<<sum.sessions<<','<<sum.purchasing<<','<<sum.revenue<<','<<sum.assists<<'\n';}}
    cerr<<"native finished "<<seconds()<<"s\n";
}
int main(int argc,char**argv){try{if(argc!=5)throw runtime_error("usage: fastio DATA WORK BEGIN END");engine(argv[1],argv[2],stoll(argv[3]),stoll(argv[4]));}catch(exception&e){cerr<<e.what()<<'\n';return 1;}return 0;}
