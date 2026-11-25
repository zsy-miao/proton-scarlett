"""Exercise the real +c8 setter with a controlled registry and host calls, no device."""
from pathlib import Path
import subprocess as sp
import tempfile
s=(Path(__file__).resolve().parents[3]/'native/queue/startup_list.h').read_text().split('static HRESULT record_clear4(',1)[1].split('static void WINAPI pool_pipeline',1)[0]
prog=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int HRESULT;typedef void IUnknown,ID3D12PipelineState,ID3D12RootSignature;
#define E_NOTIMPL -1
#define S_OK 0
#define XS_RECTLIST 17
#define XS_IM96_FACTORY
static char a[12],cache_template[4];
struct startup_list{int open,guest_topology,borrow_lock;char*cache_page;struct{void*buffer;}previous_vb;
    void *prior_pso,*required_root,*graphics_identity,*bound,*pool_owner,*device,*pipeline,*graphics_root,*host;};
static int pins,sets,psos,operations;
static int open_direct_state(struct startup_list*s){return s->open;}
static void AcquireSRWLockExclusive(int*p){assert(!(*p)++);}
static void ReleaseSRWLockExclusive(int*p){assert((*p)--==1);}
static void* xs_pin_im96_pso(void*o,void*d,void*id,void*r,void**pr){
    if(o!=a+1 || d!=a+2 || id!=a+3 || r!=a+4)return NULL;
    pins++;*pr=a+5;return a+6;}
static void IUnknown_Release(void*i){assert(i==a+3);pins--;}
static void ID3D12GraphicsCommandList7_SetGraphicsRootSignature(void*l,void*r){assert(l==a+7&&r==a+5);sets++;}
static void ID3D12GraphicsCommandList7_SetPipelineState(void*l,void*p){assert(l==a+7&&p==a+6&&sets==psos+1);psos++;}
static void recorded_direct_state(struct startup_list*s){assert(s->borrow_lock==1);operations++;}
#define xs_pin_colour_pso(o,d,i,r) ((void*)0)
#define xs_pin_clear4(o,d,i,r) ((void*)0)
static HRESULT record_clear4('''+s+r'''
int main(void){
    struct startup_list l={.open=1,.guest_topology=17,.cache_page=cache_template,.prior_pso=a+8,.required_root=a+4,
        .graphics_identity=a+4,.pool_owner=a+1,.device=a+2,.host=a+7,.graphics_root=a+9};
    assert(record_clear4(&l,a+1)<0);
    l.pool_owner=a;assert(record_clear4(&l,a+3)<0);l.pool_owner=a+1;
    l.device=a;assert(record_clear4(&l,a+3)<0);l.device=a+2;
    l.graphics_identity=a;assert(record_clear4(&l,a+3)<0);l.graphics_identity=a+4;
    l.guest_topology=4;assert(record_clear4(&l,a+3)<0);l.guest_topology=17;
    assert(!pins&&!sets&&!psos&&!operations&&!l.bound&&l.graphics_root==a+9);
    assert(!record_clear4(&l,a+3)&&l.graphics_identity==a+4&&l.required_root==a+4&&l.graphics_root==a+5);
    assert(l.bound==a+3&&l.pipeline==a+6&&pins==1&&sets==1&&psos==1&&operations==1);
    assert(record_clear4(&l,a+10)<0 && !record_clear4(&l,a+3)&&pins==1&&sets==2&&psos==2);
    puts("PASS explicit +c8: registry/owner/device/alias/topology rejection; one wrapper pin; private host root precedes PSO; guest alias unchanged");
}
'''
with tempfile.TemporaryDirectory() as t:
    exe=Path(t)/'check'
    sp.run(['gcc','-Wall','-Wextra','-Werror','-O2','-x','c','-','-o',exe],input=prog,text=True,check=True)
    sp.run([exe],check=True)
