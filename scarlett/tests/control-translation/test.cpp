#include "control.hpp"
#include <atomic>
#include <cstdio>
#include <initializer_list>
using Microsoft::WRL::ComPtr;
#define CK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); ExitProcess(1); } } while(0)
#define HR(x) do { HRESULT h_=(x); if(FAILED(h_)) { fprintf(stderr,"FAIL %d: %s %#lx\n",__LINE__,#x,(unsigned long)h_); ExitProcess(1); } } while(0)
constexpr SIZE_T cell=0x120, banks=0x324;  // supplied roles, not retail offsets
constexpr UINT64 target=0x10000000bull;
enum Result { pending, matched, expired, cancelled };

struct Packet {
    Control::Ptr page; // owns both observer alias and placed resource through retirement
    const UINT64 *sample=nullptr;
    ComPtr<ID3D12CommandQueue> q;
    ComPtr<ID3D12CommandAllocator> ca;
    ComPtr<ID3D12GraphicsCommandList2> lists[2];
    ComPtr<ID3D12Fence> progress,gate;
    HANDLE wake=nullptr,thread=nullptr;
    std::atomic<Result> result{pending};
    ULONGLONG deadline=0;
    Packet(ID3D12Device3 *d,Control::Ptr p):page(std::move(p)) {
        BYTE *b=page->public_base();
        HR(page->record_wait(b+cell,7,1,&sample));
        D3D12_COMMAND_QUEUE_DESC desc={};
        HR(d->CreateCommandQueue(&desc,IID_PPV_ARGS(&q)));
        HR(d->CreateCommandAllocator(desc.Type,IID_PPV_ARGS(&ca)));
        HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&progress)));
        HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));
        for (UINT i=0;i<2;i++) {
            HR(d->CreateCommandList(0,desc.Type,ca.Get(),nullptr,IID_PPV_ARGS(&lists[i])));
            auto *l=lists[i].Get();
            CK(page->marker(l,b+cell,8,0,0)==E_INVALIDARG);
            CK(page->marker(l,b+banks+1,8,0,0)==E_INVALIDARG);
            CK(page->marker(l,b+banks+216,8,0,0)==E_INVALIDARG);
            CK(page->marker(l,b+banks,8,1,0)==E_NOTIMPL && page->marker(l,b+banks,8,0,1)==E_NOTIMPL);
            HR(page->marker(l,b+banks+4*(17+i),i+1,0,0));
            HR(l->Close());
        }
        wake=CreateEventW(nullptr,TRUE,FALSE,nullptr); CK(wake);
    }
    UINT64 completed() { UINT64 n=progress->GetCompletedValue(); CK(n!=UINT64_MAX); return n; }
    void await(UINT64 n) {
        HANDLE e=CreateEventW(nullptr,FALSE,FALSE,nullptr); CK(e);
        HR(progress->SetEventOnCompletion(n,e));
        CK(WaitForSingleObject(e,10000)==WAIT_OBJECT_0 && completed()>=n); CloseHandle(e);
    }
    void execute(UINT i) { ID3D12CommandList *ls[]={lists[i].Get()}; q->ExecuteCommandLists(1,ls); }
    void start(DWORD ms) {
        CK(!thread);
        execute(0); HR(q->Signal(progress.Get(),1)); HR(q->Wait(gate.Get(),1)); HR(q->Signal(progress.Get(),2));
        deadline=GetTickCount64()+ms;
        thread=CreateThread(nullptr,0,observe,this,0,nullptr); CK(thread);
    }
    static DWORD WINAPI observe(void *v) {
        auto &s=*(Packet *)v;
        while (s.result.load()==pending) {
            Result r=pending;
            if (GetTickCount64()>=s.deadline) r=expired;
            else if (s.completed()>=1 && __atomic_load_n(s.sample,__ATOMIC_ACQUIRE)>=target) r=matched;
            if (r!=pending) { Result expected=pending; s.result.compare_exchange_strong(expected,r); break; }
            DWORD w=WaitForSingleObject(s.wake,1); CK(w==WAIT_TIMEOUT || w==WAIT_OBJECT_0);
        }
        HR(s.gate->Signal(1));
        if (s.result.load()==matched) s.execute(1);
        HR(s.q->Signal(s.progress.Get(),3));
        return 0;
    }
    void finish() { CK(WaitForSingleObject(thread,10000)==WAIT_OBJECT_0); await(3); }
    ~Packet() {
        if (thread) { Result e=pending; if(result.compare_exchange_strong(e,cancelled)) CK(SetEvent(wake));
            finish(); CloseHandle(thread); }
        CloseHandle(wake);
    }
};

