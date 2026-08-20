// main.cpp  (Phase 3 - CRDT merge)
// g++ main.cpp display.cpp -o editor -std=c++17 -lpthread -lrt
// sudo rm /dev/shm/sync_registry_v1


#include <bits/stdc++.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <mqueue.h>
#include <sys/types.h>
#include <signal.h>
#include <ctime>
#include <atomic>
#include <thread>
#include "display.h"
using namespace std;

constexpr const char* SHM_NAME="/sync_registry_v1";
constexpr size_t MAX_USERS=5;
constexpr size_t USER_ID_LEN=32;
constexpr size_t MQ_NAME_LEN=64;
constexpr int BROADCAST_BATCH=5;
constexpr int MERGE_BATCH=5;
constexpr size_t INCOMING_Q_CAP=1024;

struct Registry{
    int slot_in_use[MAX_USERS];
    char user_id[MAX_USERS][USER_ID_LEN];
    char mq_name[MAX_USERS][MQ_NAME_LEN];
};

struct Update{
    string op;
    int line,col_start,col_end;
    string old_text,new_text;
    long long timestamp;
    string user_id;
};

// ---------- Lock-free incoming queue ----------
class LFQueue{
    Update* buf; size_t cap;
    atomic<size_t> head,tail;
public:
    LFQueue(){cap=INCOMING_Q_CAP;buf=new Update[cap];head=0;tail=0;}
    ~LFQueue(){delete[] buf;}
    bool push(const Update&u){size_t t=tail.load();size_t n=(t+1)&(cap-1);
        if(n==head.load())return false;buf[t]=u;tail.store(n);return true;}
    bool pop(Update&u){size_t h=head.load();if(h==tail.load())return false;
        u=buf[h];head.store((h+1)&(cap-1));return true;}
};

// ---------- Globals ----------
int shm_fd=-1; Registry* regptr=nullptr; int myslot=-1;
string myid,mqname; mqd_t mqd=(mqd_t)-1;
LFQueue incoming;
vector<Update> localbuf,recvbuf;
mutex io; // only for printing, not data logic

// ---------- Utility ----------
long long now_ms(){using namespace chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();}
void cleanup(int code);
void sigint(int){cout<<"\nSIGINT\n";cleanup(0);}

// ---------- Registry ----------
void attach_reg(){
    shm_fd=shm_open(SHM_NAME,O_CREAT|O_RDWR,0666);
    ftruncate(shm_fd,sizeof(Registry));
    regptr=(Registry*)mmap(nullptr,sizeof(Registry),PROT_READ|PROT_WRITE,MAP_SHARED,shm_fd,0);
    bool bad=false;for(size_t i=0;i<MAX_USERS;i++)
        if(regptr->slot_in_use[i]!=0&&regptr->slot_in_use[i]!=1)bad=true;
    if(bad)for(size_t i=0;i<MAX_USERS;i++){regptr->slot_in_use[i]=0;
        memset(regptr->user_id[i],0,USER_ID_LEN);memset(regptr->mq_name[i],0,MQ_NAME_LEN);}
}
int reg_user(const string &uid, const string &mqname) {

    // STEP 1 — Heal stale slots first
    for (int i = 0; i < MAX_USERS; i++) {
        if (regptr->slot_in_use[i] == 1) {

            // Check if message queue exists
            mqd_t mq = mq_open(regptr->mq_name[i], O_WRONLY | O_NONBLOCK);

            // MQ is missing → stale user
            if (mq == (mqd_t)-1) {
                // clear stale slot
                regptr->slot_in_use[i] = 0;
                memset(regptr->user_id[i], 0, USER_ID_LEN);
                memset(regptr->mq_name[i], 0, MQ_NAME_LEN);
            } else {
                // valid active user
                mq_close(mq);
            }
        }
    }

    // STEP 2 — Attempt to register this user atomically
    for (int i = 0; i < MAX_USERS; i++) {

        int expected = 0;
        // CAS: attempt to grab free slot
        if (__sync_bool_compare_and_swap(&regptr->slot_in_use[i], expected, 1)) {

            // write user_id
            strncpy(regptr->user_id[i], uid.c_str(), USER_ID_LEN - 1);
            regptr->user_id[i][USER_ID_LEN - 1] = '\0';

            // write mqname
            strncpy(regptr->mq_name[i], mqname.c_str(), MQ_NAME_LEN - 1);
            regptr->mq_name[i][MQ_NAME_LEN - 1] = '\0';

            // success
            return i;
        }
    }

    // If we reach here, no slots available
    return -1;
}

