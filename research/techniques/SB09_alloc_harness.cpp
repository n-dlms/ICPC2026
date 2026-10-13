// SB09 allocation harness: measures heap allocations per frame before/after fixes
// Build: g++ -std=c++17 -O2 -Wall -Wextra -Werror -Wno-pedantic -o /tmp/sb09alloc research/techniques/SB09_alloc_harness.cpp
// This harness replicates the hot-path allocation patterns found in src/scheduler.cpp, state.cpp, protocol.cpp
// without copying src/ verbatim (analysis-only), but with identical std::vector/string usage.
#include <bits/stdc++.h>
using namespace std;

static atomic<long long> g_new_count{0};
static atomic<long long> g_new_bytes{0};

void* operator new(size_t s){
    g_new_count.fetch_add(1, memory_order_relaxed);
    g_new_bytes.fetch_add((long long)s, memory_order_relaxed);
    if(void* p=malloc(s)) return p;
    throw bad_alloc();
}
void operator delete(void* p) noexcept { free(p); }
void operator delete(void* p, size_t) noexcept { free(p); }
void* operator new[](size_t s){ g_new_count.fetch_add(1,memory_order_relaxed); g_new_bytes.fetch_add((long long)s,memory_order_relaxed); if(void* p=malloc(s)) return p; throw bad_alloc(); }
void operator delete[](void* p) noexcept { free(p); }
void operator delete[](void* p, size_t) noexcept { free(p); }

void reset_count(){ g_new_count=0; g_new_bytes=0; }

// --- BEFORE: hot path as currently written (allocating) ---
struct TaskSpecBefore {
    vector<int> ids; // heap per assignment
};

void hot_before(int K, int /*frame*/){
    // Simulate one decide() frame
    // 1. out vector
    struct Assignment{ TaskSpecBefore spec; };
    vector<Assignment> out; // heap when reserve
    out.reserve(K+1);
    // 2. D_PRE combine: others vector
    vector<pair<int,int>> others;
    for(int c=0;c<K;++c) others.push_back({c,c});
    sort(others.rbegin(), others.rend());
    // 3. State::issue seen/cnt per D_PRE
    vector<char> seen(K,0);
    vector<int> cnt(K,0);
    // 4. ids per assignment (group of size m=8)
    TaskSpecBefore spec;
    for(int i=0;i<8;++i) spec.ids.push_back(i);
    out.push_back({move(spec)});
    // 5. Event spec strings per TDN (2 strings)
    vector<string> ev_spec;
    ev_spec.push_back(string("P"));
    ev_spec.push_back(string("PRE"));
    ev_spec.push_back(string("0"));
    ev_spec.push_back(string("123"));
    // prevent optimize out
    volatile size_t sink = out.size() + others.size() + seen.size() + cnt.size() + ev_spec.size();
    (void)sink;
}

// --- AFTER: allocation-free fixed-capacity replacements ---
struct TaskSpecAfter {
    static constexpr int MAX = 2048;
    int ids[MAX];
    int n=0;
    void push(int v){ assert(n<MAX); ids[n++]=v; }
    void clear(){ n=0; }
};

struct SmallOthers {
    int sz[8]; int cloud[8]; int n=0;
    void push(int s,int c){ assert(n<8); sz[n]=s; cloud[n]=c; ++n; }
    // sort descending by sz (simple bubble for 8 entries, no allocation)
    void sort_desc(){
        for(int i=0;i<n;++i) for(int j=i+1;j<n;++j) if(sz[j]>sz[i]){ swap(sz[i],sz[j]); swap(cloud[i],cloud[j]); }
    }
};

void hot_after(int K, int /*frame*/){
    // fixed buffers reused (static thread_local to mimic per-frame reuse)
    // out as static array
    TaskSpecAfter out_specs[9]; int out_n=0;
    SmallOthers others;
    for(int c=0;c<K;++c) others.push(c,c);
    others.sort_desc();
    // seen/cnt as stack arrays (no heap)
    char seen[8]={0}; int cnt[8]={0};
    (void)seen; (void)cnt;
    TaskSpecAfter spec; spec.clear();
    for(int i=0;i<8;++i) spec.push(i);
    out_specs[out_n++]=spec;
    // ev_spec as enum + ints (no string)
    // simulate without allocation: store as small ints
    int ev_kind=0; (void)ev_kind;
    volatile int sink = out_n + others.n;
    (void)sink;
}

int main(){
    const int K=8;
    const int FRAMES=10000;
    // Warm up (allocate once)
    hot_before(K,0); reset_count();
    hot_after(K,0); reset_count();

    // BEFORE
    reset_count();
    long long before_allocs=0, before_bytes=0;
    for(int f=0;f<FRAMES;++f){
        reset_count();
        hot_before(K,f);
        before_allocs += g_new_count.load();
        before_bytes += g_new_bytes.load();
    }
    double avg_before = (double)before_allocs / FRAMES;
    double avg_bytes_before = (double)before_bytes / FRAMES;
    printf("BEFORE: avg allocs/frame = %.2f, avg bytes/frame = %.0f over %d frames\n", avg_before, avg_bytes_before, FRAMES);
    printf("BEFORE total allocs %lld for %d frames\n", before_allocs, FRAMES);

    // AFTER
    reset_count();
    long long after_allocs=0, after_bytes=0;
    for(int f=0;f<FRAMES;++f){
        reset_count();
        hot_after(K,f);
        after_allocs += g_new_count.load();
        after_bytes += g_new_bytes.load();
    }
    double avg_after = (double)after_allocs / FRAMES;
    double avg_bytes_after = (double)after_bytes / FRAMES;
    printf("AFTER:  avg allocs/frame = %.2f, avg bytes/frame = %.0f over %d frames\n", avg_after, avg_bytes_after, FRAMES);
    printf("AFTER total allocs %lld for %d frames\n", after_allocs, FRAMES);

    printf("\nReduction: %.1fx fewer allocs, %.1f%% bytes saved\n", avg_before/max(0.1,avg_after), (1-avg_bytes_after/max(1.0,avg_bytes_before))*100);
    if(avg_after==0) printf("AFTER is allocation-free (0 heap allocs per frame) - PASS\n");
    else printf("AFTER still allocates - needs further fix\n");

    // Also estimate worst-case 2M frames
    double sec_before = (avg_before * 0.5e-6) * 2000000; // assume 0.5 us per alloc overhead (malloc)
    double sec_after = (avg_after * 0.5e-6) * 2000000;
    printf("\nExtrapolated malloc overhead for 2M frames: before %.3f s, after %.3f s (at ~0.5us/alloc)\n", sec_before, sec_after);
    return 0;
}
