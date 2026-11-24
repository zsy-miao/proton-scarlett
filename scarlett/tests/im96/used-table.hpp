#include "used-proof.h"
extern "C" {
HRESULT xs_write_address_srv(IUnknown*,const void*,const void*,SIZE_T);
HRESULT xs_write_view(IUnknown*,UINT,IUnknown*,const void*,SIZE_T);
IUnknown *test_texture(IUnknown*,ID3D12Resource*,BOOL);
BOOL test_used_current(const used_proof*);
HRESULT test_used_copy(const used_proof*,BOOL,ID3D12GraphicsCommandList*,ID3D12Resource*);
IUnknown *test_used_cb_pin(IUnknown*,SIZE_T,UINT);
UINT64 test_used_generation(IUnknown*);
}
#define SLOT(o,n,T) ((T)((*reinterpret_cast<void***>(o))[n/8]))
using Make=HRESULT(WINAPI*)(IUnknown*,const void*,REFIID,void**);
using Start=SIZE_T(WINAPI*)(IUnknown*);
#include "../../native/test-raw32-setup.hpp"
struct UsedTable{
    Raw32Setup arena;
    used_proof u{};
    ComPtr<IUnknown> cb_pin,visible,texture_key;
    ComPtr<ID3D12DescriptorHeap> bank,color_heap;
    ComPtr<ID3D12Resource> object,camera,padding,checks,texture;
    std::vector<BYTE> late_obj=file("object.bin",64),late_cam=file("camera.bin",512);
    DWORD srv_words[8]={28,4,0x1688,0,0,1,0,0};
    UsedTable(IUnknown *owner):arena(owner,true){}
    void setup(ID3D12Device *d,ID3D12GraphicsCommandList *l){
        u.owner=arena.guest.Get();u.base=(SIZE_T)arena.at[2];u.raw_allocation=arena.placed.Get();
        cb_pin.Attach(test_used_cb_pin(u.owner,u.base+0x3dc40,480));CK(cb_pin);
        CK(!test_used_cb_pin(u.owner,u.base+0x3dc40,512)&&!test_used_cb_pin(u.owner,u.base+0x3dc50,480));
        u.cb_allocation=cb_pin.Get();u.cb_generation=test_used_generation(cb_pin.Get());
        u.raw_slot=arena.slot;u.src_heap=arena.src.Get();u.raw_serial=*(UINT64*)arena.slot;
        u.cb_slot=SLOT(u.src_heap,0x48,Start)(u.src_heap)+18*32;
        UINT hd[]={0,0x130000,1,1};
        HR(SLOT(u.owner,0x70,Make)(u.owner,hd,IID_ID3D12DescriptorHeap,(void**)visible.GetAddressOf()));
        u.visible=visible.Get();SIZE_T base=SLOT(u.visible,0x48,Start)(u.visible);
        u.tex_slot=base+98*32;u.table=base+368373*32;
        auto buf=[&](UINT n,D3D12_HEAP_TYPE t,D3D12_RESOURCE_STATES state){
            D3D12_HEAP_PROPERTIES h={};h.Type=t;h.CreationNodeMask=h.VisibleNodeMask=1;
            D3D12_RESOURCE_DESC r={};r.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;r.Width=n;r.Height=r.MipLevels=r.DepthOrArraySize=r.SampleDesc.Count=1;r.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            ComPtr<ID3D12Resource> v;HR(d->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&r,state,nullptr,IID_PPV_ARGS(&v)));return v;};
        object=buf(64,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
        camera=buf(512,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
        padding=buf(32,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
        checks=buf(576,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_HEAP_PROPERTIES h={};h.Type=D3D12_HEAP_TYPE_DEFAULT;h.CreationNodeMask=h.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC td={};td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;td.Width=td.Height=8;
        td.MipLevels=2;td.DepthOrArraySize=td.SampleDesc.Count=1;td.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        HR(d->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&texture)));
        texture_key.Attach(test_texture(u.owner,texture.Get(),FALSE));CK(texture_key);
        HR(xs_write_view(u.owner,0x90,texture_key.Get(),srv_words,u.tex_slot));u.tex_serial=*(UINT64*)u.tex_slot;u.image=texture.Get();
        u.view.Format=td.Format;u.view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;u.view.Shader4ComponentMapping=0x1688;u.view.Texture2D.MipLevels=1;
        D3D12_DESCRIPTOR_HEAP_DESC hh={D3D12_DESCRIPTOR_HEAP_TYPE_RTV,2,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,1};
        HR(d->CreateDescriptorHeap(&hh,IID_PPV_ARGS(&color_heap)));auto at=color_heap->GetCPUDescriptorHandleForHeapStart();
        D3D12_RENDER_TARGET_VIEW_DESC rv={};rv.Format=td.Format;rv.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2D;
        for(UINT mip=0;mip<2;mip++){
            rv.Texture2D.MipSlice=mip;d->CreateRenderTargetView(texture.Get(),&rv,at);
            const float c[]={mip?0.f:1.f,mip?1.f:0.f,mip?0.f:1.f,1};l->ClearRenderTargetView(at,c,0,nullptr);
            at.ptr+=d->GetDescriptorHandleIncrementSize(hh.Type);
        }
        D3D12_RESOURCE_BARRIER image={};image.Transition={texture.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};
        l->ResourceBarrier(1,&image);
        const UINT64 inline_cb[]={0x0010000000000000ULL+u.base+0x3dc40,0x0002403c0000001eULL,0,0};
        memcpy((void*)u.cb_slot,inline_cb,32);
        memcpy((void*)u.table,(void*)u.raw_slot,32);memcpy((void*)(u.table+32),(void*)u.tex_slot,32);memcpy((void*)(u.table+64),(void*)u.cb_slot,32);
        CK(test_used_current(&u));auto bad=u;bad.cb_allocation=nullptr;CK(!test_used_current(&bad));
        bad=u;bad.cb_generation++;CK(!test_used_current(&bad));bad=u;bad.view.Texture2D.MostDetailedMip=1;CK(!test_used_current(&bad));
        bad=u;bad.tex_serial++;CK(!test_used_current(&bad));bad=u;bad.raw_serial++;CK(!test_used_current(&bad));
        BYTE*p;HR(padding->Map(0,nullptr,(void**)&p));memset(p,0xa5,32);padding->Unmap(0,nullptr);
        memset((void*)(u.base+0x3dc40),0xe7,480);memset((void*)(u.base+0x3de20),0x93,128);MemoryBarrier();
        l->CopyBufferRegion(camera.Get(),480,padding.Get(),0,32);
        HR(test_used_copy(&u,FALSE,l,object.Get()));HR(test_used_copy(&u,TRUE,l,camera.Get()));
        D3D12_RESOURCE_BARRIER b[2]={};b[0].Transition={object.Get(),0,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
        b[1].Transition={camera.Get(),0,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER};l->ResourceBarrier(2,b);
        hh={D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,3,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,1};HR(d->CreateDescriptorHeap(&hh,IID_PPV_ARGS(&bank)));at=bank->GetCPUDescriptorHandleForHeapStart();
        D3D12_SHADER_RESOURCE_VIEW_DESC v={};v.Shader4ComponentMapping=0x1688;v.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;v.Buffer.NumElements=1;v.Buffer.StructureByteStride=64;
        d->CreateShaderResourceView(object.Get(),&v,at);UINT step=d->GetDescriptorHandleIncrementSize(hh.Type);at.ptr+=step;
        d->CreateShaderResourceView(texture.Get(),&u.view,at);at.ptr+=step;
        D3D12_CONSTANT_BUFFER_VIEW_DESC cb={camera->GetGPUVirtualAddress(),512};CK(!(cb.BufferLocation%256));d->CreateConstantBufferView(&cb,at);
    }
    HRESULT consume(ID3D12GraphicsCommandList *l){
        if(!test_used_current(&u))return E_INVALIDARG;
        auto *h=bank.Get();l->SetDescriptorHeaps(1,&h);l->SetGraphicsRootDescriptorTable(0,h->GetGPUDescriptorHandleForHeapStart());return S_OK;
    }
    void copy_checks(ID3D12GraphicsCommandList *l){
        D3D12_RESOURCE_BARRIER b[2]={};b[0].Transition={object.Get(),0,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE};
        b[1].Transition={camera.Get(),0,D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,D3D12_RESOURCE_STATE_COPY_SOURCE};l->ResourceBarrier(2,b);
        l->CopyBufferRegion(checks.Get(),0,object.Get(),0,64);l->CopyBufferRegion(checks.Get(),64,camera.Get(),0,512);
    }
    HRESULT submit(ID3D12CommandQueue*q,ID3D12CommandList *l){
        if(!test_used_current(&u))return E_INVALIDARG;
        q->ExecuteCommandLists(1,&l);return S_OK;
    }
    void replace(){
        memcpy((void*)(u.base+0x3dc40),late_cam.data(),480);MemoryBarrier();
        arena.replace(late_obj.data());CK(!test_used_current(&u));
        HR(xs_write_view(u.owner,0x90,texture_key.Get(),srv_words,u.tex_slot));CK(*(UINT64*)u.tex_slot!=u.tex_serial);
    }
    void verify(){
        BYTE *p;HR(checks->Map(0,nullptr,(void**)&p));
        CK(!memcmp(p,late_obj.data(),64)&&!memcmp(p+64,late_cam.data(),480));
        for(UINT k=0;k<32;k++)CK(p[544+k]==0xa5);
        checks->Unmap(0,nullptr);
        puts("PASS used im96 table: exact RAW32 origin +3de20, producer-pinned 480-byte CBV +3dc40, 32 padding canaries, original generation through fenced VA reuse");
    }
};