void unreg_user(int slot) {
    if (slot < 0 || slot >= MAX_USERS) return;

    // Clear slot atomically
    regptr->slot_in_use[slot] = 0;

    memset(regptr->user_id[slot], 0, USER_ID_LEN);
    memset(regptr->mq_name[slot], 0, MQ_NAME_LEN);
}
vector<string> active_users() {
    vector<string> users;

    for (int i = 0; i < MAX_USERS; i++) {
        if (regptr->slot_in_use[i] == 1) {
            users.push_back(string(regptr->user_id[i]));
        }
    }

    return users;
}

void print_active(){auto a=active_users();
    cout<<"Active("<<a.size()<<"): ";for(size_t i=0;i<a.size();i++)cout<<a[i]<<(i+1==a.size()?"":", ");cout<<"\n";}

// ---------- File utils ----------
vector<string> read_lines(const string&f){vector<string>v;ifstream s(f);string l;while(getline(s,l))v.push_back(l);return v;}
void write_lines(const string&f,const vector<string>&v){ofstream s(f);for(auto &x:v)s<<x<<"\n";}
void init_doc(const string&f){struct stat st;if(stat(f.c_str(),&st)==0)return;
    ofstream s(f);s<<"Hello World\nThis is a collaborative editor\nWelcome to SyncText\nEdit this document and see real-time updates\n";}

// ---------- Diff & serialization ----------
pair<int,int> diffcols(const string&a,const string&b){int n=a.size(),m=b.size(),i=0;while(i<n&&i<m&&a[i]==b[i])i++;
    int j=0;while(j<n-i&&j<m-i&&a[n-1-j]==b[m-1-j])j++;int s=i,e=m-1-j;if(e<s)e=s-1;return{s,e};}
string esc(const string&s){string o;for(char c:s){if(c=='|')o+="<P>";else if(c=='\n')o+="<N>";else o+=c;}return o;}
string unesc(string s){size_t p;while((p=s.find("<P>"))!=string::npos)s.replace(p,3,"|");
    while((p=s.find("<N>"))!=string::npos)s.replace(p,3,"\n");return s;}
string ser(const Update&u){ostringstream ss;ss<<u.op<<"|"<<u.line<<"|"<<u.col_start<<"|"<<u.col_end
    <<"|"<<u.timestamp<<"|"<<esc(u.user_id)<<"|"<<esc(u.old_text)<<"|"<<esc(u.new_text);return ss.str();}
bool deser(const string&s,Update&o){vector<string>p;size_t st=0,pos;while((pos=s.find('|',st))!=string::npos){p.push_back(s.substr(st,pos-st));st=pos+1;}p.push_back(s.substr(st));
    if(p.size()<8)return false;o.op=p[0];o.line=stoi(p[1]);o.col_start=stoi(p[2]);o.col_end=stoi(p[3]);
    o.timestamp=stoll(p[4]);o.user_id=unesc(p[5]);o.old_text=unesc(p[6]);o.new_text=unesc(p[7]);return true;}

// ---------- Messaging ----------
void send_all(const string&msg){for(int i=0;i<MAX_USERS;i++){if(!regptr->slot_in_use[i])continue;
    string id=regptr->user_id[i];if(id==myid)continue;
    mqd_t q=mq_open(regptr->mq_name[i],O_WRONLY|O_NONBLOCK);if(q==(mqd_t)-1)continue;
    mq_send(q,msg.c_str(),min<size_t>(msg.size(),900),0);mq_close(q);}}
void listener(const string&mqn){
    struct mq_attr a{0,10,1024,0};
    mqd=mq_open(mqn.c_str(),O_CREAT|O_RDONLY|O_NONBLOCK,0666,&a);
    if(mqd==(mqd_t)-1){perror("mq_open");return;}
    char buf[2048];
    while(true){
        ssize_t b=mq_receive(mqd,buf,sizeof(buf)-1,nullptr);
        if(b>=0){buf[b]='\0';Update u;if(deser(buf,u))incoming.push(u);}
        else if(errno==EAGAIN)this_thread::sleep_for(chrono::milliseconds(200));
        else this_thread::sleep_for(chrono::milliseconds(500));
    }
}

