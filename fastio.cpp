#include <algorithm>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
using namespace std;
using I = int64_t;

bool fields(const string& line, vector<string_view>& out, size_t n) {
    out.clear(); size_t first=0;
    while(true) {
        size_t end=line.find(',',first); if(end==string::npos) end=line.size();
        string_view s(line.data()+first,end-first);
        if(!s.empty() && s.front()=='"') {
            if(s.size()<2 || s.back()!='"') return false;
            s.remove_prefix(1); s.remove_suffix(1);
        }
        if(s.find('"')!=string_view::npos) return false;
        out.push_back(s); if(end==line.size()) break; first=end+1;
    }
    return out.size()==n;
}
bool digits(string_view s) { if(s.empty())return false;for(unsigned char c:s)if(c<'0'||c>'9')return false;return true; }
I number(string_view s) { I x=0;for(char c:s)x=x*10+(c-'0');return x; }
I days(int y,unsigned m,unsigned d) {
    y-=m<=2;const int era=(y>=0?y:y-399)/400; const unsigned yo=y-era*400;
    const unsigned doy=(153*(m+(m>2?-3:9))+2)/5+d-1;
    return era*146097+yo*365+yo/4-yo/100+doy-719468;
}
string date(I day) {
    I z=day+719468;I era=(z>=0?z:z-146096)/146097;unsigned doe=z-era*146097;
    unsigned yoe=(doe-doe/1460+doe/36524-doe/146096)/365;int y=yoe+era*400;
    unsigned doy=doe-(365*yoe+yoe/4-yoe/100);unsigned mp=(5*doy+2)/153;
    unsigned d=doy-(153*mp+2)/5+1;unsigned m=mp+(mp<10?3:-9);y+=m<=2;
    char result[32];snprintf(result,sizeof(result),"%04d-%02u-%02u",y,m,d);return result;
}
bool timestamp(string_view s,I &value) {
    if(digits(s)) {
        while(s.size()>1&&s.front()=='0')s.remove_prefix(1);
        if(s.size()>18)return false;value=number(s);return true;
    }
    if(s.size()!=20&&s.size()!=25)return false;
    if(s[4]!='-'||s[7]!='-'||s[10]!='T'||s[13]!=':'||s[16]!=':')return false;
    for(int p:{0,1,2,3,5,6,8,9,11,12,14,15,17,18})if(s[p]<'0'||s[p]>'9')return false;
    int y=number(s.substr(0,4)),m=number(s.substr(5,2)),d=number(s.substr(8,2));
    int h=number(s.substr(11,2)),mi=number(s.substr(14,2)),sec=number(s.substr(17,2));
    if(y<1||m<1||m>12||h>23||mi>59||sec>59)return false;
    const int md[]={31,28,31,30,31,30,31,31,30,31,30,31};
    int dim=md[m-1]+(m==2&&y%4==0&&(y%100!=0||y%400==0));if(d<1||d>dim)return false;
    int offset=0;
    if(s.size()==20) {if(s[19]!='Z')return false;}
    else {
        if((s[19]!='+'&&s[19]!='-')||s[22]!=':')return false;
        for(int p:{20,21,23,24})if(s[p]<'0'||s[p]>'9')return false;
        int oh=number(s.substr(20,2)),om=number(s.substr(23,2));if(oh>23||om>59)return false;
        offset=(oh*60+om)*(s[19]=='+'?1:-1);
    }
    value=((days(y,m,d)*24+h)*60+mi-offset)*60000+sec*1000;return true;
}
void text(ostream &o,string_view s) {if(!s.empty())o<<'"'<<s<<'"';}
vector<filesystem::path> csvs(const filesystem::path& p) {
    vector<filesystem::path> r;for(auto &f:filesystem::directory_iterator(p))if(f.is_regular_file()&&f.path().extension()==".csv")r.push_back(f.path());sort(r.begin(),r.end());return r;
}
template<class F>void rows(const filesystem::path&p,size_t n,F f) {
    ifstream in(p,ios::binary);if(!in)throw runtime_error("cannot open "+p.string());string line;getline(in,line);vector<string_view> v;
    while(getline(in,line))if(fields(line,v,n))f(v);
}
struct Union {
    unordered_map<string,int> index;vector<int> parent,rank;vector<string> minimum;
    int add(string_view s) {auto it=index.find(string(s));if(it!=index.end())return it->second;int i=parent.size();index.emplace(s,i);parent.push_back(i);rank.push_back(0);minimum.emplace_back(s);return i;}
    int root(int x) {int y=x;while(parent[y]!=y)y=parent[y];while(parent[x]!=x){int z=parent[x];parent[x]=y;x=z;}return y;}
    void join(string_view a,string_view b){int x=root(add(a)),y=root(add(b));if(x==y)return;if(rank[x]<rank[y])swap(x,y);parent[y]=x;minimum[x]=min(minimum[x],minimum[y]);if(rank[x]==rank[y])rank[x]++;}
    string_view name(string_view s){auto it=index.find(string(s));return it==index.end()?s:string_view(minimum[root(it->second)]);}
};
void sanitize(const filesystem::path& data,const filesystem::path& work,I begin,I end) {
    Union uf;for(auto p:csvs(data/"links"))rows(p,2,[&](auto&v){if(!v[0].empty()&&!v[1].empty())uf.join(v[0],v[1]);});
    unordered_map<string,string> products,campaigns;
    rows(data/"products.csv",2,[&](auto&v){products[string(v[0])]=v[1];});
    rows(data/"campaigns.csv",2,[&](auto&v){campaigns[string(v[0])]=v[1];});
    ofstream events(work/"events.csv",ios::binary);events<<"eid,u,t,i,k,cat,p\n";
    for(auto path:csvs(data/"events"))rows(path,7,[&](auto&v){
        if(v[0].empty())return;I ts=0,ingest=0;bool good=timestamp(v[2],ts)&&ts>=begin&&ts<end;
        if(!timestamp(v[3],ingest))throw runtime_error("invalid ingest_ts outside documented input");
        int kind=v[4]=="view"?1:v[4]=="cart"?2:v[4]=="purchase"?3:0;
        bool priceok=true;I price=0;
        if(kind==3){auto s=v[6];priceok=digits(s);while(s.size()>1&&s.front()=='0')s.remove_prefix(1);if(s.size()>8)priceok=false;if(priceok){price=number(s);priceok=price>=1&&price<=10000000;}}
        text(events,v[0]);events<<',';text(events,uf.name(v[1]));events<<',';if(good)events<<ts;events<<','<<ingest<<','<<kind<<',';
        if(!v[5].empty()){auto it=products.find(string(v[5]));text(events,it==products.end()?string_view("unknown"):string_view(it->second));}
        events<<',';if(priceok)events<<price;events<<'\n';
    });
    events.close();ofstream clicks(work/"clicks.csv",ios::binary);clicks<<"id,u,c,t\n";
    for(auto path:csvs(data/"clicks"))rows(path,4,[&](auto&v){
        I ts=0;if(v[0].empty()||v[1].empty()||!campaigns.count(string(v[2]))||!timestamp(v[3],ts)||ts<begin-604800000||ts>=end)return;
        text(clicks,v[0]);clicks<<',';text(clicks,uf.name(v[1]));clicks<<',';text(clicks,v[2]);clicks<<','<<ts<<'\n';
    });
    ofstream channels(work/"channels.csv",ios::binary);channels<<"campaign_id,channel\n";for(auto &[c,ch]:campaigns){text(channels,c);channels<<',';text(channels,ch);channels<<'\n';}
}
struct Stats {I sessions=0,purchasing=0,revenue=0,assists=0;};
void reduce(const filesystem::path& work) {
    unordered_map<string,string> channel;rows(work/"channels.csv",2,[&](auto&v){channel[string(v[0])]=v[1];});
    map<pair<I,string>,Stats> result;
    string person,lastid,lastcampaign;I latest=-1;deque<pair<I,string>> active;unordered_map<string,I> counts;
    rows(work/"timeline.csv",7,[&](auto&v){
        if(v[0]!=person){person=v[0];active.clear();counts.clear();latest=-1;lastid.clear();lastcampaign.clear();}
        I ts=number(v[1]);int kind=number(v[2]);
        if(kind==0){string camp(v[4]);active.emplace_back(ts,camp);counts[camp]++;if(ts>latest||(ts==latest&&v[3]<lastid)){latest=ts;lastid=v[3];lastcampaign=camp;}return;}
        I lower=ts-604800000;while(!active.empty()&&active.front().first<lower){auto it=counts.find(active.front().second);if(--it->second==0)counts.erase(it);active.pop_front();}
        I day=ts/86400000;string credit=latest>=lower?lastcampaign:"organic";auto &st=result[{day,credit}];st.sessions++;st.revenue+=number(v[5]);st.purchasing+=number(v[6]);
        for(auto &[campaign,count]:counts)result[{day,campaign}].assists++;
    });
    ofstream out(work/"campaign_daily.csv",ios::binary);out<<"campaign_id,channel,day,sessions,purchasing_sessions,revenue_cents,assisted_sessions\n";
    for(auto &[key,s]:result){auto &[day,c]=key;text(out,c);out<<',';text(out,c=="organic"?string_view("organic"):string_view(channel.at(c)));out<<','<<date(day)<<','<<s.sessions<<','<<s.purchasing<<','<<s.revenue<<','<<s.assists<<'\n';}
}
int main(int argc,char**argv){try{if(argc>=2&&string(argv[1])=="sanitize"&&argc==6)sanitize(argv[2],argv[3],stoll(argv[4]),stoll(argv[5]));else if(argc==3&&string(argv[1])=="reduce")reduce(argv[2]);else throw runtime_error("usage: fastio sanitize DATA WORK BEGIN END | fastio reduce WORK");}catch(exception&e){cerr<<e.what()<<'\n';return 1;}return 0;}