int main() {
    ComPtr<ID3D12Device3> d; HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));
    Control::Ptr rejected;
    CK(FAILED(Control::request(d.Get(),nullptr,0x10000,0x20401000,0x283,0x2404,cell,banks,rejected)) && !rejected);
    CK(FAILED(Control::request(d.Get(),nullptr,0x10000,0x20401000,(1ull<<32)|0x283,0x42404,cell,banks,rejected)) && !rejected);
#ifdef TEST_UNQUALIFIED
    CK(FAILED(Control::request(d.Get(),nullptr,0x10000,0x20401000,0x283,0x42404,cell,banks,rejected)) && !rejected);
    puts("PASS unqualified creator: exact tuple failed closed");
    return 0;
#endif
    for (auto mode:{matched,expired,cancelled}) {
        Control::Ptr p,next;
        CK(Control::create(d.Get(),nullptr,cell,cell+4,p)==E_INVALIDARG);
        HR(Control::request(d.Get(),nullptr,0x10000,0x20401000,0x283,0x42404,cell,banks,p));
        BYTE *va=p->public_base();
        memset(va,0,Control::extent);
        const UINT64 *a=nullptr;
        CK(p->record_wait(va+cell,7,0,&a)==E_NOTIMPL && !a);
        CK(p->record_wait(va+cell,6,1,&a)==E_NOTIMPL && !a);
        CK(p->record_wait(va+banks,7,1,&a)==E_INVALIDARG && !a);
        CK(p->record_wait(va+cell+1,7,1,&a)==E_INVALIDARG && !a);
        // Independent old alias represents a separately held CPU producer (after public release).
        scarlett_section_info info; HR(scarlett_acquire_section(va,Control::extent,&info));
        BYTE *old=(BYTE *)MapViewOfFile(info.section,FILE_MAP_ALL_ACCESS,0,0,Control::extent);
        CK(old && CloseHandle(info.section));
        auto *old_words=(volatile UINT32 *)(old+banks);
        auto *public_words=(volatile UINT32 *)(va+banks);
        __atomic_store_n((UINT64 *)(va+cell),target & UINT32_MAX,__ATOMIC_RELEASE);
        std::weak_ptr<Control> life=p;
        {
            Packet cmd(d.Get(),p);
            cmd.start(mode==expired?4000:20000); cmd.await(1);
            CK(public_words[17]==1 && public_words[18]==0 && cmd.completed()==1);
            public_words[17]=0; MemoryBarrier();
            CK(!VirtualFree(va,Control::extent-1,MEM_RELEASE));
            CK(VirtualFree(va,Control::extent,MEM_RELEASE));
            CK(FAILED(p->record_wait(va+cell,7,1,&a)) && !a);
            HR(Control::create(d.Get(),va,cell,banks,next)); CK(next->public_base()==va);
            CK(p->record_wait(va+cell,7,1,&a)==E_INVALIDARG && !a);
            // Stale generation rejects even a structurally valid marker; never reaches closed host list.
            CK(p->marker(cmd.lists[0].Get(),va+banks,88,0,0)==E_INVALIDARG);
            HR(next->record_wait(va+cell,7,1,&a));
            __atomic_store_n((UINT64 *)(va+cell),target+2,__ATOMIC_RELEASE);
            public_words[18]=345; MemoryBarrier(); p.reset();
            Sleep(50); CK(!life.expired() && cmd.result.load()==pending && cmd.completed()==1 && old_words[18]==0);
            if(mode==matched) {
                __atomic_store_n((UINT64 *)(old+cell),target+1,__ATOMIC_RELEASE);
                cmd.finish(); CK(cmd.result.load()==matched);
            } else if (mode==expired) { cmd.finish(); CK(cmd.result.load()==expired); }
        }
        CK(life.expired() && public_words[18]==345 && old_words[17]==0 && old_words[18]==(mode==matched?2u:0u));
        CK(VirtualFree(va,Control::extent,MEM_RELEASE) && UnmapViewOfFile(old));
        HR(d->GetDeviceRemovedReason());
        printf("PASS %s: prefix visible and cleared; same-VA new cell cannot open gate; old suffix=%u new slot=345; retired\n",
               mode==matched?"late old store":mode==expired?"timeout":"destroy pending",mode==matched?2:0);
    }
}