// ---------- CRDT Merge ----------
void apply_update(vector<string>&doc,const Update&u){
    if(u.line>=(int)doc.size())doc.resize(u.line+1);
    doc[u.line]=u.new_text;
}

void merge_and_apply(const string &fname) {
    // ---- DO NOT POP FROM 'incoming' HERE ----
    // Monitor already drained 'incoming' into 'recvbuf'

    vector<Update> all;
    all.reserve(localbuf.size() + recvbuf.size());

    for (auto &x : localbuf) all.push_back(x);
    for (auto &x : recvbuf) all.push_back(x);

    if (all.empty())
        return;

    // Sort deterministically (timestamp -> user_id)
    sort(all.begin(), all.end(), [](const Update &a, const Update &b) {
        if (a.timestamp != b.timestamp) return a.timestamp < b.timestamp;
        return a.user_id < b.user_id;
    });

    // LWW per line
    unordered_map<int, Update> chosen;
    for (auto &upd : all) {
        int ln = upd.line;
        auto it = chosen.find(ln);
        if (it == chosen.end()) {
            chosen[ln] = upd;
        } else {
            Update &ex = it->second;
            if (upd.timestamp > ex.timestamp ||
               (upd.timestamp == ex.timestamp && upd.user_id < ex.user_id)) {
                ex = upd;
            }
        }
    }

    // Apply in line order
    auto doc = read_lines(fname);
    vector<pair<int, Update>> sortedIndex;
    sortedIndex.reserve(chosen.size());
    for (auto &p : chosen) sortedIndex.push_back(p);
    sort(sortedIndex.begin(), sortedIndex.end(),
         [](const pair<int, Update> &a, const pair<int, Update> &b) {
             return a.first < b.first;
         });

    for (auto &p : sortedIndex) {
        int line = p.first;
        const Update &upd = p.second;
        if (line >= (int)doc.size()) doc.resize(line + 1);

        if (upd.op == "insert" || upd.op == "replace") {
            doc[line] = upd.new_text;
        } else if (upd.op == "delete") {
            doc[line] = "";
        }
    }

    // Write back and bump mtime so monitor notices
    write_lines(fname, doc);
    std::ofstream ofs(fname, std::ios::app); ofs << ""; ofs.close();

    // UI
    vector<string> mergeSummary;
    for (auto &p : sortedIndex) {
        const auto &u = p.second;
        mergeSummary.push_back(u.user_id + " updated line " + to_string(u.line) +
                               ": \"" + u.new_text + "\"");
    }
    showSummary(mergeSummary);
    vector<LineStatus> statuses(doc.size(), LINE_NORMAL);
    showDocument(doc, fname, statuses);

    // Clear buffers (we consumed them)
    localbuf.clear();
    recvbuf.clear();
}


pair<int,int> diff_columns_old(const string& oldl, const string& newl) {
    int n = (int)oldl.size();
    int m = (int)newl.size();

    // Longest common prefix
    int i = 0;
    while (i < n && i < m && oldl[i] == newl[i]) ++i;

    // Identical lines
    if (i == n && i == m) return {0, -1};

    // Longest common suffix after the prefix
    int j = 0;
    while (j < (n - i) && j < (m - i) && oldl[n - 1 - j] == newl[m - 1 - j]) ++j;

    int start = i;
    int end_old = n - 1 - j;         // last index of the differing region in OLD
    if (end_old < start) end_old = start - 1;  // pure insertion

    return {start, end_old};
}


