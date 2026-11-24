#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <atomic>
#include <cstdio>
#include <initializer_list>
#include <memory>

using Microsoft::WRL::ComPtr;
// Fail-stop on device loss/watchdog: never unwind resource owners under a live queue.
#define REQUIRE(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); ExitProcess(1); } } while (0)
#define HR(x) do { HRESULT hr_=(x); if (FAILED(hr_)) { fprintf(stderr,"FAIL %d: %s hr=%08lx\n",__LINE__,#x,hr_); ExitProcess(1); } } while (0)

static constexpr UINT before=0xbef01234, after=0xa12f5678;
static constexpr UINT64 target=0x10000000bull;
using Cell=std::atomic<UINT64>;
static_assert(Cell::is_always_lock_free && alignof(Cell)>=8);

struct Gpu {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list[2];
    ComPtr<ID3D12Resource> upload, readback;
    ComPtr<ID3D12Fence> progress, gate;

    ComPtr<ID3D12Resource> buffer(D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state) {
        D3D12_HEAP_PROPERTIES h={}; h.Type=type; h.CreationNodeMask=h.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC d={}; d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; d.Width=16;
        d.Height=d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1; d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ComPtr<ID3D12Resource> r;
        HR(device->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&d,state,nullptr,IID_PPV_ARGS(&r)));
        return r;
    }
    explicit Gpu(ID3D12Device *d) : device(d) {
        D3D12_COMMAND_QUEUE_DESC q={}; q.Type=D3D12_COMMAND_LIST_TYPE_COPY;
        HR(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));
        HR(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&progress)));
        HR(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));
        HR(device->CreateCommandAllocator(q.Type,IID_PPV_ARGS(&allocator)));
        upload=buffer(D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
        readback=buffer(D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
        UINT *p=nullptr; D3D12_RANGE none={0,0}, written={0,12};
        HR(upload->Map(0,&none,(void **)&p)); p[0]=before; p[1]=0; p[2]=after; upload->Unmap(0,&written);
        for(unsigned i=0;i<2;i++) {
            HR(device->CreateCommandList(0,q.Type,allocator.Get(),nullptr,IID_PPV_ARGS(&list[i])));
            list[i]->CopyBufferRegion(readback.Get(),i?4:0,upload.Get(),i?8:0,i?4:8);
            HR(list[i]->Close());
        }
    }
    void submit(unsigned i) {
        ID3D12CommandList *batch[]={list[i].Get()}; queue->ExecuteCommandLists(1,batch);
    }
    UINT64 completed() const {
        UINT64 n=progress->GetCompletedValue(); REQUIRE(n!=UINT64_MAX); return n;
    }
    void await(UINT64 n) {
        HANDLE e=CreateEventW(nullptr,FALSE,FALSE,nullptr); REQUIRE(e);
        HR(progress->SetEventOnCompletion(n,e));
        REQUIRE(WaitForSingleObject(e,10000)==WAIT_OBJECT_0 && completed()>=n); CloseHandle(e);
    }
    void markers(UINT expected) {
        UINT *p=nullptr; D3D12_RANGE read={0,8},none={0,0};
        HR(readback->Map(0,&read,(void **)&p)); REQUIRE(p[0]==before && p[1]==expected); readback->Unmap(0,&none);
    }
};

enum Result {pending, satisfied, timed_out, cancelled};
// The caller lends exclusive queue access and both command lists until destruction.
struct Submission {
    Gpu &g;
    std::shared_ptr<Cell> cell;
    HANDLE cancel, thread;
    ULONGLONG deadline;
    std::atomic<Result> result{pending};
    Submission(const Submission &)=delete;
    Submission(Gpu &gpu,std::shared_ptr<Cell> owned,DWORD ms):g(gpu),cell(std::move(owned)) {
        cancel=CreateEventW(nullptr,TRUE,FALSE,nullptr); REQUIRE(cancel);
        g.submit(0); HR(g.queue->Signal(g.progress.Get(),1));
        HR(g.queue->Wait(g.gate.Get(),1));
        HR(g.queue->Signal(g.progress.Get(),2)); // Already submitted beyond the unsatisfied wait.
        deadline=GetTickCount64()+ms;
        thread=CreateThread(nullptr,0,observe,this,0,nullptr); REQUIRE(thread);
    }
    static DWORD WINAPI observe(void *ctx) {
        auto &s=*(Submission *)ctx;
        for(;;) {
            Result decision=pending;
            if(GetTickCount64()>=s.deadline) decision=timed_out;
            else if(s.cell->load(std::memory_order_acquire)>=target) decision=satisfied;
            if(decision!=pending) {
                Result old=pending;
                s.result.compare_exchange_strong(old,decision);
                break;
            }
            DWORD w=WaitForSingleObject(s.cancel,1); // No notification from the CPU writer.
            if(w==WAIT_OBJECT_0) break;
            REQUIRE(w==WAIT_TIMEOUT);
        }
        // Cancellation/timeout never queues the suffix. Release only our private gate.
        HR(s.g.gate->Signal(1));
        if(s.result.load()==satisfied) s.g.submit(1);
        HR(s.g.queue->Signal(s.g.progress.Get(),3));
        return 0;
    }
    void join() {
        REQUIRE(WaitForSingleObject(thread,10000)==WAIT_OBJECT_0);
        g.await(3);
    }
    ~Submission() {
        Result old=pending;
        if(result.compare_exchange_strong(old,cancelled)) REQUIRE(SetEvent(cancel));
        join(); CloseHandle(thread); CloseHandle(cancel);
    }
};

int main() {
    ComPtr<ID3D12Device> device;
    HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device)));
    for(Result mode:{satisfied,timed_out,cancelled}) {
        Gpu g(device.Get());
        auto cell=std::make_shared<Cell>(11); // Same low DWORD as target; high DWORD differs.
        std::weak_ptr<Cell> lifetime=cell;
        {
            Submission s(g,cell,mode==timed_out?1500:15000);
            g.await(1); REQUIRE(g.completed()==1); g.markers(0);
            Sleep(50); REQUIRE(g.completed()==1 && s.result.load()==pending);
            if(mode==satisfied) {
                cell->store(target+1,std::memory_order_release); // Only publishing action.
                s.join(); REQUIRE(s.result.load()==satisfied); g.markers(after);
            } else if(mode==timed_out) {
                s.join(); REQUIRE(s.result.load()==timed_out); g.markers(0);
            }
            cell.reset(); REQUIRE(!lifetime.expired());
        }
        REQUIRE(lifetime.expired() && g.completed()==3);
        g.markers(mode==satisfied?after:0);
        HR(device->GetDeviceRemovedReason());
        printf("PASS %s: prefix=%08x progress_before_store=1 final=3 suffix=%08x cell_released=1\n",
               mode==satisfied?"late store":mode==timed_out?"timeout":"destroy pending",before,
               mode==satisfied?after:0);
    }
}