void monitor(const string &fname) {
    auto last = read_lines(fname);

    struct stat sb;
    time_t mt = 0;
    stat(fname.c_str(), &sb);
    mt = sb.st_mtime;
    off_t last_size = sb.st_size;

    while (true) {
        this_thread::sleep_for(chrono::seconds(2));

        if (stat(fname.c_str(), &sb) == -1)
            continue;

        bool fileModified = false;

        if (sb.st_mtime != mt || sb.st_size != last_size) {
            fileModified = true;
            mt = sb.st_mtime;
            last_size = sb.st_size;
        }

        // ---------------- LOCAL CHANGE DETECTION ----------------
        if (fileModified) {
            auto now = read_lines(fname);
            int L = max(last.size(), now.size());

            vector<Update> batchUpdates;
            batchUpdates.reserve(L);

            for (int i = 0; i < L; i++) {
                string oldLine = (i < (int)last.size()) ? last[i] : "";
                string newLine = (i < (int)now.size())  ? now[i]  : "";
                if (oldLine == newLine) continue;

                auto cols = diff_columns_old(oldLine, newLine);

                Update u;
                u.line = i;
                u.col_start = cols.first;
                u.col_end = cols.second;
                u.old_text = oldLine;
                u.new_text = newLine;
                u.timestamp = now_ms();
                u.user_id = myid;

                if (oldLine.empty() && !newLine.empty())
                    u.op = "insert";
                else if (!oldLine.empty() && newLine.empty())
                    u.op = "delete";
                else
                    u.op = "replace";

                cout << "Change detected: line=" << u.line
                     << " old=\"" << oldLine << "\" new=\"" << newLine << "\"\n";

                batchUpdates.push_back(u);
            }

            // ---------- BROADCASTING RULES ----------
            if (batchUpdates.size() == 1) {
                send_all(ser(batchUpdates[0]));
            } else if (!batchUpdates.empty()) {
                localbuf.insert(localbuf.end(), batchUpdates.begin(), batchUpdates.end());
                if (localbuf.size() >= BROADCAST_BATCH) {
                    for (auto &u : localbuf)
                        send_all(ser(u));
                    localbuf.clear();
                }
            }

            // ---------- TERMINAL DISPLAY REFRESH ----------
            vector<LineStatus> statuses(now.size(), LINE_NORMAL);
            int maxl = max((int)last.size(), (int)now.size());
            for (int i = 0; i < maxl; i++) {
                string a = (i < (int)last.size()) ? last[i] : "";
                string b = (i < (int)now.size()) ? now[i] : "";
                if (a != b) {
                    if (a.empty() && !b.empty()) statuses[i] = LINE_NEW;
                    else if (!a.empty() && b.empty()) statuses[i] = LINE_DELETED;
                    else statuses[i] = LINE_MODIFIED;
                }
            }

            // CLEAR screen before showing document
            cout << "\033[2J\033[H"; // clear terminal and home cursor

            showDocument(now, fname, statuses);
            showSummary({"Monitoring for changes..."});

            fflush(stdout);  // ensure immediate visual update

            last = now;
        }

        // ----------- REMOTE UPDATE COLLECTION -----------
        Update x;
        int got = 0;
        while (incoming.pop(x)) {
            recvbuf.push_back(x);
            got++;
        }
        if (got > 0)
            cout << "[Recv] +" << got << " remote ops\n";

        // ----------- MERGE TRIGGER -----------------------
        if (!recvbuf.empty()) {
            merge_and_apply(fname);

            // AFTER MERGE — refresh display so user sees updated document
            auto doc = read_lines(fname);

            cout << "\033[2J\033[H"; // clear terminal
            vector<LineStatus> statuses(doc.size(), LINE_NORMAL);
            showDocument(doc, fname, statuses);
            showSummary({"Merged remote updates..."});

            fflush(stdout);

            // Update snapshot
            last = doc;
            stat(fname.c_str(), &sb);
            mt = sb.st_mtime;
            last_size = sb.st_size;
        }
        else if (localbuf.size() >= MERGE_BATCH) {
            merge_and_apply(fname);
        }
    }
}


void cleanup(int code) {
    if (mqd != (mqd_t)-1) {
        mq_close(mqd);
        mq_unlink(mqname.c_str());
    }
    if (myslot != -1) {
        unreg_user(myslot);
    }
    if (regptr) {
        munmap(regptr, sizeof(Registry));
        close(shm_fd);
    }
    restoreCursor();
    cout << "Cleanup done\n";
    exit(code);
}



// ---------- main ----------
int main(int argc,char**argv){
    if(argc<2){cerr<<"Usage: ./editor <user_id>\n";return 1;}
    signal(SIGINT,sigint);
    myid=argv[1];mqname="/queue_"+myid;
    attach_reg();myslot=reg_user(myid,mqname);
    if(myslot==-1){cerr<<"Too many users\n";return 1;}
    cout<<"Registered "<<myid<<" slot "<<myslot<<"\n";
    print_active();
    display_init();
    string file=myid+"_doc.txt";init_doc(file);
    thread t(listener,mqname);
    monitor(file);
    t.join();
    cleanup(0);
}
